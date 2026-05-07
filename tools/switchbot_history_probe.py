#!/usr/bin/env python3
"""
Probe SwitchBot Outdoor Meter history over BLE.

This only replays commands observed from the official app:
  57 0f 3a
  57 0f 3b 00
  57 0f 3c 00 <index:u32_be> <count>

Default UUIDs match the common SwitchBot BLE service layout, but they are CLI-overridable.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from bleak import BleakClient

DEFAULT_WRITE_UUID = "cba20002-224d-11e6-9fb8-0002a5d5c51b"
DEFAULT_NOTIFY_UUID = "cba20003-224d-11e6-9fb8-0002a5d5c51b"


@dataclass(frozen=True)
class Metadata:
    start_epoch: int
    end_epoch: int
    end_index: int
    interval_seconds: int


@dataclass(frozen=True)
class PageRequest:
    index: int
    count: int


def parse_int(value: str) -> int:
    value = value.strip().lower()
    if value.startswith("0x"):
        return int(value, 16)
    return int(value, 10)


def parse_time_arg(value: str) -> int:
    raw = value.strip()
    lowered = raw.lower()
    if lowered.startswith("0x") or lowered.isdigit():
        return parse_int(raw)

    # Accept ISO-like datetime strings, for example:
    #   2026-05-07T08:00:00+02:00
    #   2026-05-07 08:00:00+02:00
    #   2026-05-07T06:00:00Z
    normalized = raw.replace("Z", "+00:00").replace("z", "+00:00")
    try:
        dt = datetime.fromisoformat(normalized)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "time must be Unix epoch or ISO datetime, e.g. 2026-05-07T08:00:00+02:00"
        ) from exc

    if dt.tzinfo is None:
        raise argparse.ArgumentTypeError(
            "datetime must include timezone, e.g. 2026-05-07T08:00:00+02:00 or ...Z"
        )
    return int(dt.timestamp())


def utc_iso(epoch: int) -> str:
    return datetime.fromtimestamp(epoch, tz=timezone.utc).isoformat()


def build_time_sync_command(epoch: int | None = None) -> bytes:
    # Observed shape: 57 00 05 03 0d 00 00 00 00 <epoch:u32_be>
    if epoch is None:
        epoch = int(time.time())
    return bytes.fromhex("570005030d00000000") + epoch.to_bytes(4, "big")


def build_start_command() -> bytes:
    return bytes.fromhex("570f3a")


def build_metadata_command() -> bytes:
    return bytes.fromhex("570f3b00")


def build_page_command(index: int, count: int) -> bytes:
    if not 0 <= index <= 0xFFFFFFFF:
        raise ValueError("index must fit uint32")
    if not 1 <= count <= 0xFF:
        raise ValueError("count must fit uint8 and be non-zero")
    return bytes.fromhex("570f3c00") + index.to_bytes(4, "big") + bytes([count])


def parse_metadata(data: bytes) -> Metadata:
    # Observed: 01 <start:u32_be> <end:u32_be> <end_index:u32_be> <interval:u16_be>
    if len(data) < 15 or data[0] != 0x01:
        raise ValueError(f"metadata response has unexpected shape: {data.hex()}")
    return Metadata(
        start_epoch=int.from_bytes(data[1:5], "big"),
        end_epoch=int.from_bytes(data[5:9], "big"),
        end_index=int.from_bytes(data[9:13], "big"),
        interval_seconds=int.from_bytes(data[13:15], "big"),
    )


def epoch_to_index(metadata: Metadata, epoch: int, *, round_up: bool) -> int:
    offset = (epoch - metadata.start_epoch) / metadata.interval_seconds
    index = math.ceil(offset) if round_up else math.floor(offset)
    return max(0, min(metadata.end_index, int(index)))


def build_page_plan(args: argparse.Namespace, metadata: Metadata) -> list[PageRequest]:
    if args.start_index is not None and args.start_epoch is not None:
        raise ValueError("use only one of --start-index or --start/--start-epoch")

    if args.end_index is not None and args.end_epoch is not None:
        raise ValueError("use only one of --end-index or --end/--end-epoch")

    # Keep page reads simple: always request full pages.
    # If a user supplies an end time/index, we may over-fetch slightly; trim later after decode.
    full_count = args.page_count

    if args.start_epoch is not None or args.end_epoch is not None or args.end_index is not None:
        start_index = (
            epoch_to_index(metadata, args.start_epoch, round_up=False)
            if args.start_epoch is not None
            else (args.start_index if args.start_index is not None else 0)
        )
        end_index = (
            epoch_to_index(metadata, args.end_epoch, round_up=True)
            if args.end_epoch is not None
            else (args.end_index if args.end_index is not None else metadata.end_index)
        )

        start_index = max(0, min(metadata.end_index, start_index))
        end_index = max(start_index, min(metadata.end_index, end_index))

        requests: list[PageRequest] = []
        index = start_index
        while index < end_index:
            requests.append(PageRequest(index=index, count=full_count))
            index += full_count
        return requests

    if args.start_index is None:
        start_index = max(0, metadata.end_index - args.pages * full_count)
    else:
        start_index = max(0, min(metadata.end_index, args.start_index))

    requests = []
    index = start_index
    for _ in range(args.pages):
        if index >= metadata.end_index:
            break
        requests.append(PageRequest(index=index, count=full_count))
        index += full_count
    return requests


class Probe:
    def __init__(self, client: BleakClient, notify_uuid: str, write_uuid: str, timeout: float, out_path: Path):
        self.client = client
        self.notify_uuid = notify_uuid
        self.write_uuid = write_uuid
        self.timeout = timeout
        self.out_path = out_path
        self.queue: asyncio.Queue[bytes] = asyncio.Queue()
        self.out = out_path.open("w", encoding="utf-8")

    async def close(self) -> None:
        self.out.close()

    def log(self, direction: str, label: str, data: bytes, extra: dict[str, Any] | None = None) -> None:
        row: dict[str, Any] = {
            "t": time.time(),
            "direction": direction,
            "label": label,
            "hex": data.hex(),
        }
        if extra:
            row.update(extra)
        self.out.write(json.dumps(row, sort_keys=True) + "\n")
        self.out.flush()
        print(f"{direction:>2} {label:<12} {data.hex()}")

    def on_notify(self, _sender: Any, data: bytearray) -> None:
        payload = bytes(data)
        self.log("<-", "notify", payload)
        self.queue.put_nowait(payload)

    async def start_notify(self) -> None:
        await self.client.start_notify(self.notify_uuid, self.on_notify)

    async def drain_queue(self) -> None:
        while not self.queue.empty():
            self.queue.get_nowait()
            self.queue.task_done()

    async def write_and_wait(self, label: str, command: bytes, wait: bool = True) -> bytes | None:
        await self.drain_queue()
        self.log("->", label, command)
        await self.client.write_gatt_char(self.write_uuid, command, response=True)
        if not wait:
            return None
        return await asyncio.wait_for(self.queue.get(), timeout=self.timeout)


async def run(args: argparse.Namespace) -> None:
    out_path = Path(args.out)

    async with BleakClient(args.mac, timeout=args.connect_timeout) as client:
        if not client.is_connected:
            raise RuntimeError("failed to connect")

        print(f"connected: {args.mac}")
        probe = Probe(client, args.notify_uuid, args.write_uuid, args.timeout, out_path)

        try:
            await probe.start_notify()

            if args.time_sync:
                await probe.write_and_wait("time-sync", build_time_sync_command(args.epoch))
                await asyncio.sleep(args.delay_ms / 1000)

            await probe.write_and_wait("start", build_start_command())
            await asyncio.sleep(args.delay_ms / 1000)

            meta_response = await probe.write_and_wait("metadata", build_metadata_command())
            if meta_response is None:
                raise RuntimeError("no metadata response")
            metadata = parse_metadata(meta_response)

            print("metadata:")
            print(f"  start_epoch: {metadata.start_epoch}  {utc_iso(metadata.start_epoch)}")
            print(f"  end_epoch:   {metadata.end_epoch}  {utc_iso(metadata.end_epoch)}")
            print(f"  end_index:   0x{metadata.end_index:08x} ({metadata.end_index})")
            print(f"  interval:    {metadata.interval_seconds}s")

            probe.log(
                "==",
                "metadata-parsed",
                b"",
                {
                    "start_epoch": metadata.start_epoch,
                    "end_epoch": metadata.end_epoch,
                    "end_index": metadata.end_index,
                    "interval_seconds": metadata.interval_seconds,
                },
            )

            if args.metadata_only:
                return

            requests = build_page_plan(args, metadata)
            print(f"page requests: {len(requests)}")

            for page_no, req in enumerate(requests):
                command = build_page_command(req.index, req.count)
                response = await probe.write_and_wait(f"page-{page_no}", command)
                probe.log(
                    "==",
                    "page-parsed",
                    b"",
                    {
                        "page_no": page_no,
                        "index": req.index,
                        "count": req.count,
                        "response_hex": response.hex() if response else None,
                    },
                )
                await asyncio.sleep(args.delay_ms / 1000)

        finally:
            try:
                await client.stop_notify(args.notify_uuid)
            finally:
                await probe.close()

    print(f"wrote: {out_path}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Probe SwitchBot Outdoor Meter history over BLE")
    parser.add_argument("--mac", required=True, help="sensor MAC address")
    parser.add_argument("--write-uuid", default=DEFAULT_WRITE_UUID, help="GATT write characteristic UUID")
    parser.add_argument("--notify-uuid", default=DEFAULT_NOTIFY_UUID, help="GATT notify characteristic UUID")
    parser.add_argument("--out", default="switchbot_history_raw.jsonl", help="output JSONL file")

    parser.add_argument("--pages", type=int, default=5, help="number of history pages to request when no end is supplied")
    parser.add_argument("--page-count", type=int, default=6, help="record count byte in 3c page request")
    parser.add_argument("--start-index", type=parse_int, help="first history index, decimal or 0xhex")
    parser.add_argument("--end-index", type=parse_int, help="exclusive end history index, decimal or 0xhex")
    parser.add_argument("--start", "--start-epoch", dest="start_epoch", type=parse_time_arg, help="first wanted time: Unix epoch or ISO datetime with timezone")
    parser.add_argument("--end", "--end-epoch", dest="end_epoch", type=parse_time_arg, help="exclusive end time: Unix epoch or ISO datetime with timezone")
    parser.add_argument("--metadata-only", action="store_true", help="stop after metadata read")

    parser.add_argument("--no-time-sync", dest="time_sync", action="store_false", help="skip observed time-sync command")
    parser.set_defaults(time_sync=True)
    parser.add_argument("--epoch", type=parse_int, help="epoch to use for time-sync; default is current time")

    parser.add_argument("--delay-ms", type=int, default=50, help="delay between commands")
    parser.add_argument("--timeout", type=float, default=5.0, help="notification wait timeout")
    parser.add_argument("--connect-timeout", type=float, default=15.0, help="BLE connect timeout")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
