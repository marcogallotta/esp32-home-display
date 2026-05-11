#!/usr/bin/env python3
"""
Probe SwitchBot Outdoor Meter history over BLE.

This only replays commands observed from the official app:
  57 0f 3a
  57 0f 3b <bank>
  57 0f 3c <bank> <index:u32_be> <count>

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


@dataclass(frozen=True)
class DecodedSample:
    index: int
    epoch: int
    temperature_c: float
    humidity_pct: int


def parse_int(value: str) -> int:
    value = value.strip().lower()
    if value.startswith("0x"):
        return int(value, 16)
    return int(value, 10)


def parse_hex_blob(value: str) -> bytes:
    raw = value.strip().lower()
    if raw in ("", "-", "none", "empty"):
        return b""
    raw = raw.replace("0x", "").replace(":", "").replace(" ", "")
    if len(raw) % 2 != 0:
        raw = "0" + raw
    return bytes.fromhex(raw)


def parse_hex_blob_list(value: str) -> list[bytes]:
    raw = value.strip()
    if not raw:
        return []
    return [parse_hex_blob(part) for part in raw.split(",")]


def hex_label(data: bytes) -> str:
    return data.hex() if data else "none"


def parse_bank_list(value: str, start_response: bytes | None = None) -> list[int]:
    raw = value.strip()
    if not raw:
        return []
    if raw.lower() == "start":
        if start_response is None or len(start_response) <= 2:
            return []
        return list(start_response[2:])

    banks: list[int] = []
    for part in raw.split(","):
        if not part.strip():
            continue
        bank = parse_int(part)
        if not 0 <= bank <= 0xFF:
            raise ValueError("bank must fit uint8")
        banks.append(bank)
    return banks


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


def resolve_time_sync_epoch(args: argparse.Namespace) -> int | None:
    supplied = [
        value for value in (args.epoch, args.time_sync_epoch, args.time_sync_iso)
        if value is not None
    ]
    if len(supplied) > 1:
        raise ValueError("use only one of --epoch, --time-sync-epoch, or --time-sync-iso")
    return supplied[0] if supplied else None


def build_start_command(extra: bytes = b"") -> bytes:
    return bytes.fromhex("570f3a") + extra


def build_metadata_command(bank: int = 0, extra: bytes = b"") -> bytes:
    if not 0 <= bank <= 0xFF:
        raise ValueError("bank must fit uint8")
    return bytes.fromhex("570f3b") + bytes([bank]) + extra


def build_page_command(index: int, count: int, bank: int = 0, extra: bytes = b"") -> bytes:
    if not 0 <= index <= 0xFFFFFFFF:
        raise ValueError("index must fit uint32")
    if not 1 <= count <= 0xFF:
        raise ValueError("count must fit uint8 and be non-zero")
    if not 0 <= bank <= 0xFF:
        raise ValueError("bank must fit uint8")
    return bytes.fromhex("570f3c") + bytes([bank]) + index.to_bytes(4, "big") + bytes([count]) + extra


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


def decode_temperature(raw_temp: int, decimal: int) -> float:
    # Same sign convention as the passive advertisement decoder:
    # high bit set = positive, low 7 bits = integer part.
    sign = 1 if (raw_temp & 0x80) else -1
    return sign * ((raw_temp & 0x7F) + decimal / 10.0)


def decode_history_payload(data: bytes, start_index: int, metadata: Metadata) -> list[DecodedSample]:
    # Observed history page response:
    #   01 <15 bytes>
    # The 15-byte body is 3 groups of 5 bytes. Each group contains 2 samples:
    #   temp1 humidity1 decimals temp2 humidity2
    # where high nibble of decimals belongs to sample1, low nibble to sample2.
    if not data or data[0] != 0x01:
        return []

    body = data[1:]
    samples: list[DecodedSample] = []
    sample_offset = 0

    for pos in range(0, len(body) - 4, 5):
        t1 = body[pos]
        h1 = body[pos + 1]
        decimals = body[pos + 2]
        t2 = body[pos + 3]
        h2 = body[pos + 4]

        for raw_t, raw_h, dec in (
            (t1, h1, (decimals >> 4) & 0x0F),
            (t2, h2, decimals & 0x0F),
        ):
            index = start_index + sample_offset
            samples.append(
                DecodedSample(
                    index=index,
                    epoch=metadata.start_epoch + index * metadata.interval_seconds,
                    temperature_c=decode_temperature(raw_t, dec),
                    humidity_pct=raw_h & 0x7F,
                )
            )
            sample_offset += 1

    return samples


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
    def __init__(self, client: BleakClient, notify_uuid: str, write_uuid: str, timeout: float, raw_out_path: Path | None):
        self.client = client
        self.notify_uuid = notify_uuid
        self.write_uuid = write_uuid
        self.timeout = timeout
        self.queue: asyncio.Queue[bytes] = asyncio.Queue()
        self.raw_out = raw_out_path.open("w", encoding="utf-8") if raw_out_path else None

    async def close(self) -> None:
        if self.raw_out:
            self.raw_out.close()

    def log_raw(self, direction: str, label: str, data: bytes, extra: dict[str, Any] | None = None) -> None:
        if not self.raw_out:
            return
        row: dict[str, Any] = {
            "t": time.time(),
            "direction": direction,
            "label": label,
            "hex": data.hex(),
        }
        if extra:
            row.update(extra)
        self.raw_out.write(json.dumps(row, sort_keys=True) + "\n")
        self.raw_out.flush()

    def on_notify(self, _sender: Any, data: bytearray) -> None:
        payload = bytes(data)
        self.log_raw("<-", "notify", payload)
        self.queue.put_nowait(payload)

    async def start_notify(self) -> None:
        await self.client.start_notify(self.notify_uuid, self.on_notify)

    async def drain_queue(self) -> None:
        while not self.queue.empty():
            self.queue.get_nowait()
            self.queue.task_done()

    async def write_and_wait(self, label: str, command: bytes, wait: bool = True) -> bytes | None:
        await self.drain_queue()
        self.log_raw("->", label, command)
        await self.client.write_gatt_char(self.write_uuid, command, response=True)
        if not wait:
            return None
        return await asyncio.wait_for(self.queue.get(), timeout=self.timeout)


def print_metadata(metadata: Metadata, *, prefix: str = "metadata") -> None:
    print(f"{prefix}:")
    print(f"  start_epoch: {metadata.start_epoch}  {utc_iso(metadata.start_epoch)}")
    print(f"  end_epoch:   {metadata.end_epoch}  {utc_iso(metadata.end_epoch)}")
    print(f"  end_index:   0x{metadata.end_index:08x} ({metadata.end_index})")
    print(f"  interval:    {metadata.interval_seconds}s")


def parse_probe_indexes(value: str) -> list[int]:
    raw = value.strip()
    if not raw:
        return []
    return [parse_int(part) for part in raw.split(",") if part.strip()]


def boundary_probe_indexes(metadata: Metadata, page_count: int) -> list[int]:
    candidates = [0]
    if metadata.end_index >= page_count:
        candidates.append(metadata.end_index - page_count)
    candidates.append(metadata.end_index)

    indexes: list[int] = []
    for index in candidates:
        if 0 <= index <= 0xFFFFFFFF and index not in indexes:
            indexes.append(index)
    return indexes


def latest_full_page_index(metadata: Metadata, page_count: int) -> int | None:
    if metadata.end_index < page_count:
        return None
    return metadata.end_index - page_count


async def run(args: argparse.Namespace) -> None:
    raw_out_path = Path(args.raw_out) if args.raw_out else None

    async with BleakClient(args.mac, timeout=args.connect_timeout) as client:
        if not client.is_connected:
            raise RuntimeError("failed to connect")

        print(f"connected: {args.mac}")
        probe = Probe(client, args.notify_uuid, args.write_uuid, args.timeout, raw_out_path)

        try:
            await probe.start_notify()

            if args.time_sync:
                sync_epoch = resolve_time_sync_epoch(args)
                command = build_time_sync_command(sync_epoch)
                effective_epoch = sync_epoch if sync_epoch is not None else int.from_bytes(command[-4:], "big")
                print(f"time_sync_epoch: {effective_epoch}  {utc_iso(effective_epoch)}")
                print(f"time_sync_command: {command.hex()}")
                await probe.write_and_wait("time-sync", command)
                await asyncio.sleep(args.delay_ms / 1000)

            start_extras = parse_hex_blob_list(args.start_extras)
            metadata_extras = parse_hex_blob_list(args.metadata_extras) or [parse_hex_blob(args.metadata_extra)]
            page_extra = parse_hex_blob(args.page_extra)

            async def read_metadata(bank: int, metadata_extra: bytes) -> Metadata | None:
                meta_response = await probe.write_and_wait(
                    f"metadata-bank-0x{bank:02x}-extra-{hex_label(metadata_extra)}",
                    build_metadata_command(bank, metadata_extra),
                )
                if meta_response is None:
                    print(f"bank 0x{bank:02x} metadata_extra={hex_label(metadata_extra)}: no metadata response")
                    return None
                try:
                    bank_metadata = parse_metadata(meta_response)
                except ValueError as exc:
                    print(
                        f"bank 0x{bank:02x} metadata_extra={hex_label(metadata_extra)}: "
                        f"bad metadata raw={meta_response.hex()} error={exc}"
                    )
                    return None

                print(f"bank 0x{bank:02x} metadata raw: {meta_response.hex()}")
                print_metadata(
                    bank_metadata,
                    prefix=f"bank 0x{bank:02x} metadata extra={hex_label(metadata_extra)}",
                )
                probe.log_raw(
                    "==",
                    "metadata-parsed",
                    b"",
                    {"bank": bank, "metadata_extra": metadata_extra.hex(), **bank_metadata.__dict__},
                )
                return bank_metadata

            async def read_probe_page(bank: int, index: int, count: int, metadata: Metadata | None, label: str) -> None:
                response = await probe.write_and_wait(
                    f"page-bank-0x{bank:02x}-{label}-index-{index}-extra-{hex_label(page_extra)}",
                    build_page_command(index, count, bank, page_extra),
                )
                samples = decode_history_payload(response or b"", index, metadata) if metadata else []
                print(
                    f"bank 0x{bank:02x} probe_page label={label} index={index} count={count} "
                    f"page_extra={hex_label(page_extra)} response={(response or b'').hex()} decoded={len(samples)}"
                )
                for sample in samples:
                    print(f"  {sample.index},{utc_iso(sample.epoch)},{sample.temperature_c:.1f},{sample.humidity_pct}")

            async def probe_metadata_for_banks(start_response: bytes | None, *, label: str) -> bool:
                bank_probe_list = parse_bank_list(args.banks, start_response)
                if not bank_probe_list:
                    return False

                explicit_probe_indexes = parse_probe_indexes(args.probe_indexes)

                for bank in bank_probe_list:
                    for metadata_extra in metadata_extras:
                        bank_metadata = await read_metadata(bank, metadata_extra)
                        if bank_metadata is None:
                            continue

                        page_indexes: list[tuple[str, int]] = []
                        if args.read_last_page:
                            page_index = latest_full_page_index(bank_metadata, args.page_count)
                            if page_index is None:
                                print(f"bank 0x{bank:02x}: fewer than {args.page_count} samples; skipping last-page read")
                            else:
                                page_indexes.append(("last", page_index))
                        if args.boundary_probes:
                            page_indexes.extend(("boundary", index) for index in boundary_probe_indexes(bank_metadata, args.page_count))
                        page_indexes.extend(("probe", index) for index in explicit_probe_indexes)

                        seen: set[tuple[str, int]] = set()
                        for page_label, page_index in page_indexes:
                            key = (page_label, page_index)
                            if key in seen:
                                continue
                            seen.add(key)
                            await read_probe_page(
                                bank,
                                page_index,
                                args.page_count,
                                bank_metadata,
                                f"{label}-{page_label}",
                            )
                            await asyncio.sleep(args.delay_ms / 1000)

                return True

            if args.pre_start_metadata:
                if not await probe_metadata_for_banks(None, label="before-start"):
                    print("pre-start metadata requested but --banks did not name concrete banks")
                if args.skip_start:
                    return

            if args.skip_start:
                if await probe_metadata_for_banks(None, label="no-start"):
                    return

                metadata_extra = metadata_extras[0]
                meta_response = await probe.write_and_wait(
                    f"metadata-bank-0x{args.bank:02x}-extra-{hex_label(metadata_extra)}",
                    build_metadata_command(args.bank, metadata_extra),
                )
                if meta_response is None:
                    raise RuntimeError("no metadata response")
                print(f"bank 0x{args.bank:02x} metadata raw: {meta_response.hex()}")
                metadata = parse_metadata(meta_response)
                print_metadata(metadata, prefix=f"bank 0x{args.bank:02x} metadata")
                probe.log_raw("==", "metadata-parsed", b"", {"bank": args.bank, **metadata.__dict__})
                if args.metadata_only:
                    return

            if start_extras:
                for start_extra in start_extras:
                    start_response = await probe.write_and_wait(
                        f"start-extra-{hex_label(start_extra)}",
                        build_start_command(start_extra),
                    )
                    await asyncio.sleep(args.delay_ms / 1000)
                    if start_response is None:
                        print(f"start_extra={hex_label(start_extra)}: no start response")
                        continue
                    start_banks = parse_bank_list("start", start_response)
                    print(f"start_extra={hex_label(start_extra)} start_response: {start_response.hex()}")
                    if start_banks:
                        print("start_banks: " + ",".join(f"0x{bank:02x}" for bank in start_banks))
                    await probe_metadata_for_banks(start_response, label="after-start")
                return

            start_extra = parse_hex_blob(args.start_extra)
            start_response = await probe.write_and_wait(
                f"start-extra-{hex_label(start_extra)}",
                build_start_command(start_extra),
            )
            await asyncio.sleep(args.delay_ms / 1000)

            if start_response is None:
                raise RuntimeError("no start response")
            start_banks = parse_bank_list("start", start_response)
            print(f"start_response: {start_response.hex()}")
            if start_extra:
                print(f"start_extra: {start_extra.hex()}")
            if start_banks:
                print("start_banks: " + ",".join(f"0x{bank:02x}" for bank in start_banks))

            if await probe_metadata_for_banks(start_response, label="after-start"):
                return

            metadata_extra = metadata_extras[0]
            meta_response = await probe.write_and_wait(
                f"metadata-bank-0x{args.bank:02x}-extra-{hex_label(metadata_extra)}",
                build_metadata_command(args.bank, metadata_extra),
            )
            if meta_response is None:
                raise RuntimeError("no metadata response")
            metadata = parse_metadata(meta_response)

            print_metadata(metadata, prefix=f"bank 0x{args.bank:02x} metadata")

            probe.log_raw("==", "metadata-parsed", b"", {"bank": args.bank, **metadata.__dict__})

            if args.metadata_only:
                return

            requests = build_page_plan(args, metadata)
            print(f"page requests: {len(requests)}")
            decoded_samples: list[DecodedSample] = []

            for page_no, req in enumerate(requests):
                command = build_page_command(req.index, req.count, args.bank, parse_hex_blob(args.page_extra))
                response = await probe.write_and_wait(f"page-{page_no}", command)
                samples = decode_history_payload(response or b"", req.index, metadata)
                decoded_samples.extend(samples)
                probe.log_raw(
                    "==",
                    "page-parsed",
                    b"",
                    {
                        "page_no": page_no,
                        "bank": args.bank,
                        "index": req.index,
                        "count": req.count,
                        "decoded_samples": [sample.__dict__ for sample in samples],
                    },
                )
                await asyncio.sleep(args.delay_ms / 1000)

            # Trim over-fetched full pages to the requested range.
            start_epoch = args.start_epoch if args.start_epoch is not None else metadata.start_epoch
            end_epoch = args.end_epoch if args.end_epoch is not None else metadata.end_epoch
            trimmed = [s for s in decoded_samples if start_epoch <= s.epoch < end_epoch]

            print(f"samples: {len(trimmed)}")
            print("index,datetime_utc,temperature_c,humidity_pct")
            for sample in trimmed:
                print(
                    f"{sample.index},{utc_iso(sample.epoch)},{sample.temperature_c:.1f},{sample.humidity_pct}"
                )

            if args.decoded_out:
                decoded_path = Path(args.decoded_out)
                with decoded_path.open("w", encoding="utf-8") as f:
                    f.write("index,epoch,datetime_utc,temperature_c,humidity_pct\n")
                    for sample in trimmed:
                        f.write(
                            f"{sample.index},{sample.epoch},{utc_iso(sample.epoch)},"
                            f"{sample.temperature_c:.1f},{sample.humidity_pct}\n"
                        )
                print(f"wrote decoded: {decoded_path}")

            if raw_out_path:
                print(f"wrote raw: {raw_out_path}")

        finally:
            try:
                await client.stop_notify(args.notify_uuid)
            finally:
                await probe.close()



def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Probe SwitchBot Outdoor Meter history over BLE")
    parser.add_argument("--mac", required=True, help="sensor MAC address")
    parser.add_argument("--write-uuid", default=DEFAULT_WRITE_UUID, help="GATT write characteristic UUID")
    parser.add_argument("--notify-uuid", default=DEFAULT_NOTIFY_UUID, help="GATT notify characteristic UUID")
    parser.add_argument("--raw-out", default="", help="optional raw JSONL output for debugging")
    parser.add_argument("--decoded-out", default="switchbot_history_decoded.csv", help="decoded CSV output; pass empty string to disable")

    parser.add_argument("--pages", type=int, default=5, help="number of history pages to request when no end is supplied")
    parser.add_argument("--page-count", type=int, default=6, help="record count byte in 3c page request")
    parser.add_argument("--bank", type=parse_int, default=0, help="history bank byte for normal metadata/page reads")
    parser.add_argument("--banks", default="", help="comma-separated bank bytes to probe, or 'start' to use bytes from start response")
    parser.add_argument("--read-last-page", action="store_true", help="with --banks, read the last full page for each bank")
    parser.add_argument("--pre-start-metadata", action="store_true", help="read --banks metadata before sending the start command")
    parser.add_argument("--skip-start", action="store_true", help="do not send the start command; useful with concrete --banks values")
    parser.add_argument("--probe-indexes", default="", help="comma-separated page indexes to probe for each bank, e.g. 77970,0x13092")
    parser.add_argument("--boundary-probes", action="store_true", help="probe page indexes 0, end_index-page_count, and end_index for each bank")
    parser.add_argument("--start-extra", default="", help="hex bytes appended to the start command, e.g. 00")
    parser.add_argument("--start-extras", default="", help="comma-separated start-command extra variants, e.g. none,00,01,02")
    parser.add_argument("--metadata-extra", default="", help="hex bytes appended to each metadata command")
    parser.add_argument("--metadata-extras", default="", help="comma-separated metadata extra variants, e.g. none,00,01,02")
    parser.add_argument("--page-extra", default="", help="hex bytes appended to page command after count")
    parser.add_argument("--start-index", type=parse_int, help="first history index, decimal or 0xhex")
    parser.add_argument("--end-index", type=parse_int, help="exclusive end history index, decimal or 0xhex")
    parser.add_argument("--start", "--start-epoch", dest="start_epoch", type=parse_time_arg, help="first wanted time: Unix epoch or ISO datetime with timezone")
    parser.add_argument("--end", "--end-epoch", dest="end_epoch", type=parse_time_arg, help="exclusive end time: Unix epoch or ISO datetime with timezone")
    parser.add_argument("--metadata-only", action="store_true", help="stop after metadata read")

    parser.add_argument("--no-time-sync", dest="time_sync", action="store_false", help="skip observed time-sync command")
    parser.set_defaults(time_sync=True)
    parser.add_argument("--epoch", type=parse_time_arg, help="epoch/ISO time to use for time-sync; default is current time")
    parser.add_argument("--time-sync-epoch", type=parse_int, help="Unix epoch to use for time-sync")
    parser.add_argument("--time-sync-iso", type=parse_time_arg, help="ISO datetime with timezone to use for time-sync, e.g. 2026-05-07T03:30:00Z")

    parser.add_argument("--delay-ms", type=int, default=50, help="delay between commands")
    parser.add_argument("--timeout", type=float, default=5.0, help="notification wait timeout")
    parser.add_argument("--connect-timeout", type=float, default=15.0, help="BLE connect timeout")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
