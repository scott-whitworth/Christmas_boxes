
#include <Adafruit_CircuitPlayground.h> // https://github.com/adafruit/Adafruit_CircuitPlayground

#include <Arduino.h>
//#include <Adafruit_NeoPixel.h>
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

Adafruit_CPlay_NeoPixel neopixel = Adafruit_CPlay_NeoPixel(10,8); //10 lights, on pin 8

int count;

int intensity = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while(!Serial){
    delay(10);
  } //Wait for serial to pop up

  Serial.println("Hello From Circuit Playground"); //This occurs too fast, would need a display to show

  //Set up NeoPixel
  neopixel.begin();
  neopixel.setBrightness(25);

  CircuitPlayground.begin();

  CircuitPlayground.setAccelRange(LIS3DH_RANGE_4_G);

  count = 0;

}

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
    intensity = 150;

    uint32_t color = neopixel.Color( 
      static_cast<int>(255*(abs(x/20))),
      static_cast<int>(255*(abs(y/20))),
      static_cast<int>(255*abs(z/20)) );
    
    neopixel.fill(color);
  }

  
  neopixel.setBrightness(intensity);
  neopixel.show();

  delay(75);
  count++;
  if(intensity > 0){
    intensity -= 10;
  }

}



//Breath Lights
/*
void loop() {
  // put your main code here, to run repeatedly:
  if(count > 9){ count = 0; } // reset count

  Serial << "Loop: " << count << "\n";
  Serial << "Current temp is " << CircuitPlayground.temperatureF() << endl;

  float fTemp = CircuitPlayground.temperatureF();
  float fUpper = 68.0;
  float fLower = 59.0;
  float scale = abs((fTemp - fLower) / (fUpper - fLower)); // This should be between 0 and 1
  Serial << "Current scale: " << scale << endl;

  uint32_t bl_color = neopixel.Color(50,50,255);
  uint32_t re_color = neopixel.Color(230,10,10);

  //neopixel.clear();
  uint32_t color = neopixel.Color( static_cast<int>(255*scale),0,static_cast<int>(255*(1-scale)) );
  neopixel.fill(color);
  neopixel.setBrightness((scale/10) * 255);
  //neopixel.setPixelColor(count,color); 
  neopixel.show();

  delay(50);
  count++;

}
*/