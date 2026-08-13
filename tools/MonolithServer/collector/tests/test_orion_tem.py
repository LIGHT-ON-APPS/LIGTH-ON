import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from orion_tem import (
    ORION_TEM_GENERAL_CAN_ID,
    decode_orion_tem_general,
)


class DecodeOrionTemGeneralTests(unittest.TestCase):
    def decode(self, data: bytes, **overrides):
        args = {
            "can_id": ORION_TEM_GENERAL_CAN_ID,
            "extended": True,
            "remote": False,
            "dlc": 8,
            "data": data,
        }
        args.update(overrides)
        return decode_orion_tem_general(**args)

    def test_decodes_positive_temperature_and_zero_based_sensor(self):
        sample = self.decode(bytes([0x2C, 0x01, 45, 17, 2, 61, 18, 3]))

        self.assertIsNotNone(sample)
        self.assertEqual(sample.module_address, 0x80)
        self.assertEqual(sample.sensor_id, 17)
        self.assertEqual(sample.temperature_c, 45)
        self.assertFalse(sample.fault)

    def test_decodes_negative_signed_int8_temperature(self):
        sample = self.decode(bytes([0, 0, 0xFB, 4, 0, 0, 0, 0]))

        self.assertEqual(sample.temperature_c, -5)
        self.assertEqual(sample.sensor_id, 4)

    def test_extracts_fault_bit_without_changing_sensor_id(self):
        sample = self.decode(bytes([0, 0, 72, 0x80 | 44, 0, 0, 0, 0]))

        self.assertEqual(sample.sensor_id, 44)
        self.assertEqual(sample.temperature_c, 72)
        self.assertTrue(sample.fault)

    def test_accepts_valid_sparse_sensor_id(self):
        sample = self.decode(bytes([0, 0, 20, 60, 0, 0, 0, 0]))

        self.assertEqual(sample.sensor_id, 60)

    def test_ignores_sensor_outside_module_range(self):
        self.assertIsNone(self.decode(bytes([0, 0, 20, 80, 0, 0, 0, 0])))

    def test_ignores_standard_frame_with_same_numeric_id(self):
        self.assertIsNone(self.decode(bytes(8), extended=False))

    def test_ignores_remote_frame(self):
        self.assertIsNone(self.decode(bytes(8), remote=True))

    def test_ignores_other_extended_id(self):
        self.assertIsNone(self.decode(bytes(8), can_id=0x1838F381))

    def test_ignores_wrong_dlc_or_data_length(self):
        self.assertIsNone(self.decode(bytes(7), dlc=7))
        self.assertIsNone(self.decode(bytes(7), dlc=8))
        self.assertIsNone(self.decode(bytes(8), dlc=7))


if __name__ == "__main__":
    unittest.main()
