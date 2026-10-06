"""Real installed-host/API acceptance. Writes only project build/acceptance files.

No UI input, screen capture, installed files, preferences or user models are used.
Run each mode in a separate Python process (RizomUVLink owns native resources).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import traceback

import psutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("baseline", "localized"))
    parser.add_argument("--host", default=r"C:\Program Files\Rizom Lab\RizomUV 2025.0\rizomuv.exe")
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = root / "build/acceptance" / args.mode
    output.mkdir(parents=True, exist_ok=True)
    host = Path(args.host)
    sys.path.insert(0, str(host.parent / "RizomUVLink"))
    from RizomUVLink import CRizomUVLink

    source = output / "Material Camera.obj"
    source.write_text("o Material\ng Camera\n"
                      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                      "f 1/1 2/2 3/3 4/4\n", encoding="utf-8")
    original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    command = [str(host), "-id", str(args.port)]
    if args.mode == "localized":
        command.insert(0, str(root / "build/out/RizomUVChineseLauncher.exe"))
    report = {"command": command, "started": time.time(), "mode": args.mode,
              "host_sha256": hashlib.sha256(host.read_bytes()).hexdigest()}
    process = None
    child = None
    try:
        stdout = (output / "stdout.log").open("wb")
        stderr = (output / "stderr.log").open("wb")
        process = subprocess.Popen(command, cwd=host.parent, stdout=stdout, stderr=stderr)
        report["launcher_pid"] = process.pid
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            if args.mode == "baseline":
                child = psutil.Process(process.pid)
            else:
                for candidate in psutil.process_iter(["pid", "name", "cmdline"]):
                    cmd = candidate.info["cmdline"] or []
                    if candidate.info["name"].lower() == "rizomuv.exe" and str(args.port) in cmd:
                        child = candidate
                        break
            try:
                with socket.create_connection(("127.0.0.1", args.port), timeout=.3):
                    break
            except OSError:
                time.sleep(.2)
        else:
            raise TimeoutError("Host IPC did not open within 45 seconds")
        report["host_pid"] = child.pid
        report["host_command"] = child.cmdline()
        link = CRizomUVLink()
        link.Connect(args.port)
        report["version"] = link.RizomUVVersion()
        report["load"] = link.Load({"File.Path": str(source), "File.XYZUVW": True,
                                    "File.UVWProps": True, "File.ImportGroups": True, "__Focus": True})
        report["data"] = link.Save({"Data": True})
        report["save"] = link.Save({"File.Path": str(output / "roundtrip.obj")})
        report["raster_export"] = link.RasterExport({"FilePath": str(output / "uv.png"),
                    "Width": 256, "Height": 256, "WidthHeightUnit": "px",
                    "AASamples": 1, "TransparentBackground": False,
                    "BackgroundColor": [0, 0, 0], "PolygonColorMode": "Off",
                    "EdgeColorMode": "Color", "EdgeColor": [1, 1, 1]})
        # Let normal UI draw/measure passes and startup diagnostic sampling run.
        time.sleep(12)
        report["runtime_modules"] = [m.path for m in child.memory_maps()
                                      if "Chinese" in m.path and m.path.lower().endswith(".dll")]
        report["source_sha256_before"] = original_hash
        report["source_sha256_after"] = hashlib.sha256(source.read_bytes()).hexdigest()
        report["output_obj"] = (output / "roundtrip.obj").read_text(encoding="utf-8")
        report["png_sha256"] = hashlib.sha256((output / "uv.png").read_bytes()).hexdigest()
        report["quit"] = link.Quit({})
        report["host_exit_code"] = child.wait(timeout=20)
        report["launcher_exit_code"] = process.wait(timeout=10)
        report["status"] = "PASS"
    except Exception:
        report["status"] = "FAIL"
        report["error"] = traceback.format_exc()
        # Only the uniquely identified process from this test may be stopped.
        if child and child.is_running():
            child.terminate()
            report["forced_test_host_exit"] = child.wait(timeout=15)
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
    report["finished"] = time.time()
    (output / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)
    # Avoid vendor native module shutdown after a failed IPC request; test host
    # exit code above is independent and is always awaited first.
    os._exit(0 if report["status"] == "PASS" else 1)


if __name__ == "__main__":
    main()
