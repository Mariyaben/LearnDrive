"""Summarize bench map-on/off/on counters; never infer watts or GPU cost.

Usage: python tools/analyze_thermal_map_comparison.py capture.jsonl [--output report.json]
       python tools/analyze_thermal_map_comparison.py --self-test
"""
import argparse
import json
import math
from pathlib import Path


PHASES = ("on_1", "off", "on_2")
COUNTS = ("frames", "frame_failures", "detector_calls", "detector_failures",
          "spatial_calls", "spatial_native_completions", "spatial_failures")
TOTALS = ("cpu_ms", "frame_ms_total", "detector_ms_total", "spatial_ms_total")
ERRORS = ("frame_failures", "detector_failures", "spatial_failures")
IDENTITY = ("provider", "source", "vision_source", "vision_instance", "setup",
            "guiding", "route_ready", "navigation_session", "route_epoch")
BOUNDS = ("width", "height", "rotation", "map_width", "map_height")
NUMERIC = COUNTS + TOTALS + ("elapsed_s", "capture_s", "published_s", "camera_age_s")


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def load_jsonl(path):
    records, errors = [], []
    for line_number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError("record must be an object")
            records.append(value)
        except (ValueError, json.JSONDecodeError) as error:
            errors.append(f"line {line_number}: {error}")
    return records, errors


def phase_summary(samples, settling_count):
    result = {"measured_samples": len(samples), "settling_samples": settling_count}
    if not samples:
        return result
    first, last = samples[0], samples[-1]
    result.update(start_elapsed_s=first.get("elapsed_s"), end_elapsed_s=last.get("elapsed_s"),
                  first_thermal=first.get("thermal"), last_thermal=last.get("thermal"))
    if len(samples) < 2 or not all(number(s.get(k)) for s in (first, last) for k in NUMERIC):
        return result
    duration = last["elapsed_s"] - first["elapsed_s"]
    delta = {key: last[key] - first[key] for key in COUNTS + TOTALS}
    result.update(duration_s=duration, deltas=delta)
    if duration <= 0 or any(value < 0 for value in delta.values()):
        return result
    result.update(
        process_cpu_percent=delta["cpu_ms"] / duration / 10,
        completed_fps=delta["frames"] / duration,
        detector_hz=delta["detector_calls"] / duration,
        detector_mean_run_output_copy_ms=(delta["detector_ms_total"] / delta["detector_calls"]
                                          if delta["detector_calls"] else None),
        spatial_native_completions_hz=delta["spatial_native_completions"] / duration,
        spatial_mean_stage_ms=(delta["spatial_ms_total"] / delta["spatial_calls"]
                               if delta["spatial_calls"] else None),
        frame_stage_duty_fraction=delta["frame_ms_total"] / duration / 1000,
    )
    return result


def analyze(records, parse_errors=()):
    criteria, reasons = {}, list(parse_errors)

    def require(name, condition, detail):
        criteria[name] = criteria.get(name, True) and bool(condition)
        if not condition and detail not in reasons:
            reasons.append(detail)

    headers = [r for r in records if r.get("event") == "header"]
    completes = [r for r in records if r.get("event") == "complete"]
    aborted = [r.get("reason", "unspecified abort") for r in records if r.get("event") == "aborted"]
    samples = [r for r in records if r.get("event") == "sample"]
    phase_events = [r.get("phase") for r in records if r.get("event") == "phase"]
    header = headers[0] if headers else {}
    summaries = {phase: phase_summary(
        [s for s in samples if s.get("phase") == phase and s.get("settling") is False],
        sum(s.get("phase") == phase and s.get("settling") is True for s in samples)) for phase in PHASES}
    declared_seconds = header.get("phase_seconds")
    valid_header = len(headers) == 1 and number(declared_seconds) and 20 <= declared_seconds <= 60
    target = declared_seconds if valid_header else 20
    windows_complete = all(s.get("duration_s", 0) + 1e-6 >= target for s in summaries.values())
    collection_complete = bool(not parse_errors and valid_header and len(completes) == 1 and not aborted
                               and phase_events == list(PHASES) and windows_complete
                               and records and records[-1].get("event") == "complete")
    require("collection_complete", collection_complete,
            "Collection is incomplete, aborted, malformed, or lacks all declared measured windows.")
    if aborted:
        reasons.extend(f"Aborted: {reason}" for reason in aborted)
    reported = completes[-1].get("matched_workload") if completes else None
    if not isinstance(reported, bool):
        reported = None
    require("samples_present", bool(samples), "No sampled workload evidence is available.")
    require("known_sample_phases", all(s.get("phase") in (*PHASES, "eligibility") for s in samples),
            "A sample has an unknown phase.")
    observed_phases = []
    for s in samples:
        phase = s.get("phase")
        if not observed_phases or observed_phases[-1] != phase:
            observed_phases.append(phase)
    require("sample_phase_order", observed_phases == ["eligibility", *PHASES],
            "Sample phases are missing, repeated out of order, or interleaved.")
    require("sample_values_valid", all(
        all(number(s.get(k)) and s[k] >= 0 for k in NUMERIC)
        and all(integer(s[k]) for k in COUNTS)
        for s in samples), "A sample has missing, nonfinite, negative, or noninteger counters/times.")
    valid_samples = bool(samples) and criteria["sample_values_valid"]
    baseline = next((s for s in samples if s.get("phase") == "eligibility"), None)
    require("eligibility_baseline", baseline is not None, "Eligibility baseline is missing.")
    baseline = baseline or (samples[0] if samples else {})
    require("identity_present", all(key in baseline for key in IDENTITY), "Baseline identity fields are missing.")
    require("identity_stable", all(all(s.get(k) == baseline.get(k) for k in IDENTITY) for s in samples),
            "Provider, source, model instance, setup, route, session or guidance state changed.")
    require("identity_valid", bool(samples) and baseline.get("provider") in ("google", "offline")
            and all(integer(baseline.get(k)) and baseline[k] > 0 for k in ("source", "vision_source", "vision_instance"))
            and baseline.get("source") == baseline.get("vision_source"), "Baseline source/provider identity is invalid.")
    require("camera_bounds_stable", all(all(s.get(k) == baseline.get(k) for k in BOUNDS) for s in samples),
            "Camera geometry or map host bounds changed.")
    require("camera_bounds_valid", bool(samples) and all(number(baseline.get(k)) and baseline[k] > 0
            for k in ("width", "height", "map_width", "map_height")) and baseline.get("rotation") in (0, 90, 180, 270),
            "Camera/map dimensions or rotation are invalid.")
    require("foreground_camera_fresh", all(s.get("foreground") is True and s.get("camera_running") is True
            and number(s.get("camera_age_s")) and 0 <= s["camera_age_s"] <= .8 for s in samples),
            "Foreground camera stopped, locked, or exceeded the 0.8-second freshness bound.")
    require("ready4", all(s.get("readiness") == 4 for s in samples), "Spatial readiness was not consistently 4.")
    require("state_types", all(all(integer(s.get(k)) for k in ("thermal", "plugged", "readiness", "setup"))
            and all(isinstance(s.get(k), bool) for k in ("guiding", "route_ready", "foreground", "camera_running",
                                                        "renderer_suspended", "map_rendered", "power_saver")) for s in samples),
            "A state field has an invalid type.")
    require("not_charging", all(s.get("plugged") == 0 for s in samples), "Charging was present or unknown.")
    require("power_saver_off", all(s.get("power_saver") is False for s in samples),
            "Power saver was enabled or was not recorded in every sample.")
    require("thermal_constant_zero", all(s.get("thermal") == 0 for s in samples),
            "Thermal state changed or was not the required cooled status 0.")
    require("no_new_errors", all(all(s.get(k) == baseline.get(k) for k in ERRORS) for s in samples),
            "Frame, detector or spatial failure counters increased.")
    require("model_count_consistency", valid_samples and all(
        s["spatial_native_completions"] == s["spatial_calls"] - s["spatial_failures"]
        and s["detector_failures"] <= s["detector_calls"] for s in samples),
        "Native completion, model call or failure counts are inconsistent.")
    require("renderer_states", all(
        s.get("renderer_suspended") is (s.get("phase") == "off")
        and (s.get("phase") == "off" or s.get("map_rendered") is True) for s in samples),
        "An on-phase map was not rendered or renderer suspension did not match its phase.")

    monotonic = fresh_clocks = gaps = valid_samples
    if valid_samples:
        for a, b in zip(samples, samples[1:]):
            monotonic &= b["elapsed_s"] > a["elapsed_s"] and all(b[k] >= a[k] for k in COUNTS + TOTALS + ("capture_s", "published_s"))
            gaps &= 0 < b["elapsed_s"] - a["elapsed_s"] <= 6.5
        fresh_clocks = all(s["capture_s"] <= s["published_s"] <= s["elapsed_s"] for s in samples)
    require("monotonic_counters_time", monotonic, "Sample time or cumulative counters moved backwards/reset.")
    require("sample_gaps", gaps, "Sample gaps exceed 6.5 seconds or cannot be validated.")
    require("capture_clock_valid", fresh_clocks, "Capture/publication clocks are invalid or in the future.")

    def tilt_matches(sample):
        a, b = baseline.get("camera_up"), sample.get("camera_up")
        if not all(isinstance(v, list) and len(v) == 3 and all(number(x) for x in v) for v in (a, b)):
            return False
        norm = math.sqrt(sum(x*x for x in a) * sum(x*x for x in b))
        return norm > 0 and math.degrees(math.acos(max(-1, min(1, sum(x*y for x, y in zip(a, b))/norm)))) <= 2

    require("tilt_stable", all(tilt_matches(s) for s in samples), "Camera up vector is missing or tilt changed by more than 2 degrees.")
    require("measured_duration", all(s.get("duration_s", 0) >= 20 for s in summaries.values()),
            "Every phase needs at least 20 measured seconds.")
    require("spatial_every_frame", all(s.get("deltas", {}).get("frames", 0) > 0
            and s["deltas"]["frames"] == s["deltas"]["spatial_native_completions"] for s in summaries.values()),
            "Completed frames and successful native spatial completions differ, or no frames completed.")
    for metric, label in (("detector_hz", "detector"), ("spatial_native_completions_hz", "spatial")):
        values = [s.get(metric) for s in summaries.values()]
        matched = all(number(v) and v > 0 for v in values) and max(values) / min(values) <= 1.2
        require(f"{label}_rates_matched", matched, f"{label.title()} call rates are missing or differ by more than 20%.")
    independently_matched = all(criteria.values())
    cpu = {phase: summaries[phase].get("process_cpu_percent") for phase in PHASES}
    comparison = None
    if independently_matched:
        endpoints = (cpu["on_1"] + cpu["on_2"]) / 2
        comparison = {"on_endpoints_mean_cpu_percent": endpoints,
                      "on_minus_off_cpu_percentage_points": endpoints - cpu["off"],
                      "method": "Equal-weight average of on_1/on_2 CPU endpoints minus off."}
    return {
        "collection_complete": collection_complete, "reported_matched": reported,
        "independently_matched": independently_matched, "criteria": criteria, "reasons": reasons,
        "sample_count": len(samples), "baseline_failures": {k: baseline.get(k) for k in ERRORS},
        "aborted_reasons": aborted, "header": header, "phases": summaries,
        "process_cpu_percent": cpu, "comparison": comparison,
        "interpretation": ("Short comparison meets sampled matching checks; CPU difference is descriptive."
                           if independently_matched else "Partial or unmatched evidence; no causal conclusion."),
        "limits": ["100% process CPU represents one core; this is not watts or GPU utilization.",
                   "Spatial duration includes geometry/packing/JNI/inference, not pure model latency.",
                   "Frame-stage duty is elapsed processing time divided by measured wall time, not CPU utilization.",
                   "Error stability is checked against eligibility; historical baseline failures are reported separately.",
                   "Camera up vectors check tilt only; yaw and translation are not independently measured.",
                   "Initialization benchmarks are outside these counters; this is not sustained thermal qualification."],
    }


def self_test():
    import unittest

    def fixture():
        records = [{"event": "header", "phase_seconds": 20}]
        def sample(t, phase, settling, cpu):
            return dict(event="sample", phase=phase, settling=settling, elapsed_s=t, cpu_ms=cpu,
                        source=1, vision_source=1, vision_instance=100, setup=0, provider="offline",
                        guiding=False, route_ready=False, navigation_session=None, route_epoch=None,
                        thermal=0, plugged=0, power_saver=False, foreground=True, camera_running=True, camera_age_s=.1,
                        renderer_suspended=phase == "off", map_rendered=phase != "off",
                        map_width=400, map_height=300, width=640, height=480, rotation=0,
                        camera_up=[0, 1, 0], capture_s=t-.2, published_s=t-.1, readiness=4,
                        frames=t*10, frame_failures=0, frame_ms_total=t*700,
                        detector_calls=t*5, detector_failures=0, detector_ms_total=t*50,
                        spatial_calls=t*10, spatial_native_completions=t*10,
                        spatial_failures=0, spatial_ms_total=t*400)
        records.append(sample(100, "eligibility", False, 0))
        t, cpu = 100, 0
        for phase in PHASES:
            records.append(dict(event="phase", phase=phase, elapsed_s=t))
            rate = 100 if phase == "off" else 150
            for index in range(6):
                t += 5; cpu += rate*50
                records.append(sample(t, phase, index < 2, cpu))
                if index == 1:
                    # The real protocol records a separate measured start after settling.
                    t += .001
                    s = sample(t, phase, False, cpu)
                    for key in COUNTS:
                        s[key] = int(s[key])
                    records.append(s)
        records.append(dict(event="complete", matched_workload=True))
        # Integer calls must not depend on fixture's fractional measured-start clock.
        for r in records:
            if r.get("event") == "sample":
                for key in COUNTS:
                    r[key] = int(r[key])
        return records

    class Tests(unittest.TestCase):
        def test_valid(self):
            result = analyze(fixture())
            self.assertTrue(result["independently_matched"], result["reasons"])
            self.assertAlmostEqual(result["process_cpu_percent"]["off"], 100)
            self.assertAlmostEqual(result["comparison"]["on_minus_off_cpu_percentage_points"], 50)

        def test_aborted(self):
            data = fixture()[:8] + [dict(event="aborted", reason="Thermal state changed")]
            result = analyze(data)
            self.assertFalse(result["collection_complete"])
            self.assertIsNone(result["reported_matched"])
            self.assertIsNone(result["comparison"])
            self.assertGreater(result["sample_count"], 0)

        def test_mismatched(self):
            data = fixture()
            next(r for r in data if r.get("event") == "sample" and r.get("phase") == "off")["plugged"] = 2
            result = analyze(data)
            self.assertTrue(result["collection_complete"])
            self.assertTrue(result["reported_matched"])
            self.assertFalse(result["independently_matched"])
            self.assertFalse(result["criteria"]["not_charging"])

        def test_counter_reset(self):
            data = fixture()
            next(r for r in data if r.get("event") == "sample" and r.get("phase") == "off")["frames"] = 1
            self.assertFalse(analyze(data)["criteria"]["monotonic_counters_time"])

        def test_intermediate_thermal_change_with_cooled_endpoints(self):
            data = fixture()
            measured = [r for r in data if r.get("event") == "sample"
                        and r.get("phase") == "off" and r.get("settling") is False]
            measured[len(measured)//2]["thermal"] = 1
            self.assertEqual((measured[0]["thermal"], measured[-1]["thermal"]), (0, 0))
            result = analyze(data)
            self.assertTrue(result["collection_complete"])
            self.assertTrue(result["reported_matched"])
            self.assertFalse(result["criteria"]["thermal_constant_zero"])
            self.assertFalse(result["independently_matched"])
            self.assertIsNone(result["comparison"])

        def test_unrendered_second_on_phase_is_not_a_matched_comparison(self):
            data = fixture()
            measured = [r for r in data if r.get("event") == "sample"
                        and r.get("phase") == "on_2" and r.get("settling") is False]
            current = measured[len(measured)//2]
            self.assertIs(current["renderer_suspended"], False)
            current["map_rendered"] = False
            result = analyze(data)
            self.assertTrue(result["collection_complete"])
            self.assertTrue(result["reported_matched"])
            self.assertFalse(result["criteria"]["renderer_states"])
            self.assertFalse(result["independently_matched"])
            self.assertIsNone(result["comparison"])

        def test_enabled_power_saver_rejects_matching_cpu_windows(self):
            data = fixture()
            next(r for r in data if r.get("event") == "sample" and r.get("phase") == "off")["power_saver"] = True
            result = analyze(data)
            self.assertTrue(result["collection_complete"])
            self.assertFalse(result["criteria"]["power_saver_off"])
            self.assertFalse(result["independently_matched"])
            self.assertIsNone(result["comparison"])
            self.assertAlmostEqual(result["process_cpu_percent"]["off"], 100)

        def test_unrecorded_power_saver_keeps_partial_cpu_evidence(self):
            data = fixture()
            for record in data:
                record.pop("power_saver", None)
            result = analyze(data)
            self.assertTrue(result["collection_complete"])
            self.assertFalse(result["criteria"]["power_saver_off"])
            self.assertFalse(result["independently_matched"])
            self.assertIsNone(result["comparison"])
            self.assertAlmostEqual(result["process_cpu_percent"]["on_1"], 150)
            self.assertAlmostEqual(result["process_cpu_percent"]["off"], 100)
            self.assertAlmostEqual(result["process_cpu_percent"]["on_2"], 150)

    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    return 0 if result.wasSuccessful() else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.input:
        parser.error("input JSONL is required unless --self-test is used")
    try:
        records, errors = load_jsonl(args.input)
    except (OSError, UnicodeError) as error:
        records, errors = [], [str(error)]
    report = analyze(records, errors)
    output = json.dumps(report, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0 if report["independently_matched"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
