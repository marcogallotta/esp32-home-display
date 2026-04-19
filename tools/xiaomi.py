import argparse
import asyncio
from collections import defaultdict
from datetime import datetime, timedelta
from typing import Any

from bleak import BleakClient, BleakScanner
from bleak.backends.scanner import AdvertisementData


LOCAL_NAME = "Flower care"
FE95_SERVICE_UUID = "0000fe95-0000-1000-8000-00805f9b34fb"

MODE_UUID = "00001a00-0000-1000-8000-00805f9b34fb"
REALTIME_UUID = "00001a01-0000-1000-8000-00805f9b34fb"
BATTERY_UUID = "00001a02-0000-1000-8000-00805f9b34fb"

HIST_CTRL_UUID = "00001a10-0000-1000-8000-00805f9b34fb"
HIST_DATA_UUID = "00001a11-0000-1000-8000-00805f9b34fb"
EPOCH_UUID = "00001a12-0000-1000-8000-00805f9b34fb"


def normalize_mac(mac: str | None) -> str | None:
    return mac.upper() if mac else None


def entry_datetime(entry_ts: int, device_now: int, wall_now: datetime) -> datetime:
    age_s = device_now - entry_ts
    return wall_now - timedelta(seconds=age_s)


def decode_passive_payload(payload: bytes) -> tuple[str, Any] | None:
    if len(payload) < 15:
        return None

    obj_id = payload[12] | (payload[13] << 8)
    length = payload[14]
    data = payload[15:15 + length]

    if obj_id == 0x1004 and length == 2:
        return ("temperature_c", int.from_bytes(data, "little", signed=True) / 10.0)
    if obj_id == 0x1007 and length == 3:
        return ("lux", int.from_bytes(data, "little"))
    if obj_id == 0x1008 and length == 1:
        return ("moisture_pct", data[0])
    if obj_id == 0x1009 and length == 2:
        return ("conductivity_us_cm", int.from_bytes(data, "little"))

    return None


def parse_realtime(data: bytes) -> dict[str, Any]:
    if len(data) < 10:
        raise ValueError(f"unexpected realtime payload length: {len(data)}")

    return {
        "temperature_c": data[0] / 10.0,
        "lux": int.from_bytes(data[3:7], "little"),
        "moisture_pct": data[7],
        "conductivity_us_cm": int.from_bytes(data[8:10], "little"),
        "raw_hex": data.hex(),
    }


def parse_battery(data: bytes) -> dict[str, Any]:
    if not data:
        raise ValueError("empty battery payload")

    firmware = data[2:].decode("utf-8", errors="ignore").rstrip("\x00") if len(data) > 2 else ""
    return {
        "battery_pct": data[0],
        "firmware": firmware,
        "raw_hex": data.hex(),
    }


def parse_history_entry(data: bytes) -> dict[str, Any] | None:
    if len(data) != 16:
        return None
    if data == b"\xff" * 16:
        return None

    ts = int.from_bytes(data[0:4], "little")
    if ts == 0xFFFFFFFF:
        return None

    return {
        "ts": ts,
        "temperature_c": int.from_bytes(data[4:6], "little", signed=True) / 10.0,
        "lux": int.from_bytes(data[7:11], "little"),
        "moisture_pct": data[11],
        "conductivity_us_cm": int.from_bytes(data[12:14], "little"),
        "raw_hex": data.hex(),
    }


async def read_current(mac: str) -> dict[str, Any]:
    async with BleakClient(mac) as client:
        await client.write_gatt_char(MODE_UUID, b"\xA0\x1F", response=True)
        await asyncio.sleep(0.5)

        realtime_raw = bytes(await client.read_gatt_char(REALTIME_UUID))
        battery_raw = bytes(await client.read_gatt_char(BATTERY_UUID))

        return {
            "mac": mac,
            "realtime": parse_realtime(realtime_raw),
            "battery": parse_battery(battery_raw),
        }


async def read_history(
    mac: str,
    seconds_ago: int,
    max_entries: int,
    stop_on_empty: bool = True,
) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []

    async with BleakClient(mac) as client:
        wall_now = datetime.now()
        device_now = int.from_bytes(bytes(await client.read_gatt_char(EPOCH_UUID)), "little")

        await client.write_gatt_char(HIST_CTRL_UUID, b"\xA0\x00\x00", response=True)
        await asyncio.sleep(0.3)

        for idx in range(max_entries):
            cmd = bytes([0xA1, idx & 0xFF, (idx >> 8) & 0xFF])
            await client.write_gatt_char(HIST_CTRL_UUID, cmd, response=True)
            await asyncio.sleep(0.2)

            raw = bytes(await client.read_gatt_char(HIST_DATA_UUID))
            entry = parse_history_entry(raw)

            if entry is None:
                if stop_on_empty:
                    break
                continue

            age_s = device_now - entry["ts"]
            if age_s < 0:
                continue

            entry["age_s"] = age_s
            entry["datetime"] = entry_datetime(entry["ts"], device_now, wall_now)

            if age_s > seconds_ago:
                break

            rows.append(entry)

        return {
            "mac": mac,
            "device_now": device_now,
            "wall_now": wall_now,
            "history": rows,
        }


async def run_passive(
    mac_filter: str | None,
    duration: int,
) -> dict[str, list[tuple[str, Any]]]:
    seen: dict[str, list[tuple[str, Any]]] = defaultdict(list)

    def on_device(device, adv: AdvertisementData):
        addr = device.address.upper()

        if mac_filter and addr != mac_filter:
            return
        if adv is None or adv.local_name != LOCAL_NAME:
            return
        if FE95_SERVICE_UUID not in adv.service_data:
            return

        decoded = decode_passive_payload(adv.service_data[FE95_SERVICE_UUID])
        if decoded is None:
            return
        if decoded in seen[addr]:
            return

        seen[addr].append(decoded)
        print(f"{addr=} {decoded=}")

    scanner = BleakScanner(detection_callback=on_device)
    await scanner.start()
    await asyncio.sleep(duration)
    await scanner.stop()

    return dict(seen)


def print_current(result: dict[str, Any]) -> None:
    print("\n[current]")
    print(f"mac={result['mac']}")
    print(result["realtime"])
    print(result["battery"])


def print_history(result: dict[str, Any]) -> None:
    print("\n[history]")
    print(f"mac={result['mac']}")
    print(f"device_now={result['device_now']}")
    print(f"wall_now={result['wall_now']}")
    for row in result["history"]:
        print(row)


def require_mac(args: argparse.Namespace, for_modes: list[str]) -> None:
    needs_mac = any(getattr(args, mode) for mode in for_modes)
    if needs_mac and not args.mac:
        raise SystemExit("--mac is required for --current and/or --history")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Xiaomi Flower Care / MiFlora utility")

    p.add_argument("--mac", type=str, help="sensor MAC address, e.g. 5C:85:7E:14:43:45")

    p.add_argument("--passive", action="store_true", help="run passive FE95 scan")
    p.add_argument("--current", action="store_true", help="read current realtime + battery")
    p.add_argument("--history", action="store_true", help="read history entries")

    p.add_argument("--passive-seconds", type=int, default=60, help="passive scan duration")
    p.add_argument("--history-seconds-ago", type=int, default=24 * 3600, help="history cutoff window")
    p.add_argument("--history-max-entries", type=int, default=24, help="history max entries to probe")

    p.add_argument("--no-stop-on-empty", action="store_true", help="history: continue past empty slots")

    return p


async def async_main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    args.mac = normalize_mac(args.mac)
    require_mac(args, ["current", "history"])

    if not args.passive and not args.current and not args.history:
        parser.error("select at least one mode: --passive, --current, --history")

    if args.passive:
        await run_passive(
            mac_filter=args.mac,
            duration=args.passive_seconds,
        )

    if args.current:
        current_result = await read_current(
            mac=args.mac,
        )
        print_current(current_result)

    if args.history:
        history_result = await read_history(
            mac=args.mac,
            seconds_ago=args.history_seconds_ago,
            max_entries=args.history_max_entries,
            stop_on_empty=not args.no_stop_on_empty,
        )
        print_history(history_result)


if __name__ == "__main__":
    asyncio.run(async_main())
