import asyncio
from collections import defaultdict
from bleak import BleakClient, BleakScanner
from bleak.backends.scanner import AdvertisementData

LOCAL_NAME   = "Flower care"
SERVICE_UUID = "0000fe95-0000-1000-8000-00805f9b34fb"

sensors = defaultdict(list)

def new_device(device: str, adv: AdvertisementData):
    addr = device.address.upper()
    if adv is None:
        return
    if adv.local_name == LOCAL_NAME:
        payload = decode_payload(adv.service_data[SERVICE_UUID])
        if payload not in sensors[addr]:
            print(f"{addr=} {payload=}")
            sensors[addr].append(payload)

def decode_payload(payload: bytes) -> tuple[str, int] | None:
    if len(payload) < 14:
        return None

    # Object starts at byte 12
    obj_id = payload[12] | (payload[13] << 8)
    length = payload[14]
    data = payload[15:15+length]

    if obj_id == 0x1004 and length == 2:
        val = int.from_bytes(data, "little", signed=True) / 10.0
        return ("temperature_c", val)
    elif obj_id == 0x1007 and length == 3:
        val = int.from_bytes(data, "little")
        return ("lux", val)
    elif obj_id == 0x1008 and length == 1:
        return ("moisture_pct", data[0])
    elif obj_id == 0x1009 and length == 2:
        val = int.from_bytes(data, "little")
        return ("conductivity_us_cm", val)
    return None

async def main():
    scanner = BleakScanner(detection_callback=new_device)
    await scanner.start()
    await asyncio.sleep(60)
    await scanner.stop()

asyncio.run(main())
