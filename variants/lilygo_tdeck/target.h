#pragma once

#define RADIOLIB_STATIC_ONLY 1

#include <Arduino.h>
#include <Wire.h>
#include <RadioLib.h>

#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>

#include <TDeckBoard.h>
#include <helpers/SensorManager.h>
#include <helpers/ui/MomentaryButton.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

#ifdef DISPLAY_CLASS
  #include <helpers/ui/GxEPDDisplay.h>
#endif

extern TDeckBoard board;
extern RADIO_CLASS radio;
extern WRAPPER_CLASS radio_driver;
extern ESP32RTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();