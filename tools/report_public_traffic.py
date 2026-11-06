"""Produce the local public-video evidence report and interactive review index."""
import csv
import html
import json
from pathlib import Path
import sys

from test_public_traffic import ROOT, audit_report


def strict_json(path):
    def invalid(value):
        raise ValueError(f"Non-standard JSON constant {value}: {path}")
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=invalid)


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/public-traffic"
    manifest = strict_json(ROOT / "scenarios/public-traffic/sources.json")
    findings = strict_json(ROOT / "scenarios/public-traffic/review-findings.json")
    with (ROOT / "scenarios/public-traffic/review-events.csv").open(newline="") as source:
        references = list(csv.DictReader(source))
    results = []
    for clip in manifest["clips"]:
        for folder in sorted((root / "runs").glob(clip["id"] + "-*")):
            if not (folder / "report.json").exists():
                continue
            report = strict_json(folder / "report.json")
            strict_json(folder / "observations.json")
            mode = report["assumptions"]["mode"]
            speed = report["assumptions"]["manual_test_speed_kmh"]
            audited = audit_report(folder, mode, speed)
            overlaps = []
            for reference in references:
                if reference["clip"] != clip["id"]:
                    continue
                ids = {int(p) for p in reference["accepted_prompt_ids"].split("|")}
                matches = [{"time": e["time"], "prompt": e["prompt"]} for e in report["events"]
                           if float(reference["start"]) <= e["time"] <= float(reference["end"]) and e["prompt"] in ids]
                overlaps.append({"event_id": reference["event_id"], "compatible_cue_windows": matches,
                                 "interpretation": "Window overlap only; actor association and correct timing are not established"})
            results.append({"clip": clip["id"], "mode": mode, "manual_test_speed_kmh": speed,
                            "hfov": report["assumptions"]["horizontal_fov_degrees"],
                            "folder": folder.relative_to(root).as_posix(),
                            "video": (folder.relative_to(root) / "guidance-review.mp4").as_posix()
                            if (folder / "guidance-review.mp4").exists() else None,
                            "poster": (folder.relative_to(root) / "poster.jpg").as_posix()
                            if (folder / "poster.jpg").exists() else None,
                            "source_page": clip["source_page"], "conditions": clip["conditions"],
                            "reference_windows": overlaps, **audited})
    primary = [r for r in results if r["manual_test_speed_kmh"] == 30 and (r["mode"] == "semantic" or r["hfov"] == 110)]
    if len(primary) != 8:
        raise ValueError(f"Expected eight primary runs, found {len(primary)}")
    per_clip = {r["clip"]: r["frames"] for r in primary}
    aggregate = {"source_clips": manifest, "unique_frames": sum(per_clip.values()),
                 "run_count": len(results), "evaluated_frames": sum(r["frames"] for r in results),
                 "contract_checks": "pass", "guidance_acceptance": "not passed: exploratory visual review found false and irrelevant cues",
                 "findings": findings, "runs": results,
                 "limitations": ["Four short public clips are now development data, not held-out validation",
                    "Manual 30 km/h is a sensitivity input, not measured ego speed; missing-speed cases are separate",
                    "Spatial experiments use assumed pinhole optics and level pose, without original calibration",
                    "US clips are out-of-domain; current model traffic convention is configured for India",
                    "640x360 resizing and mixed source frame rates affect perception; no phone latency/thermal claim",
                    "No true-collision ground truth or independent instructor labels; no precision/recall certification"]}
    (root / "aggregate.json").write_text(json.dumps(aggregate, indent=2, allow_nan=False), encoding="utf-8")
    lines = ["# Public traffic video test results", "", "Contract checks pass. **Guidance acceptance does not pass this exploratory review.**", "",
             f"Four source clips; {aggregate['unique_frames']:,} unique analyzed frames; {len(results)} replay configurations. All clips were processed in full.", "",
             "Primary inputs: manual test speed 30 km/h (not a measurement), 180 ms simulated latency, six-second startup pre-roll. Spatial cases assume 110-degree HFOV; semantic cases have no original optics.", "",
             "| Clip | Mode | Frames | Ordinary cues | Road-valid frames | Speech during clip | Emergency candidate ticks |",
             "| --- | --- | ---: | ---: | ---: | ---: | ---: |"]
    for r in primary:
        lines.append(f"| {r['clip']} | {r['mode']} | {r['frames']} | {r['spoken_events']} | {r['road_valid_frames']} | {r['speech_fraction']:.1%} | {r['emergency_candidate_ticks']} |")
    lines += ["", "## Visual findings", ""]
    for f in findings["findings"]:
        lines.append(f"- **{f['id']} — {f['classification']} ({f['clip']})**: {f['observation']}")
    lines += ["", "Speech occupancy includes interruption and clipping at the video end. It is not a driver-distraction measurement.", "",
              "Reference-event windows were written before the initial inference run. Generic cue overlap is not credited as event/actor recognition. False-cue discoveries are post-hoc exploratory findings to become development regressions.", "",
              "## Calibration sensitivity", "",
              "The winding-road experiment at assumed 70-degree HFOV produces 127 road-valid frames out of 1,199 and bend cues at 21.38 s (left) and 33.94 s (right). At assumed 110 degrees, none of those frames pass the road gate. This supports a calibration dependency, not a claim that 70 degrees is correct or that the model has proven bend recall.", "",
              "## Missing-speed controls", "",
              "Four semantic replays with no speed input preserve null speed in observations and traces. Road-user/obstacle wording becomes less speed-specific. The false obstacle at the end of the winding-road clip still occurs, so missing speed does not explain that detector failure.", "",
              "## Limits", "", *["- " + note for note in aggregate["limitations"]], "", "## Sources", ""]
    for clip in manifest["clips"]:
        lines.append(f"- [{clip['id']}]({clip['source_page']}): {clip['attribution']}; source SHA-256 `{clip['sha256']}`.")
    (root / "RESULTS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    data = json.dumps(aggregate).replace("<", "\\u003c")
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LearnDrive public-video review</title><style>body{font:16px system-ui;margin:24px auto;max-width:1200px;padding:0 16px;background:#101821;color:#e9eff6}a{color:#8dccff}select{padding:10px;width:100%;font:inherit}video{width:100%;max-height:68vh;background:#000}table{border-collapse:collapse;width:100%}td,th{padding:8px;border-bottom:1px solid #405060;text-align:left}button{margin:5px;padding:10px;cursor:pointer}.note{color:#ffd27d}article{margin:24px 0}p{line-height:1.5}</style>
<h1>LearnDrive public traffic review</h1><p class="note">Development tests. Guidance acceptance has not passed: false obstacle and irrelevant traffic cues were observed.</p>
<p id="counts"></p><p>Manual 30 km/h is an assumed test setting. Unknown-speed runs are separate. Spatial optics are assumed; these are shared-core desktop replays, not phone tests.</p>
<select id="run"></select><p id="context"></p><video id="player" controls preload="metadata"></video><div id="cues"></div><p id="stats"></p>
<p><a id="report">Raw report</a> · <a id="trace">Decision trace</a> · <a href="RESULTS.md">Written results</a> · <a href="aggregate.json">All measurements</a></p>
<h2>Reviewed problems</h2><div id="findings"></div><h2>Primary comparison</h2><table><thead><tr><th>Clip</th><th>Mode</th><th>Cues</th><th>Road valid</th><th>Speech</th></tr></thead><tbody id="table"></tbody></table>
<h2>Limits</h2><ul id="limits"></ul><script>const data=DATA;
const $=id=>document.getElementById(id);$('counts').textContent=`${data.source_clips.clips.length} source clips · ${data.unique_frames} unique analyzed frames · ${data.run_count} configurations`;
data.runs.forEach((r,i)=>{const o=document.createElement('option');o.value=i;o.textContent=`${r.clip} / ${r.mode} / ${r.manual_test_speed_kmh===null?'speed unknown':'manual '+r.manual_test_speed_kmh+' km/h'}${r.hfov?' / assumed '+r.hfov+'°':''}`;$('run').append(o)});
function show(){const r=data.runs[+$('run').value];$('context').textContent=r.conditions;if(r.video){$('player').src=r.video;$('player').hidden=false}else{$('player').removeAttribute('src');$('player').hidden=true}$('cues').replaceChildren();r.events.forEach(e=>{const b=document.createElement('button');b.textContent=e.time.toFixed(2)+'s: '+e.text;b.onclick=()=>{$('player').currentTime=e.time;$('player').play()};$('cues').append(b)});$('stats').textContent=`${r.frames} frames · ${r.spoken_events} ordinary cues · ${r.road_valid_frames} road-valid frames · ${(100*r.speech_fraction).toFixed(1)}% speech occupancy · ${r.emergency_candidate_ticks} emergency candidate ticks`;$('report').href=r.folder+'/report.json';$('trace').href=r.folder+'/decisions.jsonl'}$('run').onchange=show;show();
data.findings.findings.forEach(f=>{const a=document.createElement('article');const h=document.createElement('h3');h.textContent=f.id+' — '+f.classification;const p=document.createElement('p');p.textContent=f.observation;const l=document.createElement('a');l.href=f.evidence;l.textContent='Open cue/source frames';a.append(h,p,l);$('findings').append(a)});
data.runs.filter(r=>r.manual_test_speed_kmh===30&&(r.mode==='semantic'||r.hfov===110)).forEach(r=>{const tr=document.createElement('tr');[r.clip,r.mode,r.spoken_events,r.road_valid_frames+'/'+r.frames,(100*r.speech_fraction).toFixed(1)+'%'].forEach(v=>{const td=document.createElement('td');td.textContent=v;tr.append(td)});$('table').append(tr)});data.limitations.forEach(x=>{const li=document.createElement('li');li.textContent=x;$('limits').append(li)});
function cueButtons(){const r=data.runs[+$('run').value];$('player').poster=r.poster||'';const missing=$('player').hidden;$('cues').querySelectorAll('button').forEach(b=>b.disabled=missing);if(missing)$('context').textContent+=' No annotated video was generated for this control; use its raw report.'}$('run').addEventListener('change',cueButtons);cueButtons();
</script></html>'''.replace("DATA", data)
    (root / "index.html").write_text(page, encoding="utf-8")
    print(json.dumps({"unique_frames": aggregate["unique_frames"], "runs": len(results),
                      "contracts": "pass", "guidance_acceptance": aggregate["guidance_acceptance"]}))


if __name__ == "__main__":
    main()
