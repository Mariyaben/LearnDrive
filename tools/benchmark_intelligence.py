"""Frozen Balanced vs native temporal vs real pretrained hybrid on matched inputs."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import random
import time
import wave

from coaching_contract import canonical,empty_world,sha256
from generate_coaching_fixtures import attention,route
from intelligence_model import LocalModel,process_metrics,CHOICE_SYSTEM
from replay_coaching import NativeBridge,percentiles,decode_snapshot

ROOT=Path(__file__).resolve().parents[1]


def scenarios():
    names=['pedestrian-parallel-uncertain','newly-relevant-person','same-person-addressed','temporary-occlusion',
        'right-turn','left-turn','slowing-before-turn','maintaining-speed','competing-events','normal-silence',
        'repeated-detections','navigation-reroute','missing-gps','stale-camera','uncertain-optical-motion',
        'slow-inference','voice-submission-failure','voice-disabled','important-escalation','completed-session-review']
    for name in names:
        rows=[]
        for i in range(61):
            t=round(i*.1,4);w=empty_world(t,sequence=i+1)
            w['telemetry'].update(speed_kmh=30.,speed_valid=True,acceleration_mps2=0.)
            if name not in ('normal-silence','completed-session-review'):attention(w,t)
            if name in ('right-turn','left-turn','slowing-before-turn','maintaining-speed','navigation-reroute','missing-gps'):
                route(w,t,5 if name=='right-turn' else 4,event=12 if name=='navigation-reroute' and t>=1.5 else 11)
                w['route']['distance_m']=max(10,80-t*8)
            if name=='slowing-before-turn':w['telemetry']['speed_kmh']=30-t*3
            if name=='missing-gps':w['route'].update(valid=False,gps_time=None)
            if name=='newly-relevant-person' and t<1:w['actors']=[];w['candidates']=[]
            if name=='temporary-occlusion' and 1<=t<1.6:w['actors']=[];w['candidates']=[]
            if name in ('stale-camera','slow-inference'):
                w['capture_time']=None if t<.6 else round(t-.6,4)
                for a in w['actors']:a['capture_time']=w['capture_time']
                for c in w['candidates']:c['capture_time']=w['capture_time']
            if name=='voice-submission-failure':w['audio_ready']=False
            if name=='competing-events':
                a=empty_world(t);route(a,t);w['route']=a['route'];w['candidates']+=a['candidates']
            if name=='important-escalation' and t>=1.5:
                w['actors'][0].update(optical_closing=True,in_path=True,tti_seconds=2.,confidence=.9)
                w['candidates'][0].update(urgent=True,kind=2,evidence_kind=2,tti_seconds=2.,confidence=.9)
            if name=='completed-session-review':w['active']=False
            rows.append({'published_at':t,'profile':1,'voice':name!='voice-disabled','revision':1,'world':w})
        yield {'scenario_id':name,'evidence_kind':'synthetic','independent_instructor_labels':None,
            'split':'held-out' if int(hashlib.sha256(name.encode()).hexdigest()[:4],16)%5==0 else 'development',
            'notes':'Software test with explicitly supplied measurements; title is authored scenario intent, not model-inferred truth.',
            'rows':rows}


def clip_seconds(prompt):
    with wave.open(str(ROOT/f'android/app/src/main/assets/audio/prompt-{prompt}.wav')) as audio:
        return audio.getnframes()/audio.getframerate()


def recorded_case(path):
    packed=json.loads(path.read_text());rows=[]
    for row in packed['rows']:
        if not row['native_world']:continue
        rows.append({k:v for k,v in row.items() if k!='native_world'}|{'world':decode_snapshot(row['native_world'])})
    if not rows:raise ValueError('Empty recorded perception stream')
    return {'scenario_id':'recorded-'+path.parent.name,'evidence_kind':'recorded_native_perception',
        'split':'development','independent_instructor_labels':None,'source_sha256':packed['provenance'],
        'notes':'Actual native perception stream sampled at10Hz; no speed/IMU/navigation fabricated. Original candidate producer reflects its recording replay history. All three policies receive that same frozen stream.',
        'rows':rows}


def run_case(case,model,baseline,current,max_calls=3):
    bridges=[NativeBridge(baseline),NativeBridge(current),NativeBridge(current)]
    bridges[2].use_hybrid();events=[[],[],[]];ticks=[];samples=[]
    previous=None;last_inference=-100;calls=0;playing=[None,None,None]
    counters={'ticks':0,'unchanged_context':0,'throttled_or_call_cap':0,'ineligible_context':0}
    try:
        for i,row in enumerate(case['rows']):
            now=row['published_at'];w=row['world'];decisions=[]
            counters['ticks']+=1
            for arm,b in enumerate(bridges):
                b.configure(row);b.world(w,now)
                if playing[arm] and now>=playing[arm]['due']:
                    active=playing[arm];active['event']['completion_hook_acknowledged']=b.delivery(active['decision'],now)
                    active['event']['simulated_pcm_completed_at']=now;playing[arm]=None
            for arm,b in enumerate(bridges):
                decision_start=time.perf_counter();d=b.decide(i+1)
                if arm==2:
                    context=b.context();key=(context['source_id'],context['revision'],context['generation'])
                    if context['model_eligible'] and key!=previous and now-last_inference>=1.5 and calls<max_calls:
                        calls+=1;previous=key;last_inference=now
                        sample={'context':context,'scenario_id':case['scenario_id'],'input_sha256':hashlib.sha256(canonical(context).encode()).hexdigest()}
                        try:
                            sample.update(model.infer(context))
                            if sample['status']=='completed':
                                sample['native_proposal_admitted']=b.proposal(sample['proposal'],context,w)
                                d=b.decide(10000+i)
                                sample['hybrid_decision']=d
                        except Exception as error:sample.update(status='inference_failed',error=type(error).__name__+': '+str(error))
                        sample['clock_scope']='offline decision evaluation freezes scenario time; not live latency qualification'
                        sample['exceeds_experimental_2s_live_budget']=sample.get('wall_ms',0)>2000
                        samples.append(sample)
                    elif not context['model_eligible']:counters['ineligible_context']+=1
                    elif key==previous:counters['unchanged_context']+=1
                    else:counters['throttled_or_call_cap']+=1
                d['decision_wall_ms']=(time.perf_counter()-decision_start)*1000
                if d['eligible']:
                    submitted=row['voice'] and w['audio_ready']
                    if b.accept(d,now,submitted):
                        onset=b.playback(d,now) if submitted else False
                        event={'time':now,'prompt':d['prompt'],'source_id':d['source_id'],
                            'voice_submitted':submitted,'simulated_onset':onset,'event_number':d['event_number']}
                        events[arm].append(event)
                        if onset:playing[arm]={'due':now+clip_seconds(d['prompt']),'event':event,'decision':d}
                decisions.append(d)
            ticks.append({'time':now,'decisions':decisions})
    finally:
        for b in bridges:b.close()
    return {'scenario_id':case['scenario_id'],'split':case['split'],'evidence_kind':case['evidence_kind'],
        'events':dict(zip(['A_balanced','B_temporal','C_pretrained_hybrid'],events)),'ticks':ticks,'model_samples':samples,'invocation_counters':counters}


def blind_review(runs,folder):
    randomizer=random.Random(17);mapping={};panels=[]
    for run in runs:
        arms=list(run['events']);randomizer.shuffle(arms)
        for number,arm in enumerate(arms):
            panel=run['scenario_id']+'-'+str(number+1)
            mapping[panel]=arm
            # Remove engine-specific event numbers/delivery hooks from reviewer views.
            panels.append({'panel_id':panel,'scenario_id':run['scenario_id'],'split':run['split'],
                'cues':[{'time':e['time'],'instruction_id':e['prompt'],'voice':e['voice_submitted']} for e in run['events'][arm]],
                'evidence_reference':'inputs.json / scenario_id (full supplied evidence timeline)',
                'labels':None})
    (folder/'review-panels.json').write_text(json.dumps(panels,indent=2)+'\n')
    (folder/'review-unblinding.json').write_text(json.dumps(mapping,indent=2)+'\n')
    with (folder/'blinded-review.csv').open('w',newline='',encoding='utf-8') as f:
        writer=csv.writer(f);writer.writerow(['panel_id','reviewer_id','reviewer_role','unnecessary_cues','missed_events','correct_silence_intervals','timing_rating','rationale','reviewed'])
        for panel in panels:writer.writerow([panel['panel_id'],'','','','','','','','false'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server',type=Path,required=True);p.add_argument('--model',type=Path,required=True)
    p.add_argument('--library',type=Path,default=ROOT/'build/native-spatial/liblearndrive_coaching_replay.dll')
    p.add_argument('--baseline',type=Path,default=ROOT/'build/intelligence-v2/baseline-native/liblearndrive_coaching_replay.dll')
    p.add_argument('--output',type=Path,required=True);p.add_argument('--limit',type=int,default=20)
    p.add_argument('--recorded-worlds',type=Path,nargs='*',default=[])
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    cases=list(scenarios())[:args.limit]+[recorded_case(path) for path in args.recorded_worlds]
    (args.output/'inputs.json').write_text(json.dumps(cases,indent=2)+'\n')
    runs=[]
    with LocalModel(args.server,args.model,args.output/'runtime') as model:
        for case in cases:
            result=run_case(case,model,args.baseline,args.library,max_calls=6 if case['evidence_kind']=='recorded_native_perception' else 3);runs.append(result)
            print(case['scenario_id'],{k:len(v) for k,v in result['events'].items()},flush=True)
            (args.output/(case['scenario_id']+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        all_samples=[s for r in runs for s in r['model_samples']]
        report={'schema_version':2,'status':'actual_pretrained_execution','model_sha256':sha256(args.model),
            'server_sha256':sha256(args.server),'server_command':model.command,'load_ms':model.load_ms,
            'baseline_library_sha256':sha256(args.baseline),'current_library_sha256':sha256(args.library),
            'scenario_count':len(runs),'model_calls':len(all_samples),
            'scenario_evidence':{kind:sum(c['evidence_kind']==kind for c in cases) for kind in sorted({c['evidence_kind'] for c in cases})},
            'model_wall_ms':percentiles([s['wall_ms'] for s in all_samples if 'wall_ms' in s]),
            'invalid_or_failed':sum(s['status']!='completed' for s in all_samples),
            'exceeds_2s_live_budget':sum(s['exceeds_experimental_2s_live_budget'] for s in all_samples),
            'process_counters':process_metrics(model.process.pid),
            'total_decision_wall_ms':{arm:percentiles([tick['decisions'][i]['decision_wall_ms'] for run in runs for tick in run['ticks']]) for i,arm in enumerate(('A_balanced','B_temporal','C_pretrained_hybrid'))},
            'prompt_sha256':hashlib.sha256(CHOICE_SYSTEM.encode()).hexdigest(),
            'tool_sha256':{p:sha256(ROOT/p) for p in ('tools/benchmark_intelligence.py','tools/intelligence_model.py','tools/replay_coaching.py')},
            'input_sha256':sha256(args.output/'inputs.json'),
            'distinct_neural_choices':sorted({s.get('neural_choice') for s in all_samples if s.get('neural_choice')}),
            'instructor_nuisance_recall_silence_metrics':None,'quality_status':'unavailable; no qualified instructor labels',
            'neural_choice_is_real':True,'metadata_binding_is_deterministic':True,'live_authority':False,
            'limits':['Synthetic scenario expectations are not instructor judgments.',
                'Matched10Hz supplied sensor publications; policy histories diverge according to their own simulated deliveries. Recorded candidate producers have their original replay history.',
                'Offline logical time freezes during neural inference. Actual wall time and deadline misses are retained.',
                'No learned model is promoted or claimed superior.','No physical Android speed, heat, battery or audibility evidence.']}
        (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    blind_review(runs,args.output)
    print(args.output/'report.json')


if __name__=='__main__':main()
