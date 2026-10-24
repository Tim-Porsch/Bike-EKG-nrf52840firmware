#!/usr/bin/env python3
"""Build and run the firmware's synthetic host tests, without Arduino hardware."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    # name, additional production source, stub include directories
    tests = [
        ("console_service", "ConsoleService.cpp", ["console_stubs"]),
        ("settings_service", "SettingsService.cpp", ["settings_stubs", "sd_stubs"]),
        ("signal_filter", "SignalFilterService.cpp", []),
        ("preview_buffer", None, []),
        ("recording_metadata", None, []),
        ("record_compression", None, []),
        ("sha256", None, []),
        ("sd_file_service", "SdFileBleService.cpp", ["sd_stubs"]),
        ("sd_stream", "SdFileBleService.cpp", ["sd_stubs"]),
        ("log_writer_metadata", "LogWriter.cpp", ["sd_stubs"]),
        ("sd_power", "SdService.cpp", ["sd_power_stubs", "sd_stubs"]),
    ]
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="bike-ekg-hosttests-") as directory:
        for name, source, stubs in tests:
            print(f"Build/run: {name}", flush=True)
            command = [os.environ.get("CXX", "g++"), *flags]
            for stub in stubs:
                command += ["-I", str(root / "tests" / stub)]
            command.append(str(root / "tests" / f"{name}_test.cpp"))
            if source:
                command.append(str(root / source))
            binary = str(Path(directory) / name)
            subprocess.run([*command, "-o", binary], check=True, cwd=directory)
            subprocess.run([binary], check=True, cwd=directory)
    print(f"PASS: {len(tests)} host test programs")


if __name__ == "__main__":
    main()
