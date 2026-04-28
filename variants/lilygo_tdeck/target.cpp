#include <Arduino.h>
#include <SPI.h>
#include "target.h"

TDeckBoard board;

/* =======================
   RADIO INSTANCE
   ONE Module, ONE radio, ONE wrapper
   ======================= */

static Module lora_module(
  P_LORA_NSS,
  P_LORA_DIO_1,
  P_LORA_RESET,
  P_LORA_BUSY
);

RADIO_CLASS radio(&lora_module);
WRAPPER_CLASS radio_driver(radio, board);

/* =======================
   SYSTEM COMPONENTS
   ======================= */

MicroNMEALocationProvider gps(Serial2, &rtc_clock);
EnvironmentSensorManager sensors(gps);


#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

/* =======================
   RADIO INIT
   ======================= */

bool radio_init() {
  Serial.println("radio_init(): T-Deck Pro SX1262");

  

#ifdef PIN_PERF_POWERON
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);
  delay(50);
#endif

#ifdef PIN_LORA_EN
  pinMode(PIN_LORA_EN, OUTPUT);
  digitalWrite(PIN_LORA_EN, HIGH);
  delay(100);
#endif

#ifdef PIN_DISPLAY_CS
  pinMode(PIN_DISPLAY_CS, OUTPUT);
  digitalWrite(PIN_DISPLAY_CS, HIGH);
#endif

#ifdef DISP_CS
  pinMode(DISP_CS, OUTPUT);
  digitalWrite(DISP_CS, HIGH);
#endif

#ifdef BOARD_SD_CS
  pinMode(BOARD_SD_CS, OUTPUT);
  digitalWrite(BOARD_SD_CS, HIGH);
#endif

#ifdef PIN_SD_CS
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
#endif

  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);

#ifdef P_LORA_BUSY
  pinMode(P_LORA_BUSY, INPUT);
#endif

#ifdef P_LORA_DIO_1
  pinMode(P_LORA_DIO_1, INPUT);
#endif

#if defined(P_LORA_SCLK)
  SPI.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  SPI.setFrequency(4000000);
#endif

#ifdef P_LORA_RESET
  pinMode(P_LORA_RESET, OUTPUT);
  digitalWrite(P_LORA_RESET, LOW);
  delay(20);
  digitalWrite(P_LORA_RESET, HIGH);
  delay(100);
#endif

  int state = radio.begin(
    915.0,
    125.0,
    9,
    7,
    RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
    22,
    16,
    1.8,
    false
  );

  Serial.printf("radio.begin() = %d\n", state);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("ERROR: radio init failed: %d\n", state);
    return false;
  }

  state = radio.setCRC(1);
  Serial.printf("radio.setCRC(1) = %d\n", state);
  if (state != RADIOLIB_ERR_NONE) return false;

  state = radio.setCurrentLimit(140);
  Serial.printf("radio.setCurrentLimit(140) = %d\n", state);
  if (state != RADIOLIB_ERR_NONE) return false;

  state = radio.setRxBoostedGainMode(true);
  Serial.printf("radio.setRxBoostedGainMode(true) = %d\n", state);
  if (state != RADIOLIB_ERR_NONE) return false;

#ifdef SX126X_DIO2_AS_RF_SWITCH
  state = radio.setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
  Serial.printf("radio.setDio2AsRfSwitch(%d) = %d\n", SX126X_DIO2_AS_RF_SWITCH, state);
  if (state != RADIOLIB_ERR_NONE) return false;
#endif

#if defined(SX126X_RXEN) || defined(SX126X_TXEN)
#ifndef SX126X_RXEN
#define SX126X_RXEN RADIOLIB_NC
#endif
#ifndef SX126X_TXEN
#define SX126X_TXEN RADIOLIB_NC
#endif

  state = radio.setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
  Serial.printf("radio.setRfSwitchPins() = %d\n", state);
  if (state != RADIOLIB_ERR_NONE) return false;
#endif

#ifdef SX126X_REGISTER_PATCH
  uint8_t r_data = 0;
  radio.readRegister(0x8B5, &r_data, 1);
  r_data |= 0x01;
  radio.writeRegister(0x8B5, &r_data, 1);
#endif

  return true;
}

/* =======================
   RADIO HELPERS
   ======================= */

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  Serial.println("Applying radio params:");

  int state;

  state = radio.setFrequency(freq);
  Serial.printf("  freq=%.6f -> %d\n", freq, state);

  state = radio.setSpreadingFactor(sf);
  Serial.printf("  sf=%u -> %d\n", sf, state);

  state = radio.setBandwidth(bw);
  Serial.printf("  bw=%.2f -> %d\n", bw, state);

  state = radio.setCodingRate(cr);
  Serial.printf("  cr=%u -> %d\n", cr, state);
}

void radio_set_tx_power(int8_t dbm) {
  int state = radio.setOutputPower(dbm);
  Serial.printf("TX power=%d -> %d\n", dbm, state);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}