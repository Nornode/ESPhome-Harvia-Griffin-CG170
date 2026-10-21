#pragma once

#include "esphome.h"
#include <vector>
#include <cmath>

#if defined(USE_ESP_IDF) || defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

inline float harvia_get_memory_used_pct() {
#if defined(USE_ESP_IDF) || defined(ESP_PLATFORM)
  size_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  size_t free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (total == 0) return NAN;
  return ((float)(total - free) / (float)total) * 100.0f;
#else
  return NAN;
#endif
}

// CRC-16/XMODEM implementation (Poly 0x1021, Init 0x0000)
static inline uint16_t harvia_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// Global state holding decoded values and verification data
struct HarviaState {
  // Current bus values
  float current_temp = NAN;
  float target_temp = NAN;
  float timer_remaining_min = NAN;
  bool heater = false;
  bool light = false;
  bool fan = false;

  // Verification & Mismatch tracking between Power Unit and Display
  float pu_target_temp = NAN;
  float disp_target_temp = NAN;
  int pu_relays = -1;
  int disp_relays = -1;
  bool state_mismatch = false;

  // Liveness timestamps (millis)
  uint32_t last_pu_rx_time = 0;
  uint32_t last_disp_rx_time = 0;
  uint32_t last_ha_publish_time = 0;

  // Last values published to Home Assistant (for change detection)
  float published_cur_temp = NAN;
  float published_target_temp = NAN;
  float published_timer_min = NAN;
  int published_heater = -1;
  int published_light = -1;
  int published_fan = -1;
};

static HarviaState g_harvia;

// Internal helper to decode a verified, complete packet
inline void harvia_decode_packet(
    const uint8_t *p,
    size_t len,
    const char *source_tag) {

  uint32_t now = millis();
  char p_type = (char)p[1];    // 'Q' (Power Unit) or 'R' (Display)
  char subtype = (char)p[2];   // 'G', 'D', 'C', 'H', 'B', etc.

  if (p_type == 'Q') {
    g_harvia.last_pu_rx_time = now;
  } else if (p_type == 'R') {
    g_harvia.last_disp_rx_time = now;
  }

  // -------------------------------------------------------------
  // 1. Telemetry Packet: 'G' (TQG or TRG)
  // -------------------------------------------------------------
  if (subtype == 'G' && len >= 14) {
    float cur_t = (float)p[7];
    float set_t = (float)p[8];
    uint8_t relays = p[9];

    // Log always in ESPHome console (unthrottled)
    ESP_LOGI("harvia", "[%s %c%c] Temp=%.0f°C, Target=%.0f°C, Relays=0x%02X",
             source_tag, p_type, subtype, cur_t, set_t, relays);

    if (cur_t > 0 && cur_t < 130) {
      g_harvia.current_temp = cur_t;
    }
    if (set_t > 0 && set_t < 130) {
      g_harvia.target_temp = set_t;
    }

    g_harvia.heater = bool(relays & 0x01);
    g_harvia.light  = bool(relays & 0x02);
    g_harvia.fan    = bool(relays & 0x04);

    // Track state verification between Power Unit and Display
    if (p_type == 'Q') {
      g_harvia.pu_target_temp = set_t;
      g_harvia.pu_relays = relays;
    } else {
      g_harvia.disp_target_temp = set_t;
      g_harvia.disp_relays = relays;
    }

    if (g_harvia.pu_relays >= 0 && g_harvia.disp_relays >= 0) {
      g_harvia.state_mismatch = (g_harvia.pu_relays != g_harvia.disp_relays);
      if (g_harvia.state_mismatch) {
        ESP_LOGW("harvia", "State mismatch detected! PowerUnit relays=0x%02X vs Display relays=0x%02X",
                 g_harvia.pu_relays, g_harvia.disp_relays);
      }
    }
  }
  // -------------------------------------------------------------
  // 2. Setpoint / Timer Packet: 'D' (TQD or TRD)
  // -------------------------------------------------------------
  else if (subtype == 'D' && len >= 11) {
    uint8_t cmd = p[6];
    if (cmd == 0x1E) { // Timer remaining seconds
      uint16_t secs = (uint16_t)p[7] | ((uint16_t)p[8] << 8);
      float mins = (float)secs / 60.0f;
      ESP_LOGI("harvia", "[%s %c%c] Timer: %d sec (%.0f min)", source_tag, p_type, subtype, secs, mins);
      g_harvia.timer_remaining_min = mins;
      g_harvia.heater = (secs > 0);
    } else if (cmd == 0x28) { // Setpoint in tenths of °C
      uint16_t temp_tenths = (uint16_t)p[7] | ((uint16_t)p[8] << 8);
      float target = (float)temp_tenths / 10.0f;
      ESP_LOGI("harvia", "[%s %c%c] Setpoint: %.1f°C", source_tag, p_type, subtype, target);
      if (target > 0 && target < 130) {
        g_harvia.target_temp = target;
      }
    }
  }
  // -------------------------------------------------------------
  // 3. Command / Relay Toggle Packet: 'C' (TQC or TRC)
  // -------------------------------------------------------------
  else if (subtype == 'C' && len >= 10) {
    uint8_t cmd = p[6];
    uint8_t state = p[7];
    if (cmd == 0x22 || cmd == 0x23) {
      g_harvia.heater = (state == 0x01);
      ESP_LOGI("harvia", "[%s %c%c] Heater toggle: %s", source_tag, p_type, subtype, state == 0x01 ? "ON" : "OFF");
    } else if (cmd == 0x3C) {
      g_harvia.light = (state == 0x01);
      ESP_LOGI("harvia", "[%s %c%c] Light toggle: %s", source_tag, p_type, subtype, state == 0x01 ? "ON" : "OFF");
    } else if (cmd == 0x32) {
      g_harvia.fan = (state == 0x01);
      ESP_LOGI("harvia", "[%s %c%c] Fan toggle: %s", source_tag, p_type, subtype, state == 0x01 ? "ON" : "OFF");
    }
  }
}

// Channel reassembly stream buffers
struct ChannelRxBuffer {
  std::vector<uint8_t> buf;
  uint32_t last_byte_time = 0;
};
static ChannelRxBuffer s_disp_rx;
static ChannelRxBuffer s_pu_rx;

// Called by UART debug whenever bytes are received on either tapped channel
inline void harvia_process_packet(
    const std::vector<uint8_t> &bytes,
    const char *source_tag) {

  ChannelRxBuffer &rx = (strcmp(source_tag, "DISPLAY") == 0) ? s_disp_rx : s_pu_rx;
  uint32_t now = millis();

  // If more than 250ms has elapsed since the last byte, discard incomplete stale bytes
  if (!rx.buf.empty() && (now - rx.last_byte_time > 250)) {
    rx.buf.clear();
  }
  rx.last_byte_time = now;

  // Append new incoming chunk to the channel stream buffer
  rx.buf.insert(rx.buf.end(), bytes.begin(), bytes.end());

  // Process all complete packets in the buffer
  while (!rx.buf.empty()) {
    // 1. Locate packet sync: 'T' (0x54) followed by 'Q' (0x51) or 'R' (0x52)
    size_t sync_pos = 0;
    bool sync_found = false;
    while (sync_pos + 1 < rx.buf.size()) {
      if (rx.buf[sync_pos] == 0x54 && (rx.buf[sync_pos + 1] == 0x51 || rx.buf[sync_pos + 1] == 0x52)) {
        sync_found = true;
        break;
      }
      sync_pos++;
    }

    if (!sync_found) {
      // Retain the single trailing byte if it could be 'T' (0x54)
      if (rx.buf.back() == 0x54) {
        rx.buf = {0x54};
      } else {
        rx.buf.clear();
      }
      break;
    }

    // Discard any preamble / garbage bytes prior to sync sequence
    if (sync_pos > 0) {
      rx.buf.erase(rx.buf.begin(), rx.buf.begin() + sync_pos);
    }

    // Smallest valid Harvia frame is 9 bytes
    if (rx.buf.size() < 9) {
      break; // Need more bytes from the next chunk
    }

    // 2. Scan candidate packet lengths (Harvia frames range from 9 to 18 bytes)
    bool packet_parsed = false;
    size_t max_scan = std::min(rx.buf.size(), (size_t)24);

    for (size_t l = 9; l <= max_scan; l++) {
      uint16_t exp_crc = ((uint16_t)rx.buf[l - 2] << 8) | rx.buf[l - 1];
      uint16_t calc_crc = harvia_crc16(&rx.buf[1], l - 3);

      if (calc_crc == exp_crc) {
        harvia_decode_packet(rx.buf.data(), l, source_tag);
        rx.buf.erase(rx.buf.begin(), rx.buf.begin() + l);
        packet_parsed = true;
        break;
      }
    }

    if (!packet_parsed) {
      // If we have accumulated 24 bytes and still no CRC match, the '0x54' is not a valid frame
      if (rx.buf.size() >= 24) {
        rx.buf.erase(rx.buf.begin()); // Advance past invalid start byte and re-scan
      } else {
        // Frame may still be in transit, wait for remaining bytes
        break;
      }
    }
  }
}

// Rate-limiting publisher called every interval (e.g. 1s) from an ESPHome interval script
inline void harvia_sync_to_homeassistant(
    esphome::sensor::Sensor *s_temp,
    esphome::sensor::Sensor *t_temp,
    esphome::sensor::Sensor *timer,
    esphome::binary_sensor::BinarySensor *heater,
    esphome::binary_sensor::BinarySensor *light,
    esphome::binary_sensor::BinarySensor *fan,
    esphome::binary_sensor::BinarySensor *online,
    esphome::binary_sensor::BinarySensor *mismatch) {

  uint32_t now = millis();

  // Bus is considered online if packets were heard from either side within last 5 seconds
  bool is_online = (now - g_harvia.last_pu_rx_time < 5000) || (now - g_harvia.last_disp_rx_time < 5000);
  if (online != nullptr && (!online->has_state() || online->state != is_online)) {
    online->publish_state(is_online);
  }

  // State mismatch sensor (alerts HA if Power Unit and Display disagree)
  if (mismatch != nullptr && (!mismatch->has_state() || mismatch->state != g_harvia.state_mismatch)) {
    mismatch->publish_state(g_harvia.state_mismatch);
  }

  if (!is_online) return;

  // Check if any state or measurement actually changed
  bool value_changed = false;

  if (!std::isnan(g_harvia.current_temp) && g_harvia.current_temp != g_harvia.published_cur_temp) {
    value_changed = true;
  }
  if (!std::isnan(g_harvia.target_temp) && g_harvia.target_temp != g_harvia.published_target_temp) {
    value_changed = true;
  }
  if (!std::isnan(g_harvia.timer_remaining_min) && g_harvia.timer_remaining_min != g_harvia.published_timer_min) {
    value_changed = true;
  }
  if ((int)g_harvia.heater != g_harvia.published_heater) {
    value_changed = true;
  }
  if ((int)g_harvia.light != g_harvia.published_light) {
    value_changed = true;
  }
  if ((int)g_harvia.fan != g_harvia.published_fan) {
    value_changed = true;
  }

  // Rate-limit: Only publish to Home Assistant if value changed AND at least 60s has passed,
  // or on initial state acquisition.
  bool time_elapsed = (now - g_harvia.last_ha_publish_time >= 60000);
  bool initial_publish = (g_harvia.last_ha_publish_time == 0 && !std::isnan(g_harvia.current_temp));

  if (value_changed && (time_elapsed || initial_publish)) {
    g_harvia.last_ha_publish_time = now;

    if (s_temp != nullptr && !std::isnan(g_harvia.current_temp)) {
      g_harvia.published_cur_temp = g_harvia.current_temp;
      s_temp->publish_state(g_harvia.current_temp);
    }
    if (t_temp != nullptr && !std::isnan(g_harvia.target_temp)) {
      g_harvia.published_target_temp = g_harvia.target_temp;
      t_temp->publish_state(g_harvia.target_temp);
    }
    if (timer != nullptr && !std::isnan(g_harvia.timer_remaining_min)) {
      g_harvia.published_timer_min = g_harvia.timer_remaining_min;
      timer->publish_state(g_harvia.timer_remaining_min);
    }
    if (heater != nullptr) {
      g_harvia.published_heater = (int)g_harvia.heater;
      heater->publish_state(g_harvia.heater);
    }
    if (light != nullptr) {
      g_harvia.published_light = (int)g_harvia.light;
      light->publish_state(g_harvia.light);
    }
    if (fan != nullptr) {
      g_harvia.published_fan = (int)g_harvia.fan;
      fan->publish_state(g_harvia.fan);
    }

    ESP_LOGD("harvia", "Synced updated state to Home Assistant (60s rate limit window reset)");
  }
}
