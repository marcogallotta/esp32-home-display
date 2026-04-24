function Card({ className = "", children, onClick }) {
  return <div className={`card ${className}`} onClick={onClick}>{children}</div>;
}

function SensorCard({ sensor, kind, selected, onClick, clickable = true }) {
  const stale = window.metrics.isStale(sensor.latestTimestamp);

  return (
    <Card
      className={`sensor-card ${selected ? "selected" : ""} ${clickable ? "" : "static"}`}
      onClick={clickable ? onClick : undefined}
    >
      <div className="row">
        <div>
          <div className="sensor-name">{sensor.name}</div>
          <div className="muted">Updated {window.metrics.formatAgo(sensor.latestTimestamp)}</div>
        </div>
        <div className={`pill ${stale ? "warn" : "ok"}`}>{stale ? "Stale" : "Live"}</div>
      </div>

      <div className="metric-grid">
        {kind === "switchbot" ? (
          <>
            <div className="metric">
              <div className="metric-label">Temp</div>
              <div className="metric-value">{sensor.latestTempText}</div>
            </div>
            <div className="metric">
              <div className="metric-label">Humidity</div>
              <div className="metric-value">{sensor.latestHumidityText}</div>
            </div>
            <div className="metric">
              <div className="metric-label">Abs humidity</div>
              <div className="metric-value">{sensor.latestAbsoluteHumidityText}</div>
            </div>
            <div className="metric">
              <div className="metric-label">VPD</div>
              <div className="metric-value">{sensor.latestVpdText}</div>
            </div>
          </>
        ) : (
          <>
            <div className="metric">
              <div className="metric-label">Moisture</div>
              <div className="metric-value">
                {sensor.latestMoisture == null ? "—" : `${Math.round(sensor.latestMoisture)}%`}
              </div>
            </div>
            <div className="metric">
              <div className="metric-label">Lux</div>
              <div className="metric-value">
                {sensor.latestLux == null ? "—" : sensor.latestLux}
              </div>
            </div>
          </>
        )}
      </div>
    </Card>
  );
}

function ChartCard({ title, right, canvasRef }) {
  return (
    <Card className="chart-card">
      <div className="chart-head">
        <h3 className="chart-title">{title}</h3>
        {right}
      </div>
      <div className="chart-box">
        <canvas ref={canvasRef}></canvas>
      </div>
    </Card>
  );
}

window.Card = Card;
window.SensorCard = SensorCard;
window.ChartCard = ChartCard;
