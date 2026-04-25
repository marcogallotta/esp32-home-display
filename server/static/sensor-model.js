window.sensorModel = {
  splitByType(sensors) {
    return {
      switchbot: sensors.filter((sensor) => sensor.type === "switchbot"),
      xiaomi: sensors.filter((sensor) => sensor.type === "xiaomi"),
    };
  },

  buildRangeWindow(range) {
    const cfg = window.CONFIG.rangeConfig[range] || window.CONFIG.rangeConfig["24h"];
    const endMs = Date.now();
    const startMs = endMs - cfg.hours * 60 * 60 * 1000;

    return {
      startMs,
      endMs,
      startTs: new Date(startMs).toISOString(),
      endTs: new Date(endMs).toISOString(),
      maxPoints: cfg.maxPoints,
    };
  },

  normalizeReadings(rows) {
    return rows.slice().reverse();
  },

  mergeLatestIntoRows(rows, latest) {
    if (!latest) return rows;
    const existingIndex = rows.findIndex((row) => row.timestamp === latest.timestamp);
    if (existingIndex >= 0) {
      const nextRows = rows.slice();
      nextRows[existingIndex] = latest;
      return nextRows;
    }
    return [...rows, latest];
  },

  buildSwitchbotSummary(sensor, rows) {
    const latest = rows[rows.length - 1];
    const latestTemp = latest?.temperature_c ?? null;
    const latestHumidity = latest?.humidity_pct ?? null;
    const latestAbsoluteHumidity = window.metrics.calcAbsoluteHumidity(latestTemp, latestHumidity);
    const latestVpd = window.metrics.calcVpd(latestTemp, latestHumidity);

    return {
      ...sensor,
      latestTimestamp: latest?.timestamp,
      latestTempText: window.metrics.formatTemp(latestTemp),
      latestHumidityText: window.metrics.formatPct(latestHumidity),
      latestAbsoluteHumidityText: window.metrics.formatAbsHumidity(latestAbsoluteHumidity),
      latestVpdText: window.metrics.formatVpd(latestVpd),
    };
  },

  buildXiaomiSummary(sensor, rows) {
    const latest = rows[rows.length - 1];
    return {
      ...sensor,
      latestTimestamp: latest?.timestamp,
      latestMoisture: latest?.moisture_pct ?? null,
      latestLux: latest?.light_lux ?? null,
    };
  },
};
