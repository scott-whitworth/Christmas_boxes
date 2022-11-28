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

# Async function for starting the connections to the boards
# Address list should be a list of all of the addresses to be connected to
async def connect_to_all_devices(address_list):
    #single queue to pull in data from the devices
    # this will need to have each device identify itself in the message to know where to put the info
    receive_queue = asyncio.Queue()

    #TODO: Need to figure out a list of send queues for each of the transmit queues
    send_queue = asyncio.Queue()

    ##############################
    # Define the async functions #
    ##############################

    # callback that puts the received message into the receive queue
    # remember: this happens agnostic of which node sends it
    def handle_rx(_: BleakGATTCharacteristic, data: bytearray):
        print("received:", data)
        #receive_queue.put(data)
        receive_queue.put_nowait(data)

    async def process_receive_queue():
        print("started process_receive_queue, hopefully waiting for queue data")
        while True: #I am assuming this is going to keep looping, pulling out one at a time
            itm = await receive_queue.get() 
            print("In PRQ queue: ", itm)

    async def process_keyboard():
        print("Getting info from the keyboard...")

        loop = asyncio.get_running_loop()

        while True:
                data = await loop.run_in_executor(None, sys.stdin.buffer.readline)
                print("Just got |",data,"| from keyboard. Adding to send_queue")
                await send_queue.put(data)




    #single routine to handle connecting to one single device
    #TODO: need to make this lazy (i.e. keep trying to reconnect)
    async def connect_to_device(address):
        # probably want to manage the connection in a try / catch loop
        print("connect_to_device - Trying to connect to ", address)

        async with BleakClient(address) as client:
            await client.start_notify(UART_TX_CHAR_UUID, handle_rx)
            print("Connected to to device: ", client.address)

            # Set up client srevices
            client_service = client.services.get_service(UART_SERVICE_UUID)
            rx_char_service = client_service.get_characteristic(UART_RX_CHAR_UUID)

            while True:
                #TODO: I assume this is going to be where we need to manage the outgoing queues (this 'client' is a specific device)
                await asyncio.sleep(0.05)
                print("testing...")
                data = await send_queue.get() # get data from send queue!
                print("Just got |", data , "| from send_queue")
                await client.write_gatt_char(rx_char_service, data)
                #await asyncio.sleep(10)
                #await client.write_gatt_char(rx_char_service, b'B')



    # set up tasks
    taskList = []
    for addr in address_list:
        print("Adding ", addr, " to the task list")
        taskList.append(asyncio.create_task(connect_to_device(addr))) # create the task, add it to the task list

    taskList.append(asyncio.create_task(process_receive_queue())) # Add task to parse the queue

    taskList.append(asyncio.create_task(process_keyboard()))

    print("Tring to connect to devices.")
    for tsk in taskList:
        print("About to try ", tsk)
        await tsk
        print("Done with that task...", tsk)
    
    #print("Starting Queue Printout: ")
    #await process_receive_queue() # does not look like it ever triggers

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