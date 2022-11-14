
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

  //Set up NeoPixel
  neopixel.begin();
  neopixel.setBrightness(30);

  CircuitPlayground.begin();
  CircuitPlayground.setAccelRange(LIS3DH_RANGE_4_G);

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

uint32_t color;

void loop() {
  // put your main code here, to run repeatedly:
  if(count > 9){ count = 0; } // reset count

  //Serial << "Loop: " << count << "\n";
  float x = CircuitPlayground.motionX();
  float y = CircuitPlayground.motionY();
  float z = CircuitPlayground.motionZ();
  float sum = abs(x) + abs(y) + abs(z);
  //Serial << "Sum: " << sum <<  " \tX:" << x << "\tY:" << y << "\tZ:" << z << endl;

  if(sum > 18.0){ // trigger level
    Serial << "Sum: " << sum <<  " \tX:" << x << "\tY:" << y << "\tZ:" << z << endl;

    //Send an A to the BLUUART
    ble_uart.write('A' );
    ble_uart.write("Acce. Triggered!");

    intensity = 150;

    color = neopixel.Color( 
      static_cast<int>(255*(abs(x/20))),
      static_cast<int>(255*(abs(y/20))),
      static_cast<int>(255*abs(z/20)) );
    
    //neopixel.fill(color);
  }

  color = color * (intensity / 150.0);
  neopixel.fill(color);
  
  //neopixel.setBrightness(intensity);
  neopixel.show();

  delay(75);
  count++;
  if(intensity > 0){
    intensity -= 10;
  }

  while(ble_uart.available()){
    uint8_t ch;
    ch = static_cast<uint8_t>(ble_uart.read());
    Serial << "|" << ch << "|" << endl;

    uint32_t color2;

    Serial << "Waiting for neopixel:|";
    while(!neopixel.canShow()){
      delay(5); // Wait until neopixel can display
      Serial << ".";
    }
    Serial << "|" << endl;

    if(ch == static_cast<uint8_t>('R')){
      color2 = neopixel.Color(255,0,0);

    } else if(ch == 71){
      color2 = neopixel.Color(0,255,0);
      
    } else if(ch == 'B'){
      color2 = neopixel.Color(0,0,255);
      
    } else {
      color2 = neopixel.Color( 255,255,255);
      
    }

    neopixel.fill(color2);
    //neopixel.setBrightness(100);
    neopixel.show();

    delay(3000);
  }

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