//Separated for development on 9/28
// Eventually merge them together
// ## This is the IR Sender ##

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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while(!Serial){
    delay(10);
  } //Wait for serial to pop up

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


uint8_t toggle = 0;
uint8_t IR_FLASH = 0;

uint16_t countTestCNT = 500;
uint32_t delayCount = 142; //138-146 seem good

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
      } else if(UART_In_buffer[1] == 'O'){ //Turn off IR
        digitalWrite(IR_LED_OUT,LOW);
      } else if(UART_In_buffer[1] == 'I'){ //Change IR Delay
        Serial << "Chaning IR Delay... " << endl;
        uint8_t count = 2; // Index of first number
        uint32_t newNum = 0;
        while(count < UART_In_size ){ //For each of the values left in the buffer...
          newNum = newNum * 10; //Shift previous number over by 10
          newNum += UART_In_buffer[count] - '0'; //Convert ASCII to int, and add it
          count++;
        }
        Serial << "Changing IR_Delay to " << newNum << endl;
        delayCount = newNum;
      } else if(UART_In_buffer[1] == 'L'){ //TimeLoop
        NRF_TIMER2->TASKS_STOP = 1; //Stop timer
        NRF_TIMER2->TASKS_CLEAR = 1; //Clear timer to zero
        NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;   //sets up TIMEr mode as "Timer"
        NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;    // sets values for Timer input
        NRF_TIMER2->PRESCALER = 0;       //read at max resolution (at MCU speed)

        Serial << "Starting..." << endl;

        NRF_TIMER2->TASKS_START = 1;
        NRF_TIMER2->TASKS_CAPTURE[0] = 1; //Grab time for 0
        uint32_t cc0 = NRF_TIMER2->CC[0];
        uint32_t cc2 = 0;

        do{
          NRF_TIMER2->TASKS_CAPTURE[2] = 1; //Capture 2
          cc2 = NRF_TIMER2->CC[2];

        } while( cc2 < 16000000 + cc0); //One second

        NRF_TIMER2->TASKS_CAPTURE[1] = 1; //Grab timer 1

        Serial << "...end" << endl;

        Serial << "end 0 Current NRF Time: " << NRF_TIMER2->CC[0] << endl;
        Serial << "end 1 Current NRF Time: " << NRF_TIMER2->CC[1] << endl;
        Serial << "end 2 Current NRF Time: " << NRF_TIMER2->CC[2] << endl;
        Serial << "end 3 Current NRF Time: " << NRF_TIMER2->CC[3] << endl;


      } else if(UART_In_buffer[1] == 'Y'){ //Test Time
        //Via testing:
        // Delay(5):   69477 (13,895 / ms)
        // Delay(10): 147623 (14,762 / ms)
        // Delay(15): 225777 (15,051 / ms)
        // Delay(20): 302841 (15,142 / ms)
        // Delay(25): 380572 (15,222 / ms)
        // Delay(30): 460127 (15,337 / ms)

        //Testing 2
        // Delay(500): 7990879 (15,981 / ms)
        // Delay(600): 9584499 (15,974 / ms)

        Serial << "Testing a delay(" << countTestCNT << ")" << endl;
        
          NRF_TIMER2->TASKS_STOP = 1;    //stops timer
          NRF_TIMER2->TASKS_CLEAR = 1;    //clear timer to zero
          NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;   //sets up TIMEr mode as "Timer"
          NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;    // sets values for Timer input
          NRF_TIMER2->PRESCALER = 0;       //read at max resolution (at MCU speed)
          Serial << "0 Current NRF Time: " << NRF_TIMER2->CC[0] << endl;
          Serial << "1 Current NRF Time: " << NRF_TIMER2->CC[1] << endl;
          Serial << "3 Current NRF Time: " << NRF_TIMER2->CC[2] << endl;
          Serial << "4 Current NRF Time: " << NRF_TIMER2->CC[3] << endl;
          NRF_TIMER2->TASKS_START = 1;      // starts the Timer
          NRF_TIMER2->TASKS_CAPTURE[0] = 1;
          //Serial << "Testing" << endl;



        //Serial << "1 Current Time: " << xTaskGetTickCount() << endl;
        delay(countTestCNT);
        NRF_TIMER2->TASKS_CAPTURE[1] = 1;
        NRF_TIMER2->TASKS_STOP = 1; //Stop Timer
        //Serial << "2 Current Time: " << xTaskGetTickCount() << endl;
        Serial << "end 0 Current NRF Time: " << NRF_TIMER2->CC[0] << endl;
        Serial << "end 1 Current NRF Time: " << NRF_TIMER2->CC[1] << endl;
        Serial << "end 2 Current NRF Time: " << NRF_TIMER2->CC[2] << endl;
        Serial << "end 3 Current NRF Time: " << NRF_TIMER2->CC[3] << endl;

        countTestCNT += 100;

      } else if(UART_In_buffer[1] == 'T'){ //Test Time
        Serial << "Start...";
        //Testing the microsecond function
        for(uint32_t i = 0; i < 15 ; i++){ //Seconds loop
          for(uint32_t mil = 0; mil < 1000; mil++){ //Milliseconds loop
            for(uint32_t usec = 0; usec < 40; usec++){
              //delayMicroseconds(1);
              //Should be 1 us
              IR_Delay(25); //100us -> 10 of them in one milli
            }
          }
          Serial << " . ";
          //delay(1);
        }
        Serial << " end!" << endl;

        //1000 : 1 -> 26 seconds
        //100:10 -> 10 seconds (but no . . .)
        //100:10 (15 seconds) -> 15.52
        //50:20 (15 seconds) -> 15.55

      } else if(UART_In_buffer[1] == 'F'){ //IR Flash!
        Serial << "Sending IR flash!" << endl; 

        //140-145 ish looks ok.
        
        uint8_t IR_ID = 6;
        IR_send_ID(IR_ID);
        // int delayCount = 12; //2.6x10^-5 -> 0.000 026
        // for(uint32_t i = 0; i < 100; i++){
        //   digitalWrite(IR_LED_OUT,HIGH);
        //   //delay(delayCount);
        //   delayMicroseconds(delayCount);
        //   digitalWrite(IR_LED_OUT,LOW);
        //   delayMicroseconds(delayCount);
        //   //delay(delayCount);
        // }
        Serial << "IR FLASHED at " << delayCount << endl;
      } else {
        Serial << "Error Parsing Color Command! |" << UART_In_buffer[1] << "|" <<endl;
        desired_color = xMas_Color(255,0,255);
      }
    }


  }

  //Make sure IR LED is off
  digitalWrite(IR_LED_OUT,LOW);
  //Update the color situation
  processColor();
  delay(50);

}


//IR Remote (first two 'on', second two 'off' )
//
//
//
//ON IR_IN_BUFF_SIZE: 71 buffer: [ 10296 6311 849 793 821 784 816 788 804 800 800 804 811 785 815 789 811 793 821 2349 813 2320 852 2311 859 2312 850 2321 850 2313 858 2313 849 2322 849 2321 849 786 814 2318 845 797 803 801 813 791 809 788 812 791 727 794  792 1945 812 793 807 2354 817 2316 847 2324 847 2316 899 2272 899 56141 12483 3093 897 ]
//ON IR_IN_BUFF_SIZE: 71 buffer: [ 11612 6304 856 787 813 791 809 796 811 793 807 798 802 796 819 783 817 788 812 2320 842 2246 591 2319 850 2321 842 2329 842 2322 849 2322 848 2324 839 2332 853 782 818 2314 849 793 807 797 803 801 814 782 862 742 858 745  847 2285 760 800 843 2027 861 2310 853 2318 853 2318 852 2311 860 55844 12740 2840 852 ]
//                                            H   L   H   L   H   L   H   L   H   L   H   L   H   L   H   L   H   L    H   L    H   L    H   L    H   L    H   L    H   L    H   L    H   L    H   L   H   L    H   L   H   L   H   L   H   L   H   L   H   L    H   L    H   L   H   L    H   L    H   L    H   L    H   L    H   L     H      L   H   
//                                                                                                                                                                                        ***                                                               ***                                                                                              
//OF IR_IN_BUFF_SIZE: 75 buffer: [ 11012 6275 840 802 812 792 808 789 811 793 822 782 818 785 807 797 802 802 813 2227 860 2058 853 2318 852 2311 860 2310 861 2310 852 2318 853 2309 847 795  820 784 808 2323 848 794 806 790 810 794 821 783 809 795 805 2326 845 2234 832 559 809 2322 840 2330 841 2322 848 2323 840 2331 840 55846 12486 3098 847 134758 12818 3101 844 ]
//OF IR_IN_BUFF_SIZE: 71 buffer: [ 8975  6300 859 783 817 787 805 800 800 804 811 711 806 790 724 628 820 784 816 2317 846 2325 845 2318 853 2318 845 2326 844 2319 852 2319 844 2328 843 800  814 783 817 2315 856 787 805 799 801 803 812 701 816 768 581 2319 842 2329 842 801 814 2311 860 2311 852 2318 852 2311 860 2310 861 55849 12484 3097 848 ]
//                                            H   L   H   L   H   L   H   L   H   L   H   L   H   L   H   L   H   L    H   L    H   L    H   L    H   L    H   L    H   L    H   L    H   L    H   L   H   L    H   L   H   L   H   L   H   L   H   L   H   L    H   L    H   L   H   L    H   L    H   L    H   L    H   L    H   L     H      L   H   

// Count++


// IR_IN_BUFF_SIZE: 59 buffer: [ 617 389 290 2719 281 891 289 850 292 396 511 140 1416 663 287 1979 285 801 288 850 273 616 2190 663 287 2764 289 1423 295 849 1009 1463 284 6535 1621 897 290 1768 290 1975 296 1566 867 1237 865 76252 910 870 862 7836 527 6199 348 1282 450 15239 496 4322 286 ]
//                               H   L   H   L    H   L   H   L   H   L   H   L   H    L   H   L    H   L   H   L   H   L   H    L   H   L    H   L    H   L   H    L    H   L    H    L   H   L    H   L    H   L    H   L    H   L     H   L   H   L    H   L    H   L    H   L     H   L    H
//                                           m    0       1       2       3       4        5        6       7      !0       1        2        3        4       5         6        7     


//const int delayCount = 25; //2.6x10^-5 -> 0.000 026 (12 is good)
const uint8_t IR_BUF = 0b11111111; //All Ones (flip a bunch of bits)
const uint8_t IR_beg_BUF = 0b10100101; //Start Align check
const uint8_t IR_end_BUF = 0b01011010; //End   Align check

//Send info out the IR LED
//Assumes IR pin is set above in IR_LED_OUT
// Sends FF A5 ID 5A [IR_BUF IR_beg_BUF <data> IR_end_BUF]
void IR_send_ID(uint8_t IR_data){

  for(uint8_t n = 0; n < 200; n++){
    IR_send_byte(IR_BUF);
    IR_send_byte(n);
    IR_Delay(delayCount*4);
  }



  digitalWrite(IR_LED_OUT,LOW);
  return;

  IR_send_byte(IR_BUF); //(delay count * 2 * 8) = ~ 0.00014 ms (142)
  digitalWrite(IR_LED_OUT,HIGH);
  delay(1);
  IR_send_byte(IR_BUF);
  digitalWrite(IR_LED_OUT,HIGH);
  delay(1);
  IR_send_byte(IR_BUF);
  digitalWrite(IR_LED_OUT,HIGH);
  delay(1);
  IR_send_byte(IR_BUF);
  digitalWrite(IR_LED_OUT,HIGH);
  delay(1);
  IR_send_byte(IR_BUF);
  digitalWrite(IR_LED_OUT,HIGH);
  delay(1);

  IR_send_byte(IR_beg_BUF);
  IR_send_byte(IR_data);
  IR_send_byte(IR_end_BUF);  

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

    if(ir_bit){ //Bit 1 = two counts
      IR_Delay(delayCount*2);
    } else {
      IR_Delay(delayCount);
    }
    /*
    if(ir_bit){
      //If 1: L to H
      digitalWrite(IR_LED_OUT,LOW);
      IR_Delay(delayCount);
      digitalWrite(IR_LED_OUT,HIGH);
      IR_Delay(delayCount);
    } else {
    //If 0: H to L
      digitalWrite(IR_LED_OUT,HIGH);
      IR_Delay(delayCount);
      digitalWrite(IR_LED_OUT,LOW);
      IR_Delay(delayCount);
    }*/
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