"""Orion Thermistor Expansion Module (TEM) CAN decoding.

The module's general broadcast is an extended 29-bit CAN frame sent every
100 ms.  A frame carries one thermistor reading; enabled thermistors are
cycled rather than sampled simultaneously.

Protocol source: Orion BMS Thermistor Module CANBUS protocol, frame
``0x1838F380`` for expansion module #1 (source address 0x80).
"""

from __future__ import annotations

from dataclasses import dataclass


ORION_TEM_GENERAL_CAN_ID = 0x1838F380
ORION_TEM_GENERAL_DLC = 8
ORION_TEM_MODULE_ADDRESS = 0x80
ORION_TEM_MAX_SENSOR_COUNT = 80


@dataclass(frozen=True, slots=True)
class ThermistorSample:
    """One decoded TEM general-broadcast sample.

    ``sensor_id`` is the module-local, zero-based ID from byte 4 bits 0..6.
    User-facing exports may add one, but storage deliberately preserves the
    wire value.  ``module_address`` is the J1939 source-address byte from the
    CAN ID, so a future second module remains distinguishable.
    """

    module_address: int
    sensor_id: int
    temperature_c: int
    fault: bool


def _signed_int8(value: int) -> int:
    return value - 256 if value & 0x80 else value


def decode_orion_tem_general(
    *, can_id: int, extended: bool, remote: bool, dlc: int, data: bytes
) -> ThermistorSample | None:
    """Decode a module-#1 ``0x1838F380`` frame, otherwise return ``None``.

    Matching is intentionally strict: a standard frame, RTR frame, another
    source address, non-8-byte DLC, or inconsistent data length is not a TEM
    sample.  Raw CAN storage still retains all such frames for later review.
    """

    if (
        not extended
        or remote
        or can_id != ORION_TEM_GENERAL_CAN_ID
        or dlc != ORION_TEM_GENERAL_DLC
        or len(data) != ORION_TEM_GENERAL_DLC
    ):
        return None

    sensor_and_fault = data[3]
    sensor_id = sensor_and_fault & 0x7F
    if sensor_id >= ORION_TEM_MAX_SENSOR_COUNT:
        return None

    return ThermistorSample(
        module_address=can_id & 0xFF,
        sensor_id=sensor_id,
        temperature_c=_signed_int8(data[2]),
        fault=bool(sensor_and_fault & 0x80),
    )
