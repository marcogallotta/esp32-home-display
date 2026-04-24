window.metrics = {
  round1(value) {
    return value == null ? null : Math.round(value * 10) / 10;
  },

  calcAbsoluteHumidity(tempC, rhPct) {
    if (tempC == null || rhPct == null) return null;
    const svp = 6.112 * Math.exp((17.67 * tempC) / (tempC + 243.5));
    const avp = svp * (rhPct / 100);
    return (2.1674 * avp) / (273.15 + tempC) * 100;
  },

  calcVpd(tempC, rhPct) {
    if (tempC == null || rhPct == null) return null;
    const svp = 0.6108 * Math.exp((17.27 * tempC) / (tempC + 237.3));
    return svp * (1 - rhPct / 100);
  },

  formatTemp(value) {
    return value == null ? "—" : `${this.round1(value).toFixed(1)}°C`;
  },

  formatPct(value) {
    return value == null ? "—" : `${Math.round(value)}%`;
  },

  formatAbsHumidity(value) {
    return value == null ? "—" : `${this.round1(value).toFixed(1)} g/m³`;
  },

  formatVpd(value) {
    return value == null ? "—" : `${this.round1(value).toFixed(1)} kPa`;
  },

  formatAgo(ts) {
    if (!ts) return "No data";
    const diffMin = Math.round((Date.now() - new Date(ts).getTime()) / 60000);
    if (diffMin < 1) return "just now";
    if (diffMin < 60) return `${diffMin}m ago`;
    const h = Math.round(diffMin / 60);
    if (h < 48) return `${h}h ago`;
    return `${Math.round(h / 24)}d ago`;
  },

  isStale(ts) {
    if (!ts) return true;
    return (Date.now() - new Date(ts).getTime()) > window.CONFIG.staleAfterMs;
  },
};
