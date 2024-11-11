
#include <Adafruit_CircuitPlayground.h> // https://github.com/adafruit/Adafruit_CircuitPlayground

#include <Arduino.h>
#include <bluefruit.h>

#include "Adafruit_Crickit.h"
#include "seesaw_servo.h"

// Support for printing normally
// https://forum.arduino.cc/t/text-and-variable-both-in-display-println/586907/4
template <typename T>
Print& operator<<(Print& printer, T value)
{
    printer.print(value);
    return printer;
}
#define endl "\n"

//Utility class to hold / average colors
class xMas_Color{
  public:
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  xMas_Color(int r, int g, int b) : red(r), green(g), blue(b){}
  xMas_Color() : red(0), green(0), blue(0){}

  void average(xMas_Color other, float weight){
    this->red = static_cast<uint32_t>(this->red * (1.0 - weight) ) + static_cast<uint32_t>(other.red * weight); 
    this->green = static_cast<uint32_t>(this->green * (1.0 - weight) ) + static_cast<uint32_t>(other.green * weight); 
    this->blue = static_cast<uint32_t>(this->blue * (1.0 - weight) ) + static_cast<uint32_t>(other.blue * weight); 
  }

  uint32_t getColor() const{
    return Adafruit_CPlay_NeoPixel::Color(red,green,blue);
  }

  bool operator==(xMas_Color other) const{
    return (this->red == other.red ) && (this->green == other.green) && (this->blue == other.blue);
  }

  bool operator!=(xMas_Color other) const{
    return !(*this == other);
  }
};

//constants for figuring out BL Address
typedef volatile uint32_t REG32;
#define pREG32 (REG32 *)
#define MAC_ADDRESS_HIGH  (*(pREG32 (0x100000a8)))
#define MAC_ADDRESS_LOW   (*(pREG32 (0x100000a4)))

// Addresses of boards:
// "CE:71:7A:35:C3:18", # Station 1
// "CD:30:BC:87:C4:35", # Station 2
// "E4:27:BB:05:B0:03", # Station 3
// "CB:03:A2:60:90:6A", # Base Station
int BOARD_ADDRESS;

void assignUniqueBoardAddress(){
  uint32_t addr_high = ((MAC_ADDRESS_HIGH) & 0x0000ffff) | 0x0000c000;
  uint32_t addr_low  = MAC_ADDRESS_LOW;

  if(addr_low == 0x7A35C318 && addr_high == 0x0000CE71){
    BOARD_ADDRESS = 1;
  } else if(addr_low == 0xBC87C435 && addr_high == 0x0000CD30){
    BOARD_ADDRESS = 2;
  } else {
    BOARD_ADDRESS = 69;
  }

}

//Color smooth transitions via averaging
xMas_Color current_color; // Should only be set by the managing process
xMas_Color processing_color; // Intermediate color to help with smoothing
xMas_Color desired_color; // What the desired color should be 
uint32_t color_count; // How many steps should be taken
const uint32_t MAX_color_count = 20; // Determines max color steps

// Manage the above values
void processColor();

// Set up for NeoPixel's attached to this board
Adafruit_CPlay_NeoPixel neopixel = Adafruit_CPlay_NeoPixel(10,8); //10 lights, on pin 8

//Bluetooth globals
BLEDfu  ble_dfu;  // OTA DFU service
BLEDis  ble_dis;  // device information
BLEUart ble_uart; // uart over ble
BLEBas  ble_bas;  // battery

int count;
int intensity = 0;


//Info for the servo
Adafruit_Crickit crickit;
seesaw_Servo myservo1(&crickit);  // create servo object to control a servo

void setup() {
  Serial.begin(115200);

  //This is all and good, but if you are running from battery, this won't work (beacuse serial never starts!)
  //while(!Serial){
  //  delay(10);
  //} //Wait for serial to boot up

  Serial << "Starting Basebox." << endl;

  delay(1000);

  if(!crickit.begin()){
    Serial.println("ERROR!");
    while(1) delay(1);
  } else Serial.println("Crickit started");

  myservo1.attach(CRICKIT_SERVO1);  // attaches the servo to CRICKIT_SERVO1 pin
  // Done setting up servo!

    Serial << "Starting to get Bluetooth working" << endl;

  //Figuring out which board this is
  assignUniqueBoardAddress();

  // MAC Address
  uint32_t addr_high = ((MAC_ADDRESS_HIGH) & 0x0000ffff) | 0x0000c000;
  uint32_t addr_low  = MAC_ADDRESS_LOW;
  Serial.print("MAC Address: ");
  Serial.print((addr_high >> 8) & 0xFF, HEX); Serial.print(":");
  Serial.print((addr_high) & 0xFF, HEX); Serial.print(":");
  Serial.print((addr_low >> 24) & 0xFF, HEX); Serial.print(":");
  Serial.print((addr_low >> 16) & 0xFF, HEX); Serial.print(":");
  Serial.print((addr_low >> 8) & 0xFF, HEX); Serial.print(":");
  Serial.print((addr_low) & 0xFF, HEX); Serial.println("");
  Serial << "Calculated address: " << addr_high << ", " << addr_low << endl;

  //info end

  //Set up NeoPixel
  neopixel.begin();
  neopixel.setBrightness(30);

  //Preset the color smoothing
  current_color =  xMas_Color(0,0,0);
  processing_color = current_color;
  desired_color = current_color; // What the desired color should be 
  color_count = -1; // How many steps should be taken

  CircuitPlayground.begin();
  CircuitPlayground.setAccelRange(LIS3DH_RANGE_4_G);

  
  //Bluetooth initilization
  Serial << "Trying to initilize BT" << endl;

  //Set max, must be set before begin
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

  // Assuming 1 periph, 0 central
  Bluefruit.begin(1,0);

  //Power settings pulled from the .h:
  //// - nRF52840: -40dBm, -20dBm, -16dBm, -12dBm, -8dBm, -4dBm, 0dBm, +2dBm, +3dBm, +4dBm, +5dBm, +6dBm, +7dBm and +8dBm.
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values

  //Possibly something I need to mess with
  //Bluefruit.setName(getMcuUniqueID()); // useful testing with multiple central connections

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  ble_dfu.begin(); // dfu - device firmware upgrade
                  // ota - over the air

  // Configure and Start Device Information Service
  ble_dis.setManufacturer("ISB Industries");
  ble_dis.setModel("BUSART Test Other");
  ble_dis.begin(); //Dis - device display

  // Configure and Start BLE Uart Service
  ble_uart.begin();

  // Start BLE Battery Service
  ble_bas.begin();
  ble_bas.write(100); //This seems odd to just assume 100%

  // Set up and start advertising
  startAdv();

  Serial << "Done setting up bluTooth" << endl;

  Serial << "Done with setup!" << endl;
}


//BT_UART data
uint8_t UART_In_buffer[10];
uint8_t UART_In_size;

void loop() {
    // Manage incoming message / set state
  UART_In_size = 0;
  while(ble_uart.available()){
    //Keep looping until either done or size is maxed out
    UART_In_buffer[UART_In_size] = static_cast<uint8_t>(ble_uart.read());
    UART_In_size++;

    if(UART_In_size >= 10){
      Serial << "ERROR: UART BUFFER FULL! Size: " << UART_In_size << endl;
    }
  }

  if(UART_In_size > 0){
    Serial << "Received message (size: UART_In_size: " << UART_In_size << " |" ;
    for(int i = 0; i < UART_In_size; i++ ){
      Serial << UART_In_buffer[i];
    }
    Serial << "|" <<endl;
    
    //Possibly received messages, manage it!
    if(UART_In_buffer[0] == 'C'){
      // Received Color Command
      Serial << "Setting color." << endl;

      //Next character is the next color
      if(UART_In_buffer[1] == 'S'){
        Serial << "Servo command: ";
        if(UART_In_buffer[2] == 'O'){
          //Open the servo
          Serial << "Open!" << endl;
          //myservo1.write(180);
          myservo1.writeMicroseconds(800);
          delay(1000); //Delay, then turn off servo
          myservo1.writeMicroseconds(0);
        } else if(UART_In_buffer[2] == 'C'){
          //Close the servo
          Serial << "Close!" << endl;
          //myservo1.write(0);
          myservo1.writeMicroseconds(2200);
          delay(1000); //Delay, then turn off servo
          myservo1.writeMicroseconds(0);
        } else {
          Serial << "Error. Can't parse: " << UART_In_buffer[2] << endl;
        }
      } else if(UART_In_buffer[1] == 'R'){
        desired_color = xMas_Color(255,0,0);
      } else if(UART_In_buffer[1] == 'G'){
        desired_color = xMas_Color(0,255,0);
      } else if(UART_In_buffer[1] == 'B'){
        desired_color = xMas_Color(0,0,255);
      } else {
        Serial << "Error Parsing Color Command! |" << UART_In_buffer[1] << "|" <<endl;
        desired_color = xMas_Color(255,0,255);
      }
    }


  }



  //myservo1.write(count * 45 % 180);




  //Update the color situation
  processColor();
  //delay(50);

  delay(5); //Needs to align with IR_send
}




// Manage the above values
void processColor(){
  while(!neopixel.canShow()){
    delay(2); // Wait until neopixel can display
  }

  //If we don't need to update, just leave
  if((color_count == -1) && (processing_color == desired_color)){
    return;
  }

  if(color_count == MAX_color_count){
    //color_count is maxed out, reset things
    color_count = -1;
    current_color = desired_color; // Update to desired color
    processing_color = desired_color;

    //Send that info to the pixels
    neopixel.fill(desired_color.getColor());
    neopixel.show();

    return; //We have nothing left to do
  }

  // calculate some percentage of the total count 1/20 -> 19/20
  float cScalar = color_count / static_cast<float>(MAX_color_count);

  xMas_Color setColor = current_color;
  setColor.average(processing_color, cScalar);

  if(processing_color != desired_color){
    //Received a new color! Lock in the current_color
    current_color = setColor; //Make sure we grab the currently changing state

    processing_color = desired_color; //Set processing_color
    color_count = 0; //Reset the count
  }

  neopixel.fill(setColor.getColor());
  neopixel.show();

  color_count++;

  return;
}


//Start advertising, I assume?
void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include bleuart 128-bit uuid
  Bluefruit.Advertising.addService(ble_uart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();
  
  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

////////////////////////////
//Call Backs for actions  //
////////////////////////////

// callback invoked when central connects
void connect_callback(uint16_t conn_handle)
{
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  Serial << "Got a connection! |" << central_name << "|" << endl;

}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  (void) reason;

  Serial << "Something went wrong, Disconected: 0x";
  Serial.println(reason, HEX); 
}