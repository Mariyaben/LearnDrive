"""Publish aggregate engineering evidence, never source video or recorded contexts."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import hashlib

ROOT=Path(__file__).resolve().parents[1]


def summarize(folder):
    report=json.loads((folder/'report.json').read_text());cases=[];examples=[];failures=[]
    for path in sorted(folder.glob('*.json')):
        if path.name in ('inputs.json','report.json','review-panels.json','review-unblinding.json'):continue
        case=json.loads(path.read_text())
        if 'model_samples' not in case:continue
        cases.append({'scenario_id':case['scenario_id'],'evidence_kind':case['evidence_kind'],
            'event_counts':{key:len(events) for key,events in case['events'].items()},
            'model_status_counts':dict(Counter(s['status'] for s in case['model_samples'])),
            'invocation_counters':case['invocation_counters']})
        for sample in case['model_samples']:
            if sample['status']!='completed':failures.append({'scenario_id':case['scenario_id'],'status':sample['status'],'error':sample.get('error'),
                'measured_wall_ms':sample.get('wall_ms'),'clock_scope':sample['clock_scope']})
            elif case['evidence_kind']=='synthetic' and len(examples)<3 and not any(e['raw_content']==sample['raw_content'] and e['proposal']['instruction_id']==sample['proposal']['instruction_id'] for e in examples):
                examples.append({k:sample[k] for k in ('scenario_id','input_sha256','raw_content','proposal','wall_ms','usage','timings','metadata_source','live_authority') if k in sample})
    return {'report':report,'cases':cases,'synthetic_actual_neural_examples':examples,'failures':failures,
        'latency_denominator':'Only returned samples with measured wall_ms. Failures are listed separately, never counted as fast calls.',
        'quality_metrics':None,'phone_model_execution':None}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folders',type=Path,nargs='+')
    p.add_argument('--output',type=Path,default=ROOT/'docs/intelligence/evidence/engineering-results.json');args=p.parse_args()
    checks=ROOT/'build/intelligence-v2'
    native=(checks/'current-ctest.txt').read_text();python=(checks/'current-python.txt').read_text()
    java=(checks/'current-java.txt').read_text();android=(checks/'android-build-final.txt').read_text()
    native_match=re.search(r'100% tests passed, 0 tests failed out of (\d+)',native)
    python_match=re.search(r'Ran (\d+) tests',python)
    if not native_match or not python_match or not re.search(r'^OK$',python,re.M):raise ValueError('Passing native/Python evidence missing')
    if '12 reasoning source/revision/generation/clock/thermal/cancellation gates passed' not in java or re.search(r'^(Exception|Error:|FAIL )',java,re.M):raise ValueError('Passing Java evidence missing')
    if 'BUILD SUCCESSFUL' not in android or 'BUILD FAILED' in android:raise ValueError('Successful Android build evidence missing')
    result={'date':'2026-10-27','candidate':'0.6.0','checks':{'native_ctest_groups':int(native_match[1]),'python_tests':int(python_match[1]),
        'java_policy_runner':'passed including12 reasoning gates','android_application_assemble':'passed',
        'android_instrumentation_assemble':'passed','android_device_execution':'unavailable; no ADB device'},
        'check_log_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (checks/'current-ctest.txt',checks/'current-python.txt',checks/'current-java.txt',checks/'android-build-final.txt')},
        'models':{folder.name:summarize(folder) for folder in args.folders}}
    apk=ROOT/'build/LearnDrive-0.6.0-debug.apk';proof=checks/'apk-verification.json'
    if apk.is_file() and proof.is_file():
        verification=json.loads(proof.read_text());apk_sha=hashlib.sha256(apk.read_bytes()).hexdigest()
        if verification.get('apk_sha256')==apk_sha:
            result['apk']={'path':'build/LearnDrive-0.6.0-debug.apk','bytes':apk.stat().st_size,'sha256':apk_sha,'verification':verification}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(args.output)


if __name__=='__main__':main()
