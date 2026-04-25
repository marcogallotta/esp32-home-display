function App() {
  const [range, setRange] = React.useState("24h");
  const [selectedSensorId, setSelectedSensorId] = React.useState("all");
  const [xiaomiMetric, setXiaomiMetric] = React.useState("moisture_pct");
  const [showDerivedCharts, setShowDerivedCharts] = React.useState(false);

  const [sensors, setSensors] = React.useState([]);
  const [historyBySensorId, setHistoryBySensorId] = React.useState({});
  const [loadingSensors, setLoadingSensors] = React.useState(true);
  const [loadingHistory, setLoadingHistory] = React.useState(false);
  const [sensorError, setSensorError] = React.useState("");
  const [historyError, setHistoryError] = React.useState("");
  const [rangeWindow, setRangeWindow] = React.useState(window.sensorModel.buildRangeWindow("24h"));

  const tempCanvas = React.useRef(null);
  const humidityCanvas = React.useRef(null);
  const absHumidityCanvas = React.useRef(null);
  const vpdCanvas = React.useRef(null);
  const xiaomiCanvas = React.useRef(null);

  const tempChart = React.useRef(null);
  const humidityChart = React.useRef(null);
  const absHumidityChart = React.useRef(null);
  const vpdChart = React.useRef(null);
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

  React.useEffect(() => {
    const datasets = switchbotSensorsToPlot.map((sensor) =>
      window.chartFactory.timeSeriesDataset(
        sensor.name,
        historyBySensorId[sensor.id] || [],
        (row) => (row.temperature_c == null ? null : window.metrics.round1(row.temperature_c))
      )
    );

    window.chartFactory.lineChart(tempCanvas.current, tempChart, datasets, "°C", rangeWindow);
  }, [switchbotSensorsToPlot, historyBySensorId, rangeWindow]);

  React.useEffect(() => {
    const datasets = switchbotSensorsToPlot.map((sensor) =>
      window.chartFactory.timeSeriesDataset(
        sensor.name,
        historyBySensorId[sensor.id] || [],
        (row) => (row.humidity_pct == null ? null : Math.round(row.humidity_pct))
      )
    );

    window.chartFactory.lineChart(humidityCanvas.current, humidityChart, datasets, "%", rangeWindow);
  }, [switchbotSensorsToPlot, historyBySensorId, rangeWindow]);

  React.useEffect(() => {
    if (!showDerivedCharts) {
      window.chartFactory.destroy(absHumidityChart);
      return;
    }

    const datasets = switchbotSensorsToPlot.map((sensor) =>
      window.chartFactory.timeSeriesDataset(
        sensor.name,
        historyBySensorId[sensor.id] || [],
        (row) => window.metrics.round1(window.metrics.calcAbsoluteHumidity(row.temperature_c, row.humidity_pct))
      )
    );

    window.chartFactory.lineChart(absHumidityCanvas.current, absHumidityChart, datasets, "g/m³", rangeWindow);
  }, [showDerivedCharts, switchbotSensorsToPlot, historyBySensorId, rangeWindow]);

  React.useEffect(() => {
    if (!showDerivedCharts) {
      window.chartFactory.destroy(vpdChart);
      return;
    }

    const datasets = switchbotSensorsToPlot.map((sensor) =>
      window.chartFactory.timeSeriesDataset(
        sensor.name,
        historyBySensorId[sensor.id] || [],
        (row) => window.metrics.round1(window.metrics.calcVpd(row.temperature_c, row.humidity_pct))
      )
    );

    window.chartFactory.lineChart(vpdCanvas.current, vpdChart, datasets, "kPa", rangeWindow);
  }, [showDerivedCharts, switchbotSensorsToPlot, historyBySensorId, rangeWindow]);

  React.useEffect(() => {
    if (xiaomiSensors.length === 0) {
      window.chartFactory.destroy(xiaomiChart);
      return;
    }

    const sensor = xiaomiSensors[0];
    const datasets = [
      window.chartFactory.timeSeriesDataset(
        `${sensor.name} ${xiaomiMetric.replaceAll("_", " ")}`,
        historyBySensorId[sensor.id] || [],
        (row) => row[xiaomiMetric]
      ),
    ];

    window.chartFactory.lineChart(xiaomiCanvas.current, xiaomiChart, datasets, xiaomiMetric, rangeWindow);
  }, [xiaomiSensors, historyBySensorId, xiaomiMetric, rangeWindow]);

  const loading = loadingSensors || loadingHistory;
  const error = sensorError || historyError;

  return (
    <div className="wrap">
      <div className="topbar">
        <div>
          <h1>Sensor Overview</h1>
          <p className="sub">Cheap insight first. Decisions later.</p>
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
        </div>
      </div>

      {error && <div className="card error-card">{error}</div>}
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
