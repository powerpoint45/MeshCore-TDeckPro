#pragma once

#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

#define PIN_VBAT_READ 4
#define BATTERY_SAMPLES 8
#define ADC_MULTIPLIER (2.0f * 3.3f * 1000)

class TDeckBoard : public ESP32Board {
public:
  void begin();

  #ifdef P_LORA_TX_LED
    void onBeforeTransmit() override{
      digitalWrite(P_LORA_TX_LED, LOW); // turn TX LED on - invert pin for SX1276
    }

    void onAfterTransmit() override{
      digitalWrite(P_LORA_TX_LED, HIGH); // turn TX LED off - invert pin for SX1276
    }
  #endif

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    #ifdef P_LORA_DIO_1
      rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);
    #endif

    #ifdef P_LORA_NSS
      rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
    #endif

      uint64_t wake_mask = 0;

    #ifdef P_LORA_DIO_1
      wake_mask |= (1ULL << P_LORA_DIO_1);
    #endif

    if (pin_wake_btn >= 0) {
      wake_mask |= (1ULL << pin_wake_btn);
    }

    if (wake_mask != 0) {
      esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
    }

    delay(100);
    esp_deep_sleep_start();
  }

uint16_t getBattMilliVolts() {
  #if defined(HAS_BQ27220_BATTERY)
    const uint8_t BQ27220_ADDR = 0x55;

    Wire.beginTransmission(BQ27220_ADDR);
    if (Wire.endTransmission() != 0) {
      return 0;
    }

    // BQ27220 Voltage register = 0x08, little endian, mV
    Wire.beginTransmission(BQ27220_ADDR);
    Wire.write(0x08);
    if (Wire.endTransmission(false) != 0) {
      return 0;
    }

    if (Wire.requestFrom(BQ27220_ADDR, (uint8_t)2) != 2) {
      return 0;
    }

    uint16_t mv = Wire.read();
    mv |= ((uint16_t)Wire.read()) << 8;

    if (mv < 2500 || mv > 5000) {
      return 0;
    }

    return mv;
  #else
    return 0;
  #endif
}

  const char* getManufacturerName() const{
    return "LilyGo T-Deck";
  }
};