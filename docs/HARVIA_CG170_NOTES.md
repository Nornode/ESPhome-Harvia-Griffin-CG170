# Technical Notes: Harvia CG170

## Device Overview
- **Model:** Harvia **Griffin CG170** (model `CG170400`, type code `CG110`, control panel type
  *Griffin*, discontinued). Separate control panel connected to the power unit by a data cable.
- **Role:** Power unit managing current to sauna heaters and temperature sensors.
- **Interface:** Proprietary communication between the main switchboard and the user display.

## Hardware Findings (2026-09-25)
Full write-up: [`DISPLAY_BOARD_TEARDOWN.md`](DISPLAY_BOARD_TEARDOWN.md)

- **Display MCU identified:** `MC9S08GT32ACFBE` — NXP/Freescale **HCS08 8-bit**, 40 MHz,
  32 KB flash, 44-pin QFP, on-chip **SCI (UART)** + SPI + I2C.
- **Data cable (4-conductor RJ45-style):** **12 V measured *across* the two outer leads** ⇒
  outer pair = **power distribution** (+12 V / return), inner pair = **data** (presumed).
- ⚠️ **Data pair voltage is UNKNOWN.** The 3.3 V test points only prove the *MCU's* supply rail,
  **not** the signal level on the cable. The data pair could be 3.3 V, 5 V, an open-collector
  line pulled up to the 12 V rail, an isolated current loop, or RS-485. See the measurement
  matrix in [`DISPLAY_BOARD_TEARDOWN.md`](DISPLAY_BOARD_TEARDOWN.md) §3.3 — **this gates any
  ESP32 connection.**
- **Board logic is 3.3 V** — multiple 3.3 V test points suitable for jumper wires (MCU domain
  only).
- **2×3 hole array in a corner:** almost certainly the standard **Freescale 6-pin BDM debug
  header** (VDD, GND, RESET, BKGD, 2× NC). Potential route to a firmware dump.
- **Board markings:** `SK038972`, barcode `1306023730` (no public matches; see teardown doc).
- **Remaining unknown:** the voltage measurement reference for the 12 V reading (teardown Q1),
  and which conductors carry data.

## Observed Behaviors & Errors
- **E9 Error:** A known error code that appears on the display.
- **Meaning:** Typically indicates a communication failure between the control panel (display) and the power unit.
- **Trigger:** Likely caused by timeouts (no response from slave) or data corruption (checksum mismatch).

## Technical Hypothesis

### Physical Layer
- **Potential Protocol:** Most likely RS-485 or a specialized current-loop. 
- **Reasoning:** High electrical noise environment (high-current heater contactors) requires differential signaling for stability over distance.
- **Voltages:** **Confirmed 12 V across the two outer conductors of the 4-pin data cable** —
  these are the **power distribution** pair (+12 V / return), not a signal level (see
  [`DISPLAY_BOARD_TEARDOWN.md`](DISPLAY_BOARD_TEARDOWN.md) §3.1). The MCU domain after
  regulation is **3.3 V**, but **the data pair's signal level is unknown** (§3.3).
  **Warning:** Do not connect the data pair to 3.3V logic (ESP32) until that level is measured —
  use an RS-485 transceiver / level shifting if it turns out to be 12 V, 5 V, or floating.

### Data Link Layer
- **Topology:** Master-Slave.
- **Master:** CG170 Power Unit.
- **Slave:** User Display.
- **Interaction:** The Master likely polls the Slave for state updates and sends status updates (Temp, Error codes) to the Slave.
- **Timing:** The system is sensitive to latency. If a MitM device introduces significant delay, the Master will trigger the E9 error.

## Interface Analysis Requirements
To reverse engineer the protocol, the following data points must be captured:
1. **Idle State:** Baseline communication between the unit and display.
2. **Control Inputs:** Packets sent when buttons (Temp +, Temp -, Start, Stop) are pressed.
3. **Status Updates:** Packets sent when temperature changes.
4. **Error State:** The specific packet sequence that triggers "E9" on the display.

## Hardware Requirements for Analysis
- **Logic Analyzer:** 24MHz 8-channel (Saleae compatible).
- **Software:** PulseView (sigrok) for waveform analysis.
- **Protection:** Level shifters or RS-485 to TTL converters (MAX485) to protect the analyzer and ESP32.
