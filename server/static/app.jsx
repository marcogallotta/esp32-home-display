const _SENSOR_COLORS = {
  "Bed": "rgb(54, 162, 235)",
  "South": "rgb(255, 99, 132)",
  "South wall": "rgb(255, 159, 64)",
  "West": "rgb(255, 205, 86)",
};

function _sensorDataset(sensor, rows, valueFn) {
  return {
    ...window.chartFactory.timeSeriesDataset(sensor.name, rows, valueFn),
    borderColor: _SENSOR_COLORS[sensor.name],
    backgroundColor: "transparent",
  };
}

if (!window.CONFIG) {
  ReactDOM.createRoot(document.getElementById("root")).render(
    <div className="card error-card">
      {window.__configError || "Dashboard config missing: static/config.js not found"}
    </div>
  );
} else {

function App() {
  const [range, setRange] = React.useState("24h");
  const [selectedSensorId, setSelectedSensorId] = React.useState("all");
  const [xiaomiMetric, setXiaomiMetric] = React.useState("moisture_pct");
  const [showDerivedCharts, setShowDerivedCharts] = React.useState(false);

  const [sensors, setSensors] = React.useState([]);
  const [historyBySensorId, setHistoryBySensorId] = React.useState({});
  const [zoomedHistoryBySensorId, setZoomedHistoryBySensorId] = React.useState(null);
  const [loadingSensors, setLoadingSensors] = React.useState(true);
  const [loadingHistory, setLoadingHistory] = React.useState(false);
  const [sensorError, setSensorError] = React.useState("");
  const [historyError, setHistoryError] = React.useState("");
  const [openMeteoData, setOutdoorWeather] = React.useState([]);
  const [openMeteoError, setOutdoorError] = React.useState("");
  const [latestPollError, setLatestPollError] = React.useState("");
  const [lastLatestPollAt, setLastLatestPollAt] = React.useState(null);
  const [rangeWindow, setRangeWindow] = React.useState(window.sensorModel.buildRangeWindow("24h"));

  const sensorsRef = React.useRef(sensors);
  React.useEffect(() => { sensorsRef.current = sensors; }, [sensors]);

  const rangeWindowRef = React.useRef(rangeWindow);
  React.useEffect(() => { rangeWindowRef.current = rangeWindow; }, [rangeWindow]);

  const zoomCacheRef = React.useRef(null);
  React.useEffect(() => { zoomCacheRef.current = null; }, [range]);

  React.useEffect(() => {
    window.chartFactory.setZoomChangeCallback(async (zoomRange) => {
      if (!zoomRange) {
        setZoomedHistoryBySensorId(null);
        return;
      }
      const { min, max } = zoomRange;
      const rw = rangeWindowRef.current;
      const zoomedDurationMs = max - min;
      const originalDurationMs = rw.endMs - rw.startMs;
      const maxPoints = Math.max(50, Math.round(rw.maxPoints * (zoomedDurationMs / originalDurationMs)));
      const cache = zoomCacheRef.current;
      if (cache && cache.startMs <= min && cache.endMs >= max && cache.maxPoints >= maxPoints) {
        setZoomedHistoryBySensorId(cache.data);
        return;
      }

      const nowMs = Date.now();
      if (min >= nowMs) {
        setZoomedHistoryBySensorId({});
        return;
      }

      // Fetch a padded window (3× zoom duration) so panning doesn't re-fetch
      const pad = zoomedDurationMs;
      const fetchStart = Math.max(rw.startMs, min - pad);
      const fetchEnd = Math.min(nowMs, max + pad);
      const fetchMaxPoints = Math.max(50, Math.round(maxPoints * (fetchEnd - fetchStart) / zoomedDurationMs));
      const zoomWindow = {
        startTs: new Date(fetchStart).toISOString(),
        endTs: new Date(fetchEnd).toISOString(),
        maxPoints: fetchMaxPoints,
      };
      try {
        const entries = await Promise.all(
          sensorsRef.current.map(async (sensor) => {
            const rows = await window.api.fetchSensorReadings(sensor.id, zoomWindow);
            return [sensor.id, window.sensorModel.normalizeReadings(rows)];
          })
        );
        const result = Object.fromEntries(entries);
        zoomCacheRef.current = { startMs: fetchStart, endMs: fetchEnd, maxPoints: fetchMaxPoints, data: result };
        setZoomedHistoryBySensorId(result);
      } catch (err) {
        // leave existing data in place if the fetch fails
      }
    });
    return () => window.chartFactory.setZoomChangeCallback(null);
  }, []);

  const tempCanvas = React.useRef(null);
  const humidityCanvas = React.useRef(null);
  const absHumidityCanvas = React.useRef(null);
  const vpdCanvas = React.useRef(null);
  const precipCanvas = React.useRef(null);
  const windCanvas = React.useRef(null);
  const xiaomiCanvas = React.useRef(null);

  const tempChart = React.useRef(null);
  const humidityChart = React.useRef(null);
  const absHumidityChart = React.useRef(null);
  const vpdChart = React.useRef(null);
  const precipChart = React.useRef(null);
  const windChart = React.useRef(null);
  const xiaomiChart = React.useRef(null);

  const sensorGroups = React.useMemo(() => window.sensorModel.splitByType(sensors), [sensors]);
  const switchbotSensors = sensorGroups.switchbot;
  const xiaomiSensors = sensorGroups.xiaomi;

  const selectedSwitchbotSensor = React.useMemo(() => {
    if (selectedSensorId === "all") return null;
    return switchbotSensors.find((sensor) => sensor.id === selectedSensorId) || null;
  }, [selectedSensorId, switchbotSensors]);

  React.useEffect(() => {
    async function loadSensors() {
      setLoadingSensors(true);
      setSensorError("");
      try {
        const loadedSensors = await window.api.fetchSensors();
        setSensors(loadedSensors);
      } catch (err) {
        setSensorError(String(err));
      } finally {
        setLoadingSensors(false);
      }
    }

    loadSensors();
  }, []);

  React.useEffect(() => {
    if (sensors.length === 0) return;

    async function loadHistory() {
      const nextRangeWindow = window.sensorModel.buildRangeWindow(range);
      setZoomedHistoryBySensorId(null);
      window.chartFactory.clearZoomState();
      setLoadingHistory(true);
      setHistoryError("");

      try {
        const entries = await Promise.all(
          sensors.map(async (sensor) => {
            const rows = await window.api.fetchSensorReadings(sensor.id, nextRangeWindow);
            return [sensor.id, window.sensorModel.normalizeReadings(rows)];
          })
        );

        setRangeWindow(nextRangeWindow);
        setHistoryBySensorId(Object.fromEntries(entries));
      } catch (err) {
        setHistoryError(String(err));
      } finally {
        setLoadingHistory(false);
      }
    }

    loadHistory();
  }, [range, sensors]);

  React.useEffect(() => {
    async function loadOutdoorWeather() {
      const rw = window.sensorModel.buildRangeWindow(range);
      setOutdoorError("");
      try {
        const alwaysForecastEnd = new Date(Date.now() + 7 * 24 * 60 * 60 * 1000).toISOString();
        const data = await window.api.fetchOpenMeteoWeather(rw.startTs, alwaysForecastEnd);
        setOutdoorWeather(Array.isArray(data) ? data : []);
      } catch {
        setOutdoorError("Outdoor weather unavailable");
        setOutdoorWeather([]);
      }
    }
    loadOutdoorWeather();
  }, [range]);

  React.useEffect(() => {
    if (sensors.length === 0) return;

    let cancelled = false;

    async function pollLatest() {
      try {
        const data = await window.api.fetchLatestReadings();

        if (cancelled) return;

        setHistoryBySensorId((prev) => {
          const next = { ...prev };
          for (const item of data.sensors) {
            const row = { timestamp: item.latest_timestamp, ...item.reading };
            next[item.sensor_id] = window.sensorModel.mergeLatestIntoRows(
              next[item.sensor_id] || [],
              row
            );
          }
          return next;
        });

        setLatestPollError("");
        setLastLatestPollAt(new Date());
      } catch (err) {
        if (!cancelled) setLatestPollError(String(err));
      }
    }

    const intervalId = setInterval(pollLatest, window.CONFIG.latestPollMs);
    return () => {
      cancelled = true;
      clearInterval(intervalId);
    };
  }, [sensors]);

  const switchbotSummary = React.useMemo(() => {
    return switchbotSensors.map((sensor) =>
      window.sensorModel.buildSwitchbotSummary(sensor, historyBySensorId[sensor.id] || [])
    );
  }, [switchbotSensors, historyBySensorId]);

  const xiaomiSummary = React.useMemo(() => {
    return xiaomiSensors.map((sensor) =>
      window.sensorModel.buildXiaomiSummary(sensor, historyBySensorId[sensor.id] || [])
    );
  }, [xiaomiSensors, historyBySensorId]);

  const switchbotSensorsToPlot = React.useMemo(() => {
    return selectedSwitchbotSensor ? [selectedSwitchbotSensor] : switchbotSensors;
  }, [selectedSwitchbotSensor, switchbotSensors]);

  const historyFor = React.useCallback((sensorId) => {
    return (zoomedHistoryBySensorId?.[sensorId] ?? historyBySensorId[sensorId]) || [];
  }, [zoomedHistoryBySensorId, historyBySensorId]);

  const _OUTDOOR_COLOR = "rgb(0, 150, 80)";

  function _openMeteoDatasets(rows, valueFn) {
    const todayStart = new Date();
    todayStart.setHours(0, 0, 0, 0);
    const todayStartMs = todayStart.getTime();

    const past = rows.filter((r) => new Date(r.timestamp).getTime() < todayStartMs);
    const future = rows.filter((r) => new Date(r.timestamp).getTime() >= todayStartMs);
    const bridge = past.length > 0 ? [past[past.length - 1]] : [];

    function makeDs(label, rowSet, dash) {
      return {
        label,
        data: rowSet.map((r) => ({ x: new Date(r.timestamp), y: valueFn(r) })).filter((p) => p.y != null),
        borderColor: _OUTDOOR_COLOR,
        backgroundColor: "transparent",
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.15,
        ...(dash ? { borderDash: [5, 5] } : {}),
      };
    }

    const ds = [];
    if (past.length > 0) ds.push(makeDs("OpenMeteo", past, false));
    if (future.length > 0) ds.push(makeDs("OpenMeteo forecast", [...bridge, ...future], true));
    return ds;
  }

  const openMeteoTempDatasets = React.useMemo(() => {
    if (openMeteoData.length === 0) return [];
    return _openMeteoDatasets(openMeteoData, (r) => r.temperature_2m);
  }, [openMeteoData]);

  const openMeteoHumidityDatasets = React.useMemo(() => {
    if (openMeteoData.length === 0) return [];
    return _openMeteoDatasets(openMeteoData, (r) => r.relative_humidity_2m);
  }, [openMeteoData]);

  const openMeteoAbsHumidityDatasets = React.useMemo(() => {
    if (openMeteoData.length === 0) return [];
    return _openMeteoDatasets(openMeteoData, (r) =>
      window.metrics.round1(window.metrics.calcAbsoluteHumidity(r.temperature_2m, r.relative_humidity_2m))
    );
  }, [openMeteoData]);

  const openMeteoVpdDatasets = React.useMemo(() => {
    if (openMeteoData.length === 0) return [];
    return _openMeteoDatasets(openMeteoData, (r) =>
      window.metrics.round1(window.metrics.calcVpd(r.temperature_2m, r.relative_humidity_2m))
    );
  }, [openMeteoData]);

  const openMeteoPrecipDatasets = React.useMemo(() => {
    if (openMeteoData.length === 0) return [];
    return [
      ..._openMeteoDatasets(openMeteoData, (r) => r.rain != null && r.showers != null ? window.metrics.round1(r.rain + r.showers) : null).map((ds) => ({ ...ds, label: ds.label.replace("OpenMeteo", "Rain"), borderColor: "rgb(30, 80, 180)" })),
      ..._openMeteoDatasets(openMeteoData, (r) => r.snowfall).map((ds) => ({ ...ds, label: ds.label.replace("OpenMeteo", "Snow"), borderColor: "rgb(140, 200, 240)" })),
    ];
  }, [openMeteoData]);

  const openMeteoWindDatasets = React.useMemo(() => {
    if (openMeteoData.length === 0) return [];
    const warn = (ctx, threshold, defaultColor) =>
      ctx.p0.parsed.y >= threshold || ctx.p1.parsed.y >= threshold ? "rgb(220, 40, 40)" : defaultColor;
    return [
      ..._openMeteoDatasets(openMeteoData, (r) => r.wind_speed_10m).map((ds) => ({
        ...ds,
        label: ds.label.replace("OpenMeteo", "Wind"),
        borderColor: "rgb(60, 120, 210)",
        segment: { borderColor: (ctx) => warn(ctx, 15, "rgb(60, 120, 210)") },
      })),
      ..._openMeteoDatasets(openMeteoData, (r) => r.wind_gusts_10m).map((ds) => ({
        ...ds,
        label: ds.label.replace("OpenMeteo", "Gusts"),
        borderColor: "rgb(40, 40, 40)",
        segment: { borderColor: (ctx) => warn(ctx, 50, "rgb(40, 40, 40)") },
      })),
    ];
  }, [openMeteoData]);

  const tempDatasets = React.useMemo(() => {
    return [
      ...switchbotSensorsToPlot.map((sensor) =>
        _sensorDataset(sensor, historyFor(sensor.id), (row) => row.temperature_c == null ? null : window.metrics.round1(row.temperature_c))
      ),
      ...openMeteoTempDatasets,
    ];
  }, [switchbotSensorsToPlot, historyFor, openMeteoTempDatasets]);

  const humidityDatasets = React.useMemo(() => {
    return [
      ...switchbotSensorsToPlot.map((sensor) =>
        _sensorDataset(sensor, historyFor(sensor.id), (row) => row.humidity_pct == null ? null : Math.round(row.humidity_pct))
      ),
      ...openMeteoHumidityDatasets,
    ];
  }, [switchbotSensorsToPlot, historyFor, openMeteoHumidityDatasets]);

  const absHumidityDatasets = React.useMemo(() => {
    return [
      ...switchbotSensorsToPlot.map((sensor) =>
        _sensorDataset(sensor, historyFor(sensor.id),
          (row) => window.metrics.round1(window.metrics.calcAbsoluteHumidity(row.temperature_c, row.humidity_pct))
        )
      ),
      ...openMeteoAbsHumidityDatasets,
    ];
  }, [switchbotSensorsToPlot, historyFor, openMeteoAbsHumidityDatasets]);

  const vpdDatasets = React.useMemo(() => {
    return [
      ...switchbotSensorsToPlot.map((sensor) =>
        _sensorDataset(sensor, historyFor(sensor.id),
          (row) => window.metrics.round1(window.metrics.calcVpd(row.temperature_c, row.humidity_pct))
        )
      ),
      ...openMeteoVpdDatasets,
    ];
  }, [switchbotSensorsToPlot, historyFor, openMeteoVpdDatasets]);

  const xiaomiDatasets = React.useMemo(() => {
    if (xiaomiSensors.length === 0) return [];

    const sensor = xiaomiSensors[0];
    return [
      window.chartFactory.timeSeriesDataset(
        `${sensor.name} ${xiaomiMetric.replaceAll("_", " ")}`,
        historyFor(sensor.id),
        (row) => row[xiaomiMetric]
      ),
    ];
  }, [xiaomiSensors, historyFor, xiaomiMetric]);

  React.useEffect(() => {
    window.chartFactory.lineChart(tempCanvas.current, tempChart, tempDatasets, "°C", rangeWindow);
  }, [tempDatasets, rangeWindow]);

  React.useEffect(() => {
    window.chartFactory.lineChart(humidityCanvas.current, humidityChart, humidityDatasets, "%", rangeWindow);
  }, [humidityDatasets, rangeWindow]);

  React.useEffect(() => {
    if (!showDerivedCharts) {
      window.chartFactory.destroy(absHumidityChart);
      return;
    }
    window.chartFactory.lineChart(absHumidityCanvas.current, absHumidityChart, absHumidityDatasets, "g/m³", rangeWindow);
  }, [showDerivedCharts, absHumidityDatasets, rangeWindow]);

  React.useEffect(() => {
    if (!showDerivedCharts) {
      window.chartFactory.destroy(vpdChart);
      return;
    }
    window.chartFactory.lineChart(vpdCanvas.current, vpdChart, vpdDatasets, "kPa", rangeWindow);
  }, [showDerivedCharts, vpdDatasets, rangeWindow]);

  React.useEffect(() => {
    if (!showDerivedCharts) {
      window.chartFactory.destroy(precipChart);
      return;
    }
    window.chartFactory.lineChart(precipCanvas.current, precipChart, openMeteoPrecipDatasets, "mm", rangeWindow);
  }, [showDerivedCharts, openMeteoPrecipDatasets, rangeWindow]);

  React.useEffect(() => {
    if (!showDerivedCharts) {
      window.chartFactory.destroy(windChart);
      return;
    }
    window.chartFactory.lineChart(windCanvas.current, windChart, openMeteoWindDatasets, "km/h", rangeWindow);
  }, [showDerivedCharts, openMeteoWindDatasets, rangeWindow]);

  React.useEffect(() => {
    if (xiaomiDatasets.length === 0) {
      window.chartFactory.destroy(xiaomiChart);
      return;
    }
    window.chartFactory.lineChart(xiaomiCanvas.current, xiaomiChart, xiaomiDatasets, xiaomiMetric, rangeWindow);
  }, [xiaomiDatasets, xiaomiMetric, rangeWindow]);

  const loading = loadingSensors || loadingHistory;
  const error = sensorError || historyError;

  return (
    <div className="wrap">
      <div className="topbar">
        <div>
          <h1>Sensor Overview</h1>
          <p className="sub">Cheap insight first. Decisions later.</p>
          <div className="status-line">
            Latest polling: every {Math.round(window.CONFIG.latestPollMs / 1000)}s
            {lastLatestPollAt ? ` · last update ${window.metrics.formatAgo(lastLatestPollAt.toISOString())}` : ""}
            {latestPollError ? ` · latest poll failed` : ""}
          </div>
        </div>

        <div className="controls">
          <div className="seg">
            {Object.keys(window.CONFIG.rangeConfig).map((value) => (
              <button
                key={value}
                className={range === value ? "active" : ""}
                onClick={() => setRange(value)}
              >
                {value}
              </button>
            ))}
          </div>

          <select value={selectedSensorId} onChange={(e) => setSelectedSensorId(e.target.value)}>
            <option value="all">All SwitchBot</option>
            {switchbotSensors.map((sensor) => (
              <option key={sensor.id} value={sensor.id}>{sensor.name}</option>
            ))}
          </select>

          <button
            className="ghost-btn"
            onClick={() =>
              window.chartFactory.resetZoom([
                tempChart,
                humidityChart,
                absHumidityChart,
                vpdChart,
                precipChart,
                windChart,
                xiaomiChart,
              ])
            }
          >
            Reset zoom
          </button>
        </div>
      </div>

      {error && <div className="card error-card">{error}</div>}
      {latestPollError && <div className="card error-card">Latest poll error: {latestPollError}</div>}
      {openMeteoError && <div className="card error-card">{openMeteoError}</div>}
      {loading && <div className="card loading-card">Loading…</div>}

      <div className="section-title">SwitchBot</div>
      <div className="sensor-grid">
        {switchbotSummary.map((sensor) => (
          <window.SensorCard
            key={sensor.id}
            sensor={sensor}
            kind="switchbot"
            selected={selectedSensorId === sensor.id}
            onClick={() => setSelectedSensorId(selectedSensorId === sensor.id ? "all" : sensor.id)}
            clickable={true}
          />
        ))}
      </div>

      <div className="section-title">Xiaomi</div>
      <div className="sensor-grid">
        {xiaomiSummary.map((sensor) => (
          <window.SensorCard
            key={sensor.id}
            sensor={sensor}
            kind="xiaomi"
            selected={false}
            clickable={false}
          />
        ))}
      </div>

      <div className="charts">
        <window.ChartCard
          title="Temperature"
          right={<div className="hint">Overlay view for SwitchBot sensors</div>}
          canvasRef={tempCanvas}
        />

        <window.ChartCard
          title="Humidity"
          right={<div className="hint">Overlay view for SwitchBot sensors</div>}
          canvasRef={humidityCanvas}
        />

        <div className="toggle-row">
          <button className="ghost-btn" onClick={() => setShowDerivedCharts((v) => !v)}>
            {showDerivedCharts ? "Hide derived charts" : "Show derived charts"}
          </button>
        </div>

        {showDerivedCharts && (
          <>
            <window.ChartCard
              title="Absolute Humidity"
              right={<div className="hint">Derived from air temp + RH</div>}
              canvasRef={absHumidityCanvas}
            />
            <window.ChartCard
              title="Air VPD"
              right={<div className="hint">Derived from air temp + RH</div>}
              canvasRef={vpdCanvas}
            />
            <window.ChartCard
              title="Precipitation"
              right={<div className="hint">OpenMeteo · rain+showers mm, snow cm</div>}
              canvasRef={precipCanvas}
            />
            <window.ChartCard
              title="Wind"
              right={<div className="hint">OpenMeteo · 10m · km/h</div>}
              canvasRef={windCanvas}
            />
          </>
        )}

        {xiaomiSensors.length > 0 && (
          <window.ChartCard
            title="Xiaomi Detail"
            right={
              <select value={xiaomiMetric} onChange={(e) => setXiaomiMetric(e.target.value)}>
                <option value="moisture_pct">Moisture</option>
                <option value="light_lux">Lux</option>
                <option value="conductivity_us_cm">Conductivity</option>
                <option value="temperature_c">Temperature</option>
              </select>
            }
            canvasRef={xiaomiCanvas}
          />
        )}
      </div>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(<App />);
}
