#!/usr/bin/env py

import asyncio
import sys
import ctypes

from bleak import BleakScanner, BleakClient
from bleak.backends.characteristic import BleakGATTCharacteristic

import time

address = [
            #"CE:71:7A:35:C3:18", # Station 1
            #"CD:30:BC:87:C4:35", # Station 2
            "E4:27:BB:05:B0:03", # Station 3
            #"E6:BA:04:A1:64:C2", # Station 4
            #"CB:96:AC:0E:E6:87", #Station 5
            #"FC:E7:8D:26:C0:79", #Station 6
            #"F0:8F:E3:E3:6D:C7", #Station 7
            "CB:03:A2:60:90:6A" # Base Station
          ]

# Bleak standards:
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


######################################
## Utility to pare out board message
######################################
def parseBoardNum(data):
    # Data is some numbers followed by not numbers
    # return the numbers (in numeric form) followed by the rest of the data

    if not chr(data[0]).isdigit(): #failure case, pass back error
        print("Data[0] is not a digit! Sending back nothing!")
        return -1, data

    i = 0
    nums = 0
    while(chr(data[i]).isdigit()):
        nums += int(chr(data[i])) # get value of number
        
        i = i + 1 # Update index
        nums *= 10 # Keep moving 10's digit
        
    # At this point nums is shifted too much, so bring it back
    nums /= 10
    #i represents the rest of the string
    sub_data = data[i:-2] # Removes the trailing "\r\n" as well

    return int(nums), sub_data

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

    #A whole collection of send_queues, so we can keep track of specific data for each device
    # Managed in process_receive_queue and connect_to_device
    individual_send_queues = []
    for i in address_list:
        print("Making queue for address: ", i)
        individual_send_queues.append( asyncio.Queue() )

    ##############################
    # Define the async functions #
    ##############################

    # callback that puts the received message into the receive queue
    # remember: this happens agnostic of which node sends it
    def handle_rx(_: BleakGATTCharacteristic, data: bytearray):
        print("[HRX]:", data)
        #receive_queue.put(data)
        receive_queue.put_nowait(data)

    # Manages the data that is put in the receive_queue in handle_rx
    # TODO: This will be were the main logic goes
    async def process_receive_queue():
        print("started process_receive_queue, hopefully waiting for queue data")
        while True: #I am assuming this is going to keep looping, pulling out one at a time
            itm = await receive_queue.get() 
            #print("In PRQ queue: ", itm)
            print("Received a message from board ", int(itm[0]), " : ", end="")
            if(itm[1] == ord('A')):
                print("Accelerometer Triggered!")
            elif(itm[1] == ord('R')):
                print("IR Flash Detected!")
                print("Attempting to trigger servo")
                await individual_send_queues[1].put(bytearray(b"CSO"))

            # process received messages
            if(int(itm[0]) == 1):
                print("Attempting to send message to second one (boardID:2)")
                await individual_send_queues[1].put(bytearray(b"RGRG"))
            if(int(itm[0]) == 2):
                print("Attempting to send message to first one (boardID:1)")
                await individual_send_queues[0].put(bytearray(b"BRGB"))

    # Manage info coming in from the keyboard
    # Needs to have data in the form of [boardID][message]
    # boardID is assumed to correlate to individual_send_queues indexes
    async def process_keyboard():
        print("Starting task to get info from the keyboard")

        loop = asyncio.get_running_loop()

        while True:
                data = await loop.run_in_executor(None, sys.stdin.buffer.readline)
                print("Just got |",data,"| from keyboard. Parsing to correct board")

                #Figure out which board is being updated
                [select_board,selected_data] = parseBoardNum(data)

                # Send data to the relevant board (make sure it is a valid board first)
                if(select_board-1 < len(individual_send_queues) and select_board-1 >= 0 ):
                    print("Adding ",selected_data," to board ",select_board,"'s individual queue")
                    #                                        VV 0 offset
                    await individual_send_queues[select_board-1].put(selected_data)
                else:
                    print("Cannot parse ",data," to send to individual queues. Got back: ",select_board, " + ", selected_data)

                # if(data[0] == ord('1')):
                #     print("Adding ",data[1:]," to board 1 individual queue")
                #     await individual_send_queues[0].put(data[1:])
                # elif (data[0] == ord('2')):
                #     print("Adding ",data[1:]," to board 1 individual queue")
                #     await individual_send_queues[1].put(data[1:])
                # else:
                #     print("Message cannot be parsed, don't know what to do with |",data,"|")
                
                #await send_queue.put(data)


    #single routine to handle connecting to one single device
    #TODO: need to make this lazy (i.e. keep trying to reconnect)
    async def connect_to_device(address, boardID):
        # probably want to manage the connection in a try / catch loop
        print("connect_to_device - Trying to connect to ", address)

        async with BleakClient(address) as client:
            await client.start_notify(UART_TX_CHAR_UUID, handle_rx)
            print("Connected to to device: ", client.address)
            print("This is board ID: ", boardID, end="\n\n")

            # Set up client srevices
            client_service = client.services.get_service(UART_SERVICE_UUID)
            rx_char_service = client_service.get_characteristic(UART_RX_CHAR_UUID)

            while True: # Keep running this forever
                # Wait for data on this individual send_queue, then send it
                data = await individual_send_queues[boardID-1].get() #Offset by one here
                print("Board ",boardID," just got |", data , "| from i_s_q")
                await client.write_gatt_char(rx_char_service, data)



    # set up tasks address_list
    taskList = []
    for index in range(len(address_list)) :
        print("Adding ", address_list[index], " to the task list")
        taskList.append(asyncio.create_task(connect_to_device(address_list[index], index+1))) # create the task, add it to the task list

    print("Adding process_receive_queue() to the taskList")
    taskList.append(asyncio.create_task(process_receive_queue())) # Add task to parse the queue

    print("Adding keyboard processing to the taskList")
    taskList.append(asyncio.create_task(process_keyboard()))

    print("Starting up task list.", end="\n\n")
    for tsk in taskList:
        #print("About to try ", tsk)
        await tsk
        print("Done with task...", tsk)
    


#############################################################

print("About to run main async")
#asyncio.run(main(address))
asyncio.run(connect_to_all_devices(address))