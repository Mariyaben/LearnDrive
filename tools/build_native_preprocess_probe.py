"""Build an ARM64 Android 28 O0/O2 pure-preprocessing comparison executable.

This only builds local files. It does not connect to, install on or run a phone.
Run the executable on a device with its copied libc++_shared.so, for example:
  LD_LIBRARY_PATH=/data/local/tmp/ld-probe /data/local/tmp/ld-probe/native-preprocess-probe
Its optional first argument saves the same stdout JSON to a device-local file.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NDK = Path(r"C:\Users\mariy\AppData\Local\Android\Sdk\ndk\28.2.13676358")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, default=DEFAULT_NDK)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/native-preprocess-probe")
    parser.add_argument("--fp-contract-off", action="store_true",
                        help="Disable contraction identically in both variants; changes the comparison contract.")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    toolchain = args.ndk.resolve() / "toolchains/llvm/prebuilt/windows-x86_64"
    compiler = toolchain / "bin/clang++.exe"
    sysroot = toolchain / "sysroot"
    cxx_directory = sysroot / "usr/lib/aarch64-linux-android"
    runtime = cxx_directory / "libc++_shared.so"
    if not compiler.is_file() or not runtime.is_file():
        raise FileNotFoundError("Expected Windows NDK compiler and ARM64 shared C++ runtime were not found")
    common = [str(compiler), "--target=aarch64-linux-android28", f"--sysroot={sysroot}",
              "-std=c++17", "-stdlib=libc++", "-g", "-fPIE", "-ffunction-sections", "-fdata-sections",
              "-fstack-protector-strong", f"-I{ROOT / 'core/include'}"]
    if args.fp_contract_off:
        common.append("-ffp-contract=off")
    commands = []
    objects = []
    variant_sources = [ROOT / "core/src/vision.cpp", ROOT / "core/src/perception/supercombo.cpp",
                       ROOT / "tools/native_preprocess_variant.cpp"]
    for name, optimize, defines in [("baseline", "-O0", []),
                                    ("candidate", "-O2", ["-Dld=ld_candidate", "-DPROBE_CANDIDATE=1"])]:
        for index, source in enumerate(variant_sources):
            obj = output / f"{name}-{index}.o"
            command = common + [optimize] + defines + ["-c", str(source), "-o", str(obj)]
            commands.append(command)
            subprocess.run(command, cwd=ROOT, check=True)
            objects.append(obj)
    probe_source = ROOT / "tools/native_preprocess_probe.cpp"
    probe_object = output / "probe.o"
    command = common + ["-O2", "-c", str(probe_source), "-o", str(probe_object)]
    commands.append(command)
    subprocess.run(command, cwd=ROOT, check=True)
    executable = output / "native-preprocess-probe"
    command = common + [str(probe_object)] + [str(obj) for obj in objects] + [
        "-pie", "-Wl,--gc-sections", "-Wl,-rpath,$ORIGIN", "-nostdlib++", f"-L{cxx_directory}",
        "-lc++_shared", "-lm", "-o", str(executable)]
    commands.append(command)
    subprocess.run(command, cwd=ROOT, check=True)
    shutil.copy2(runtime, output / runtime.name)
    sources = variant_sources + [probe_source, Path(__file__).resolve()]
    manifest = {"schema": 1, "target": "aarch64-linux-android28", "baseline": "O0", "candidate": "O2",
                "fast_math": False, "fp_contract": "off" if args.fp_contract_off else "compiler_default",
                "commands": commands,
                "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                "artifacts": {p.name: {"bytes": p.stat().st_size,
                                       "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                              for p in [executable, output / runtime.name]},
                "scope": "Pure native preprocessing; fixture parity and same-process timings, not app power or heat"}
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"executable": str(executable), "manifest": str(output / "build-manifest.json")}))


if __name__ == "__main__":
    main()
