#!/usr/bin/env py

# See IR_puzzleBox.ino for message specification

# BIG TODOS: Change temp settings
# Change number of boards
#LED Brightness (in the arduino code)

import asyncio
import sys
import os
import ctypes
import time

from bleak import BleakScanner, BleakClient
from bleak.backends.characteristic import BleakGATTCharacteristic

import time

#Master list of the station addresses (used for connecting)
address = [
            "FF:FF:FF:FF:FF:FF", #Null station for ease of indexing
            "CE:71:7A:35:C3:18", # Station 1
            "CD:30:BC:87:C4:35", # Station 2
            "E4:27:BB:05:B0:03", # Station 3
            "E6:BA:04:A1:64:C2", # Station 4
            "CB:96:AC:0E:E6:87", # Station 5
            "FC:E7:8D:26:C0:79", # Station 6
            "F0:8F:E3:E3:6D:C7", # Station 7
            "CB:03:A2:60:90:6A"  # Base Station
          ]

#Dictionary matching ID to address  
boardAddr = {
    1: "CE:71:7A:35:C3:18", # Station 1
    2: "CD:30:BC:87:C4:35", # Station 2
    3: "E4:27:BB:05:B0:03", # Station 3
    4: "E6:BA:04:A1:64:C2", # Station 4
    5: "CB:96:AC:0E:E6:87", # Station 5
    6: "FC:E7:8D:26:C0:79", # Station 6
    7: "F0:8F:E3:E3:6D:C7", # Station 7
    8: "CB:03:A2:60:90:6A"  # Base Station
}

#Status Array
# True if connected, false otherwise
boardConnected = [True for i in range(len(address))] 

####  Board State Info  #### 
# Accelerometer x/y/z averages, set from a nX... command
# stored as 0 ms^2 at 127 (offset to fit in uint8)
accel_SI = [[0 for i in range(3)] for j in range(len(address))]

# Accelerometer shock state (set from a nX... command)
accel_rest_SI = [0]*len(address)

# Accelerometer shock state (set via nAn command, threshold)
accel_shock_SI = [0]*len(address)

# If IR signal was received (set via nR)
irRX_SI = [0]*len(address)

# Temperature status (set via nTt)
temp_SI = [0]*len(address)

#Lock status
lockDesire = False
lockState = False

#Box state
game_SI = [0] * len(address) #Individual box state
numberOfGameBoxes = 2 # There are more addresses than there are boxes, this is used for game state evaluation
lockBox = 8 #ID of the lockbox (the one that is tied to the servo)
game_State = 0 # Overall state of the game, see below

# Each box can be of one greater state than the overall game
# When all boxes get to the same state, game state will change
# TODO: Maybe color intensity can be correlated with number of boxes?

# Game State structure
# off / hidden -> On -> Chilled -> Ordered -> Shake / orientation -> OpenLock
#                 0       1           2             3                   4           
#
# State 0: Default
#           Action: take immediate temp reading for comparison later
#           Light: purple
#           Transition: getting to be some value temp lower from initialReading
#           TODO: Need to set this target up and check for it
tempTarget = [0] * len(address)
tempCoolOffset = 2 #TODO: Is this reasonable?

# State 1: Chilled
#           Action:start reading IR info
#           Light: Snow Blue
#           Transition: getting every box in the correct IR order
#           Need to set an order, it is ok to get multiple of the boxes to your right, but not left
# 
# State 2: Ordered 
#           Light: Green when in the correct Order (i.e. the box(s) to its right are correct), back to Snow Blue otherwise
#           Transition: all boxes are in the correct order

# State 3: Shake / Orientation
#           Action: Some boxes will need to be tipped around, some boxes will need to be shaken
#           At the start, take orientation measurements for comparison
#           Light: Red if oriented the wrong way, Flashing Yellow/Orange if need to be shaken, Dark Blue when correct
#  
# State 4: OpenLock
#           Action: open the lock!
#           Lights: Rainbow flash!
# 
# TODO: probably need an override command to force the state in the event that connection is lost / power lost


# Bleak standards:
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


######################################
## Utility to parse out board message
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
        print("[HRX]: hex: |", data.hex() ,"|, unformatted: " , data)
        #receive_queue.put(data)
        receive_queue.put_nowait(data)

    # Manages the data that is put in the receive_queue in handle_rx
    # TODO: This will be were the main reactive logic goes
    async def process_receive_queue():
        print("started process_receive_queue, hopefully waiting for queue data")
        while True: #I am assuming this is going to keep looping, pulling out one at a time
            itm = await receive_queue.get() 
            #print("In PRQ queue: ", itm)
            bInt = int(itm[0]) #Shorthand accessing board ID
            print("Message from board ", bInt, " : " , chr(itm[1]))
            if(itm[1] == ord('A')):
                #print(bInt," Accelerometer Triggered!") #DEBUG
                accel_shock_SI[bInt] = True
            elif(itm[1] == ord('R')):
                #print(bInt," IR Flash Detected!")#DEBUG
                irRX_SI[bInt] = True
            elif(itm[1] == ord('T')): # Temperature Measurement
                #print(bInt," Temp Received!")#DEBUG
                temp_SI[bInt] = itm[2]
            elif(itm[1] == ord('X')):
                #print(bInt," Accel data")#DEBUG
                # Capture Data Input
                accel_SI[bInt][0] = int(itm[2]) #X value
                accel_SI[bInt][1] = int(itm[4]) #X value
                accel_SI[bInt][2] = int(itm[6]) #X value
                if( chr(itm[7]) == 'R' ):
                    #board at rest
                    accel_rest_SI[bInt] = True
                else:
                    #Board shaken
                    accel_rest_SI[bInt] = False



    async def gameLogic():
        global lockState
        global lockDesire
        global game_State

        print("started gameLogic. Game is now running")

        #################################
        ## Game Logic Helper Utilities ##
        #################################
        #TODO: Might want to have some verification checking on these

        # Sets specific board to color based on RGB values
        # Follows Command Color (CCrgb)
        async def setColor(board, red, green, blue):
            await individual_send_queues[board].put(bytearray(b"CC") + bytearray([red,green,blue]))

        # Sets specific board to color based on Red/Green/Blue character
        # Follows Command coLor w/ preset (CL[R/G/B])
        # TODO: This might be better to just do via python based mapping, not CP based mapping, just use setColor
        async def setColorChar(board, colorChar):
            await individual_send_queues[board].put(bytearray(b"CL") + bytearray(colorChar,'utf-8'))

        # Turn all on to the same color
        async def setColorAll(red,green,blue):
            for i in range(len(address)): #Go over all of the boards
                if(boardConnected[i]): #If board is connected, send color
                    await setColor(i, red, green, blue)

        async def getAccelAll():
            for i in range(len(address)): #Go over all of the boards
                if(boardConnected[i]): #If board is connected, send color
                    await individual_send_queues[i].put(bytearray(b"CA"))

        async def getTempAll():
            for i in range(len(address)): #Go over all of the boards
                if(boardConnected[i]): #If board is connected, send color
                    await individual_send_queues[i].put(bytearray(b"CT"))

        async def openServo(yes): # Specifically for the 8th station, open is yes:T, close is yes:F
            if(yes == True):
                await individual_send_queues[lockBox].put(bytearray(b"CSO"))
            else:
                await individual_send_queues[lockBox].put(bytearray(b"CSC"))
          
        await asyncio.sleep(10) #Give it some time to set up
        #TODO: This should be based on connected boards

        #Initialization
        await setColorAll(200,0,200)
        game_State = 0 #Set game state to 0
        for b in range(len(address)):
            game_SI[b] = 0 #They are all starting at state 0

        #Record temperature
        await getTempAll()
        await asyncio.sleep(2)
        for b in range(len(address)):
            tempTarget[b] = temp_SI[b] + tempCoolOffset # Add offset to target temperature. Assuming that we are at a steady state at the start
            print("Setting ",b,"'s target to ",tempTarget[b])
        
        await asyncio.sleep(2)


        while True:
            os.system('cls') # Possibly remove this if DEBUGGING
            print("GL Loop start")

            if(game_State == 0): # Waiting to transition to state 1, boxes need to cool off
                # Do stage 1 stuff
                atTemp = 0
                #Go though all the temps and see which satisfy the temp requirement
                for b in range(len(address)):
                    if(tempTarget[b] < temp_SI[b] or game_SI[b] == 1): #TODO: THIS NEEDS TO CHANGE THIS IS JUST FOR TESTING!
                        atTemp += 1
                        lightFactor = atTemp/numberOfGameBoxes
                        await setColor(b,int(50*(lightFactor)),int(50*(lightFactor)),int(250*(lightFactor))) #Change color for this one box
                        game_SI[b] = 1 #Change game state for this one box
                        

                # Transition out of stage 1
                if atTemp == numberOfGameBoxes:
                    game_State = 1
            
            elif(game_State == 1):
                print("DONE!")
                await setColorAll(10,10,10)
                lockDesire = True

            #Check the lock state
            if(lockState != lockDesire):
                print("Changing the lock state to " , lockDesire)
                await openServo(lockDesire) # Modify
                lockState = lockDesire # Reset toggle

            await getAccelAll()
            await getTempAll()

            # Count up the number of connected devices as a status update
            connected = 0
            for c in boardConnected:
                if c: connected += 1
            print("Number of connected devices: ", connected, " Status: ", boardConnected)
            
            # Get status of all of the boards
            tmp = "############# STATUS ##################\n"
            tmp += "Current Game state: " + str(game_State) + "\n"
            tmp += "Connected Boards: " + str(connected) + "\n"
            for i in range(len(address)):
                #            id
                tmp += "[" + str(i) + "] s:" + str(game_SI[i])  
                tmp += " (x:" + str(accel_SI[i][0]) + " y:" + str(accel_SI[i][1]) + " z:" + str(accel_SI[i][2]) + " " + str(accel_rest_SI[i]) + ")"
                if(accel_shock_SI[i]):
                    tmp += " SHOCK! "
                if(irRX_SI[i]):
                    tmp += " IR_Flag"
                tmp += " t: " + str(temp_SI[i]) + "\n" 
            
            print(tmp+"\n")
            await asyncio.sleep(2.0)


    # Manage info coming in from the keyboard
    # Needs to have data in the form of [boardID][message]
    # boardID is assumed to correlate to individual_send_queues indexes
    async def process_keyboard():
        global lockState
        global lockDesire

        print("Starting task to get info from the keyboard")

        loop = asyncio.get_running_loop()

        while True:
                data = await loop.run_in_executor(None, sys.stdin.buffer.readline)
                print("Just got |",data,"| from keyboard. Parsing to correct board")

                #Figure out which board is being updated
                [select_board,selected_data] = parseBoardNum(data)

                if(select_board == -1): #Received a non-board based message
                    if(data[0] == ord('L')):
                        if(data[1] == ord('O')):
                            print("Opening the Lock")
                            lockDesire = True
                        else:
                            print("Closing the Lock")
                            lockDesire = False
                
                # Send data to the relevant board (make sure it is a valid board first)
                elif(select_board in boardAddr): # Makes sure the specified board is in the board address dictionary
                    print("Adding ",selected_data," to board ",select_board,"'s individual queue")
                    await individual_send_queues[select_board].put(selected_data)
                else:
                    print("Cannot parse ",data," to send to individual queues. Got back: ",select_board, " + ", selected_data)

    #single routine to handle connecting to one single device
    #TODO: need to make this lazy (i.e. keep trying to reconnect)
    async def connect_to_device(address, boardID):
        # probably want to manage the connection in a try / catch loop
        print("connect_to_device - Trying to connect to ", address)

        try: #Set up a try catch for the connection, hopefully this will make it passive if it is not there

            async with BleakClient(address) as client:
                await client.start_notify(UART_TX_CHAR_UUID, handle_rx)
                print("Connected to to device: ", client.address)
                print("This is board ID: ", boardID, end="\n\n")

                # Set up client srevices
                client_service = client.services.get_service(UART_SERVICE_UUID)
                rx_char_service = client_service.get_characteristic(UART_RX_CHAR_UUID)

                while True: # Keep running this forever
                    # Wait for data on this individual send_queue, then send it
                    data = await individual_send_queues[boardID].get() #Offset by one here
                    print("Board ",boardID," just got |", data , "| from i_s_q") #DEBUG
                    await client.write_gatt_char(rx_char_service, data)
        
        except Exception as e: #Grab the exception, this should only happen if a connection is not made
            print("Issue trying to connect to ", boardID, ":" , address, " E:", e)
            boardConnected[boardID] = False #Set to be off

    # set up tasks address_list
    taskList = []
    for index in range(len(address_list)) :
        print("Adding ", address_list[index], " to the task list")
        taskList.append(asyncio.create_task(connect_to_device(address_list[index], index))) # create the task, add it to the task list

    print("Adding process_receive_queue() to the taskList")
    taskList.append(asyncio.create_task(process_receive_queue())) # Add task to parse the queue

    print("Adding keyboard processing to the taskList")
    taskList.append(asyncio.create_task(process_keyboard()))

    print("Starting up GameLogic")
    taskList.append(asyncio.create_task(gameLogic()))

    print("Starting up task list.", end="\n\n")
    for tsk in taskList:
        #print("About to try ", tsk)
        await tsk
        print("Done with task...", tsk)

#############################################################

print("About to run main async")
#asyncio.run(main(address))
asyncio.run(connect_to_all_devices(address))