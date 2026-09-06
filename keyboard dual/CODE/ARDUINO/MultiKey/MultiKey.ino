// @file MultiKey.ino
#include <Keypad.h>
/* @file Keypad.h
|| @version 3.1
|| @author Mark Stanley, Alexander Brevig
|| @contact mstanley@technologist.com, alexanderbrevig@gmail.com*/
#include <Keyboard.h>
/*   Copyright (c) 2015, Arduino LLC
  Original code (pre-library): Copyright (c) 2011, Peter Barrett*/
#include <FastLED.h>
#include <HardwareSerial.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial1(PA10, PA9);  // RX, TX UART1

//Keyboard modifiers
#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82  //Option (⌥) on Mac
#define KEY_LEFT_GUI 0x83  //OS logo, Command (⌘) on Mac
#define KEY_RIGHT_CTRL 0x84
#define KEY_RIGHT_SHIFT 0x85
#define KEY_RIGHT_ALT 0x86  //also AltGr, Option (⌥) on Mac
#define KEY_RIGHT_GUI 0x87  //OS logo, Command (⌘) on Mac


//Special keys
#define KEY_TAB 0xB3
#define KEY_CAPS_LOCK 0xC1
#define KEY_BACKSPACE 0xB2
#define KEY_RETURN 0xB0
#define KEY_MENU 0xED

//Navigation cluster
#define KEY_INSERT 0xD1
#define KEY_DELETE 0xD4
#define KEY_HOME 0xD2
#define KEY_END 0xD5
#define KEY_PAGE_UP 0xD3
#define KEY_PAGE_DOWN 0xD6
#define KEY_UP_ARROW 0xDA
#define KEY_DOWN_ARROW 0xD9
#define KEY_LEFT_ARROW 0xD8
#define KEY_RIGHT_ARROW 0xD7

//Numeric keypad
#define KEY_NUM_LOCK 0xDB
#define KEY_KP_SLASH 0xDC
#define KEY_KP_ASTERISK 0xDD
#define KEY_KP_MINUS 0xDE
#define KEY_KP_PLUS 0xDF
#define KEY_KP_ENTER 0xE0
#define KEY_KP_1 0xE1
#define KEY_KP_2 0xE2
#define KEY_KP_3 0xE3
#define KEY_KP_4 0xE4
#define KEY_KP_5 0xE5
#define KEY_KP_6 0xE6
#define KEY_KP_7 0xE7
#define KEY_KP_8 0xE8
#define KEY_KP_9 0xE9
#define KEY_KP_0 0xEA
#define KEY_KP_DOT 0xEB

// Escape and function keys
#define KEY_ESC 0xB1
#define KEY_F1 0xC2
#define KEY_F2 0xC3
#define KEY_F3 0xC4
#define KEY_F4 0xC5
#define KEY_F5 0xC6
#define KEY_F6 0xC7
#define KEY_F7 0xC8
#define KEY_F8 0xC9
#define KEY_F9 0xCA
#define KEY_F10 0xCB
#define KEY_F11 0xCC
#define KEY_F12 0xCD
#define KEY_F13 0xF0
#define KEY_F14 0xF1
#define KEY_F15 0xF2
#define KEY_F16 0xF3
#define KEY_F17 0xF4
#define KEY_F18 0xF5
#define KEY_F19 0xF6
#define KEY_F20 0xF7
#define KEY_F21 0xF8
#define KEY_F22 0xF9
#define KEY_F23 0xFA
#define KEY_F24 0xFB

//Function control keys
#define KEY_PRINT_SCREEN 0xCE  //Print Screen or PrtSc / SysRq
#define KEY_SCROLL_LOCK 0xCF
#define KEY_PAUSE 0xD0  //Pause / Break

#define BT_RST_PIN  PA7
#define BT_KEY_PIN  PA8
#define BT_LINK_PIN PA15
#define BT_WORK_PIN PA6

#define LED_PIN PB0
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define ROWS 7  //outputs
#define COLS 5  //inputs
#define NUM_LEDS (ROWS * COLS)
#define BRIGHTNESS 20

void sweepRainbow(bool clear);

CRGB leds[NUM_LEDS];
bool hello_flag = true;

char keys[ROWS][COLS] = {
  { KEY_DELETE, '0', KEY_LEFT_ALT, 't',             KEY_RIGHT_ARROW },
  { ' ',        '8', 'm',           KEY_UP_ARROW,   KEY_DOWN_ARROW },
  { KEY_F4,     '6', 'r',           'f',            KEY_LEFT_ARROW },
  { KEY_F3,     '4', 'e',           'd',            'b' },
  { KEY_F2,     '2', 'w',           's',            'x' },
  { KEY_F1,     '1', 'q',           'a',            'z' },
  { KEY_ESC,    '~', KEY_TAB,       KEY_LEFT_SHIFT, KEY_LEFT_CTRL }
};
byte colPins[COLS] = { PB4, PB5, PC13, PC14, PB8 };                //connect to the row pinouts of the kpd (i have them swapped)
byte rowPins[ROWS] = { PB1, PB10, PB11, PB12, PB13, PB14, PB15 };  //connect to the column pinouts of the kpd

Keypad kpd = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
unsigned long startTime;

void setup() {
pinMode(BT_RST_PIN, OUTPUT);
pinMode(BT_KEY_PIN, OUTPUT);
pinMode(BT_LINK_PIN, INPUT);
pinMode(BT_WORK_PIN, INPUT);

digitalWrite(BT_KEY_PIN, HIGH);
digitalWrite(BT_RST_PIN, HIGH);
digitalWrite(BT_RST_PIN, LOW);
delay(200);
digitalWrite(BT_RST_PIN, HIGH);





  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.clear();
  FastLED.setBrightness(BRIGHTNESS);
  //Serial.begin(9600);
  mySerial1.begin(9600);

  Keyboard.begin();
  startTime = millis();
  delay(10);
  mySerial1.println("AT");
  Keyboard.write(mySerial1.read());
}

void loop() {
  // https://docs.arduino.cc/language-reference/en/functions/usb/Keyboard/
  // https://docs.arduino.cc/language-reference/en/functions/usb/Keyboard/keyboardModifiers/?_gl=1*cm6e0a*_up*MQ..*_ga*MTYyODU5NDU5MC4xNzYwNTY4MjY4*_ga_NEXN8H46L5*czE3NjA1NjgyNjckbzEkZzEkdDE3NjA1NjgzMzUkajYwJGwwJGgxNjk2ODkxMTI1
  // getKeys - fills kpd.key[ ] array with up-to 10 active keys.
  // Returns true if there are ANY active keys.
  if (hello_flag) {
    sweepRainbow(false);
    sweepRainbow(true);
    hello_flag = false;
  }

  if (kpd.getKeys() && !hello_flag) {
    for (int i = 0; i < LIST_MAX; i++)  // Scan the whole key list.
    {
      if (kpd.key[i].stateChanged)  // Only find keys that have changed state.
      {
        // our rows are cols and vice versa, hence recalculate led pos in strip
        int k = kpd.key[i].kcode;
        int c = k % COLS;
        int r = k / COLS;
        int cc = ROWS - 1 - r;
        int rr = c;
        int ledIdx = cc + rr * ROWS;
        //Serial.println(ledIdx);
        switch (kpd.key[i].kstate) {  // Report active key state : IDLE, PRESSED, HOLD, or RELEASED
          case PRESSED:
            //Keyboard.write(kpd.key[i].kchar);
            Keyboard.press(kpd.key[i].kchar);
            leds[ledIdx].setRGB(random(10, 255), random(10, 255), random(10, 255));
            break;
          case HOLD:
            break;
          case RELEASED:
            Keyboard.release(kpd.key[i].kchar);
            leds[ledIdx].setRGB(15, random(10, 30), 15);
            //FastLED.clear();
            break;
          case IDLE:
            //
            break;
        }
      }
    }
  }

  if ((millis() - startTime) > 50) {
    FastLED.show();  // display this frame
    startTime = millis();
  }
}  // End loop






void sweepRainbow(bool clear) {
  // Sweep from top-left to bottom-right
  for (int step = 0; step < COLS * ROWS - 1; step++) {
    // Clear all LEDs
    //fill_solid(leds, NUM_LEDS, CRGB::Black);
    // Light up LEDs along the current diagonal
    for (int row = 0; row < COLS; row++) {
      int col = step - row;
      if (col >= 0 && col < ROWS) {
        int ledIndex = row * ROWS + col;

        // Calculate rainbow color based on position
        uint8_t hue = (map(step, 0, COLS * ROWS - 1 - 1, 0, 900) + 0) % 256;
        if (clear) {
          leds[ledIndex] = CHSV(hue, 0, 0);
        } else {
          leds[ledIndex] = CHSV(hue, 255, 200);
        }
      }
    }
    FastLED.show();
    delay(100);  // Adjust speed of sweep
  }
  // Optional: Clear at the end
  //fill_solid(leds, NUM_LEDS, CRGB::Black);
  //FastLED.show();
}