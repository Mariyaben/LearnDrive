"""Prepare bounded IDD-PeD excerpts via ordinary HTTP byte-range requests.

Only excerpts are saved; multi-gigabyte TAR archives are not downloaded.
The local proxy maps a read-only MP4 member to its documented archive offset.
This is diagnostic research data, not a redistribution or training grant.
"""
import argparse
import concurrent.futures
import hashlib
import http.server
import json
from pathlib import Path
import re
import socket
import subprocess
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


class ArchiveProxy(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    members = {}
    stats = {}
    lock = threading.Lock()
    budget = 1_800_000_000
    total_bytes = 0

    def log_message(self, *args):
        pass

    def do_HEAD(self):
        self.serve(False)

    def do_GET(self):
        self.serve(True)

    def serve(self, body):
        key = self.path.split("?", 1)[0].lstrip("/")
        member = self.members.get(key)
        if not member:
            self.send_error(404)
            return
        size = member["bytes"]
        start, end = 0, size - 1
        requested = self.headers.get("Range")
        if requested:
            match = re.fullmatch(r"bytes=(\d+)-(\d*)", requested)
            if not match:
                self.send_error(416)
                return
            start = int(match[1])
            if match[2]:
                end = min(int(match[2]), end)
        if start < 0 or start > end or end >= size:
            self.send_error(416)
            return
        self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
        self.send_response(206 if requested else 200)
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if requested:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if not body:
            return
        position = start
        try:
            while position <= end:
                # Small bounded upstream reads prevent waste when ffmpeg seeks.
                last = min(end, position + 512 * 1024 - 1)
                offset = member["payload_offset"]
                header = f"bytes={offset + position}-{offset + last}"
                req = urllib.request.Request(member["archive_url"], headers={
                    "Range": header, "User-Agent": "LearnDrive-local-diagnostic-review/1.0"})
                with urllib.request.urlopen(req, timeout=35) as response:
                    if response.status != 206:
                        raise RuntimeError("Archive server did not honor byte range")
                    data = response.read(last - position + 1)
                if len(data) != last - position + 1:
                    raise RuntimeError("Truncated archive byte range")
                with self.lock:
                    ArchiveProxy.total_bytes += len(data)
                    stat = self.stats.setdefault(key, {"downloaded_bytes": 0, "requests": 0})
                    stat["downloaded_bytes"] += len(data)
                    stat["requests"] += 1
                    if ArchiveProxy.total_bytes > self.budget:
                        raise RuntimeError("Corpus download budget exceeded")
                self.wfile.write(data)
                self.wfile.flush()
                position = last + 1
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception as error:
            with self.lock:
                self.stats.setdefault(key, {}).setdefault("errors", []).append(str(error))
        finally:
            self.close_connection = True


def probe(url):
    command = ["ffprobe", "-v", "error", "-rw_timeout", "35000000", "-show_entries",
               "format=duration:stream=index,codec_type,codec_name,width,height,avg_frame_rate",
               "-of", "json", url]
    result = subprocess.run(command, capture_output=True, text=True, timeout=100)
    if result.returncode:
        raise RuntimeError(result.stderr[-1500:])
    return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/traffic-review-2026-10-22")
    parser.add_argument("--count", type=int, default=28)
    parser.add_argument("--seconds", type=float, default=24)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    folder = args.output.resolve()
    clips = folder / "clips"
    clips.mkdir(parents=True, exist_ok=True)
    candidates = json.loads((folder / "download-candidates.json").read_text())
    members = sorted(candidates["smallest_candidates"], key=lambda r: r["video_id"])
    if args.only:
        members = [r for r in members if r["video_id"] in args.only]
    members = members[:args.count]
    selected = {}
    labels_path = folder / "audit-ui/labeled-windows.json"
    if labels_path.exists():
        windows = json.loads(labels_path.read_text())
        selected = windows.get("videos", windows.get("windows", windows))
        if isinstance(selected, list):
            selected = {r["video_id"]: r for r in selected}
    # Freeze excerpt choices before this process runs any inference.
    protocol = {"created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "purpose": "Current-code Indian traffic diagnostic corpus; not safety qualification",
                "selection": "One bounded excerpt per distinct raw MP4 member; annotations if available before extraction, otherwise deterministic early window",
                "members": members, "window_labels_available": str(labels_path) if labels_path.exists() else None,
                "default_seconds": args.seconds, "source_license_status": candidates["license_status"],
                "raw_video_redistribution": False, "inference_tuning": False}
    save(folder / "preparation-protocol.json", protocol)
    ArchiveProxy.members = {m["video_id"]: m for m in members}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), ArchiveProxy)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    lock = threading.Lock()
    result_path = folder / "prepared-iddped.json"
    prepared = json.loads(result_path.read_text()) if result_path.exists() else []
    prepared = {r["id"]: r for r in prepared}

    def prepare(member):
        key = member["video_id"]
        target = clips / (key + ".mp4")
        if key in prepared and target.exists():
            print("EXISTS", key, flush=True)
            return
        url = f"http://127.0.0.1:{server.server_port}/{key}"
        start_clock = time.monotonic()
        try:
            metadata = probe(url)
            duration = float(metadata["format"]["duration"])
            stream = next(s for s in metadata["streams"] if s["codec_type"] == "video")
            label = selected.get(key, {})
            numerator, denominator = map(float, stream["avg_frame_rate"].split("/"))
            source_fps = numerator / denominator
            event_window = label.get("selected_window") or {}
            start = (event_window["start_frame"] / source_fps if event_window else
                     float(label.get("start_seconds", min(15, max(0, duration * .1)))))
            requested_length = (event_window["frame_count"] / source_fps if event_window else
                                float(label.get("duration_seconds", args.seconds)))
            length = min(requested_length, duration - start)
            if length <= 0:
                raise RuntimeError("Selected window is outside source duration")
            command = ["ffmpeg", "-v", "error", "-y", "-threads", "2", "-rw_timeout", "35000000",
                       "-ss", f"{start:.6f}", "-i", url, "-t", f"{length:.6f}", "-map", "0:v:0", "-an",
                       "-vf", "scale=640:-2", "-c:v", "libx264", "-threads", "2", "-crf", "18",
                       "-movflags", "+faststart", str(target)]
            process = subprocess.run(command, capture_output=True, text=True, timeout=240)
            if process.returncode:
                raise RuntimeError(process.stderr[-1800:])
            extracted = probe(str(target))
            row = {"id": key, "path": str(target), "source_page": candidates["source_repo"],
                   "archive_url": member["archive_url"], "archive_member": member["member_path"],
                   "source_member_bytes": member["bytes"], "source_duration_seconds": duration,
                   "source_video_stream": stream, "start_seconds": start,
                   "duration_seconds": float(extracted["format"]["duration"]),
                   "prepared_sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                   "original_camera_intrinsics": None, "original_speed": None,
                   "original_imu": None, "source_fps": source_fps,
                   "aspect_ratio_preserved": True, "selection_labels": {"annotation_sha256": label.get("annotation_sha256"),
                       "selected_window": event_window, "scene_attributes": label.get("scene_attributes")},
                   "transform": command, "status": "prepared"}
            print("PREPARED", key, f"{row['duration_seconds']:.2f}s", f"source {duration:.1f}s",
                  f"{time.monotonic()-start_clock:.1f}s wall", flush=True)
        except Exception as error:
            row = {"id": key, "status": "failed", "error": str(error), "archive_member": member["member_path"]}
            print("FAILED", key, str(error)[:500], flush=True)
        with lock:
            prepared[key] = row
            save(result_path, list(prepared.values()))
            save(folder / "download-stats.json", {"total_downloaded_bytes": ArchiveProxy.total_bytes,
                                                   "members": ArchiveProxy.stats})

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            list(pool.map(prepare, members))
    finally:
        server.shutdown()
        save(folder / "download-stats.json", {"total_downloaded_bytes": ArchiveProxy.total_bytes,
                                               "members": ArchiveProxy.stats})
    failures = [r for r in prepared.values() if r["status"] != "prepared"]
    print("Preparation complete", len(prepared) - len(failures), "ok", len(failures), "failed", flush=True)


if __name__ == "__main__":
    main()
