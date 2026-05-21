from __future__ import annotations

from .config import Config, LevoitAhControllerConfig
from .levoit_api_client import LevoitApiClient, LevoitApiError, SwitchbotLatestReading, SwitchbotSensorNotFound
from .levoit_controller import LevoitControlDecision, compute_decision
from .levoit_vesync import HumidifierState, LevoitVeSyncClient, VeSyncError


def _mask_cid(cid: str) -> str:
    return f"{cid[:4]}...{cid[-4:]}" if len(cid) > 8 else cid


def _reason_sentence(decision: LevoitControlDecision) -> str:
    if decision.action == "set":
        return "target changed enough to update"
    r = decision.reason
    if "threshold" in r:
        return "device already at target"
    if "temperature" in r:
        return "temperature reading missing"
    return r


def format_decision(
    reading: SwitchbotLatestReading,
    decision: LevoitControlDecision,
    cfg: LevoitAhControllerConfig,
    device_state: HumidifierState | None = None,
    show_device: bool = True,
) -> str:
    temp = f"{reading.temperature_c:.1f}" if reading.temperature_c is not None else "?"
    rh = f"{round(reading.humidity_pct)}%" if reading.humidity_pct is not None else "?"
    cur_ah = f"{decision.current_absolute_humidity:.1f}" if decision.current_absolute_humidity is not None else "?"
    ideal = f"{decision.ideal_humidity:.1f}%" if decision.ideal_humidity is not None else "?"

    if decision.action == "set":
        command_line = f"set Levoit target to {decision.commanded_humidity}%"
    else:
        command_line = "no command sent"

    prev = ""
    if device_state is not None and device_state.current_target_humidity is not None:
        prev = f", was {device_state.current_target_humidity}%"

    status = (
        f"{reading.mac}  {temp}°C {rh} RH  AH {cur_ah}/{cfg.target_absolute_humidity:.1f} g/m³"
        f"  ideal {ideal}  Command: {command_line}  {_reason_sentence(decision)}{prev}"
    )

    lines = []
    if show_device and device_state is not None:
        lines.append(f"Device: {device_state.name} ({device_state.device_type}) {_mask_cid(device_state.cid)}")
    lines.append(status)

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
    show_device: bool = False,
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

    output = format_decision(reading, decision, cfg, device_state, show_device=show_device)

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
