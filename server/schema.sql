CREATE TABLE sensors (
    mac TEXT PRIMARY KEY,
    name TEXT,
    type SMALLINT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE switchbot_readings (
    id BIGSERIAL PRIMARY KEY,
    mac TEXT NOT NULL REFERENCES sensors(mac),
    ts TIMESTAMPTZ NOT NULL,
    temperature_c REAL NOT NULL,
    humidity_pct REAL NOT NULL
);

CREATE INDEX switchbot_readings_mac_ts_idx
    ON switchbot_readings(mac, ts DESC);

CREATE TABLE xiaomi_readings (
    id BIGSERIAL PRIMARY KEY,
    mac TEXT NOT NULL REFERENCES sensors(mac),
    ts TIMESTAMPTZ NOT NULL,
    temperature_c REAL NOT NULL,
    moisture_pct INTEGER,
    light_lux INTEGER,
    conductivity_us_cm INTEGER
);

CREATE INDEX xiaomi_readings_mac_ts_idx
    ON xiaomi_readings(mac, ts DESC);
