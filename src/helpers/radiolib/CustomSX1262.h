#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#define SX126X_IRQ_HEADER_VALID      0b0000010000
#define SX126X_IRQ_PREAMBLE_DETECTED 0x04

class CustomSX1262 : public SX1262 {
  public:
    CustomSX1262(Module *mod) : SX1262(mod) { }

  #ifdef RP2040_PLATFORM
    bool std_init(SPIClassRP2040* spi = NULL)
  #else
    bool std_init(SPIClass* spi = NULL)
  #endif
    {
      // SPI is already started in target.cpp
      (void)spi;

      Serial.println("[SX1262] std_init start");

      int status = begin(
        868.0,   // frequency
        125.0,   // bandwidth
        9,       // spreading factor
        7,       // coding rate denominator
        0x34,    // sync word
        22,      // tx power
        8,       // preamble length
        1.8,     // TCXO voltage
        false    // useRegulatorLDO
      );

      Serial.print("[SX1262] begin state = ");
      Serial.println(status);

      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;
      }

      status = setCRC(1);
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("[SX1262] setCRC failed: ");
        Serial.println(status);
        return false;
      }

      status = setCurrentLimit(140);
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("[SX1262] setCurrentLimit failed: ");
        Serial.println(status);
        return false;
      }

      status = setRxBoostedGainMode(true);
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("[SX1262] setRxBoostedGainMode failed: ");
        Serial.println(status);
        return false;
      }

  #ifdef SX126X_DIO2_AS_RF_SWITCH
      status = setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("[SX1262] setDio2AsRfSwitch failed: ");
        Serial.println(status);
        return false;
      }
  #endif

  #if defined(SX126X_RXEN) || defined(SX126X_TXEN)
    #ifndef SX126X_RXEN
      #define SX126X_RXEN RADIOLIB_NC
    #endif
    #ifndef SX126X_TXEN
      #define SX126X_TXEN RADIOLIB_NC
    #endif
      status = setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("[SX1262] setRfSwitchPins failed: ");
        Serial.println(status);
        return false;
      }
  #endif

  #ifdef SX126X_REGISTER_PATCH
      uint8_t r_data = 0;
      readRegister(0x8B5, &r_data, 1);
      r_data |= 0x01;
      writeRegister(0x8B5, &r_data, 1);
  #endif

      Serial.println("[SX1262] std_init OK");
      return true;
    }

    bool isReceiving() {
      uint16_t irq = getIrqFlags();
      bool detected = (irq & SX126X_IRQ_HEADER_VALID) || (irq & SX126X_IRQ_PREAMBLE_DETECTED);
      return detected;
    }

    bool getRxBoostedGainMode() {
      uint8_t rxGain = 0;
      readRegister(RADIOLIB_SX126X_REG_RX_GAIN, &rxGain, 1);
      return (rxGain == RADIOLIB_SX126X_RX_GAIN_BOOSTED);
    }
};