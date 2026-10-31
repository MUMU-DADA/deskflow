# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("writer", Path(__file__).with_name("deskflow-foreground-writer.py"))
writer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(writer)


class WriterTests(unittest.TestCase):
    def setUp(self):
        self.state = {"version": 1, "updatedAtMs": 10000, "application": "org.example.Game", "fullscreen": True}

    def test_rejects_stale_and_non_boolean(self):
        with self.assertRaises(ValueError):
            writer.validate_state(json.dumps(self.state), 12000)
        self.state["fullscreen"] = "true"
        with self.assertRaises(ValueError):
            writer.validate_state(json.dumps(self.state), 10000)

    def test_no_focus_cannot_lock(self):
        self.state["application"] = ""
        self.assertFalse(writer.validate_state(json.dumps(self.state), 10000)["fullscreen"])

    def test_game_identity_is_not_asserted_by_writer(self):
        self.state["game"] = True
        self.assertNotIn("game", writer.validate_state(json.dumps(self.state), 10000))

    def test_atomic_file_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            writer.write_state(Path(directory), self.state)
            self.state["fullscreen"] = False
            writer.write_state(Path(directory), self.state)
            files = list(Path(directory).iterdir())
            self.assertEqual(len(files), 1)
            self.assertEqual(json.loads(files[0].read_text()), self.state)


if __name__ == "__main__":
    unittest.main()
