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
    if (!canvas) return;

    const options = {
      responsive: true,
      maintainAspectRatio: false,
      parsing: false,
      interaction: { mode: "nearest", intersect: false },
      plugins: { legend: { display: true } },
      scales: {
        x: this.makeTimeScale(rangeWindow),
        y: { title: { display: true, text: yLabel } },
      },
    };

    if (!chartRef.current) {
      chartRef.current = new Chart(canvas, {
        type: "line",
        data: { datasets },
        options,
      });
      return;
    }

    chartRef.current.data.datasets = datasets;
    chartRef.current.options = options;
    chartRef.current.update();
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
