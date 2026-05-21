from __future__ import annotations

from .config import Config, LevoitAhControllerConfig
from .levoit_api_client import LevoitApiClient, LevoitApiError, SwitchbotLatestReading, SwitchbotSensorNotFound
from .levoit_controller import LevoitControlDecision, compute_decision
from .levoit_vesync import HumidifierState, LevoitVeSyncClient, VeSyncError


def _fmt(v: float | None) -> str:
    return f"{v:.2f}" if v is not None else "None"


def format_decision(
    reading: SwitchbotLatestReading,
    decision: LevoitControlDecision,
    cfg: LevoitAhControllerConfig,
    device_state: HumidifierState | None = None,
) -> str:
    lines = [
        f"sensor MAC:                {reading.mac}",
        f"temperature_c:             {reading.temperature_c}",
        f"humidity_pct:              {reading.humidity_pct}",
        f"current_absolute_humidity: {_fmt(decision.current_absolute_humidity)} g/m3",
        f"target_absolute_humidity:  {cfg.target_absolute_humidity} g/m3",
        f"ideal_humidity:            {_fmt(decision.ideal_humidity)} %",
        f"commanded_humidity:        {decision.commanded_humidity} %",
        f"action:                    {decision.action}",
        f"reason:                    {decision.reason}",
    ]
    if device_state is not None:
        lines += [
            f"device name:               {device_state.name}",
            f"device type:               {device_state.device_type}",
            f"device CID:                {device_state.cid}",
            f"device target humidity:    {device_state.current_target_humidity} %",
        ]
    return "\n".join(lines)


def _build_api_client(config: Config) -> LevoitApiClient:
    import httpx
    from pathlib import Path
    ca_cert = str(Path(__file__).parent.parent / "certs" / "cert.pem")
    cfg = config.levoit_ah_controller
    return LevoitApiClient(
        base_url=cfg.server_base_url,
        api_key=config.api_key,
        switchbot_mac=cfg.switchbot_mac,
        http_client=httpx.Client(verify=ca_cert),
    )


def _build_vesync_client(config: Config) -> LevoitVeSyncClient | None:
    username = config.vesync_username
    password = config.vesync_password
    cid = config.vesync_device_cid
    if username is None or password is None or cid is None:
        return None
    return LevoitVeSyncClient(username=username, password=password, cid=cid)


_sentinel = object()


def run_once(
    config: Config,
    *,
    dry_run: bool = False,
    api_client: LevoitApiClient | None = None,
    vesync_client: LevoitVeSyncClient | None = _sentinel,
) -> tuple[int, str]:
    cfg = config.levoit_ah_controller

    if cfg.switchbot_mac is None:
        return 1, "levoit_ah_controller.switchbot_mac is not configured"

    if cfg.target_absolute_humidity is None:
        return 1, "levoit_ah_controller.target_absolute_humidity is not configured"

    if cfg.server_base_url is None:
        return 1, "levoit_ah_controller.server_base_url is not configured"

    if api_client is None:
        api_client = _build_api_client(config)

    if vesync_client is _sentinel:
        vesync_client = _build_vesync_client(config)

    try:
        reading = api_client.fetch_switchbot_latest()
    except SwitchbotSensorNotFound as exc:
        return 1, f"sensor not found: {exc}"
    except LevoitApiError as exc:
        return 1, f"API error: {exc}"

    device_state: HumidifierState | None = None
    if vesync_client is not None:
        try:
            device_state = vesync_client.fetch_state()
        except VeSyncError as exc:
            return 1, f"VeSync error: {exc}"

    decision = compute_decision(
        temperature_c=reading.temperature_c,
        humidity_pct=reading.humidity_pct,
        target_absolute_humidity=cfg.target_absolute_humidity,
        minimum_humidity=cfg.minimum_humidity,
        maximum_humidity=cfg.maximum_humidity,
        humidity_change_threshold=cfg.humidity_change_threshold,
        current_device_target_humidity=(
            device_state.current_target_humidity if device_state is not None else None
        ),
    )

    output = format_decision(reading, decision, cfg, device_state)

    if decision.action == "set":
        if dry_run:
            output = f"[DRY RUN] would set humidity to {decision.commanded_humidity}%\n" + output
        else:
            if vesync_client is None:
                return 1, "VeSync credentials not configured; cannot send command"
            try:
                vesync_client.set_humidity(decision.commanded_humidity)
            except VeSyncError as exc:
                return 1, f"VeSync error: {exc}"
    elif dry_run:
        output = "[DRY RUN] no command will be sent\n" + output

    return 0, output
