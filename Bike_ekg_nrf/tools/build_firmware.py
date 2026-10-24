#!/usr/bin/env python3
"""Compile the documented board configuration; never upload to hardware."""

import argparse
from pathlib import Path
import subprocess


FQBN = "adafruit:nrf52:feather52840:softdevice=s140v6,debug=l0,debug_output=serial"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default="arduino-cli", help="Arduino CLI executable")
    parser.add_argument("--config-file", type=Path)
    parser.add_argument("--flatpak", action="store_true", help="Use cc.arduino.IDE2")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-path", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_path.resolve()
    output = args.output.resolve()
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    if args.flatpak:
        cli = ["flatpak", "run", f"--filesystem={build.parent}",
               f"--filesystem={output.parent}",
               "--command=/app/arduino-ide/resources/app/lib/backend/resources/arduino-cli",
               "cc.arduino.IDE2"]
    else:
        cli = [args.cli]
    # Define the CS hook mode for the library compilation, not only the sketch.
    # Path maps keep private build paths out of debug info and __FILE__ strings.
    flags = " ".join([
        "-DSD_CHIP_SELECT_MODE=1",
        f"-ffile-prefix-map={Path.home()}=/builder",
        f"-ffile-prefix-map={root}=/firmware/Bike_ekg_nrf",
        f"-ffile-prefix-map={build}=/build",
    ])
    command = [*cli, "compile", "--fqbn", FQBN, "--clean", "--verbose",
               "--build-path", str(build), "--output-dir", str(output),
               "--build-property", f"compiler.c.extra_flags={flags}",
               "--build-property", f"compiler.cpp.extra_flags={flags}",
               "--build-property", f"compiler.S.extra_flags={flags}"]
    if args.config_file:
        command += ["--config-file", str(args.config_file.resolve())]
    command.append(str(root))
    # The raw diagnostic log is local only; do not put it in a public release.
    log = build.parent / f"{build.name}-private.log"
    print(f"Board: {FQBN}", flush=True)
    print(f"Local build log: {log}", flush=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    text = log.read_text(encoding="utf-8", errors="replace")
    print("\n".join(text.splitlines()[-24:]))
    if result.returncode:
        raise SystemExit(result.returncode)
    print(f"Build successful. Artifacts: {output}")


if __name__ == "__main__":
    main()
