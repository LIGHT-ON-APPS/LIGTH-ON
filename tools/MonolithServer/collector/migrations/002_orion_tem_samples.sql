-- Decoded Orion Thermistor Expansion Module samples.
--
-- This is additive: can_frames remains the canonical raw record.  The UNIQUE
-- foreign key enforces one decoded TEM row per stored raw CAN frame, and
-- ON DELETE CASCADE prevents an orphan if raw retention removes that frame.
-- Collector checkpoint replay is transaction-safe; a separate manual
-- backfill must still avoid inserting duplicate can_frames rows.

CREATE TABLE IF NOT EXISTS thermistor_samples (
  id               bigserial PRIMARY KEY,
  can_frame_id     bigint NOT NULL UNIQUE
                   REFERENCES can_frames(id) ON DELETE CASCADE,
  session_id       bigint REFERENCES sessions(id),
  raw_id           bigint NOT NULL REFERENCES raw_messages(id),
  received_at      timestamptz NOT NULL,
  device_ts_ms     bigint NOT NULL,
  module_address   smallint NOT NULL CHECK (module_address BETWEEN 0 AND 255),
  sensor_id        smallint NOT NULL CHECK (sensor_id BETWEEN 0 AND 79),
  temperature_c    smallint NOT NULL CHECK (temperature_c BETWEEN -128 AND 127),
  fault            boolean NOT NULL
);

CREATE INDEX IF NOT EXISTS thermistor_samples_session_ts_idx
  ON thermistor_samples (session_id, device_ts_ms);
CREATE INDEX IF NOT EXISTS thermistor_samples_received_idx
  ON thermistor_samples (received_at);
CREATE INDEX IF NOT EXISTS thermistor_samples_raw_idx
  ON thermistor_samples (raw_id);
CREATE INDEX IF NOT EXISTS thermistor_samples_sensor_received_idx
  ON thermistor_samples (module_address, sensor_id, received_at);

COMMENT ON TABLE thermistor_samples IS
  'Decoded Orion TEM general broadcasts; can_frames remains canonical raw data';
COMMENT ON COLUMN thermistor_samples.sensor_id IS
  'Module-local zero-based thermistor ID from data[3] bits 0..6';
COMMENT ON COLUMN thermistor_samples.temperature_c IS
  'Signed int8 temperature in degrees Celsius, 1 C/bit';
COMMENT ON COLUMN thermistor_samples.module_address IS
  'J1939 source-address byte; 0x80 for configured module #1';

INSERT INTO schema_meta(key, value) VALUES ('orion_tem_schema', '1')
ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value;
