#!/usr/bin/env python3
"""Relink the supplied firmware objects. Never flash a device."""

import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, help="ARM toolchain bin directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    plan = json.loads((root / "link-command.json").read_text())
    compiler = "arm-none-eabi-gcc"
    objcopy = "arm-none-eabi-objcopy"
    if args.toolchain:
        compiler = str(args.toolchain.resolve() / compiler)
        objcopy = str(args.toolchain.resolve() / objcopy)
    flags = [arg.replace("@KIT@", str(root)).replace("@OUT@", str(output))
             for arg in plan]
    subprocess.run([compiler, *flags], check=True, cwd=output)
    elf = output / "Bike_ekg_nrf.ino.elf"
    hex_file = output / "Bike_ekg_nrf.ino.hex"
    subprocess.run([objcopy, "-O", "ihex", str(elf), str(hex_file)], check=True)
    print(f"Relink successful: {hex_file}")


if __name__ == "__main__":
    main()
