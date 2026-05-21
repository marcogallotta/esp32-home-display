window.api = {
  async fetchJson(url) {
    const res = await fetch(url);

    if (res.status === 401) {
      window.location.href = "/login";
      return;
    }

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}: ${await res.text()}`);
    }

    return res.json();
  },

  async fetchSensors() {
    return this.fetchJson("/sensors");
  },

  async fetchSensorReadings(sensorId, { startTs, endTs, maxPoints }) {
    const params = new URLSearchParams({
      start_ts: startTs,
      end_ts: endTs,
      max_points: String(maxPoints),
    });

    return this.fetchJson(
      `/sensors/${encodeURIComponent(sensorId)}/readings?${params.toString()}`
    );
  },

  async fetchOpenMeteoWeather(startTs, endTs) {
    const params = new URLSearchParams({ start_ts: startTs, end_ts: endTs });
    return this.fetchJson(`/openmeteo/weather?${params.toString()}`);
  },

  async fetchTemperaturePredictions() {
    return this.fetchJson("/predict/temperature");
  },

  async fetchLatestReadings(macs) {
    const params = macs && macs.length
      ? "?" + macs.map((m) => `mac=${encodeURIComponent(m)}`).join("&")
      : "";
    return this.fetchJson(`/sensors/latest${params}`);
  },
};
