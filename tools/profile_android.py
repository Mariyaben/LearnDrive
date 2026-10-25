"""Capture physical-device acceptance evidence; never substitutes host results."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--seconds',type=int,default=1801)
parser.add_argument('--output',type=Path,default=Path('build/device-profile.json'))
args=parser.parse_args()
device=subprocess.check_output(['adb','get-state'],text=True).strip()
if device!='device':raise SystemExit('A physical Android device must be connected and authorized')
subprocess.run(['adb','logcat','-c'],check=True)
subprocess.run(['adb','shell','am','force-stop','ai.learndrive.mobile'],check=True)
subprocess.run(['adb','shell','am','start','-n','ai.learndrive.mobile/.MainActivity'],check=True)
process=subprocess.Popen(['adb','logcat','-v','brief','LearnDrivePerf:I','*:S'],stdout=subprocess.PIPE,text=True)
report={'device':subprocess.check_output(['adb','shell','getprop','ro.product.model'],text=True).strip(),
        'abi':subprocess.check_output(['adb','shell','getprop','ro.product.cpu.abi'],text=True).strip(),
        'emulator':subprocess.check_output(['adb','shell','getprop','ro.kernel.qemu'],text=True).strip()=='1',
        'duration_seconds':args.seconds,'warmup_seconds':30,'frames':[],'decision_timing':None,'provider':None}
started=time.monotonic()
try:
    # logcat emits a line each completed one-second inference window. A stalled
    # source is terminated by communicate's timeout in the finalizer below.
    import threading
    def read():
        for line in process.stdout:
            if 'provider ' in line:report['provider']=line.strip()
            if 'decisions ' in line:report['decision_timing']=line.strip()
            match=re.search(r'frames time=([\d.]+) fps=([\d.]+) inference_ms=([\d.]+) thermal=(\d+)',line)
            if match:report['frames'].append({'elapsed':time.monotonic()-started,'capture_clock':float(match[1]),
                'fps':float(match[2]),'inference_ms':float(match[3]),'thermal':int(match[4])})
    worker=threading.Thread(target=read,daemon=True);worker.start()
    while time.monotonic()-started<args.seconds+30:time.sleep(1)
finally:
    process.terminate();process.wait(timeout=10)
    worker.join(timeout=2)
report['elapsed_seconds']=time.monotonic()-started
fps=[f['fps'] for f in report['frames'] if f['elapsed']>30]
timing=report['decision_timing'] or ''
maximum=re.search(r'max_us=([\d.]+)',timing)
windows=[f for f in report['frames'] if f['elapsed']>30]
covered=bool(windows and windows[-1]['elapsed']-windows[0]['elapsed']>=args.seconds-3 and
    report['elapsed_seconds']-windows[-1]['elapsed']<2 and all(b['elapsed']-a['elapsed']<2 for a,b in zip(windows,windows[1:])))
report['pass']=bool(args.seconds>1800 and covered and fps and min(fps)>=20 and maximum and
    float(maximum[1])<1000 and max(f['thermal'] for f in report['frames'])<2 and
    report['abi']=='arm64-v8a' and not report['emulator'])
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2)+'\n')
print(args.output,'PASS' if report['pass'] else 'FAIL / incomplete device evidence')
raise SystemExit(0 if report['pass'] else 1)
