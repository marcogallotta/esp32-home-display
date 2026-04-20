CREATE TABLE sensors (
    mac TEXT PRIMARY KEY,
    name TEXT,
    type SMALLINT NOT NULL CHECK (type IN (1, 2)),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE switchbot_readings (
    id BIGSERIAL PRIMARY KEY,
    mac TEXT NOT NULL REFERENCES sensors(mac),
    timestamp TIMESTAMPTZ NOT NULL,
    temperature_c REAL NOT NULL,
    humidity_pct REAL NOT NULL
);

CREATE INDEX switchbot_readings_mac_timestamp_idx
    ON switchbot_readings(mac, timestamp DESC);

CREATE TABLE xiaomi_readings (
    id BIGSERIAL PRIMARY KEY,
    mac TEXT NOT NULL REFERENCES sensors(mac),
    timestamp TIMESTAMPTZ NOT NULL,
    temperature_c REAL,
    moisture_pct INTEGER,
    light_lux INTEGER,
    conductivity_us_cm INTEGER
);

CREATE INDEX xiaomi_readings_mac_timestamp_idx
    ON xiaomi_readings(mac, timestamp DESC);
