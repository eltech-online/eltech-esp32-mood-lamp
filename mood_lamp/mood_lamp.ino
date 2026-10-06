// ElTech-Online ESP32 Mood Lamp — an 8-LED colour ring you control three ways:
// with a knob, with any TV remote, and from a web page on your phone.
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// Three parts, three techniques:
//   - WS2812B LED ring    -> ADDRESSABLE LEDs: one data wire sets every LED's colour
//   - KY-040 rotary knob  -> a ROTARY ENCODER, read with an interrupt
//   - KY-022 IR receiver  -> DECODING the pulses a remote control sends (NEC code)
// And the board serves its own WEB PAGE, so a phone can control real hardware.
//
// Libraries needed (Arduino IDE Library Manager):
//   Adafruit NeoPixel
// (WiFi and WebServer are built into the ESP32 board package — no separate install)
// Board package: esp32 by Espressif Systems
//
// How to use:
//   1. Flash this sketch. The ring shows a short red-green-blue test.
//   2. Turn the knob = brightness. Press = next effect. Hold for a second = on/off.
//      Hold the knob down AND turn = change the colour.
//   3. Serial Monitor shows the board's WiFi name and password. Join that
//      network with your phone and open http://192.168.4.1
//   4. On the page, "Learn" teaches the lamp the buttons of your own remote.
// Full source, wiring diagram and setup guide: github.com/eltech-online/eltech-esp32-mood-lamp
//
// ---------------------------------------------------------------------------
// New to Arduino code? How to read this file
// ---------------------------------------------------------------------------
// Lines starting with // are comments: notes for people, ignored by the board.
// The file is in this order, and you can read it top to bottom:
//   1. Settings        - pin numbers and limits you can safely change
//   2. Lamp state      - the handful of values that describe what the lamp shows
//   3. LED ring        - turning those values into colours on the 8 LEDs
//   4. Rotary knob     - counting clicks with an interrupt
//   5. IR remote       - measuring pulses and turning them into a button code
//   6. Web server      - what the board sends to, and accepts from, your phone
//   7. setup()         - runs ONCE when the board is powered on
//   8. loop()          - then runs over and over, forever
// A good first experiment: change MAX_BRIGHTNESS below and upload.

#include <WebServer.h>
#include <Preferences.h>         // saves the lamp's settings in flash
#include <Adafruit_NeoPixel.h>   // drives the WS2812B LEDs

// ---- WiFi Access Point settings ----
// Leave both empty ("") and every board gets its OWN network name (e.g.
// "ElTech-ML-A3F2") and its OWN random 8-character password, saved in flash.
// Or type your own: name up to 32 characters, password 8-63 characters.
const char* AP_SSID     = "";
const char* AP_PASSWORD = "";
const bool  AP_OPEN_NETWORK = false;  // true = no password at all
#define KIT_SSID_PREFIX "ElTech-ML-"
#define KIT_PREFS       "lamp"        // name of this kit's flash "notebook"
#include "eltech_wifi.h"     // starts the WiFi network (second tab in the IDE)
#include "page_template.h"   // the web page's HTML (third tab)

// ---- Pins ----
#define RING_PIN 4    // LED ring DI (data in)
#define ENC_CLK  5    // knob CLK
#define ENC_DT   6    // knob DT
#define ENC_SW   7    // knob SW (the push button)
#define IR_PIN   10   // IR receiver S (signal)

// ---- LED ring ----
#define NUM_LEDS 8
// 0-255. The ring is powered from the USB 5 V pin. All 8 LEDs on full white
// would draw about 0.5 A, more than some USB ports like to give, so the sketch
// never goes above this level. 120 is already bright in a room.
#define MAX_BRIGHTNESS 120

// Turning the knob the "wrong" way round? Change false to true.
const bool ENC_REVERSE = false;

Adafruit_NeoPixel ring(NUM_LEDS, RING_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

// ---------------------------------------------------------------------------
// Lamp state — everything the lamp shows comes from these few values. The knob,
// the remote and the web page all just change them; one function (drawRing)
// turns them into light. Keeping one "source of truth" like this is why the
// three controls never disagree with each other.
// ---------------------------------------------------------------------------
bool lampOn = true;
uint16_t hue = 5000;      // colour as a position on the colour wheel, 0-65535
uint8_t  saturation = 255; // 255 = pure colour, 0 = white
int brightPct = 40;       // brightness, 0-100 %
int effect = 0;           // which entry of EFFECT_NAMES is running

const char* EFFECT_NAMES[] = { "Solid", "Rainbow", "Breathe", "Candle" };
const int EFFECT_COUNT = 4;

// Settings are saved to flash 3 seconds after the last change, not on every
// knob click: flash memory wears out a little with each write.
bool stateDirty = false;
unsigned long lastChangeMs = 0;

void stateChanged() {
  stateDirty = true;
  lastChangeMs = millis();
}

void saveState() {
  Preferences prefs;
  prefs.begin(KIT_PREFS, false);
  prefs.putBool("on", lampOn);
  prefs.putUShort("hue", hue);
  prefs.putUChar("sat", saturation);
  prefs.putInt("bright", brightPct);
  prefs.putInt("effect", effect);
  prefs.end();
  stateDirty = false;
}

void loadState() {
  Preferences prefs;
  prefs.begin(KIT_PREFS, true);          // true = read only
  lampOn     = prefs.getBool("on", true);
  hue        = prefs.getUShort("hue", 5000);
  saturation = prefs.getUChar("sat", 255);
  brightPct  = prefs.getInt("bright", 40);
  effect     = prefs.getInt("effect", 0);
  prefs.end();
  brightPct = constrain(brightPct, 0, 100);
  effect = constrain(effect, 0, EFFECT_COUNT - 1);
}

// ---------------------------------------------------------------------------
// LED ring
// ---------------------------------------------------------------------------

// Works out the colour of every LED for this moment and sends it to the ring.
// Called about 50 times a second from loop(), which is what makes the moving
// effects look smooth.
void drawRing() {
  if (!lampOn) {
    ring.clear();
    ring.show();
    return;
  }
  ring.setBrightness(map(brightPct, 0, 100, 0, MAX_BRIGHTNESS));
  unsigned long now = millis();

  if (effect == 0) {
    // Solid: every LED the chosen colour.
    ring.fill(ring.gamma32(ring.ColorHSV(hue, saturation, 255)));

  } else if (effect == 1) {
    // Rainbow: each LED a step further round the colour wheel, and the whole
    // wheel slowly turns. now * 10 goes once round (65536) in about 6.5 s.
    for (int i = 0; i < NUM_LEDS; i++) {
      uint16_t h = (uint16_t)(now * 10) + i * (65536 / NUM_LEDS);
      ring.setPixelColor(i, ring.gamma32(ring.ColorHSV(h, 255, 255)));
    }

  } else if (effect == 2) {
    // Breathe: the chosen colour fades up and down like slow breathing.
    // sin() gives a smooth wave from -1 to +1; we move it to 0.1 - 1.0.
    float wave = (sin(now / 4000.0 * 2 * PI) + 1.0) / 2.0;
    uint8_t level = 25 + wave * 230;
    ring.fill(ring.gamma32(ring.ColorHSV(hue, saturation, level)));

  } else {
    // Candle: warm orange, each LED flickering by a random amount. Only
    // changed every 80 ms, or it would look like noise instead of a flame.
    static unsigned long lastFlicker = 0;
    if (now - lastFlicker < 80) return;
    lastFlicker = now;
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t level = random(90, 256);
      ring.setPixelColor(i, ring.gamma32(ring.ColorHSV(4000 + random(0, 1500), 255, level)));
    }
  }
  ring.show();   // nothing changes on the LEDs until this line
}

// ---------------------------------------------------------------------------
// Things the lamp can be told to do. The knob, the remote and the web page all
// end up calling these.
// ---------------------------------------------------------------------------
void togglePower()        { lampOn = !lampOn; stateChanged(); }
void nextEffect()         { effect = (effect + 1) % EFFECT_COUNT; lampOn = true; stateChanged(); }
void changeBrightness(int steps) {
  brightPct = constrain(brightPct + steps, 2, 100);   // never fully dark: use on/off for that
  stateChanged();
}
void changeHue(int steps) {
  hue += steps * 2048;      // 32 steps go once round the colour wheel
  saturation = 255;
  stateChanged();
}

// ---------------------------------------------------------------------------
// Rotary knob
// ---------------------------------------------------------------------------
// Inside the knob are two switches, CLK and DT, that open and close one after
// the other as it turns. Which one changes FIRST tells you the direction:
// when CLK falls, DT is still high for one direction and already low for the
// other.
//
// A click of the knob is over in a few milliseconds, and loop() might be busy
// at that moment. So CLK is watched by an INTERRUPT: the board drops whatever
// it is doing, runs knobTurned(), and carries on. An interrupt function must
// be tiny and fast, so it only counts; loop() does the real work later.
// "volatile" tells the compiler the value can change at any moment.
volatile int knobSteps = 0;
volatile unsigned long lastKnobUs = 0;

void IRAM_ATTR knobTurned() {
  unsigned long now = micros();
  if (now - lastKnobUs < 1500) return;      // ignore contact bounce
  lastKnobUs = now;
  bool clockwise = digitalRead(ENC_DT) == HIGH;
  if (ENC_REVERSE) clockwise = !clockwise;
  knobSteps += clockwise ? 1 : -1;
}

// Reads the knob's turns and its push button. Called from loop().
void handleKnob() {
  static bool wasDown = false;
  static bool turnedWhileDown = false;
  static unsigned long downSince = 0;

  // Take the count and reset it, with interrupts paused for those two lines so
  // a click can't land in between and get lost.
  noInterrupts();
  int steps = knobSteps;
  knobSteps = 0;
  interrupts();

  bool down = digitalRead(ENC_SW) == LOW;   // the button connects SW to GND

  if (steps != 0) {
    if (down) {
      changeHue(steps);                     // held down + turned = colour
      turnedWhileDown = true;
    } else {
      changeBrightness(steps * 5);          // turned = brightness
    }
  }

  if (down && !wasDown) {                   // just pressed
    downSince = millis();
    turnedWhileDown = false;
  }
  if (!down && wasDown) {                   // just released
    unsigned long heldMs = millis() - downSince;
    if (turnedWhileDown || heldMs < 30) {
      // it was a colour change, or just contact bounce: do nothing more
    } else if (heldMs > 800) {
      togglePower();                        // long press = on/off
    } else {
      nextEffect();                         // short press = next effect
    }
  }
  wasDown = down;
}

// ---------------------------------------------------------------------------
// IR remote
// ---------------------------------------------------------------------------
// A remote flashes an invisible infrared LED. The receiver turns the flashes
// into a wire that goes LOW while the remote is flashing. Most remotes use the
// "NEC" code, and it is all in the TIMING between one fall of the wire and the
// next:
//     13.5  ms  = start of a new button press
//      1.125 ms = a 0 bit
//      2.25  ms = a 1 bit
//     11.25  ms = "the button is still held down" (a repeat)
// 32 bits in a row make the button's code. So decoding is just: measure the
// time since the last fall, and decide which of those four it was. Again an
// interrupt does the measuring, because the timing has to be exact.
volatile uint32_t irBits = 0;        // the code being built up, bit by bit
volatile int irBitCount = -1;        // how many bits so far (-1 = not in a code)
volatile unsigned long irLastFallUs = 0;
volatile uint32_t irCode = 0;        // the last complete code
volatile bool irCodeReady = false;
volatile bool irRepeatSeen = false;

void IRAM_ATTR irFell() {
  unsigned long now = micros();
  unsigned long gap = now - irLastFallUs;
  irLastFallUs = now;

  if (gap > 12500 && gap < 14500) {          // start
    irBitCount = 0;
    irBits = 0;
  } else if (gap > 10500 && gap < 12000) {   // repeat
    irRepeatSeen = true;
    irBitCount = -1;
  } else if (irBitCount >= 0) {
    if (gap > 1900 && gap < 2600) {
      irBits |= (1UL << irBitCount);         // a 1 bit
    } else if (gap < 900 || gap > 1400) {
      irBitCount = -1;                       // neither 0 nor 1: give up on this code
      return;
    }
    irBitCount = irBitCount + 1;
    if (irBitCount == 32) {
      irCode = irBits;
      irCodeReady = true;
      irBitCount = -1;
    }
  }
}

// What a remote button can be taught to do.
const char* ACTION_NAMES[] = { "Power", "Next effect", "Brighter", "Dimmer", "Next colour" };
const int ACTION_COUNT = 5;
uint32_t learnedCodes[ACTION_COUNT];   // 0 = nothing learned for this action yet
int learningAction = -1;               // which action is waiting for a button (-1 = none)
uint32_t lastIrCode = 0;               // shown on the web page
int lastIrAction = -1;                 // used for "button held down" repeats

void loadIrCodes() {
  Preferences prefs;
  prefs.begin(KIT_PREFS, true);
  for (int i = 0; i < ACTION_COUNT; i++) {
    String key = "ir" + String(i);
    learnedCodes[i] = prefs.getUInt(key.c_str(), 0);
  }
  prefs.end();
}

void saveIrCode(int action) {
  Preferences prefs;
  prefs.begin(KIT_PREFS, false);
  String key = "ir" + String(action);
  prefs.putUInt(key.c_str(), learnedCodes[action]);
  prefs.end();
}

void doAction(int action) {
  if (action == 0) togglePower();
  if (action == 1) nextEffect();
  if (action == 2) changeBrightness(10);
  if (action == 3) changeBrightness(-10);
  if (action == 4) changeHue(4);
}

// Checks whether the interrupt has finished a code, and acts on it.
void handleRemote() {
  if (irCodeReady) {
    noInterrupts();
    uint32_t code = irCode;
    irCodeReady = false;
    interrupts();

    lastIrCode = code;
    Serial.printf("IR code: %08lX\n", (unsigned long)code);

    if (learningAction >= 0) {
      // Learning: whatever button was just pressed now does this action.
      learnedCodes[learningAction] = code;
      saveIrCode(learningAction);
      Serial.printf("Learned \"%s\"\n", ACTION_NAMES[learningAction]);
      learningAction = -1;
      lastIrAction = -1;
      return;
    }
    lastIrAction = -1;
    for (int i = 0; i < ACTION_COUNT; i++) {
      if (learnedCodes[i] != 0 && learnedCodes[i] == code) {
        doAction(i);
        lastIrAction = i;
      }
    }
  }
  // Holding Brighter or Dimmer keeps going; the other actions happen once.
  if (irRepeatSeen) {
    irRepeatSeen = false;
    if (lastIrAction == 2 || lastIrAction == 3) doAction(lastIrAction);
  }
}

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------
// A browser asks the board for an address, and the matching function below
// sends the answer. setup() connects each address to its function.

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "text/html", PAGE_TEMPLATE);
}

void handleStyle() {
  server.send(200, "text/css", STYLE_CSS);
}

// The current colour as the "#rrggbb" text a web colour picker uses.
String colourAsHex() {
  uint32_t c = ring.ColorHSV(hue, saturation, 255);
  char text[8];
  snprintf(text, sizeof(text), "#%06lx", (unsigned long)(c & 0xFFFFFF));
  return String(text);
}

// "/state" -> everything about the lamp as JSON, a simple text format programs
// can read. The page asks for it once a second, which is how it stays in step
// when you turn the knob or use the remote.
void handleState() {
  String json = "{";
  json += "\"on\":" + String(lampOn ? "true" : "false") + ",";
  json += "\"color\":\"" + colourAsHex() + "\",";
  json += "\"bright\":" + String(brightPct) + ",";
  json += "\"effect\":" + String(effect) + ",";
  json += "\"effects\":[";
  for (int i = 0; i < EFFECT_COUNT; i++) {
    json += "\"" + String(EFFECT_NAMES[i]) + "\"";
    if (i < EFFECT_COUNT - 1) json += ",";
  }
  json += "],\"learning\":" + String(learningAction) + ",";
  char codeText[9];
  snprintf(codeText, sizeof(codeText), "%08lX", (unsigned long)lastIrCode);
  json += "\"lastir\":\"" + String(lastIrCode ? codeText : "none yet") + "\",";
  json += "\"ir\":[";
  for (int i = 0; i < ACTION_COUNT; i++) {
    json += "[\"" + String(ACTION_NAMES[i]) + "\"," + String(learnedCodes[i] ? "true" : "false") + "]";
    if (i < ACTION_COUNT - 1) json += ",";
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", json);
}

// Turns a "#rrggbb" colour from the page into hue and saturation.
void setColourFromHex(String hex) {
  if (hex.startsWith("#")) hex = hex.substring(1);
  long rgb = strtol(hex.c_str(), NULL, 16);
  float r = ((rgb >> 16) & 0xFF) / 255.0, g = ((rgb >> 8) & 0xFF) / 255.0, b = (rgb & 0xFF) / 255.0;
  float top = max(r, max(g, b)), bottom = min(r, min(g, b)), spread = top - bottom;
  float h = 0;                                   // position on the colour wheel, 0-6
  if (spread > 0) {
    if (top == r)      h = fmod((g - b) / spread + 6.0, 6.0);
    else if (top == g) h = (b - r) / spread + 2.0;
    else               h = (r - g) / spread + 4.0;
  }
  hue = (uint16_t)(h / 6.0 * 65535);
  saturation = top > 0 ? (uint8_t)(spread / top * 255) : 0;
}

// "/set" -> the page changes something. It sends only what changed, e.g.
// /set?bright=60 or /set?color=%23ff8800.
void handleSet() {
  if (server.hasArg("on"))     lampOn = server.arg("on") == "1";
  if (server.hasArg("bright")) brightPct = constrain(server.arg("bright").toInt(), 2, 100);
  if (server.hasArg("effect")) effect = constrain(server.arg("effect").toInt(), 0, EFFECT_COUNT - 1);
  if (server.hasArg("color"))  { setColourFromHex(server.arg("color")); effect = 0; }
  stateChanged();
  handleState();   // answer with the new state
}

// "/learn?action=2" -> the next remote button pressed will do action 2.
// "/learn?action=-1" cancels. "/learn?forget=1" forgets every button.
void handleLearn() {
  if (server.hasArg("forget")) {
    for (int i = 0; i < ACTION_COUNT; i++) { learnedCodes[i] = 0; saveIrCode(i); }
    learningAction = -1;
  } else {
    learningAction = constrain(server.arg("action").toInt(), -1, ACTION_COUNT - 1);
  }
  handleState();
}

// ---------------------------------------------------------------------------
// setup() runs once at power-on, loop() then runs forever
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Is the knob connected? Its module pulls CLK and DT up to 3.3 V. With the
  // ESP32's own weak pull-DOWN switched on, the pins read HIGH only if the
  // module is really there.
  pinMode(ENC_CLK, INPUT_PULLDOWN);
  pinMode(ENC_DT, INPUT_PULLDOWN);
  delay(10);
  bool knobOK = digitalRead(ENC_CLK) == HIGH && digitalRead(ENC_DT) == HIGH;

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);   // the button has no pull-up of its own
  pinMode(IR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), knobTurned, FALLING);
  attachInterrupt(digitalPinToInterrupt(IR_PIN), irFell, FALLING);

  loadState();
  loadIrCodes();

  // Ring test: red, green, blue, half a second each. The ring can't tell the
  // board it is there, so this is a test you check by eye.
  ring.begin();
  ring.setBrightness(60);
  uint32_t testColours[] = { ring.Color(255, 0, 0), ring.Color(0, 255, 0), ring.Color(0, 0, 255) };
  for (int i = 0; i < 3; i++) {
    ring.fill(testColours[i]);
    ring.show();
    delay(500);
  }

  wifiOK = startAccessPoint();

  Serial.println("========================================");
  Serial.println("           ElTech-Online");
  Serial.println("      ESP32 Mood Lamp (BETA)");
  Serial.println("========================================");
  Serial.println("--- Self-test ---");
  Serial.println("LED ring:      shown red, green, blue (check by eye)");
  Serial.print("Rotary knob:   "); Serial.println(knobOK ? "OK" : "NOT FOUND");
  Serial.println("IR receiver:   press a remote button, a code should print here");
  Serial.print("WiFi AP:       ");
  if (wifiOK) { Serial.println("OK"); } else { Serial.print("FAILED ("); Serial.print(wifiError); Serial.println(")"); }
  Serial.print("RESULT:        "); Serial.println(knobOK && wifiOK ? "PASS" : "FAIL");
  printWifiDetails();

  server.on("/", handleRoot);
  server.on("/style.css", handleStyle);
  server.on("/state", handleState);
  server.on("/set", HTTP_POST, handleSet);
  server.on("/learn", HTTP_POST, handleLearn);
  server.begin();
}

void loop() {
  server.handleClient();   // answer any browser that's waiting
  handleKnob();
  handleRemote();

  // Redraw the ring every 20 ms (50 times a second) WITHOUT stopping
  // everything else. millis() is the number of milliseconds since the board
  // started; we note when we last drew and only draw again once 20 ms have
  // gone by. ("static" makes lastDraw keep its value between runs of loop().)
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw >= 20) {
    lastDraw = millis();
    drawRing();
  }

  // Save the settings once they have been left alone for 3 seconds.
  if (stateDirty && millis() - lastChangeMs > 3000) saveState();
}
