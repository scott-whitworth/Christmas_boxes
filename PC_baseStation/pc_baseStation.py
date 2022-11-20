#!/usr/bin/env py

import asyncio
import sys

from bleak import BleakScanner, BleakClient
from bleak.backends.characteristic import BleakGATTCharacteristic

import time


print("Hello world")

address = "CE:71:7A:35:C3:18"

# Bleak standards:
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#
# UUID from adafruit USART: looks like these are just reverse
#
# const uint8_t BLEUART_UUID_SERVICE[] =
# {
#     0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
#     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
# };

# const uint8_t BLEUART_UUID_CHR_RXD[] =
# {
#     0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
#     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
# };

# const uint8_t BLEUART_UUID_CHR_TXD[] =
# {
#     0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
#     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
# };

#async def main():
    # devices = await BleakScanner.discover()
    # for d in devices:
    #     print(d)

async def main(address):

    def handle_rx(_: BleakGATTCharacteristic, data: bytearray):
        print("received:", data)

    async with BleakClient(address) as client:
        await client.start_notify(UART_TX_CHAR_UUID, handle_rx)

        print("Connected to to device")

        loop = asyncio.get_running_loop()

        # 
        while True:

            data = await loop.run_in_executor(None, sys.stdin.buffer.readline)

            # data will be empty on EOF (e.g. CTRL+D on *nix)
            #if not data:
                #break


            print(".")
            #time.sleep(.5)

            #print("sent:", data)
            


asyncio.run(main(address))