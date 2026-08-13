import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from collector_tem_patch import store_can_and_optional_tem


class FakeCursor:
    def __init__(self, can_frame_id=9001):
        self.can_frame_id = can_frame_id
        self.calls = []

    def execute(self, sql, params):
        self.calls.append((sql, params))

    def fetchone(self):
        return (self.can_frame_id,)


class StoreCanAndOptionalTemTests(unittest.TestCase):
    def setUp(self):
        self.received_at = datetime(2026, 8, 13, 4, 5, 6, tzinfo=timezone.utc)

    def record(self, **overrides):
        rec = {
            "type": "CAN",
            "timestamp": 123456,
            "can_id": 0x1838F380,
            "extended": True,
            "remote": False,
            "dlc": 8,
            "data": bytes([0, 0, 0xFE, 0x80 | 7, 0, 0, 0, 0]),
        }
        rec.update(overrides)
        return rec

    def test_stores_raw_frame_then_traceable_decoded_row(self):
        cur = FakeCursor(can_frame_id=314)

        store_can_and_optional_tem(cur, 11, 22, self.received_at, self.record())

        self.assertEqual(len(cur.calls), 2)
        raw_sql, raw_params = cur.calls[0]
        decoded_sql, decoded_params = cur.calls[1]
        self.assertIn("RETURNING id", raw_sql)
        self.assertEqual(raw_params[:4], (11, 22, self.received_at, 123456))
        self.assertIn("ON CONFLICT (can_frame_id) DO NOTHING", decoded_sql)
        self.assertEqual(
            decoded_params,
            (314, 11, 22, self.received_at, 123456, 0x80, 7, -2, True),
        )

    def test_non_target_frame_only_stores_raw_can(self):
        cur = FakeCursor()

        store_can_and_optional_tem(
            cur, 11, 22, self.received_at, self.record(can_id=0x7A0)
        )

        self.assertEqual(len(cur.calls), 1)
        self.assertIn("INSERT INTO can_frames", cur.calls[0][0])


if __name__ == "__main__":
    unittest.main()
