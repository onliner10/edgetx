from __future__ import annotations

import unittest

from edge16_hil.cli import _parse_duration_ms
from edge16_hil.orchestrator import HilSetupError


class CliTests(unittest.TestCase):
    def test_parse_duration_units(self) -> None:
        self.assertEqual(_parse_duration_ms("1500"), 1500)
        self.assertEqual(_parse_duration_ms("2s"), 2000)
        self.assertEqual(_parse_duration_ms("1.5m"), 90000)
        self.assertEqual(_parse_duration_ms("2500ms"), 2500)

    def test_parse_duration_rejects_too_short(self) -> None:
        with self.assertRaises(HilSetupError):
            _parse_duration_ms("999ms")


if __name__ == "__main__":
    unittest.main()
