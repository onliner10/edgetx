from __future__ import annotations

import unittest

from edge16_hil.protocol import ProtocolError, format_command, parse_event_line


class ProtocolTests(unittest.TestCase):
    def test_parse_event_line(self) -> None:
        event = parse_event_line("RESULT case=ppm_identity status=pass frames=1200")
        self.assertEqual(event.kind, "RESULT")
        self.assertEqual(event.fields["case"], "ppm_identity")
        self.assertEqual(event.fields["status"], "pass")
        self.assertEqual(event.fields["frames"], "1200")

    def test_parse_quoted_value(self) -> None:
        event = parse_event_line("ERROR message='missing lock'")
        self.assertEqual(event.fields["message"], "missing lock")

    def test_reject_malformed_field(self) -> None:
        with self.assertRaises(ProtocolError):
            parse_event_line("READY fw=edge16-hil caps")

    def test_format_command(self) -> None:
        self.assertEqual(
            format_command("run", case="ppm_identity", ms=2500, seq=7),
            "RUN case=ppm_identity ms=2500 seq=7",
        )


if __name__ == "__main__":
    unittest.main()
