window.chartFactory = {
  makeTimeScale(rangeWindow) {
    return {
      type: "time",
      min: rangeWindow.startMs,
      max: rangeWindow.endMs,
      time: { tooltipFormat: "yyyy-MM-dd HH:mm" },
    };
  },

  destroy(chartRef) {
    if (chartRef.current) {
      chartRef.current.destroy();
      chartRef.current = null;
    }
  },

  lineChart(canvas, chartRef, datasets, yLabel, rangeWindow) {
    this.destroy(chartRef);
    if (!canvas) return;

    chartRef.current = new Chart(canvas, {
      type: "line",
      data: { datasets },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        parsing: false,
        interaction: { mode: "nearest", intersect: false },
        plugins: { legend: { display: true } },
        scales: {
          x: this.makeTimeScale(rangeWindow),
          y: { title: { display: true, text: yLabel } },
        },
      },
    });
  },

  timeSeriesDataset(label, rows, valueGetter) {
    return {
      label,
      data: rows
        .map((row) => ({ x: new Date(row.timestamp), y: valueGetter(row) }))
        .filter((point) => point.y != null),
      borderWidth: 2,
      pointRadius: 0,
      tension: 0.15,
    };
  },
};
