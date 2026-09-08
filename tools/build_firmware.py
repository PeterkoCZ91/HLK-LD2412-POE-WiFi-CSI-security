#!/usr/bin/env python3
"""Build shipped variants with separate framework packages and a project lock.

pioarduino removes Arduino 2 package variants while preparing Arduino 3.
A shared package directory can therefore lose a framework mid-build.
This wrapper isolates packages by stack and pre-installs them before SCons.
Outputs remain in .pio/build/<env>, so upload and artifact tools keep working.
"""
import argparse
import fcntl
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ENVS = ("esp32_poe", "esp32_poe_csi", "esp32_poe_csi_8mb",
        "esp32_poe_csi_idf5", "esp32_poe_csi_idf5_8mb")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-e", "--environment", action="append", choices=ENVS)
    args = parser.parse_args()
    (ROOT / ".pio").mkdir(exist_ok=True)
    with (ROOT / ".pio" / "firmware-build.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for name in args.environment or ENVS:
            stack = "idf5" if "idf5" in name else "idf4"
            env = dict(os.environ)
            env["PLATFORMIO_PACKAGES_DIR"] = str(ROOT / ".pio" / "packages" / stack)
            print(f"Building {name} with isolated {stack} packages", flush=True)
            subprocess.run(["pio", "pkg", "install", "-e", name], cwd=ROOT, env=env, check=True)
            log_path = ROOT / ".pio" / f"build-{name}.log"
            with log_path.open("w") as log:
                process = subprocess.Popen(["pio", "run", "-e", name], cwd=ROOT, env=env,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                for line in process.stdout:
                    print(line, end="", flush=True)
                    log.write(line)
                if process.wait():
                    raise subprocess.CalledProcessError(process.returncode, process.args)
            subprocess.run(["bash", "tools/check_size_budget.sh", name, str(log_path)],
                           cwd=ROOT, env=env, check=True)


if __name__ == "__main__":
    main()

