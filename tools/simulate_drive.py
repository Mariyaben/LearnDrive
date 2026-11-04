"""Record a MetaDrive simulator drive as LearnDrive replay inputs plus ground truth.

Run with the simulator venv (.tools/sim-venv, Python 3.11, MetaDrive 0.4.3):
    .tools/sim-venv/Scripts/python.exe tools/simulate_drive.py --seed 3 --output build/sim-drives/seed3

Outputs (all SIMULATED, labelled as such):
- drive.mp4        forward RGB camera at --fps
- speeds.csv       ground-truth ego speed (time,speed_kmh) for review_video.py --speeds
- navigation.json  turn timeline derived from the ego's actual route through junctions
                   (left/right only; straight-through and roundabouts listed separately)
- truth.json       per-step ego state, junction intervals and nearby traffic geometry

The expert IDM policy drives. It is not a learner, so learner responses are not
represented. Rendering is synthetic, so detector behaviour is a domain-gap test.
"""
import argparse
import json
import math
from pathlib import Path

import cv2
import numpy as np
from metadrive.component.sensors.rgb_camera import RGBCamera
from metadrive.envs.metadrive_env import MetaDriveEnv
from metadrive.policy.idm_policy import IDMPolicy

JUNCTION_BLOCKS = {"X": "intersection", "T": "t_junction", "O": "roundabout"}


def block_of(node):
    """MetaDrive node names encode the block, e.g. '2X1_0_' -> 'X' (or '>' for the start)."""
    text = str(node)
    for ch in text:
        if ch.isalpha():
            return ch
    return None


def wrap(angle):
    return (angle + math.pi) % (2 * math.pi) - math.pi


def scan_junctions(steps):
    """Junction intervals: contiguous steps on the turning roads, i.e. roads ENDING
    at a junction block node (MetaDrive's connecting lanes). The navigation
    maneuver point is mid-turn: the first step whose heading has changed by 45
    degrees, when a provider would advance to the next step."""
    junctions, current = [], None
    for s in steps:
        kind = JUNCTION_BLOCKS.get(block_of(s["road"][1]))
        if kind and current is None:
            current = {"kind": kind, "enter": s["time"], "heading_in": s["heading"], "turn_point": None}
        if current is not None and kind and current["turn_point"] is None and abs(wrap(s["heading"] - current["heading_in"])) > math.radians(45):
            current["turn_point"] = s["time"]
        if not kind and current is not None:
            current.update(exit=s["time"], heading_out=s["heading"])
            junctions.append(current); current = None
    for j in junctions:
        turn = math.degrees(wrap(j["heading_out"] - j["heading_in"]))
        j["turn_degrees"] = round(turn, 1)
        j["maneuver"] = "left" if turn > 35 else "right" if turn < -35 else "straight"
        if j["turn_point"] is None:
            j["turn_point"] = round((j["enter"] + j["exit"]) / 2, 2)
    turns = [{"at": j["turn_point"], "maneuver": j["maneuver"], "appear_lead_s": 45, "kind": j["kind"]}
             for j in junctions if j["maneuver"] in ("left", "right") and j["kind"] != "roundabout"]
    return junctions, turns


def write_route(output, junctions, turns):
    (output / "navigation.json").write_text(json.dumps({"synthetic": True, "source": "MetaDrive ground-truth route",
        "turns": turns, "other_junctions": [{k: j[k] for k in ("kind", "enter", "exit", "maneuver")} for j in junctions
                            if j["maneuver"] == "straight" or j["kind"] == "roundabout"]}, indent=1), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--map", default="SXSTSOS", help="MetaDrive block sequence (S straight, X/T junction, O roundabout, C curve)")
    parser.add_argument("--traffic", type=float, default=0.1)
    parser.add_argument("--seconds", type=float, default=90)
    parser.add_argument("--fps", type=int, default=10)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--recompute", action="store_true", help="Rebuild junctions/navigation from an existing truth.json")
    args = parser.parse_args()
    if args.recompute:
        truth = json.loads((args.output / "truth.json").read_text(encoding="utf-8"))
        truth["junctions"], turns = scan_junctions(truth["steps"])
        (args.output / "truth.json").write_text(json.dumps(truth), encoding="utf-8")
        write_route(args.output, truth["junctions"], turns)
        print(json.dumps(truth["junctions"], indent=1)); return
    args.output.mkdir(parents=True, exist_ok=True)
    env = MetaDriveEnv({
        "use_render": False, "image_observation": True, "norm_pixel": False,
        "sensors": {"rgb_camera": (RGBCamera, args.width, args.height)},
        "vehicle_config": {"image_source": "rgb_camera"},
        "agent_policy": IDMPolicy, "map": args.map, "traffic_density": args.traffic,
        "start_seed": args.seed, "num_scenarios": 1, "horizon": int(args.seconds * 10) + 50,
        "physics_world_step_size": 0.02, "decision_repeat": 5, "log_level": 50,
        "interface_panel": [], "show_interface": False,
        # Low-memory host (8 GB): no terrain mesh or skybox; roads, traffic and markings remain.
        "show_terrain": True, "show_skybox": False, "map_region_size": 256,
        "crash_vehicle_done": False, "crash_object_done": False})
    step_seconds = 0.02 * 5
    if abs(step_seconds * args.fps - 1) > 1e-6:
        raise ValueError("--fps must equal the 10 Hz simulation step rate")
    writer = cv2.VideoWriter(str(args.output / "drive.mp4"), cv2.VideoWriter_fourcc(*"mp4v"), args.fps, (args.width, args.height))
    steps, speeds = [], []
    ending = {}
    try:
        obs, _ = env.reset(seed=args.seed)
        t = 0.0
        while t < args.seconds:
            obs, _, terminated, truncated, info = env.step([0, 0])
            image = obs["image"][..., -1] if obs["image"].ndim == 4 else obs["image"]
            frame = np.ascontiguousarray(image.astype(np.uint8))
            writer.write(frame)  # MetaDrive camera returns BGR-ordered uint8 when norm_pixel=False
            ego = env.agent
            road = ego.navigation.current_road
            start, end = (road.start_node, road.end_node) if road is not None else (None, None)
            heading = float(ego.heading_theta)
            others = []
            for vehicle in env.engine.traffic_manager.spawned_objects.values():
                dx, dy = np.asarray(vehicle.position) - np.asarray(ego.position)
                forward = dx * math.cos(heading) + dy * math.sin(heading)
                lateral = -dx * math.sin(heading) + dy * math.cos(heading)
                if -10 < forward < 60 and abs(lateral) < 12:
                    others.append({"forward_m": round(float(forward), 2), "lateral_m": round(float(lateral), 2),
                                   "speed_kmh": round(float(vehicle.speed_km_h), 1)})
            speed = float(ego.speed_km_h)
            steps.append({"time": round(t, 2), "speed_kmh": round(speed, 2), "heading": heading,
                          "position": [float(v) for v in ego.position], "road": [str(start), str(end)],
                          "block": block_of(end), "traffic": others})
            speeds.append((round(t, 2), round(speed, 3)))
            t += step_seconds
            if terminated or truncated:
                ending = {k: bool(v) for k, v in info.items() if isinstance(v, (bool, np.bool_)) and v}
                break
    finally:
        writer.release()
        env.close()
    junctions, turns = scan_junctions(steps)
    (args.output / "speeds.csv").write_text("time,speed_kmh\n" + "".join(f"{a},{b}\n" for a, b in speeds), encoding="utf-8")
    write_route(args.output, junctions, turns)
    (args.output / "truth.json").write_text(json.dumps({"simulator": "MetaDrive 0.4.3", "seed": args.seed, "map": args.map,
        "traffic_density": args.traffic, "policy": "IDM expert (not a learner)", "ending": ending, "step_seconds": step_seconds,
        "junctions": junctions, "steps": steps}), encoding="utf-8")
    print(json.dumps({"ending": ending, "seconds": round(t, 1), "frames": len(steps), "junctions": junctions, "turns": len(turns)}, indent=1))


if __name__ == "__main__":
    main()
