# Christmas_boxes
2022 Escape room Boxes

Notes:

https://learn.adafruit.com/adafruit-circuit-playground-bluefruit

Everything at this point is based on Adafruits wonderful examples and libraries for CPBluFruit

Getting arduino IDE setup:

following: https://learn.adafruit.com/adafruit-circuit-playground-bluefruit/arduino-support-setup

https://adafruit.github.io/arduino-board-index/package_adafruit_index.json

Libraries: Circuit Playground

SetUp:
- Install arduino
    - Configure Adafruit Board Manager (see link above)
    - Manage Libraries: Adafruit Circuit Playground
- Install python
    - `pip3 install bleak`


Board Addresses:
// "CE:71:7A:35:C3:18", # Station 1  (Sender)
// "CD:30:BC:87:C4:35", # Station 2  (Receiver) (com3)

A3 - IR LED OUT  
A2 - IR IN  
A1 - SWITCH LED  


