window.api = {
  async fetchJson(url) {
    const res = await fetch(url, {
      headers: { "X-api-key": window.CONFIG.apiKey },
    });

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
};
