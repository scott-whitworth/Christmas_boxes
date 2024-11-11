
#include <Adafruit_CircuitPlayground.h> // https://github.com/adafruit/Adafruit_CircuitPlayground

#include <Arduino.h>
#include <bluefruit.h>

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

//Not sure why this is not working
//Helper just to print properly
//Print& operator<<(Print& printer, xMas_Color clr){
//  printer << "(" << clr.red << ","<<clr.green << "," << clr.blue << ")";
//  return printer;
//}


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

//A3 - IR LED OUT
//A2 - IR LED IN
#define IR_LED_OUT 10
#define IR_IN 9

//Header comments below
void IR_send_ID(uint8_t IR_data);
void IR_send_byte(uint8_t IR_byte);

//Experimentally set to delay microseconds
uint32_t IR_Delay(uint32_t delay);

uint32_t IR_IN_BUFF[128]; //128 seems like overkill, but lets try
uint8_t IR_IN_BUFF_SIZE;

//Start Capturing IR Signal
void captureIR();


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

const uint32_t SERIAL_TIMEOUT = 100;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  //This is all and good, but if you are running from battery, this won't work (beacuse serial never starts!)
  //while(!Serial){
  for(int c = 0; c < SERIAL_TIMEOUT && (!Serial); c++){
    delay(10);
  } //Wait for serial to boot up
  //TODO: This can work, but we need to time out, then probably block all Serial (like in the overload above)

  Serial.println("Hello From Circuit Playground"); //This occurs too fast, would need a display to show
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

  //Set Up IR pins
  //TODO: Probably put in something like only setting up for specific boards
  pinMode(IR_IN, INPUT);
  pinMode(IR_LED_OUT, OUTPUT);

  digitalWrite(IR_LED_OUT,LOW); // Make sure it is turned off

  count = 0;

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

}




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
      if(UART_In_buffer[1] == 'R'){
        desired_color = xMas_Color(255,0,0);
      } else if(UART_In_buffer[1] == 'G'){
        desired_color = xMas_Color(0,255,0);
      } else if(UART_In_buffer[1] == 'B'){
        desired_color = xMas_Color(0,0,255);
      } else if(UART_In_buffer[1] == 'F'){ //IR Flash!
        //Serial << "Sending IR flash!" << endl; 
        // 140-145 ish looks ok, this is from testing        
        uint8_t IR_ID = 6;
        IR_send_ID(IR_ID);
      } else {
        Serial << "Error Parsing Color Command! |" << UART_In_buffer[1] << "|" <<endl;
        desired_color = xMas_Color(255,0,255);
      }
    }


  }

  //TODO: This may be on a command, depends on timing
  //Check to see if IR is pulled LOW, this means the start of a signal
  if(LOW == digitalRead(IR_IN)){ //TODO: This probably needs to be an interrup, but I am not sure how to do them!
    captureIR(); //Seems like we got a IR signal!
  }

  //Make sure IR LED is off
  digitalWrite(IR_LED_OUT,LOW);
  //Update the color situation
  processColor();
  delay(10); //TODO: Tune this

}


//-----------------------
// IR Utility Functions 
// aka - this doesn't work, but we can at least get somethin'
//-----------------------
uint32_t delayCount = 142; //138-146 seem good

//const int delayCount = 25; //2.6x10^-5 -> 0.000 026 (12 is good)
const uint8_t IR_BUF = 0b11111111; //All Ones (flip a bunch of bits)
const uint8_t IR_beg_BUF = 0b10100101; //Start Align check
const uint8_t IR_end_BUF = 0b01011010; //End   Align check

//Send info out the IR LED
//Assumes IR pin is set above in IR_LED_OUT
// Sends FF A5 ID 5A [IR_BUF IR_beg_BUF <data> IR_end_BUF]
void IR_send_ID(uint8_t IR_data){

  IR_send_byte(IR_BUF); //(delay count * 2 * 8) = ~ 0.00014 ms (142)
  digitalWrite(IR_LED_OUT,HIGH);
  delay(1);
  IR_send_byte(IR_data);
  digitalWrite(IR_LED_OUT,HIGH);
  delay(1);
  IR_send_byte(IR_beg_BUF);
  digitalWrite(IR_LED_OUT,HIGH);
  
  digitalWrite(IR_LED_OUT,LOW); //Make sure IR LED is off
}

//Utility function to send a single byte of data
// String:   1  0  1  0  0  1  0  1
//           LH HL LH HL HL LH HL LH
//Make sure prints are commented out! Way too slow!
void IR_send_byte(uint8_t IR_byte){
  uint8_t count = 8;
  uint8_t ir_bit = 0;

  //Serial << "In IR_send_byte, trying to send: " << IR_byte << endl;

  //Loop count number of times (should be 8)
  while(count > 0){
    //Isolate bit (from left to right)
    ir_bit = (IR_byte & 0x80); //Mask out MSB, capture in ir_bit

    digitalWrite(IR_LED_OUT,HIGH);
    IR_Delay(delayCount);
    digitalWrite(IR_LED_OUT,LOW);

    if(ir_bit){ //Bit 1 = two counts, not sure why I did this
      IR_Delay(delayCount*2);
    } else {
      IR_Delay(delayCount);
    }

    count--; //Update count
    IR_byte = IR_byte << 1; //Shift byte by one position

    //Serial << "ir_bit: |" << ir_bit << "| ";
    //Serial << "Count: " << count << endl;

  }
  return;
}

//Based on v1.10.pdf: 6.30
//and https://devzone.nordicsemi.com/f/nordic-q-a/67998/nrf52-pulse-duration-counter---going-from-1-micro-second-resolution-to-nano-seconds
//Argument: delay in microseconds
uint32_t IR_Delay(uint32_t delay){

  NRF_TIMER2->TASKS_STOP = 1;    //stops timer
  NRF_TIMER2->TASKS_CLEAR = 1;    //clear timer to zero
  NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;   //sets up TIMEr mode as "Timer"
  NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;    // sets values for Timer input
  NRF_TIMER2->PRESCALER = 0;       //read at max resolution (at MCU speed)
  NRF_TIMER2->TASKS_START = 1;      // starts the Timer
  //Timer should be good to go
  NRF_TIMER2->TASKS_CAPTURE[0] = 1; //Capture time
  uint32_t cc0 = NRF_TIMER2->CC[0]; //Grab current time
  uint32_t cc1 = 0;
  do{
    NRF_TIMER2->TASKS_CAPTURE[1] = 1; //Grab [1]
    cc1 = NRF_TIMER2->CC[1];
  } while ( cc1 < (delay*15) ); //Delay mult: in theory it should be 16, but there are confounders.
  //Looks like 16 is close, but there is some over head that is making this a bit long. 
  //

  NRF_TIMER2->TASKS_STOP = 1; //Stop Timer
  return 0;
}

//TODO: Send back a message when it receives a IR signal

//Start Capturing IR Signal
void captureIR(){
  IR_IN_BUFF_SIZE = 0; // Set size to zero

  uint32_t loopTime = 0; //Running loop counter

  uint8_t curIR = LOW;
  uint8_t oldIR = LOW;

  //Loop until nothing changes for a while
  while( loopTime < 1000000 && IR_IN_BUFF_SIZE < 128){ //TODO: Probably can drop this down
    curIR = digitalRead(IR_IN);
    
    //Track the change
    if(curIR != oldIR){
      //Different IR states! Record what happened!
      IR_IN_BUFF[IR_IN_BUFF_SIZE] = loopTime; //Store loopTime
      IR_IN_BUFF_SIZE++; //Update IR_IN_BUFF_SIZE to the next location
      loopTime = 0; //Reset loopTime back to zero
    } 

    //maybe do this in the 'else'
    oldIR = curIR; //Update current signal

    loopTime++; //Update LoopTime (this will eventually be the count for each 'pulse')
    //delayMicroseconds(delayCount / 4); //Sample four times as fast as the sending
  }

  //TODO: Change this, just for testing
  if(IR_IN_BUFF_SIZE > 10){
    desired_color = xMas_Color(125,125,125);
  } else {
    desired_color = xMas_Color(255,0,255);
  }

  return;

  Serial << "loopTime: " << loopTime << endl;
  Serial << "Done getting IR Signal! " << endl;  
  Serial << "IR_IN_BUFF_SIZE: " << IR_IN_BUFF_SIZE << " buffer: [ " ;
  //Print out buffer:
  for(uint8_t i = 0; i < IR_IN_BUFF_SIZE; i++ ){
    Serial << IR_IN_BUFF[i] << " ";
  }
  Serial << "]" << endl;
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