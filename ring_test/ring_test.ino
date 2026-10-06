// ElTech-Online ESP32 Mood Lamp — LED ring test
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// The smallest possible sketch for the WS2812B LED ring: one LED at a time
// lights up, going round the ring and changing colour as it goes. No knob, no
// remote, no WiFi. Use it to check the ring is wired correctly before building
// the full project.
//
// Library needed (Arduino IDE Library Manager): Adafruit NeoPixel
//
// Wiring (3 wires):
//   ring 5V (or VCC) -> ESP32-C3 5V
//   ring GND         -> ESP32-C3 GND
//   ring DI          -> ESP32-C3 GPIO 4

#include <Adafruit_NeoPixel.h>

#define RING_PIN 4   // the pin the ring's DI (data in) wire is on
#define NUM_LEDS 8   // how many LEDs the ring has

// This line creates the "ring" object. NEO_GRB + NEO_KHZ800 describes the kind
// of LED: these take their colours in green-red-blue order, at 800 kHz.
Adafruit_NeoPixel ring(NUM_LEDS, RING_PIN, NEO_GRB + NEO_KHZ800);

// setup() runs once, when the board is powered on or reset.
void setup() {
  ring.begin();
  ring.setBrightness(40);   // 0-255. Keep it low: these LEDs are very bright.
}

// loop() runs over and over, forever.
void loop() {
  // "static" makes these keep their values between one run of loop() and the next.
  static int position = 0;        // which LED is lit, 0 to 7
  static uint16_t hue = 0;        // colour: a position on the colour wheel, 0-65535

  ring.clear();                                         // all LEDs off...
  ring.setPixelColor(position, ring.ColorHSV(hue));     // ...except this one
  ring.show();                                          // send it to the ring

  position = (position + 1) % NUM_LEDS;   // next LED; % makes 8 wrap round to 0
  hue += 2000;                            // a little further round the colour wheel
  delay(100);                             // wait a tenth of a second
}
