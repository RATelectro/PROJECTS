#include <FastLED.h>

#define DATA_PIN  4
#define NUM_LEDS  35
#define BRIGHTNESS 20

//#define FASTLED_FORCE_SOFTWARE_SPI

CRGB leds[NUM_LEDS];
uint8_t hue = 0;

void setup() {

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.clear();
  FastLED.setBrightness(BRIGHTNESS);
  fill_rainbow(leds, NUM_LEDS, 128, 7);
  FastLED.show();
}

void loop() {
  //fill_rainbow(leds, NUM_LEDS, hue, 7);
  FastLED.show();
  //hue++;
  delay(50);
}