window.chartFactory = {
  _charts: new Set(),
  _zoomedRange: null,

  makeTimeScale(rangeWindow) {
    return {
      type: "time",
      min: rangeWindow.startMs,
      max: rangeWindow.endMs,
      time: { tooltipFormat: "yyyy-MM-dd HH:mm" },
    };
  },

  _syncZoom(sourceChart) {
    const { min, max } = sourceChart.scales.x;
    this._zoomedRange = { min, max };
    for (const chart of this._charts) {
      if (chart === sourceChart) continue;
      chart.zoomScale("x", { min, max }, "none");
    }
  },

  _applyZoom(chart) {
    if (this._zoomedRange) {
      chart.zoomScale("x", this._zoomedRange, "none");
    }
  },

  destroy(chartRef) {
    if (chartRef.current) {
      this._charts.delete(chartRef.current);
      chartRef.current.destroy();
      chartRef.current = null;
    }
  },

  lineChart(canvas, chartRef, datasets, yLabel, rangeWindow) {
    if (!canvas) return;

    const self = this;
    const options = {
      responsive: true,
      maintainAspectRatio: false,
      parsing: false,
      interaction: { mode: "nearest", intersect: false },
      plugins: {
        legend: { display: true },
        zoom: {
          pan: {
            enabled: true,
            mode: "x",
            modifierKey: "shift",
            onPan: ({ chart }) => self._syncZoom(chart),
          },
          zoom: {
            wheel: { enabled: true },
            pinch: { enabled: true },
            drag: { enabled: true },
            mode: "x",
            onZoom: ({ chart }) => self._syncZoom(chart),
          },
        },
      },
      scales: {
        x: this.makeTimeScale(rangeWindow),
        y: { title: { display: true, text: yLabel } },
      },
    };

    if (!chartRef.current) {
      chartRef.current = new Chart(canvas, { type: "line", data: { datasets }, options });
      this._charts.add(chartRef.current);
      this._applyZoom(chartRef.current);
      return;
    }

    chartRef.current.data.datasets = datasets;
    chartRef.current.options = options;
    chartRef.current.update();
    this._applyZoom(chartRef.current);
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

  resetZoom(chartRefs) {
    this._zoomedRange = null;
    chartRefs.forEach((chartRef) => {
      if (chartRef.current && typeof chartRef.current.resetZoom === "function") {
        chartRef.current.resetZoom();
      }
    });
  },
};
