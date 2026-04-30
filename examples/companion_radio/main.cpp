#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include <Mesh.h>
#include "MyMesh.h"
#include "target.h"

#include <Adafruit_TCA8418.h>

#if defined(ESP32)
  #include <SPIFFS.h>
  #include <FS.h>
#endif


#include <helpers/ArduinoHelpers.h>


#include <TouchDrv.hpp>

TouchDrvCSTXXX touch;
bool hasTouch = false;


#define BOARD_GPS_EN  39
#define BOARD_1V8_EN  38
#define BOARD_GPS_RXD 44
#define BOARD_GPS_TXD 43

#if defined(ESP32)
  #include <SPIFFS.h>
  #include <FS.h>
  #include <helpers/ArduinoHelpers.h>

  ESP32RTCClock rtc_clock;
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <WiFi.h>
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#else
  #error "This main.cpp is intended for ESP32 / ESP32-S3"
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(
  radio_driver,
  fast_rng,
  rtc_clock,
  tables,
  store
#ifdef DISPLAY_CLASS
  , &ui_task
#endif
);

Adafruit_TCA8418 keypad;

static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

void halt() {
  Serial.println("SYSTEM HALTED");
  while (true) {
    delay(1000);
  }
}

static void initPowerAndSPI() {
#ifdef PIN_PERF_POWERON
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);
#endif

#ifdef PIN_LORA_EN
  pinMode(PIN_LORA_EN, OUTPUT);
  digitalWrite(PIN_LORA_EN, HIGH);
#endif

#ifdef PIN_DISPLAY_CS
  pinMode(PIN_DISPLAY_CS, OUTPUT);
  digitalWrite(PIN_DISPLAY_CS, HIGH);
#endif

#ifdef PIN_SD_CS
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
#endif

#ifdef P_LORA_NSS
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
#endif

#ifdef P_LORA_RESET
  pinMode(P_LORA_RESET, OUTPUT);
  digitalWrite(P_LORA_RESET, HIGH);
#endif

  delay(100);
}


static void keyboardLight(int hilo) {
    #ifdef PIN_KB_LED
      pinMode(PIN_KB_LED, OUTPUT);
      digitalWrite(PIN_KB_LED, hilo);
    #endif

}


static void initKeyboard() {

  if (keypad.begin(0x34, &Wire)) {
      keypad.matrix(4, 10);
      keypad.flush();
      Serial.println("LOG: Keypad OK");
  } else {
    Serial.println("LOG: Keypad FAIL");
  }
}

static void initTouch() {
#ifdef PIN_TOUCH_INT
  pinMode(PIN_TOUCH_INT, INPUT);
#endif

#ifdef PIN_TOUCH_RST
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(30);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(50);
#endif

  touch.setPins(PIN_TOUCH_RST, PIN_TOUCH_INT);

  hasTouch = touch.begin(
    Wire,
    TOUCH_ADDR,
    PIN_I2C_SDA,
    PIN_I2C_SCL
  );

  if (hasTouch) {
    Serial.print("LOG: Touch OK model=");
    Serial.println(touch.getModelName());

    touch.setCenterButtonCoordinate(85, 360);
  } else {
    Serial.println("LOG: Touch FAIL");
  }
}

static void initI2C() {
  static bool i2c_done = false;
  if (!i2c_done) {
    // T-Deck Pro uses 13 (SDA) and 14 (SCL)
    if (Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000)) {
      i2c_done = true;
      Serial.println("LOG: I2C Master Initialized");
    }
  }
}

static void printRadioPrefs() {
  NodePrefs* p = the_mesh.getNodePrefs();

  Serial.println("LOG: Mesh radio prefs:");
  Serial.printf("  name: %s\n", p->node_name);
  Serial.printf("  freq: %.3f\n", p->freq);
  Serial.printf("  bw: %.2f\n", p->bw);
  Serial.printf("  sf: %d\n", p->sf);
  Serial.printf("  cr: %d\n", p->cr);
  Serial.printf("  tx: %d dBm\n", p->tx_power_dbm);
}

static void testAdvert() {
  Serial.println("LOG: Manual advert test...");
  bool ok = the_mesh.advert();

  if (ok) {
    Serial.println("LOG: Advert sent OK");
#ifdef DISPLAY_CLASS
    ui_task.showAlert("Advert sent!", 1000);
#endif
  } else {
    Serial.println("LOG: Advert FAILED");
    Serial.println("LOG: Check radio prefs, channel busy, BUSY/DIO1 pins, and PIN_LORA_EN.");
#ifdef DISPLAY_CLASS
    ui_task.showAlert("Advert failed", 1000);
#endif
  }
}

static void initGPSHardwareFromPrefs() {
#if ENV_INCLUDE_GPS == 1
  bool gps_on = the_mesh.getNodePrefs()->gps_enabled == 1;

#ifdef PIN_GPS_EN
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, gps_on ? HIGH : LOW);
#endif

  if (gps_on) {
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, HIGH);
    delay(50);

    Serial2.end();
    Serial2.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

    Serial.printf("LOG: GPS ON RX=%d TX=%d\n", PIN_GPS_RX, PIN_GPS_TX);
  } else {
    Serial.println("LOG: GPS OFF by prefs");
  }
#endif
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("--- T-DECK PRO MESHCORE BOOT ---");

  // Isolate shared SPI devices immediately
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);

#ifdef PIN_DISPLAY_CS
  pinMode(PIN_DISPLAY_CS, OUTPUT);
  digitalWrite(PIN_DISPLAY_CS, HIGH);
#endif

#ifdef PIN_SD_CS
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
#endif

  initI2C();
  initPowerAndSPI();
  initKeyboard();
  initTouch();

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;

#ifdef PIN_DISPLAY_RST
  pinMode(PIN_DISPLAY_RST, OUTPUT);
  digitalWrite(PIN_DISPLAY_RST, LOW);
  delay(100);
  digitalWrite(PIN_DISPLAY_RST, HIGH);
  delay(100);
#endif

  if (display.begin()) {
    disp = &display;
    disp->startFrame();
    disp->drawTextCentered(disp->width() / 2, 28, "Mesh Loading...");
    disp->endFrame();
    Serial.println("LOG: Display OK");
  } else {
    Serial.println("LOG: Display FAIL");
  }
#endif

  Serial.println("LOG: Starting radio_init()...");
  if (!radio_init()) {
    Serial.println("LOG: RADIO INIT FAILED");
    halt();
  }

  Serial.println("LOG: Radio OK");

  uint32_t seed = radio_get_rng_seed();
  Serial.printf("LOG: RNG seed: 0x%08lx\n", (unsigned long)seed);
  fast_rng.begin(seed);

  if (!SPIFFS.begin(true)) {
    Serial.println("LOG: SPIFFS FAIL");
    halt();
  }

  store.begin();

  the_mesh.begin(
#ifdef DISPLAY_CLASS
    disp != NULL
#else
    false
#endif
  );

#ifdef WIFI_SSID
  board.setInhibitSleep(true);
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(
#ifdef BLE_NAME_PREFIX
    BLE_NAME_PREFIX,
#else
    "T-Deck-Pro",
#endif
    the_mesh.getNodePrefs()->node_name,
    the_mesh.getBLEPin()
  );
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif


the_mesh.startInterface(serial_interface);
initGPSHardwareFromPrefs();
sensors.begin();

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
#endif

  printRadioPrefs();

  Serial.println("--- BOOT COMPLETE ---");
  Serial.println("Type 'a' in serial monitor to test advert TX.");
}


static void pollTouch() {
#ifdef DISPLAY_CLASS
  if (!hasTouch) return;

  static int16_t x[5];
  static int16_t y[5];

  static bool touchDown = false;
  static bool waitForFreshTouch = false;

  static int16_t startX = 0, startY = 0;
  static int16_t lastX = 0, lastY = 0;
  static int16_t stableX = 0, stableY = 0;
  static int16_t releasedX = -9999, releasedY = -9999;

  static uint32_t touchStartMs = 0;
  static uint32_t lastMoveMs = 0;
  static uint32_t lastPollMs = 0;
  static uint32_t lastGestureMs = 0;

  if (millis() - lastPollMs < 10) return;
  lastPollMs = millis();

  uint8_t maxPoints = touch.getSupportTouchPoint();
  if (maxPoints > 5) maxPoints = 5;
  if (maxPoints == 0) maxPoints = 1;

  uint8_t touched = touch.getPoint(x, y, maxPoints);
  if (!touched) {
    waitForFreshTouch = false;
    return;
  }

  int16_t cx = x[0];
  int16_t cy = y[0];

  // Ignore stale repeated coordinate after synthetic release.
  if (waitForFreshTouch) {
    if (abs(cx - releasedX) < 8 && abs(cy - releasedY) < 8) {
      return;
    }
    waitForFreshTouch = false;
  }

  if (!touchDown) {
    touchDown = true;
    startX = cx;
    startY = cy;
    lastX = cx;
    lastY = cy;
    stableX = cx;
    stableY = cy;
    touchStartMs = millis();
    lastMoveMs = millis();

    Serial.printf("TOUCH DOWN x=%d y=%d points=%u\n", cx, cy, touched);
  } else {
    if (abs(cx - stableX) > 3 || abs(cy - stableY) > 3) {
      stableX = cx;
      stableY = cy;
      lastMoveMs = millis();

      Serial.printf("TOUCH MOVE x=%d y=%d points=%u\n", cx, cy, touched);
    }

    lastX = cx;
    lastY = cy;
  }

  if (touchDown && millis() - lastMoveMs > 180) {
    touchDown = false;
    waitForFreshTouch = true;
    releasedX = lastX;
    releasedY = lastY;

    int dx = lastX - startX;
    int dy = lastY - startY;
    uint32_t dt = millis() - touchStartMs;

    Serial.printf("TOUCH UP dx=%d dy=%d dt=%lu\n", dx, dy, (unsigned long)dt);

    if (millis() - lastGestureMs > 250) {
      lastGestureMs = millis();

      const int SWIPE_MIN = 45;
      const int TAP_MAX = 25;
      const int TAP_TIME_MAX = 800;

      if (abs(dx) >= SWIPE_MIN && abs(dx) > abs(dy)) {
        // Horizontal swipe → page navigation
        ui_task.handleInput(dx > 0 ? KEY_PREV : KEY_NEXT);
      } else if (abs(dy) >= SWIPE_MIN && abs(dy) > abs(dx)) {
        // Vertical swipe → list navigation (up = 'w', down = 's')
        ui_task.handleInput(dy > 0 ? 's' : 'w');
      } else if (abs(dx) <= TAP_MAX && abs(dy) <= TAP_MAX && dt <= TAP_TIME_MAX) {
        if (display.isOn()){
            ui_task.handleInput(KEY_ENTER);
        }
      }
    }
  }
#endif
}




void loop() {
  the_mesh.loop();

  // while (Serial.available()) {
  //   char c = Serial.read();

  //   Serial.write(c);
  // }

  if (keypad.available()) {
    int k = keypad.getEvent();

  

    if (k & 0x80) {
      
      uint8_t c = 0;
      uint8_t key = k & 0x7F;
      Serial.printf("PRESSED KEY: %u\n", key);

      switch (key) {
        case 10: c = 'q'; break;
        case 9:  c = 'w'; break;
        case 8:  c = 'e'; break;
        case 7:  c = 'r'; break;
        case 6:  c = 't'; break;
        case 5:  c = 'y'; break;
        case 4:  c = 'u'; break;
        case 3:  c = 'i'; break;
        case 2:  c = 'o'; break;
        case 1:  c = 'p'; break;

        case 20: c = 'a'; break;
        case 19: c = 's'; break;
        case 18: c = 'd'; break;
        case 17: c = 'f'; break;
        case 16: c = 'g'; break;
        case 15: c = 'h'; break;
        case 14: c = 'j'; break;
        case 13: c = 'k'; break;
        case 12: c = 'l'; break;
        case 11: c = 8; break;          // backspace

        case 30: c = KEY_PREV; break;   // alt/back
        case 29: c = 'z'; break;
        case 28: c = 'x'; break;
        case 27: c = 'c'; break;
        case 26: c = 'v'; break;
        case 25: c = 'b'; break;
        case 24: c = 'n'; break;
        case 23: c = 'm'; break;
        case 22: c = '$'; break;
        case 21: c = KEY_ENTER; break;

        case 35: c = 0; break;          // shift, handled later if needed
        case 34: c = 0; break;          // microphone
        case 33: c = ' '; break;        // spacebar
        case 32: c = 0; break;          // sym
        case 31: c = 0; break;          // shift

        default:
          // If already a printable char or existing UI key, keep it.
          break;
      }

      if (c == 0) return;


      if (!ui_task.inChannelChat() && ! ui_task.inContactChat()){
        if (key == 21) {
          ui_task.handleInput(KEY_ENTER);
        } else if (key == 3 || key == 20) {
          ui_task.handleInput(KEY_PREV);
        } else if (key == 2 || key == 18) {
          ui_task.handleInput(KEY_NEXT);
        } else {
          ui_task.handleInput(c);
        }
      }else{
        ui_task.handleInput(c);
      }

    }
  }
  sensors.loop();
  pollTouch();
  ui_task.loop();
  rtc_clock.tick();

  delay(50);
}


