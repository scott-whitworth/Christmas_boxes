#!/usr/bin/env py

import asyncio
import sys
import ctypes

from bleak import BleakScanner, BleakClient
from bleak.backends.characteristic import BleakGATTCharacteristic

import time


print("Hello world")

address = [
            "CE:71:7A:35:C3:18", # Station 1
            "CD:30:BC:87:C4:35" # Station 2
          ]

# Bleak standards:
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

# async def main_discover():
#     devices = await BleakScanner.discover()
#     for d in devices:
#         print(d)

# asyncio.run(main_discover())
# exit()

def handle_rx(_: BleakGATTCharacteristic, data: bytearray):
    #client.address
    print("received:", data)
    receive_queue.put(data)

async def process_receive_queue():
    itm = await receive_queue.get() 
    print("In queue: ", itm)

#single routine to handle one single device
async def connect_to_device(address):
    # probably want to manage the connection in a try / catch loop
    print("connect_to_device - Trying to connect to ", address)

    async with BleakClient(address) as client:
        await client.start_notify(UART_TX_CHAR_UUID, handle_rx)

        print("Connected to to device: ", client.address)

        while True:
            await asyncio.sleep(0.05)

# Async function for starting the connections to the boards
# Address list should be a list of all of the addresses to be connected to
async def connect_to_all_devices(address_list):
    receive_queue = asyncio.Queue
    # set up tasks
    taskList = []
    for addr in address_list:
        print("Adding ", addr, " to the task list")
        taskList.append(asyncio.create_task(connect_to_device(addr))) # create the task, add it to the task list

    print("Tring to connect to devices.")
    for tsk in taskList:
        print("About to try ", tsk)
        await tsk
    
    print("Starting Queue Printout: ")
    await process_receive_queue()

# async def main(address):
#     print("Start of main func")

#     label = "This is the second one: "

#     def handle_rx(_: BleakGATTCharacteristic, data: bytearray):
#         print(label,"received:", data)

#     async with BleakClient(address[1]) as client:
#         await client.start_notify(UART_TX_CHAR_UUID, handle_rx)

#         print("Connected to to device")

#         loop = asyncio.get_running_loop()

#         # 
#         while True:

#             data = await loop.run_in_executor(None, sys.stdin.buffer.readline)

#             # data will be empty on EOF (e.g. CTRL+D on *nix)
#             #if not data:
#                 #break


#             print(".")
#             #time.sleep(.5)

#             #print("sent:", data)
            

print("About to run main async")
#asyncio.run(main(address))
asyncio.run(connect_to_all_devices(address))