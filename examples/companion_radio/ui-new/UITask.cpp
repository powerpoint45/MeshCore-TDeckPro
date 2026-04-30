#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"

struct UiChatMsg {
  char channel[32];
  char from[32];
  char text[96];
  uint32_t timestamp;
};

#define UI_CHAT_HISTORY_MAX 12

bool _in_contact_chat = false;

static UiChatMsg ui_chat_history[UI_CHAT_HISTORY_MAX];
static int ui_chat_history_count = 0;

void UITask::handleInput(char c) {
  uint8_t raw = (uint8_t)c;

  if (raw == 21) c = KEY_ENTER;
  else if (raw == 11) c = 8;
  else if (raw == 33) c = ' ';
  

  if (_display != NULL && !_ui_display_on) {
    Serial.println("Turn on display");
    _display->turnOn();
    _ui_display_on = true;
    keyboardLight(HIGH);
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
    // do not return; process the key below
  }

  if (_display != NULL && _ui_display_on && curr != NULL) {
    if (curr->handleInput(c)) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 100;
    }
  }
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 22, _version_info);

    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 42, FIRMWARE_BUILD_DATE);

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }

};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    CONTACTS,
    CHANNELS,
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    SHUTDOWN,
    Count    // keep as last
  };

  int8_t active_contact_index = -1;
  uint8_t active_contact_pubkey[PUB_KEY_SIZE];

  uint8_t selected_contact_row = 0;
  uint8_t visible_contact_count = 0;
  uint32_t contact_pressed_until = 0;

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];

  uint8_t selected_channel_row = 0;
  int8_t selected_channel_index = -1;
  int8_t channel_rows[UI_RECENT_LIST_SIZE];
  uint8_t visible_channel_count = 0;

  int8_t pressed_row = -1;
  uint32_t pressed_until = 0;

  int batteryPercent(float v) {
    if (v <= 3.3) return 0;
    if (v >= 4.2) return 100;

    return (v - 3.3) * 100 / (4.2 - 3.3);
}


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {

    if (batteryMilliVolts == 0) {
      display.setColor(DisplayDriver::RED);
      display.drawTextRightAlign(display.width() - 1, 0, "BAT ?");
      return;
    }
    // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    const int minMilliVolts = BATT_MIN_MILLIVOLTS;
    const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
    int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
    if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
    if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(DisplayDriver::GREEN);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // show muted icon if buzzer is muted
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::RED);
      display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;
  
  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0), 
       _shutdown_init(false), sensors_lpp(200) {  }

  void poll() override {
  if (_shutdown_init && !_task->isButtonPressed()) {
      _shutdown_init = false;   // ← THIS LINE STOPS THE LOOP
      _task->shutdown();
    }
  }

  int8_t active_chat_channel = -1;
  char active_chat_name[32] = "";
  char chat_input[96] = "";
  uint8_t chat_input_len = 0;

  int render(DisplayDriver& display) override {
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 0);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

if (_task->inChannelChat() || _task->inContactChat()) {

    display.setColor(DisplayDriver::YELLOW);
    display.drawRect(0, display.height() - 17, display.width() - 1, 16);

    char input_line[110];
    snprintf(input_line, sizeof(input_line), "> %s", chat_input);

    display.setColor(DisplayDriver::LIGHT);
    display.drawTextEllipsized(
      8,
      display.height() - 100,
      display.width() - 50,
      input_line
    );
    
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);

    char title[48];
    if (_task->inChannelChat()) {
        snprintf(title, sizeof(title), "# %s", active_chat_name);
    } else {
        snprintf(title, sizeof(title), "@ %s", active_chat_name);  // @ for direct message
    }
    
    display.drawTextCentered(display.width() / 2, 18, title);

    int y = 32;
    int shown = 0;

    for (int i = ui_chat_history_count - 1; i >= 0 && shown < UI_RECENT_LIST_SIZE; i--){
      if (strcmp(ui_chat_history[i].channel, active_chat_name) != 0) continue;
      // TEMP: show all messages until channel routing exists

      char line[96];
      snprintf(line, sizeof(line), "%.10s: %.60s",
          ui_chat_history[i].from,
          ui_chat_history[i].text);

      display.setColor(DisplayDriver::LIGHT);
      display.drawTextEllipsized(0, y, display.width() - 1, line);
      y += 14;
      shown++;
    }

    if (shown == 0) {
      display.setColor(DisplayDriver::YELLOW);
      display.drawTextCentered(display.width() / 2, 46, "No messages yet");
    }

    return 1000;
  }

  if (_page == HomePage::FIRST) {
        display.setColor(DisplayDriver::YELLOW);
        display.setTextSize(2);

        // --- MSG COUNT ---
        sprintf(tmp, "MSG: %d", _task->getMsgCount());
        display.drawTextCentered(display.width() / 2, 25, tmp);

        // --- RTC TIME ---
        display.setTextSize(1);
        if (_rtc != NULL) {
          uint32_t t = _rtc->getCurrentTime();

          int hrs = (t % 86400L) / 3600;
          int mins = (t % 3600) / 60;
          int secs = t % 60;

          sprintf(tmp, "%02d:%02d:%02d", hrs, mins, secs);
          display.setColor(DisplayDriver::LIGHT);
          display.drawTextCentered(display.width() / 2, 100, tmp);
        }

      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.drawTextCentered(display.width() / 2, 54, tmp);
      #endif

        if (_task->hasConnection()) {
          display.setColor(DisplayDriver::GREEN);
          display.drawTextCentered(display.width() / 2, 50, "< Connected >");

        } else if (the_mesh.getBLEPin() != 0) {
          display.setColor(DisplayDriver::RED);
          display.setTextSize(2);
          sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
          display.drawTextCentered(display.width() / 2, 50, tmp);
        }
    }else if (_page == HomePage::CONTACTS) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 18, "Contacts");

      // Get actual contacts from the mesh
      auto iter = the_mesh.startContactsIterator();
      int y = 32;  // Start below the "Contacts" label
      int shown = 0;

      ContactInfo contact;
      while (iter.hasNext(&the_mesh, contact) && shown < UI_RECENT_LIST_SIZE) {
          if (contact.name[0] == 0) continue;

          

          char name[40];
          display.translateUTF8ToBlocks(name, contact.name, sizeof(name));

          if (shown == selected_contact_row) {
              display.setColor(DisplayDriver::YELLOW);
              display.drawTextLeftAlign(0, y, ">");
              display.setColor(millis() < contact_pressed_until ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);
          } else {
              display.setColor(DisplayDriver::LIGHT);
          }
          display.drawTextEllipsized(15, y, display.width() - 12, name);
          y += 20;  // 20px spacing
          shown++;
      }

      if (shown == 0) {
          display.setColor(DisplayDriver::LIGHT);
          display.drawTextCentered(display.width() / 2, 42, "No contacts");
      }

      visible_contact_count = shown;
      return 1000;
} else if (_page == HomePage::CHANNELS) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 18, "Channels");

    int y = 32;
    int shown = 0;

    visible_channel_count = 0;

    for (uint8_t i = 0; i < UI_RECENT_LIST_SIZE; i++) {
      channel_rows[i] = -1;
    }

    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS && shown < UI_RECENT_LIST_SIZE; i++) {
      ChannelDetails channel;

      if (the_mesh.getChannel(i, channel)) {
        channel_rows[shown] = i;
        visible_channel_count = shown + 1;

        char name[40];
        display.translateUTF8ToBlocks(name, channel.name, sizeof(name));

        if (shown == selected_channel_row) {
            display.setColor(DisplayDriver::LIGHT);
            display.drawTextLeftAlign(0, y, ">");
            display.setColor(millis() < pressed_until ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);
        } else {
            display.setColor(DisplayDriver::GREEN);
        }
        display.drawTextEllipsized(15, y, display.width() - 12, name);

        y += 20;  // Changed from 14 to 20
        shown++;
      }
    }

    if (shown == 0) {
      selected_channel_row = 0;
      selected_channel_index = -1;

      display.setColor(DisplayDriver::YELLOW);
      display.drawTextCentered(display.width() / 2, 44, "No channels");

      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, 60, "Use app to add");
    } else {
      if (selected_channel_row >= visible_channel_count) {
        selected_channel_row = visible_channel_count - 1;
      }

      selected_channel_index = channel_rows[selected_channel_row];
    }

    return 1000;
} else if (_page == HomePage::RECENT) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 18, "Recent Adverts");

        AdvertPath recent[UI_RECENT_LIST_SIZE];
        int count = the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);

        int y = 32;
        int shown = 0;
        uint32_t now = _rtc->getCurrentTime();

        for (int i = 0; i < count; i++) {
            if (recent[i].name[0] == 0) continue;

            char name[40];
            display.translateUTF8ToBlocks(name, recent[i].name, sizeof(name));
            
            // Calculate time ago
            uint32_t secs_ago = now - recent[i].recv_timestamp;
            
            char time_str[12];
            if (secs_ago < 60) {
                snprintf(time_str, sizeof(time_str), "%ds", (int)secs_ago);
            } else if (secs_ago < 3600) {
                snprintf(time_str, sizeof(time_str), "%dm", (int)(secs_ago / 60));
            } else {
                snprintf(time_str, sizeof(time_str), "%dh", (int)(secs_ago / 3600));
            }

            char line[50];
            snprintf(line, sizeof(line), "%s (%s, %dhops)", name, time_str, recent[i].path_len);

            display.setColor(DisplayDriver::LIGHT);
            display.drawTextEllipsized(0, y, display.width() - 1, line);
            y += 11;
            shown++;
        }

        if (shown == 0) {
            display.setColor(DisplayDriver::YELLOW);
            display.drawTextCentered(display.width() / 2, 42, "No recent adverts");
        }

        return 1000;
} else if (_page == HomePage::RADIO) {
    display.setColor(DisplayDriver::YELLOW);
    display.setTextSize(1);
    
    int y = 18;  // Start position
    
    // Frequency and Spreading Factor
    display.setCursor(0, y);
    sprintf(tmp, "FQ: %06.3f SF: %d", _node_prefs->freq, _node_prefs->sf);
    display.print(tmp);
    y += 20;

    // Bandwidth and Coding Rate
    display.setCursor(0, y);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    display.print(tmp);
    y += 20;

    // TX Power
    display.setCursor(0, y);
    sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
    display.print(tmp);
    y += 20;

    // Last RSSI and SNR
    display.setCursor(0, y);
    sprintf(tmp, "RSSI: %ddBm SNR: %.1f", 
            (int)radio_driver.getLastRSSI(), 
            radio_driver.getLastSNR());
    display.print(tmp);
    y += 20;

    // Noise floor
    display.setCursor(0, y);
    sprintf(tmp, "Noise: %ddBm", radio_driver.getNoiseFloor());
    display.print(tmp);
    y += 20;

    // Packets sent/received
    display.setCursor(0, y);
    sprintf(tmp, "TX: %lu RX: %lu", 
            (unsigned long)radio_driver.getPacketsSent(),
            (unsigned long)radio_driver.getPacketsRecv());
    display.print(tmp);

    return 5000;
}else if (_page == HomePage::BLUETOOTH) {
    display.setColor(DisplayDriver::GREEN);
    display.drawXbm((display.width() - 32) / 2, 18,
        _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
        32, 32);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    
    return 5000;
    
} else if (_page == HomePage::ADVERT) {
    display.setColor(DisplayDriver::GREEN);
    display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
    display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
    
    return 5000;
    
}else if (_page == HomePage::GPS) {
    LocationProvider* nmea = sensors.getLocationProvider();
    char buf[64];

    int y = 18;
    const int line = 20;   // spacing (was ~11, now cleaner)

    bool gps_state = _task->getGPSState();

    // --- HEADER ---
    display.setTextSize(1);
    display.setColor(gps_state ? DisplayDriver::GREEN : DisplayDriver::RED);
    display.drawTextLeftAlign(0, y, gps_state ? "GPS ON" : "GPS OFF");

    if (nmea == NULL) {
      y += line;
      display.setColor(DisplayDriver::RED);
      display.drawTextLeftAlign(0, y, "No GPS provider");
      return 1000;
    }

    bool valid = nmea->isValid();
    long sats = nmea->satellitesCount();
    long lat  = nmea->getLatitude();
    long lon  = nmea->getLongitude();
    long alt  = nmea->getAltitude();

    // --- FIX STATUS ---
    display.setColor(valid ? DisplayDriver::GREEN : DisplayDriver::YELLOW);
    display.drawTextRightAlign(display.width() - 1, y, valid ? "FIX" : "NO FIX");

    // --- SATELLITES ---
    y += line;
    display.setColor(DisplayDriver::LIGHT);
    snprintf(buf, sizeof(buf), "Sats: %ld", sats);
    display.drawTextLeftAlign(0, y, buf);

    // --- LATITUDE ---
    y += line;
    if (valid) {
      snprintf(buf, sizeof(buf), "Lat: %.6f", lat / 1000000.0);
    } else {
      snprintf(buf, sizeof(buf), "Lat: waiting...");
    }
    display.drawTextLeftAlign(0, y, buf);

    // --- LONGITUDE ---
    y += line;
    if (valid) {
      snprintf(buf, sizeof(buf), "Lon: %.6f", lon / 1000000.0);
    } else {
      snprintf(buf, sizeof(buf), "Lon: waiting...");
    }
    display.drawTextLeftAlign(0, y, buf);

    // --- ALTITUDE / DEBUG ---
    y += line;
    if (valid) {
      snprintf(buf, sizeof(buf), "Alt: %.1fm", alt / 1000.0);
    } else {
      snprintf(buf, sizeof(buf), "Fix pending...");
    }
    display.drawTextLeftAlign(0, y, buf);

    return 1000;
  #if UI_SENSORS_PAGE == 1
      } else if (_page == HomePage::SENSORS) {
        int y = 18;
        refresh_sensors();
        char buf[30];
        char name[30];
        LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

        for (int i = 0; i < sensors_scroll_offset; i++) {
          uint8_t channel, type;
          r.readHeader(channel, type);
          r.skipData(type);
        }

        for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
          uint8_t channel, type;
          if (!r.readHeader(channel, type)) { // reached end, reset
            r.reset();
            r.readHeader(channel, type);
          }

          display.setCursor(0, y);
          float v;
          switch (type) {
            case LPP_GPS: // GPS
              float lat, lon, alt;
              r.readGPS(lat, lon, alt);
              strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
              break;
            case LPP_VOLTAGE:
              r.readVoltage(v);
              strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
              break;
            case LPP_CURRENT:
              r.readCurrent(v);
              strcpy(name, "current"); sprintf(buf, "%.3f", v);
              break;
            case LPP_TEMPERATURE:
              r.readTemperature(v);
              strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
              break;
            case LPP_RELATIVE_HUMIDITY:
              r.readRelativeHumidity(v);
              strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
              break;
            case LPP_BAROMETRIC_PRESSURE:
              r.readPressure(v);
              strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
              break;
            case LPP_ALTITUDE:
              r.readAltitude(v);
              strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
              break;
            case LPP_POWER:
              r.readPower(v);
              strcpy(name, "power"); sprintf(buf, "%6.2f", v);
              break;
            default:
              r.skipData(type);
              strcpy(name, "unk"); sprintf(buf, "");
          }
          display.setCursor(0, y);
          display.print(name);
          display.setCursor(
            display.width()-display.getTextWidth(buf)-1, y
          );
          display.print(buf);
          y = y + 12;
        }
        if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
        else sensors_scroll_offset = 0;
  #endif
      } else if (_page == HomePage::SHUTDOWN) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        if (_shutdown_init) {
          display.drawTextCentered(display.width() / 2, 34, "hibernating...");
        } else {
          display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
          display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
        }
      }
      return 5000;   // next render after 5000 ms
  }

  
  

  public: bool handleInput(char c) override {

    if (_shutdown_init) return true;   // 🔥 ignore all input


    Serial.println("handleInput inner func");
    if (_task->inChannelChat() || _task->inContactChat()) {



      if (c == KEY_PREV || c == KEY_LEFT) {
          _task->setInChannelChat(false);
          _task->setInContactChat(false);  // ← Add this line
          chat_input_len = 0;
          chat_input[0] = 0;
          Serial.println("EXIT CHAT");
          return true;
      }

      // Backspace / delete
      if (c == 8 || c == 127) {
        if (chat_input_len > 0) {
          chat_input_len--;
          chat_input[chat_input_len] = 0;
          Serial.printf("CHAT BACKSPACE: %s\n", chat_input);
        }
        return true;
      }

      if (c == KEY_ENTER || c == KEY_SELECT) {
          if (chat_input_len == 0) {
              Serial.println("CHAT SEND ignored: empty");
              return true;
          }

          uint32_t ts = rtc_clock.getCurrentTime();
          bool ok = false;

          if (_task->inChannelChat()) {
              // Send channel message
              Serial.printf("CHAT SEND CHANNEL=%d text=%s\n",
                            active_chat_channel, chat_input);
              
              ChannelDetails ch;
              if (the_mesh.getChannel(active_chat_channel, ch)) {
                  ok = the_mesh.sendGroupMessage(
                      ts,
                      ch.channel,
                      the_mesh.getNodePrefs()->node_name,
                      chat_input,
                      strlen(chat_input)
                  );
              }
          } else if (_task->inContactChat()) {
              // Send contact message
              Serial.printf("CHAT SEND CONTACT name=%s text=%s\n",
                            active_chat_name, chat_input);
              
              ContactInfo *recipient = the_mesh.lookupContactByPubKey(
                  active_contact_pubkey, 
                  PUB_KEY_SIZE
              );
              
              if (recipient) {
                  uint32_t expected_ack;
                  uint32_t est_timeout;
                  int result = the_mesh.sendMessage(
                      *recipient, 
                      ts, 
                      0,  // attempt = 0
                      chat_input, 
                      expected_ack, 
                      est_timeout
                  );
                  ok = (result != MSG_SEND_FAILED);
              } else {
                  Serial.println("CONTACT SEND FAILED: contact not found");
              }
          }

          if (ok) {
              Serial.println("CHAT SEND OK");

              // add to UI history
              if (ui_chat_history_count >= UI_CHAT_HISTORY_MAX) {
                  for (int i = 1; i < UI_CHAT_HISTORY_MAX; i++) {
                      ui_chat_history[i - 1] = ui_chat_history[i];
                  }
                  ui_chat_history_count = UI_CHAT_HISTORY_MAX - 1;
              }

              UiChatMsg* m = &ui_chat_history[ui_chat_history_count++];
              StrHelper::strncpy(m->channel, active_chat_name, sizeof(m->channel));
              StrHelper::strncpy(m->from, "You", sizeof(m->from));
              StrHelper::strncpy(m->text, chat_input, sizeof(m->text));
              m->timestamp = ts;

              chat_input_len = 0;
              chat_input[0] = 0;

              _task->showAlert("Sent", 600);
          } else {
              Serial.println("CHAT SEND FAILED");
              _task->showAlert("Send fail", 1000);
          }

          return true;
      }

      if (c >= 32 && c <= 126 && chat_input_len < sizeof(chat_input) - 1) {
        chat_input[chat_input_len++] = c;
        chat_input[chat_input_len] = 0;
        Serial.printf("CHAT INPUT: %s\n", chat_input);
        return true;
      }

      return true;
    }





    if (_page == HomePage::CONTACTS) {
    if (visible_contact_count == 0) return true;

    if (c == KEY_ENTER || c == KEY_SELECT) {
        contact_pressed_until = millis() + 300;
        
        // Get the selected contact
        auto iter = the_mesh.startContactsIterator();
        ContactInfo contact;
        int current_row = 0;
        
        while (iter.hasNext(&the_mesh, contact)) {
            if (contact.name[0] == 0) continue;
            
            if (current_row == selected_contact_row) {
                // Found the selected contact - open chat
                Serial.printf("CONTACT SELECTED: row=%u name=%s\n", 
                              selected_contact_row, contact.name);
                
                _task->setInContactChat(true);
                active_contact_index = selected_contact_row;
                StrHelper::strncpy(active_chat_name, contact.name, sizeof(active_chat_name));
                memcpy(active_contact_pubkey, contact.id.pub_key, PUB_KEY_SIZE);
                
                Serial.printf("CONTACT OPEN CHAT row=%u name=%s\n",
                              selected_contact_row, contact.name);
                break;
            }
            current_row++;
        }
        
        return true;
    }

    if (c == 'w' || c == 'W') {
        if (selected_contact_row > 0) {
            selected_contact_row--;
        }
        return true;
    }

    if (c == 's' || c == 'S') {
        if (selected_contact_row < visible_contact_count - 1) {
            selected_contact_row++;
        }
        return true;
    }
}

    // Handle CHANNELS page
    if (_page == HomePage::CHANNELS) {
        int visible_count = visible_channel_count;

        if (visible_count == 0) return true;

        if (visible_count > 0) {
            if (c == KEY_ENTER || c == KEY_SELECT) {
                int8_t idx = channel_rows[selected_channel_row];

                pressed_row = selected_channel_row;
                pressed_until = millis() + 300;

                ChannelDetails channel;
                if (idx >= 0 && the_mesh.getChannel(idx, channel)) {
                    Serial.printf("CHANNEL CLICK row=%u index=%d name=%s\n",
                                  selected_channel_row,
                                  idx,
                                  channel.name);

                    _task->setInChannelChat(true);
                    active_chat_channel = idx;
                    StrHelper::strncpy(active_chat_name, channel.name, sizeof(active_chat_name));

                    Serial.printf("CHANNEL OPEN CHAT row=%u index=%d name=%s\n",
                                  selected_channel_row,
                                  idx,
                                  channel.name);
                }

                return true;
            }

            if (c == 'w' || c == 'W') {
                if (selected_channel_row > 0) {
                    selected_channel_row--;
                }
                return true;
            }

            if (c == 's' || c == 'S') {
                if (selected_channel_row < visible_channel_count - 1) {
                    selected_channel_row++;
                }
                return true;
            }
        }
    }














    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }

    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      return true;
    }

    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isSerialEnabled()) {  // toggle Bluetooth on/off
        _task->disableSerial();
      } else {
        _task->enableSerial();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
    #if ENV_INCLUDE_GPS == 1
        if ((c == KEY_ENTER || c == KEY_SELECT) && _page == HomePage::GPS) {
          _task->toggleGPS();
          return true;
        }
    #endif
    #if UI_SENSORS_PAGE == 1
        if (c == KEY_ENTER && _page == HomePage::SENSORS) {
          _task->toggleGPS();
          next_sensors_refresh=0;
          return true;
        }
    #endif

    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = false;
      _task->shutdown();
      return true;
    }

    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(DisplayDriver::YELLOW);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

  

  keyboardLight(HIGH);

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;
  
if (_display != NULL) {
  _display->turnOn();
  _ui_display_on = true;
  Serial.println("Begin - Turn on display");
}

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  setCurrScreen(msg_preview);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }
}

void UITask::newMsg(uint8_t path_len,
                    const char* channel_name,
                    const char* from_name,
                    const char* text,
                    int msgcount){
  if (ui_chat_history_count >= UI_CHAT_HISTORY_MAX) {
    for (int i = 1; i < UI_CHAT_HISTORY_MAX; i++) {
      ui_chat_history[i - 1] = ui_chat_history[i];
    }
    ui_chat_history_count = UI_CHAT_HISTORY_MAX - 1;
  }

  UiChatMsg* m = &ui_chat_history[ui_chat_history_count++];
  StrHelper::strncpy(m->channel, channel_name, sizeof(m->channel));
  StrHelper::strncpy(m->from, from_name, sizeof(m->from));
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  m->timestamp = rtc_clock.getCurrentTime();
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  setCurrScreen(msg_preview);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  keyboardLight(LOW);

  #ifdef DISPLAY_CLASS
    if (_display != NULL) {
      _display->startFrame();        // clear screen (e-ink)
      _display->endFrame();
      _display->turnOff();           // power down display
      _ui_display_on = false;
    }
  #endif

  #ifdef PIN_BUZZER
    buzzer.shutdown();
    uint32_t t = millis();
    while (buzzer.isPlaying() && (millis() - t) < 2500) {
      buzzer.loop();
    }
  #endif

  // IMPORTANT: stop radio cleanly
  radio_driver.powerOff();
  

  if (restart) {
    _board->reboot();
  } else {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, 0); // maybe 1 depending button
    esp_deep_sleep_start();
    //_board->powerOff();   // deep sleep / off
  }
}


void UITask:: keyboardLight(int hilo) {
    #ifdef PIN_KB_LED
      pinMode(PIN_KB_LED, OUTPUT);
      digitalWrite(PIN_KB_LED, hilo);
    #endif
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    // Toggle screen on/off with single click
    if (_display != NULL) {
      if (_ui_display_on) {
        _display->turnOff();
        _ui_display_on = false;
        keyboardLight(LOW);
      } else {
        _display->turnOn();
        _ui_display_on = true;
        keyboardLight(HIGH);
        _auto_off = millis() + AUTO_OFF_MILLIS;
        _next_refresh = 0;
      }
    }
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = checkDisplayOn(KEY_PREV);  // Navigate left/previous
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
    #if AUTO_OFF_MILLIS > 0
        if (_ui_display_on && millis() > _auto_off) {
            _display->turnOff();
            _ui_display_on = false;
            keyboardLight(LOW);
        }
    #endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {

      // show low battery shutdown alert
      // we should only do this for eink displays, which will persist after power loss
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::RED);
        _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
        _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
        _display->endFrame();
      }

      shutdown();

    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_ui_display_on) {
      Serial.println("Wake display");
      _display->turnOn();
      _ui_display_on = true;
      keyboardLight(HIGH);

      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 0;

      // Do NOT clear c here.
      // Let the same key press also navigate/select.
    } else {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 0;
    }
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {
    the_mesh.enterCLIRescue();
    c = 0;
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

// bool UITask::getGPSState() {
//   if (_sensors != NULL) {
//     int num = _sensors->getNumSettings();
//     for (int i = 0; i < num; i++) {
//       if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
//         return !strcmp(_sensors->getSettingValue(i), "1");
//       }
//     }
//   } 
//   return false;
// }

  bool UITask::getGPSState() {
  #if ENV_INCLUDE_GPS == 1
    if (_node_prefs != NULL) {
      return _node_prefs->gps_enabled == 1;
    }
  #endif
    return false;
  }

void UITask::toggleGPS() {
    #if ENV_INCLUDE_GPS == 1
      bool new_state = !_node_prefs->gps_enabled;

      _node_prefs->gps_enabled = new_state ? 1 : 0;

    #ifdef PIN_GPS_EN
      pinMode(PIN_GPS_EN, OUTPUT);
      digitalWrite(PIN_GPS_EN, new_state ? HIGH : LOW);
    #endif

      if (new_state) {
    #ifdef GPS_BAUD_RATE
        Serial2.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    #else
        Serial2.begin(38400, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    #endif
      } else {
        Serial2.end();
      }

      if (_sensors != NULL) {
        int num = _sensors->getNumSettings();
        for (int i = 0; i < num; i++) {
          if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
            _sensors->setSettingValue("gps", new_state ? "1" : "0");
            break;
          }
        }
      }

      the_mesh.savePrefs();
      notify(UIEventType::ack);
      showAlert(new_state ? "GPS: Enabled" : "GPS: Disabled", 1000);
      _next_refresh = 0;
    #endif
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
