"""Actual local GGUF inference and strict shadow proposals; no cloud service."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import urllib.error
import urllib.request

from coaching_contract import canonical, strict_json

REASONS=["ALREADY_INFORMED","RESPONSE_OBSERVED","UNCHANGED_EVENT","NEW_RELEVANT_EVENT",
         "UNCERTAIN_EVIDENCE","MANEUVER_WORKLOAD","RESOLVED_EVENT","ROUTE_PREPARATION",
         "IMPORTANT_EVENT","NO_USEFUL_INTERVENTION"]
CHOICE_ALPHABET='ABCEFGHI'  # D and S are reserved, never candidate aliases.
CHOICE_SYSTEM=(Path(__file__).resolve().parents[1]/'android/app/src/main/assets/intelligence-choice-system.txt').read_text().strip()
SYSTEM="""You select useful coaching or silence for a supervised learner-driving research replay.
Return only the constrained JSON. Select approved candidate indexes, instruction IDs and event IDs; invent nothing.
SILENCE/DEFER require null candidate_index and instruction_id. A high model confidence is not evidence.
Warn only for an already-validated urgent candidate. Never invent gear, RPM, clutch, pedals, lane, metric distance or intent.
Fresh observations are required; a lost/occluded actor is uncertain, not safely gone.
supported_path=false means unknown, not proof of being outside the path. Image motion is not a calibrated trajectory.
History delivered=false means the instruction has NOT started and is NOT intentionally visual-only: do not count it as already informed.
Prefer silence for unchanged already-addressed encounters. A genuinely new relevant event may merit a short cue.
Measured decreasing speed after a preparation instruction may make another routine slowing cue unnecessary, but proves no correct driving.
speed_trend:0 unknown,1 steady,2 increasing,3 decreasing. Pedestrian category0, two-wheeler1/3, vehicle2/5/7.
Do not infer pedestrian parallel motion from one image delta. Defer unrelated routine coaching during an imminent turn.
Select among supplied approved candidates only. This output is a shadow proposal, never a live driving instruction."""


def schema(context):
    candidates=context.get('candidates',[])
    ids=[None,*sorted({c['instruction_id'] for c in candidates})]
    indexes=[None,*sorted({c['index'] for c in candidates})]
    event_ids=[None,*sorted({str(c['space'])+':'+str(c['event_id']) for c in candidates})]
    common={'type':'object','additionalProperties':False,
        'properties':{'schema_version':{'const':2},'decision':{'enum':['SILENCE','COACH','WARN','DEFER']},
          'candidate_index':{'enum':indexes},'instruction_id':{'enum':ids},'event_id':{'enum':event_ids},
          'reason_code':{'enum':REASONS},'confidence':{'type':'number','minimum':0,'maximum':1}},
        'required':['schema_version','decision','candidate_index','instruction_id','event_id','reason_code','confidence']}
    import copy
    quiet=copy.deepcopy(common)
    quiet['properties'].update(decision={'enum':['SILENCE','DEFER']},candidate_index={'const':None},instruction_id={'const':None})
    quiet['properties']['reason_code']={'enum':['ALREADY_INFORMED','RESPONSE_OBSERVED','UNCHANGED_EVENT','UNCERTAIN_EVIDENCE','MANEUVER_WORKLOAD','RESOLVED_EVENT','NO_USEFUL_INTERVENTION']}
    branches=[quiet]
    for c in candidates:
        branch=copy.deepcopy(common)
        branch['properties'].update(decision={'const':'WARN' if c['urgent'] else 'COACH'},candidate_index={'const':c['index']},
            instruction_id={'const':c['instruction_id']},event_id={'const':str(c['space'])+':'+str(c['event_id'])})
        branch['properties']['reason_code']={'const':'IMPORTANT_EVENT' if c['urgent'] else 'ROUTE_PREPARATION' if c['space']==4 else 'NEW_RELEVANT_EVENT'}
        branches.append(branch)
    return {'oneOf':branches}


def validate(value,context):
    required={'schema_version','decision','candidate_index','instruction_id','event_id','reason_code','confidence'}
    if not isinstance(value,dict) or set(value)!=required or value['schema_version']!=2:raise ValueError('Wrong proposal schema')
    if value['decision'] not in ('SILENCE','COACH','WARN','DEFER') or value['reason_code'] not in REASONS:raise ValueError('Unknown decision/reason')
    confidence=value['confidence']
    if confidence is not None and (isinstance(confidence,bool) or not isinstance(confidence,(int,float)) or not 0<=confidence<=1):raise ValueError('Invalid uncalibrated confidence')
    candidates={c['index']:c for c in context.get('candidates',[])}
    allowed_events={str(c['space'])+':'+str(c['event_id']) for c in candidates.values()}
    if value['event_id'] is not None and value['event_id'] not in allowed_events:raise ValueError('Invented event')
    if value['decision'] in ('SILENCE','DEFER'):
        if value['candidate_index'] is not None or value['instruction_id'] is not None:raise ValueError('Quiet proposal contains an instruction')
    else:
        index=value['candidate_index']
        if isinstance(index,bool) or not isinstance(index,int) or index not in candidates:raise ValueError('Invented candidate')
        c=candidates[index]
        if value['instruction_id']!=c['instruction_id'] or value['event_id']!=str(c['space'])+':'+str(c['event_id']):raise ValueError('Instruction/event binding mismatch')
        if (value['decision']=='WARN')!=c['urgent']:raise ValueError('Model changed candidate urgency authority')
    if value['reason_code']=='RESPONSE_OBSERVED' and context['vehicle']['response']!='RESPONSE_OBSERVED':raise ValueError('Invented response evidence')
    if value['reason_code']=='ALREADY_INFORMED' and not any(h.get('delivered') for h in context.get('history',[])):raise ValueError('Invented delivered instruction')
    if value['reason_code']=='MANEUVER_WORKLOAD' and (value['decision'] not in ('SILENCE','DEFER') or context['navigation'].get('window')!=3):raise ValueError('Unsupported workload explanation')
    return value


def choice_input(context):
    candidates=context.get('candidates',[])
    if len(candidates)>8:raise ValueError('Choice budget exceeded')
    bindings={CHOICE_ALPHABET[i]:c for i,c in enumerate(candidates)}
    compact={'vehicle':context['vehicle'],'navigation':context['navigation'],
        'events':[{k:e[k] for k in ('space','id','scope','category','state','fresh','observations','relevant_region','supported_path','optical_closing','coached','image_delta')} for e in context.get('events',[])[:8]],
        'history':context.get('history',[]),'choices':{'S':'SILENCE','D':'DEFER',**bindings}}
    return compact,bindings


def bind_choice(text,bindings):
    token=text.strip()
    if token not in ('S','D',*bindings):raise ValueError('Model choice not allowlisted')
    c=bindings.get(token)
    reason='IMPORTANT_EVENT' if c and c['urgent'] else 'ROUTE_PREPARATION' if c and c['space']==4 else 'NEW_RELEVANT_EVENT' if c else 'NO_USEFUL_INTERVENTION'
    return {'schema_version':2,'decision':('WARN' if c['urgent'] else 'COACH') if c else 'SILENCE' if token=='S' else 'DEFER',
        'candidate_index':c['index'] if c else None,'instruction_id':c['instruction_id'] if c else None,
        'event_id':str(c['space'])+':'+str(c['event_id']) if c else None,'reason_code':reason,'confidence':None}


def process_metrics(pid):
    """OS process counters, not Python parent RSS or a model size estimate."""
    if os.name!='nt':return {'resident_bytes':None,'peak_resident_bytes':None,'cpu_seconds':None}
    import ctypes as C
    from ctypes import wintypes as W
    class Memory(C.Structure):
        _fields_=[('cb',W.DWORD),('PageFaultCount',W.DWORD),
            *[(n,C.c_size_t) for n in ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage')]]
    kernel=C.WinDLL('kernel32',use_last_error=True);psapi=C.WinDLL('psapi',use_last_error=True)
    kernel.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];kernel.OpenProcess.restype=W.HANDLE
    kernel.CloseHandle.argtypes=[W.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes=[W.HANDLE,C.POINTER(Memory),W.DWORD]
    kernel.GetProcessTimes.argtypes=[W.HANDLE,*([C.POINTER(W.FILETIME)]*4)]
    handle=kernel.OpenProcess(0x410,False,pid)
    if not handle:return {'resident_bytes':None,'peak_resident_bytes':None,'cpu_seconds':None}
    try:
        mem=Memory();mem.cb=C.sizeof(mem);ok=psapi.GetProcessMemoryInfo(handle,C.byref(mem),mem.cb)
        times=[W.FILETIME() for _ in range(4)];cpu_ok=kernel.GetProcessTimes(handle,*[C.byref(t) for t in times])
        ticks=lambda t:(t.dwHighDateTime<<32)|t.dwLowDateTime
        return {'resident_bytes':int(mem.WorkingSetSize) if ok else None,
            'peak_resident_bytes':int(mem.PeakWorkingSetSize) if ok else None,
            'cpu_seconds':sum(ticks(t) for t in times[2:])*1e-7 if cpu_ok else None}
    finally:kernel.CloseHandle(handle)


class LocalModel:
    """Persistent publisher llama-server, owned by caller and bound to loopback."""
    def __init__(self,executable,model,folder,threads=2,context=4096):
        self.executable=Path(executable);self.model=Path(model);self.folder=Path(folder)
        self.threads=threads;self.context=context;self.process=None;self.log=None;self.load_ms=None

    def __enter__(self):
        self.folder.mkdir(parents=True,exist_ok=True)
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        self.url='http://127.0.0.1:'+str(port)
        self.log=(self.folder/'server.log').open('wb')
        args=[str(self.executable),'-m',str(self.model),'--host','127.0.0.1','--port',str(port),
              '-c',str(self.context),'-t',str(self.threads),'-tb',str(self.threads),'-np','1','-ngl','0','--jinja']
        self.command=args;start=time.perf_counter()
        self.process=subprocess.Popen(args,stdout=self.log,stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        try:
            while time.perf_counter()-start<90:
                if self.process.poll() is not None:raise RuntimeError('Local server exited; inspect '+str(self.folder/'server.log'))
                try:
                    with urllib.request.urlopen(self.url+'/health',timeout=1) as response:
                        if response.status==200:self.load_ms=(time.perf_counter()-start)*1000;return self
                except (urllib.error.URLError,TimeoutError):time.sleep(.1)
            raise TimeoutError('Model loading exceeded90 seconds')
        except BaseException:self.__exit__(None,None,None);raise

    def __exit__(self,*_):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=5)
        if self.log:self.log.close()

    def infer(self,context,timeout=30,format='choice'):
        if context.get('schema_version')!=2 or not context.get('model_eligible'):raise ValueError('Context not eligible for model evaluation')
        if context.get('navigation',{}).get('provider_content_excluded'):raise ValueError('Google provider context excluded from model')
        payload={'model':'local','messages':[{'role':'system','content':SYSTEM},{'role':'user','content':canonical(context)}],
            'temperature':0,'seed':17,'max_tokens':160,'stream':False,
            'chat_template_kwargs':{'enable_thinking':False},
            'response_format':{'type':'json_schema','json_schema':{'name':'coaching','strict':True,'schema':schema(context)}}}
        compact,bindings=choice_input(context)
        if format=='choice':
            payload.update(messages=[{'role':'system','content':CHOICE_SYSTEM},
                {'role':'user','content':canonical(compact)}],max_tokens=4)
            payload.pop('response_format')
            payload['grammar']='root ::= '+ ' | '.join(json.dumps(k) for k in ['S','D',*bindings])
        req=urllib.request.Request(self.url+'/v1/chat/completions',data=canonical(payload).encode(),headers={'Content-Type':'application/json'})
        before=process_metrics(self.process.pid);start=time.perf_counter()
        with urllib.request.urlopen(req,timeout=timeout) as response:
            raw=response.read(262145)
            if len(raw)>262144:raise ValueError('Model response exceeds output budget')
        result=strict_json(raw.decode());text=result['choices'][0]['message']['content']
        if result['choices'][0].get('finish_reason')=='length':raise ValueError('Truncated model response')
        record={'raw_content':text,'wall_ms':(time.perf_counter()-start)*1000,
                'usage':result.get('usage'),'timings':result.get('timings'),
                'process_before':before,'process_after':process_metrics(self.process.pid),
                'input_sha256':hashlib.sha256(canonical(compact if format=='choice' else context).encode()).hexdigest(),
                'model_confidence_is_calibrated':False,'live_authority':False}
        try:
            if format=='choice':
                value=bind_choice(text,bindings)
                record.update(neural_choice=text.strip(),metadata_source='trusted choice binding; reason is deterministic, not generated explanation',token_binding=bindings)
            else:value=strict_json(text)
            record.update(status='completed',proposal=validate(value,context))
        except (ValueError,TypeError,KeyError) as error:record.update(status='invalid_proposal',error=str(error))
        return record


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server',required=True,type=Path);parser.add_argument('--model',required=True,type=Path)
    parser.add_argument('--context',required=True,type=Path);parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--format',choices=['choice','json'],default='choice')
    args=parser.parse_args();context=strict_json(args.context.read_text())
    with LocalModel(args.server,args.model,args.output.parent/'local-runtime') as model:
        result=model.infer(context,format=args.format);result.update(load_ms=model.load_ms,server_command=model.command,
            model_sha256=__import__('coaching_contract').sha256(args.model))
        args.output.write_text(json.dumps(result,indent=2)+'\n')
        print(result['status'],json.dumps(result.get('proposal')),round(result['wall_ms'],1),'ms')
