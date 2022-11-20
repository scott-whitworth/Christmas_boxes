#!/usr/bin/env py

import asyncio
import sys

from bleak import BleakScanner, BleakClient


print("Hello world")

address = "CE:71:7A:35:C3:18"

#async def main():
    # devices = await BleakScanner.discover()
    # for d in devices:
    #     print(d)

async def main(address):
    async with BleakClient(address) as client:
        await client.start_notify()


asyncio.run(main(address))