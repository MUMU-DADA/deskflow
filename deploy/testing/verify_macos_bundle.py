#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Reject undeployed Mach-O dependencies and architecture mismatches in CI."""

import pathlib
import plistlib
import re
import subprocess
import sys


def output(*args):
    return subprocess.check_output(args, text=True)


def verify(bundle):
    bundle = bundle.resolve()
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        executable = plistlib.load(stream)["CFBundleExecutable"]
    main = bundle / "Contents/MacOS" / executable
    executable_dir = main.parent
    magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
             b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}

    def expand(value, loader):
        return pathlib.Path(value.replace("@loader_path", str(loader.parent))
                            .replace("@executable_path", str(executable_dir)))

    def rpaths(binary):
        listing = output("otool", "-arch", "arm64", "-l", str(binary))
        return [expand(value, binary) for value in
                re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", listing)]

    if not main.is_file() or not main.stat().st_mode & 0o111:
        raise RuntimeError(f"Bundle executable missing or not executable: {main}")
    main_rpaths = rpaths(main)
    count = 0
    for binary in bundle.rglob("*"):
        if binary.is_symlink() or not binary.is_file():
            continue
        with binary.open("rb") as stream:
            if stream.read(4) not in magic:
                continue
        count += 1
        if "arm64" not in output("lipo", "-archs", str(binary)).split():
            raise RuntimeError(f"Missing arm64 slice: {binary}")
        search = rpaths(binary) + main_rpaths
        # Qt frameworks can contain both arm64 and x86_64. Inspect the deployed
        # architecture explicitly and never mistake an architecture header for a dependency.
        for line in output("otool", "-arch", "arm64", "-L", str(binary)).splitlines():
            match = re.match(r"^\s+(.+?) \(compatibility version", line)
            if not match:
                continue
            dependency = match.group(1)
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            if dependency.startswith("@rpath/"):
                candidates = [directory / dependency[len("@rpath/"):] for directory in search]
            elif dependency.startswith(("@loader_path/", "@executable_path/")):
                candidates = [expand(dependency, binary)]
            else:
                raise RuntimeError(f"External dependency in {binary}: {dependency}")
            if not any(path.is_file() and path.resolve().is_relative_to(bundle) for path in candidates):
                raise RuntimeError(f"Unresolved bundled dependency in {binary}: {dependency}")
    if count == 0:
        raise RuntimeError(f"No Mach-O binaries found in {bundle}")
    print(f"Verified {count} arm64 Mach-O files and bundled dependencies in {bundle.name}")


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        verify(pathlib.Path(argument))
