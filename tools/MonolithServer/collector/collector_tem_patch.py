"""Small integration helpers for the deployed Monolith collector.

This file keeps the change reviewable without copying deployment credentials or
the complete live collector into the repository.  See README.md for the two
call sites in collector.py.
"""

from __future__ import annotations

from orion_tem import decode_orion_tem_general


def store_can_and_optional_tem(cur, sid, raw_id, received_at, rec) -> None:
    """Store a raw CAN row, then its optional decoded TEM projection.

    Both inserts execute in the collector's existing transaction.  Returning
    the can_frames ID ties the decoded value to the exact raw frame.  The
    conflict clause prevents a second projection for the same existing
    can_frames row.  It does not de-duplicate a replay that first inserts a
    brand-new raw can_frames row; raw replay policy remains collector-owned.
    """

    cur.execute(
        "INSERT INTO can_frames(session_id, raw_id, received_at, device_ts_ms, "
        "can_id, extended, remote, dlc, data) "
        "VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s) RETURNING id",
        (
            sid,
            raw_id,
            received_at,
            rec["timestamp"],
            rec["can_id"],
            rec["extended"],
            rec["remote"],
            rec["dlc"],
            rec["data"],
        ),
    )
    can_frame_id = cur.fetchone()[0]

    sample = decode_orion_tem_general(
        can_id=rec["can_id"],
        extended=rec["extended"],
        remote=rec["remote"],
        dlc=rec["dlc"],
        data=rec["data"],
    )
    if sample is None:
        return

    cur.execute(
        "INSERT INTO thermistor_samples("
        "can_frame_id, session_id, raw_id, received_at, device_ts_ms, "
        "module_address, sensor_id, temperature_c, fault) "
        "VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s) "
        "ON CONFLICT (can_frame_id) DO NOTHING",
        (
            can_frame_id,
            sid,
            raw_id,
            received_at,
            rec["timestamp"],
            sample.module_address,
            sample.sensor_id,
            sample.temperature_c,
            sample.fault,
        ),
    )
