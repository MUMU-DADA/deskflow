#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Regression checks for bundle verification; no Apple developer tools required."""

import pathlib
import plistlib
import tempfile
import unittest
from unittest.mock import patch

import verify_macos_bundle


class BundleVerifierTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.bundle = pathlib.Path(self.directory.name) / "Probe.app"
        self.main = self.bundle / "Contents/MacOS/probe"
        self.framework = self.bundle / "Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
        for binary in (self.main, self.framework):
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_bytes(b"\xcf\xfa\xed\xfe")
            binary.chmod(0o755)
        with (self.bundle / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "probe"}, stream)
        self.dependencies = ["@rpath/QtCore.framework/Versions/A/QtCore", "/usr/lib/libSystem.B.dylib"]
        self.architectures = "x86_64 arm64"

    def tool_output(self, *args):
        if args[0] == "lipo":
            return self.architectures
        self.assertEqual(args[:3], ("otool", "-arch", "arm64"))
        if args[3] == "-l":
            return "Load command 1\n          cmd LC_RPATH\n      cmdsize 48\n         path @executable_path/../Frameworks (offset 12)\n"
        self.assertEqual(args[3], "-L")
        # Universal otool output contains unindented architecture headers, not
        # load dependencies. This reproduces the packaging failure seen in CI.
        listing = f"{args[4]} (architecture arm64):\n"
        for dependency in self.dependencies:
            listing += f"\t{dependency} (compatibility version 1.0.0, current version 1.0.0)\n"
        listing += f"{args[4]} (architecture x86_64):\n"
        return listing

    def test_universal_architecture_headers_are_not_dependencies(self):
        with patch.object(verify_macos_bundle, "output", self.tool_output):
            verify_macos_bundle.verify(self.bundle)

    def test_homebrew_dependency_is_rejected(self):
        self.dependencies.append("/opt/homebrew/opt/openssl/lib/libssl.3.dylib")
        with patch.object(verify_macos_bundle, "output", self.tool_output):
            with self.assertRaisesRegex(RuntimeError, "External dependency"):
                verify_macos_bundle.verify(self.bundle)

    def test_missing_bundled_framework_is_rejected(self):
        self.dependencies.append("@rpath/QtWidgets.framework/Versions/A/QtWidgets")
        with patch.object(verify_macos_bundle, "output", self.tool_output):
            with self.assertRaisesRegex(RuntimeError, "Unresolved bundled dependency"):
                verify_macos_bundle.verify(self.bundle)

    def test_wrong_architecture_is_rejected(self):
        self.architectures = "x86_64"
        with patch.object(verify_macos_bundle, "output", self.tool_output):
            with self.assertRaisesRegex(RuntimeError, "Missing arm64 slice"):
                verify_macos_bundle.verify(self.bundle)


if __name__ == "__main__":
    unittest.main()
