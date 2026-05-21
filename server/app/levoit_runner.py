from __future__ import annotations

from .config import Config, LevoitAhControllerConfig
from .levoit_api_client import LevoitApiClient, LevoitApiError, SwitchbotLatestReading, SwitchbotSensorNotFound
from .levoit_controller import LevoitControlDecision, compute_decision

def _fmt(v: float | None) -> str:
    return f"{v:.2f}" if v is not None else "None"


def format_decision(
    reading: SwitchbotLatestReading,
    decision: LevoitControlDecision,
    cfg: LevoitAhControllerConfig,
) -> str:
    return "\n".join([
        f"sensor MAC:                {reading.mac}",
        f"temperature_c:             {reading.temperature_c}",
        f"humidity_pct:              {reading.humidity_pct}",
        f"current_absolute_humidity: {_fmt(decision.current_absolute_humidity)} g/m3",
        f"target_absolute_humidity:  {cfg.target_absolute_humidity} g/m3",
        f"ideal_humidity:            {_fmt(decision.ideal_humidity)} %",
        f"commanded_humidity:        {decision.commanded_humidity} %",
        f"action:                    {decision.action}",
        f"reason:                    {decision.reason}",
    ])


def run_once(
    config: Config,
    *,
    dry_run: bool = False,
    api_client: LevoitApiClient | None = None,
) -> tuple[int, str]:
    cfg = config.levoit_ah_controller

    if cfg.switchbot_mac is None:
        return 1, "levoit_ah_controller.switchbot_mac is not configured"

    if cfg.target_absolute_humidity is None:
        return 1, "levoit_ah_controller.target_absolute_humidity is not configured"

    if cfg.server_base_url is None:
        return 1, "levoit_ah_controller.server_base_url is not configured"

    if api_client is None:
        import httpx
        from pathlib import Path
        ca_cert = str(Path(__file__).parent.parent / "certs" / "cert.pem")
        api_client = LevoitApiClient(
            base_url=cfg.server_base_url,
            api_key=config.api_key,
            switchbot_mac=cfg.switchbot_mac,
            http_client=httpx.Client(verify=ca_cert),
        )

    try:
        reading = api_client.fetch_switchbot_latest()
    except SwitchbotSensorNotFound as exc:
        return 1, f"sensor not found: {exc}"
    except LevoitApiError as exc:
        return 1, f"API error: {exc}"

    decision = compute_decision(
        temperature_c=reading.temperature_c,
        humidity_pct=reading.humidity_pct,
        target_absolute_humidity=cfg.target_absolute_humidity,
        minimum_humidity=cfg.minimum_humidity,
        maximum_humidity=cfg.maximum_humidity,
        humidity_change_threshold=cfg.humidity_change_threshold,
    )

    output = format_decision(reading, decision, cfg)
    if dry_run:
        output = "[DRY RUN] no command will be sent\n" + output
    return 0, output
