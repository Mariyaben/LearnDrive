"""Bounded local-only shadow proposals; never accepts or plays a proposal.

An existing local adapter reads one JSON request on stdin and writes exactly
one proposal on stdout. The command is supplied explicitly as a JSON argv
file. This tool never fetches weights. Offline environment flags are hints,
not an OS network sandbox. Native SafetyValidator is the action authority.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import time

from coaching_contract import canonical, load_fixture, sha256, strict_json, validate_proposal, validate_shadow_fixture
from replay_coaching import BLOCKS, NativeBridge, ROOT, percentiles, provenance


def execute(command, request, timeout_ms=150, max_output_bytes=65536, max_rss_mib=1024):
    """Poll bounded files, deadline and optional psutil process-tree RSS hook."""
    environment = dict(os.environ, HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", HF_DATASETS_OFFLINE="1")
    try:
        import psutil
    except ImportError:
        psutil = None
    started = time.perf_counter()
    status, rss_peak, stderr_text = "completed", None, ""
    with tempfile.TemporaryDirectory(prefix="learndrive-shadow-") as folder:
        folder = Path(folder)
        input_path, output_path, error_path = (folder / name for name in ("request.json", "stdout", "stderr"))
        input_path.write_text(canonical(request), encoding="utf-8")
        with input_path.open("rb") as stdin, output_path.open("wb") as stdout, error_path.open("wb") as stderr:
            process = subprocess.Popen(command, stdin=stdin, stdout=stdout, stderr=stderr,
                                       env=environment, shell=False, creationflags=(subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0))
            while process.poll() is None:
                if (time.perf_counter() - started) * 1000 >= timeout_ms:
                    status = "timeout"
                elif output_path.stat().st_size + error_path.stat().st_size > max_output_bytes:
                    status = "output_limit"
                if psutil:
                    try:
                        parent = psutil.Process(process.pid)
                        rss = sum(p.memory_info().rss for p in [parent, *parent.children(recursive=True)])
                        rss_peak = max(rss_peak or 0, rss)
                        if rss > max_rss_mib * 1024 * 1024:
                            status = "rss_limit"
                    except (psutil.NoSuchProcess, psutil.AccessDenied):
                        pass
                if status != "completed":
                    if psutil:
                        try:
                            for child in psutil.Process(process.pid).children(recursive=True):
                                child.kill()
                        except (psutil.NoSuchProcess, psutil.AccessDenied):
                            pass
                    process.kill()
                    break
                time.sleep(.005)
            process.wait(timeout=2)
        if output_path.stat().st_size + error_path.stat().st_size > max_output_bytes:
            status = "output_limit"
        if status == "completed" and process.returncode:
            status = "adapter_error"
        output = output_path.read_bytes()[:max_output_bytes].decode("utf-8", errors="replace")
        stderr_text = error_path.read_bytes()[:2048].decode("utf-8", errors="replace")
    return {"status": status, "elapsed_ms": (time.perf_counter() - started) * 1000,
            "return_code": process.returncode, "output": output, "stderr": stderr_text,
            "rss_peak_bytes": rss_peak, "rss_monitor": "sampled_process_tree_psutil" if psutil else "unavailable",
            "resource_limits": "polled deadline/output cap; sampled RSS when psutil available, not a hard OS sandbox"}


def checked_proposal(raw, world, bridge, now):
    value = validate_proposal(strict_json(raw), len(world["candidates"]))
    if value["candidate_index"] is None:
        return value, 0
    candidate = world["candidates"][value["candidate_index"]]
    if value["mode"] == "WARN" and not (candidate["urgent"] or candidate["critical"]):
        raise ValueError("model cannot promote a routine candidate to WARN")
    if value["mode"] != "WARN" and (candidate["urgent"] or candidate["critical"]):
        raise ValueError("model cannot downgrade an urgent candidate to routine coaching")
    # Same C++ validator used by deterministic selection, logical acceptance,
    # and playback. A valid proposal remains shadow-only and is never accepted.
    return value, bridge.validate(value["candidate_index"], now)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixtures", nargs="+", type=Path)
    parser.add_argument("--library", type=Path, default=ROOT / "build/native-spatial/liblearndrive_coaching_replay.dll")
    parser.add_argument("--command-json", type=Path, help="Existing local adapter argv JSON list; never a shell command")
    parser.add_argument("--model-artifact", action="append", type=Path, default=[], help="Existing local weight/tokenizer/config files to pin")
    parser.add_argument("--timeout-ms", type=int, default=150)
    parser.add_argument("--max-output-bytes", type=int, default=65536)
    parser.add_argument("--max-rss-mib", type=int, default=1024)
    parser.add_argument("--max-samples", type=int, default=20)
    parser.add_argument("--output", type=Path, default=ROOT / "build/balanced-coaching/shadow-benchmark.json")
    args = parser.parse_args()
    if not 1 <= args.timeout_ms <= 60000 or not 1024 <= args.max_output_bytes <= 1024 * 1024 or not 16 <= args.max_rss_mib <= 16384 or not 1 <= args.max_samples <= 1000:
        parser.error("Resource budgets out of bounds")
    fixtures = [(path, validate_shadow_fixture(load_fixture(path))) for path in args.fixtures]
    common = {"schema_version": 1, "shadow_only": True,
              "input_policy":"independent_project_authored_synthetic_only; no Google/Android exports or unverified recorded content",
              "fixture_sha256": {str(path.resolve()): sha256(path) for path, _ in fixtures},
              "instructor_labels": "unavailable; no precision/recall or superiority score",
              "limits": ["Explicitly supplied local artifacts only; no model downloads or external service calls by this runner",
                         "Caller-provided adapter is trusted local executable code; offline environment hints are not network isolation",
                         "Candidate proposals are schema constrained and checked by shared native SafetyValidator; never sent to acceptance, PCM or live app",
                         "Selected publication samples are a disclosed subset, not full 100 Hz model execution; timing includes fresh adapter process startup",
                         "Rationale is bounded diagnostic model text, not a spoken instruction or instructor judgment"]}
    if args.command_json is None or not args.model_artifact:
        report = {**common, "status": "unavailable", "reason": "No existing local pretrained policy adapter and model artifacts supplied", "samples": []}
    else:
        command = strict_json(args.command_json.read_text(encoding="utf-8"))
        if not isinstance(command, list) or not command or not all(isinstance(p, str) and p for p in command):
            raise ValueError("command JSON must be a nonempty argv list")
        if any(not p.is_file() for p in args.model_artifact):
            raise ValueError("Every model artifact must already exist locally")
        model_hashes = {str(p.resolve()): sha256(p) for p in args.model_artifact}
        adapter_paths = {Path(p).resolve() for p in command if Path(p).is_file()}
        executable = shutil.which(command[0])
        if executable:
            adapter_paths.add(Path(executable).resolve())
        adapter_hashes = {str(p): sha256(p) for p in sorted(adapter_paths)}
        command_hash = sha256(args.command_json)
        before = provenance(args.library, args.fixtures)
        samples = []
        for path, fixture in fixtures:
            bridge = NativeBridge(args.library)
            settings = None
            try:
                rows = fixture["observations"]
                step = max(1, (len(rows) + args.max_samples - 1) // args.max_samples)
                for index, row in enumerate(rows):
                    new = (row["profile"], row["voice"], row["revision"])
                    if new != settings:
                        if not bridge.configure(row):
                            continue
                        settings = new
                    if not bridge.world(row["world"], row["published_at"]):
                        continue
                    deterministic = bridge.decide(index + 1)
                    if index % step:
                        continue
                    request = {"schema_version": 1, "task": "shadow_coaching_proposal", "clock": row["published_at"],
                               "observation": row["world"], "profile": row["profile"], "revision": row["revision"],
                               "output_contract": {"schema_version": 1, "mode": "SILENCE|COACH|WARN|DEFER",
                                                   "candidate_index": "zero-based index or null for SILENCE/DEFER", "rationale": "diagnostic string <=512 characters"}}
                    result = execute(command, request, args.timeout_ms, args.max_output_bytes, args.max_rss_mib)
                    result.update(fixture_id=fixture["fixture_id"], observation_index=index, time=row["published_at"],
                                  observation_sha256=__import__("hashlib").sha256(canonical(row["world"]).encode()).hexdigest(),
                                  deterministic={k: deterministic[k] for k in ("mode", "prompt", "eligible", "blocked", "candidate_index")})
                    if result["status"] == "completed":
                        try:
                            proposal, block = checked_proposal(result["output"], row["world"], bridge, row["published_at"])
                            result.update(proposal=proposal, validator_block=BLOCKS[block], proposal_allowed=block == 0)
                        except (ValueError, json.JSONDecodeError) as error:
                            result.update(status="invalid_proposal", error=str(error))
                    result.pop("output", None)
                    samples.append(result)
            finally:
                bridge.close()
        if before != provenance(args.library, args.fixtures) or model_hashes != {str(p.resolve()): sha256(p) for p in args.model_artifact} or sha256(args.command_json) != command_hash or adapter_hashes != {str(p): sha256(p) for p in sorted(adapter_paths)}:
            raise RuntimeError("Model, input, adapter command, source or native binary changed during shadow benchmark")
        report = {**common, "status": "completed", "model_artifact_sha256": model_hashes,
                  "adapter_command": command, "adapter_command_sha256": command_hash,
                  "adapter_runtime_sha256": adapter_hashes, "native_provenance": before,
                  "timeout_ms": args.timeout_ms, "max_output_bytes": args.max_output_bytes, "max_rss_mib": args.max_rss_mib,
                  "elapsed_ms": percentiles([r["elapsed_ms"] for r in samples]), "samples": samples}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(report["status"], args.output)


if __name__ == "__main__":
    main()
