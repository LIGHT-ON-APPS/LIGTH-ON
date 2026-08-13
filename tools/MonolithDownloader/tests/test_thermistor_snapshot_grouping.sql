\set ON_ERROR_STOP on

/*
 * PostgreSQL regression test for Downloader snapshot reconstruction.
 *
 * Snapshot A loses sensors 10..44 and snapshot B loses sensors 0..9. Their
 * received sensor IDs therefore look like one continuous 0..44 sequence. The
 * preserved device timestamp gap must still split them. Snapshots C and D are
 * complete 45-channel snapshots delivered at 2 Hz; D crosses the uint32
 * device timestamp wrap.
 *
 * This script creates no tables and changes no persistent database state.
 */
DO $test$
DECLARE
  actual_counts integer[];
  actual_measurement_spans bigint[];
  actual_delivery_spans bigint[];
  actual_complete boolean[];
  midnight_owned_count integer;
  midnight_owned_sensor_count integer;
  midnight_owned_started_before_date boolean;
BEGIN
  WITH samples AS (
    SELECT
      (sensor_id + 1)::bigint AS id,
      1::bigint AS session_id,
      128::smallint AS module_address,
      sensor_id,
      (1000 + sensor_id * 100)::bigint AS device_ts_ms,
      timestamptz '2026-01-01 00:00:00+00'
        + sensor_id * interval '500 milliseconds' AS received_at
    FROM generate_series(0, 9) AS g(sensor_id)

    UNION ALL

    SELECT
      (100 + sensor_id)::bigint,
      1::bigint,
      128::smallint,
      sensor_id,
      (20000 + sensor_id * 100)::bigint,
      timestamptz '2026-01-01 00:00:06+00'
        + (sensor_id - 10) * interval '500 milliseconds'
    FROM generate_series(10, 44) AS g(sensor_id)

    UNION ALL

    SELECT
      (200 + sensor_id)::bigint,
      1::bigint,
      128::smallint,
      sensor_id,
      (40000 + sensor_id * 100)::bigint,
      timestamptz '2026-01-01 00:00:30+00'
        + sensor_id * interval '500 milliseconds'
    FROM generate_series(0, 44) AS g(sensor_id)

    UNION ALL

    SELECT
      (300 + sensor_id)::bigint,
      1::bigint,
      128::smallint,
      sensor_id,
      ((4294965000::bigint + sensor_id * 100) % 4294967296::bigint),
      timestamptz '2026-01-01 00:01:00+00'
        + sensor_id * interval '500 milliseconds'
    FROM generate_series(0, 44) AS g(sensor_id)
  ), stream AS (
    SELECT
      samples.*,
      lag(sensor_id) OVER (
        PARTITION BY session_id, module_address
        ORDER BY received_at, id
      ) AS previous_sensor_id,
      lag(device_ts_ms) OVER (
        PARTITION BY session_id, module_address
        ORDER BY received_at, id
      ) AS previous_device_ts_ms
    FROM samples
  ), ordered AS (
    SELECT
      stream.*,
      sum(CASE
            WHEN previous_sensor_id IS NULL
              OR sensor_id < previous_sensor_id
              OR ((device_ts_ms - previous_device_ts_ms + 4294967296::bigint)
                % 4294967296::bigint) > 6000 THEN 1
            ELSE 0
          END) OVER (
        PARTITION BY session_id, module_address
        ORDER BY received_at, id
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
      ) AS scan_no
    FROM stream
  ), scans AS (
    SELECT
      scan_no,
      count(*)::integer AS sensor_count,
      ((
        (array_agg(device_ts_ms ORDER BY received_at DESC, id DESC))[1]
        - (array_agg(device_ts_ms ORDER BY received_at, id))[1]
        + 4294967296::bigint
      ) % 4294967296::bigint) AS measurement_span_ms,
      round(extract(epoch FROM (max(received_at) - min(received_at))) * 1000)::bigint
        AS delivery_span_ms
    FROM ordered
    GROUP BY scan_no
  )
  SELECT
    array_agg(sensor_count ORDER BY scan_no),
    array_agg(measurement_span_ms ORDER BY scan_no),
    array_agg(delivery_span_ms ORDER BY scan_no),
    array_agg(sensor_count = 45 AND measurement_span_ms <= 6000 ORDER BY scan_no)
  INTO
    actual_counts,
    actual_measurement_spans,
    actual_delivery_spans,
    actual_complete
  FROM scans;

  IF actual_counts <> ARRAY[10, 35, 45, 45] THEN
    RAISE EXCEPTION 'unexpected snapshot sensor counts: %', actual_counts;
  END IF;

  IF actual_measurement_spans <> ARRAY[900, 3400, 4400, 4400]::bigint[] THEN
    RAISE EXCEPTION 'unexpected measurement spans: %', actual_measurement_spans;
  END IF;

  IF actual_delivery_spans <> ARRAY[4500, 17000, 22000, 22000]::bigint[] THEN
    RAISE EXCEPTION 'unexpected delivery spans: %', actual_delivery_spans;
  END IF;

  IF actual_complete <> ARRAY[false, false, true, true] THEN
    RAISE EXCEPTION 'unexpected complete flags: %', actual_complete;
  END IF;

  /* The target date owns a snapshot by server delivery completion. The full
   * snapshot that starts ten seconds before midnight must still be available
   * because the source query reads 35 seconds of padding on each side. */
  WITH samples AS (
    SELECT
      fixture_snapshot,
      sensor_id,
      started_at + sensor_id * interval '500 milliseconds' AS received_at
    FROM (VALUES
      (1, timestamptz '2026-01-01 23:59:20 Asia/Seoul'),
      (2, timestamptz '2026-01-01 23:59:50 Asia/Seoul'),
      (3, timestamptz '2026-01-02 23:59:50 Asia/Seoul')
    ) AS fixture(fixture_snapshot, started_at)
    CROSS JOIN generate_series(0, 44) AS g(sensor_id)
  ), bounded AS (
    SELECT *
    FROM samples
    WHERE received_at >= timestamptz '2026-01-02 00:00:00 Asia/Seoul'
        - interval '35 seconds'
      AND received_at < timestamptz '2026-01-03 00:00:00 Asia/Seoul'
        + interval '35 seconds'
  ), scans AS (
    SELECT
      fixture_snapshot,
      min(received_at) AS started_at,
      max(received_at) AS completed_at,
      count(*)::integer AS sensor_count
    FROM bounded
    GROUP BY fixture_snapshot
  ), owned AS (
    SELECT *
    FROM scans
    WHERE completed_at >= timestamptz '2026-01-02 00:00:00 Asia/Seoul'
      AND completed_at < timestamptz '2026-01-03 00:00:00 Asia/Seoul'
  )
  SELECT
    count(*)::integer,
    max(sensor_count),
    bool_or(started_at < timestamptz '2026-01-02 00:00:00 Asia/Seoul')
  INTO
    midnight_owned_count,
    midnight_owned_sensor_count,
    midnight_owned_started_before_date
  FROM owned;

  IF midnight_owned_count <> 1
    OR midnight_owned_sensor_count <> 45
    OR midnight_owned_started_before_date IS NOT TRUE THEN
    RAISE EXCEPTION
      'midnight ownership failed: count=%, sensors=%, starts_before_date=%',
      midnight_owned_count,
      midnight_owned_sensor_count,
      midnight_owned_started_before_date;
  END IF;

  RAISE NOTICE 'thermistor snapshot grouping: PASS';
END
$test$;
