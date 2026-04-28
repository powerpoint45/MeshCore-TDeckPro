#include "GxEPDDisplay.h"
#include <SPI.h>

#ifdef EXP_PIN_BACKLIGHT
  #include <PCA9557.h>
  extern PCA9557 expander;
#endif

// Set default rotation for T-Deck Pro (Landscape)
#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 1 
#endif

#ifdef ESP32
  // MeshCore standard SPI bus for ESP32-S3
  SPIClass SPI1 = SPIClass(FSPI);
#endif

/**
 * Hardware Initialization for T-Deck Pro
 */

bool GxEPDDisplay::begin() {
  // 1. Force Reset Pin (High -> Low -> High)
  pinMode(PIN_DISPLAY_RST, OUTPUT);
  digitalWrite(PIN_DISPLAY_RST, HIGH);
  delay(20);
  digitalWrite(PIN_DISPLAY_RST, LOW);
  delay(100); 
  digitalWrite(PIN_DISPLAY_RST, HIGH);
  delay(100);

  // 2. T-Deck Pro Specific: Set CS High and Busy as Input
  pinMode(34, OUTPUT); digitalWrite(34, HIGH); // CS
  pinMode(PIN_DISPLAY_BUSY, INPUT_PULLUP);    // Critical: Pro needs pullup

  // 3. Init SPI with Pro Pins
  SPI.begin(36, 47, 33, 34); 

  // 4. Initialize library with NO-WAIT mode
  // The 'false' for the 4th param prevents long blocking during init
  display.init(115200, true, 2, false);
  
  display.setRotation(0);
  display.setFullWindow();

  _init = true;
  return true;
}

void GxEPDDisplay::turnOn() {
  if (!_init) begin();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, HIGH);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, HIGH);
#endif
  _isOn = true;
}



void GxEPDDisplay::turnOff() {
  Serial.println("turnOff");
  clear();
  endFrame();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, LOW);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, LOW);
#endif
  _isOn = false;
}

void GxEPDDisplay::clear() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display_crc.reset();
}

void GxEPDDisplay::startFrame(Color bkg) {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(_curr_color = GxEPD_BLACK);
  display_crc.reset();
}

void GxEPDDisplay::setTextSize(int sz) {
  display_crc.update<int>(sz);
  switch(sz) {
    case 1:
      display.setFont(&FreeSans9pt7b);
      break;
    case 2:
      display.setFont(&FreeSansBold12pt7b);
      break;
    case 3:
      display.setFont(&FreeSans18pt7b);
      break;
    default:
      display.setFont(&FreeSans9pt7b);
      break;
  }
}

void GxEPDDisplay::setColor(Color c) {
  display_crc.update<Color>(c);
  // Dark color translates to White on black-and-white E-ink text
  if (c == DARK) {
    display.setTextColor(_curr_color = GxEPD_WHITE);
  } else {
    display.setTextColor(_curr_color = GxEPD_BLACK);
  }
}

void GxEPDDisplay::setCursor(int x, int y) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  // Uses scale and offset variables from your header
  display.setCursor((x + offset_x) * scale_x, (y + offset_y) * scale_y);
}

void GxEPDDisplay::print(const char* str) {
  display_crc.update<char>(str, strlen(str));
  display.print(str);
}

void GxEPDDisplay::fillRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.fillRect(x * scale_x, y * scale_y, w * scale_x, h * scale_y, _curr_color);
}

void GxEPDDisplay::drawRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.drawRect(x * scale_x, y * scale_y, w * scale_x, h * scale_y, _curr_color);
}

void GxEPDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display_crc.update<uint8_t>(bits, (w * h) / 8);

  uint16_t startX = x * scale_x;
  uint16_t startY = y * scale_y;
  uint16_t widthInBytes = (w + 7) / 8;
  
  for (uint16_t by = 0; by < h; by++) {
    int y1 = startY + (int)(by * scale_y);
    int y2 = startY + (int)((by + 1) * scale_y);
    int block_h = y2 - y1;
    for (uint16_t bx = 0; bx < w; bx++) {
      int x1 = startX + (int)(bx * scale_x);
      int x2 = startX + (int)((bx + 1) * scale_x);
      int block_w = x2 - x1;
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;
      if (bitSet) {
        display.fillRect(x1, y1, block_w, block_h, _curr_color);
      }
    }
  }
}

uint16_t GxEPDDisplay::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return (uint16_t)ceil((w + 1) / scale_x);
}

/**
 * CRC Finalization
 * This prevents the display from refreshing if nothing has changed.
 */
void GxEPDDisplay::endFrame() {
  uint32_t crc = display_crc.finalize();

  if (crc != (uint32_t)last_display_crc_value) {
    // Only update if not busy, otherwise we skip this frame 
    // to keep the keypad responsive
    if (digitalRead(PIN_DISPLAY_BUSY) == HIGH) { // HIGH usually means IDLE for this screen
      display.display(false); // partial update
      last_display_crc_value = (int)crc;
    }
  }
}