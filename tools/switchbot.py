import argparse
import asyncio
from typing import Any

from bleak import BleakClient, BleakScanner


SWITCHBOT_ID = 2409
BOT_CHAR_UUID = "cba20002-224d-11e6-9fb8-0002a5d5c51b"


def normalize_mac(mac: str | None) -> str | None:
    return mac.upper() if mac else None


def parse_named_sensor(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("sensor must be NAME=MAC")
    name, mac = value.split("=", 1)
    name = name.strip()
    mac = mac.strip().upper()
    if not name or not mac:
        raise argparse.ArgumentTypeError("sensor must be NAME=MAC")
    return name, mac


def is_meter_payload(payload: bytes) -> bool:
    return len(payload) >= 12 and payload[6] != 0x00


def decode_meter(addr: str, payload: bytes, known_sensors: dict[str, str]) -> dict[str, Any]:
    if len(payload) < 11:
        raise ValueError(f"payload too short: {len(payload)}")

    temp_decimal = payload[8] & 0x0F
    temp_int_raw = payload[9]
    humidity = payload[10] & 0x7F

    sign = 1 if (temp_int_raw & 0x80) else -1
    temp_int = temp_int_raw & 0x7F
    temperature = sign * (temp_int + temp_decimal / 10.0)

    return {
        "name": known_sensors.get(addr, "Unknown"),
        "mac": addr,
        "temperature_c": temperature,
        "humidity_pct": humidity,
        "raw_hex": payload.hex(),
    }


async def scan_meters(
    scan_seconds: int,
    hub_mac: str | None,
    bot_mac: str | None,
    ignore_macs: set[str],
    known_sensors: dict[str, str],
) -> dict[str, dict[str, Any]]:
    sensors: dict[str, dict[str, Any]] = {}

    def on_device(device, adv):
        if adv is None or SWITCHBOT_ID not in adv.manufacturer_data:
            return

        addr = device.address.upper()
        payload = adv.manufacturer_data[SWITCHBOT_ID]

        if hub_mac and addr == hub_mac:
            return
        if bot_mac and addr == bot_mac:
            return
        if addr in ignore_macs:
            return
        if not is_meter_payload(payload):
            return

        sensors[addr] = {
            "rssi": adv.rssi,
            "payload": payload,
        }

    scanner = BleakScanner(detection_callback=on_device)
    await scanner.start()
    await asyncio.sleep(scan_seconds)
    await scanner.stop()

    return sensors


async def bot_command(bot_mac: str, action: str) -> None:
    actions = {
        "press": 0x00,
        "on": 0x01,
        "off": 0x02,
    }

    async with BleakClient(bot_mac) as client:
        await client.write_gatt_char(
            BOT_CHAR_UUID,
            bytes([0x57, 0x01, actions[action]]),
        )


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="SwitchBot meter scan / bot control")

    p.add_argument("--scan", action="store_true", help="scan for SwitchBot meters")
    p.add_argument("--bot", choices=["press", "on", "off"], help="send bot command")

    p.add_argument("--scan-seconds", type=int, default=10, help="scan duration")
    p.add_argument("--hub-mac", type=str, help="hub MAC to exclude from scan")
    p.add_argument("--bot-mac", type=str, help="bot MAC for control and optional scan exclusion")

    p.add_argument(
        "--ignore-mac",
        action="append",
        default=[],
        help="MAC to ignore during scan; repeatable",
    )
    p.add_argument(
        "--sensor",
        action="append",
        type=parse_named_sensor,
        default=[],
        help="known sensor mapping NAME=MAC; repeatable",
    )

    return p


async def async_main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    if not args.scan and not args.bot:
        parser.error("select at least one mode: --scan and/or --bot")

    hub_mac = normalize_mac(args.hub_mac)
    bot_mac = normalize_mac(args.bot_mac)
    ignore_macs = {normalize_mac(x) for x in args.ignore_mac}
    known_sensors = {mac: name for name, mac in args.sensor}

    if args.bot and not bot_mac:
        parser.error("--bot requires --bot-mac")

    if args.scan:
        found = await scan_meters(
            scan_seconds=args.scan_seconds,
            hub_mac=hub_mac,
            bot_mac=bot_mac,
            ignore_macs=ignore_macs,
            known_sensors=known_sensors,
        )

        print("Sensors:")
        for addr in sorted(found):
            info = found[addr]
            print(decode_meter(addr, info["payload"], known_sensors))

    if args.bot:
        await bot_command(bot_mac, args.bot)


if __name__ == "__main__":
    asyncio.run(async_main())
