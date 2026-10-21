# Harvia Griffin CG170 Protocol Specification

*Reverse-engineered on 2026-10-20 via Saleae Logic 2 & SP485EE TTL tapping.*

---

## 1. Physical Layer & Serial Parameters

| Parameter | Value | Notes |
| :--- | :--- | :--- |
| **Physical Interface** | RS-485 (Half-Duplex) | Transceiver on display board: `SP485EE` |
| **Logic Domain (Transceiver TTL)** | 3.3V | Pin 1 = `RO` (RX), Pin 4 = `DI` (TX) |
| **Baud Rate** | **`7812.5 baud`** | Exact $16\times$ divider of $1\text{ MHz}$ Freescale HCS08 bus clock ($128.00\ \mu\text{s}$ per bit) |
| **Data Framing** | **`8N1`** | 8 data bits, No parity, 1 stop bit, LSB first |
| **Addressing** | Master: `0x09` (Power Unit)<br>Slave: `0x07` (Control Panel) | In response headers: `0x07` $\rightarrow$ `0x0A` / `0x09` |
| **Checksum / CRC** | **`CRC-16 / XMODEM`** | Polynomial: `0x1021`, Initial: `0x0000`, RefIn: `False`, RefOut: `False`, XorOut: `0x0000` |
| **CRC Scope** | `Packet[1 : -2]` | Calculated over all bytes starting at index 1 (`'Q'` or `'R'`), excluding leading `'T'` |

---

## 2. Bus Packet Architecture

### Master (Power Unit) Query Packet
* **Leading byte:** Always `0x54` (`'T'`)
* **Type byte:** `0x51` (`'Q'`)
* **Subtype:** ASCII char (`'C'`, `'D'`, `'G'`, `'B'`, `'F'`, `'H'`, `'E'`)
* **Source / Dest:** `0x09 0x07`
* **Token:** `0x51` (`'Q'`) + sequence/command byte
* **Payload:** 0 to 6 bytes
* **Trailer:** 2 bytes Big-Endian `CRC-16/XMODEM`

### Display (Slave) Response Packet
* **Preamble:** 5 bytes: `0xCC 0xCC 0xCC 0xCC 0xF0`
* **Leading byte:** `0x54` (`'T'`)
* **Type byte:** `0x52` (`'R'`)
* **Subtype:** ASCII char matching query
* **Source / Dest:** `0x07 0x0A`
* **Payload:** Data bytes matching response type
* **Trailer:** 2 bytes Big-Endian `CRC-16/XMODEM`

---

## 3. Core Commands & Telemetry

### 3.1 Regular Idle Heartbeat (250 ms / 4 Hz)
The power unit constantly pings the display to verify connection:
```
Query:    [54 51 43 09 07 51 B7 00 E1 33]  (TQC ping)
Response: [CC CC CC CC F0 54 52 42 07 0A 51 B7 D2 61]
```
*If this heartbeat is interrupted or times out, the unit triggers the **E9 Error**.*

### 3.2 Temperature Telemetry (`TQG` / `TRG`)
Periodically transmitted every 3 to 5 seconds:
```
Master: [54 51 47 09 07 51 9D <CUR_TEMP> <SET_TEMP> 00 00 00 <CRC_HI> <CRC_LO>]
```
* **Byte 7 (`<CUR_TEMP>`):** Current measured sauna temperature in whole °C (e.g. `0x10` = 16°C, `0x11` = 17°C).
* **Byte 8 (`<SET_TEMP>`):** Target setpoint temperature in whole °C (e.g. `0x50` = 80°C, `0x53` = 83°C).

### 3.3 High-Resolution Setpoint Setting (`TQD 51 28`)
Sent when setting the target temperature:
```
[54 51 44 09 07 51 28 <TEMP_LO> <TEMP_HI> <CRC_HI> <CRC_LO>]
```
* 16-bit unsigned integer (Little-Endian) in **tenths of a °C** ($0.1^\circ\text{C}$):
  * `20 03` $\rightarrow$ `0x0320` = $800 = \mathbf{80.0^\circ\text{C}}$
  * `3E 03` $\rightarrow$ `0x033E` = $830 = \mathbf{83.0^\circ\text{C}}$

### 3.4 Sauna Timer / Remaining Runtime (`TQD 51 1E` / `TQF 51 9C` / `TRH 51 65`)
16-bit unsigned integer (Little-Endian) representing **total runtime in seconds**:
* **3.0 Hours (10,800 s / 180 min):**
  ```
  Master:   [54 51 44 09 07 51 1E 30 2A 64 FB]
  Display:  [CC CC CC CC F0 54 52 44 07 0A 51 1E 30 2A 0D 57]
  ```
  `30 2A` $\rightarrow$ `0x2A30` = $\mathbf{10,800\text{ s}}$ ($3\text{h }00\text{m}$).
* **2.0 Hours (7,200 s / 120 min):**
  ```
  Master:   [54 51 44 09 07 51 1E 20 1C 31 1D]
  Display:  [CC CC CC CC F0 54 52 44 07 0A 51 1E 20 1C 58 B1]
  ```
  `20 1C` $\rightarrow$ `0x1C20` = $\mathbf{7,200\text{ s}}$ ($2\text{h }00\text{m}$).
* **Heater OFF (0 s):**
  ```
  Master:   [54 51 44 09 07 51 1E 00 00 E4 46]
  ```
  `00 00` $\rightarrow$ $0\text{ s}$.

### 3.5 Relay Control Commands (`TQC` / `TRC`)

All relay switches share the `TQC` / `TRC` framing with command codes:

| Output Device | Command Code | ON Packet (Master $\rightarrow$ Display) | OFF Packet (Master $\rightarrow$ Display) |
| :--- | :--- | :--- | :--- |
| **Heater** | `0x22` / `0x23` | `[54 51 43 09 07 51 22 01 16 0C]` | `[54 51 43 09 07 51 23 00 1B B0]` (Response `TRC`) |
| **Fan** | `0x32` | `[54 51 43 09 07 51 32 01 15 7F]` | `[54 51 43 09 07 51 32 00 05 5E]` |
| **Light** | `0x3C` | `[54 51 43 09 07 51 3C 01 36 70]` | `[54 51 43 09 07 51 3C 00 26 51]` |

### 3.6 Relay Status Bitmask (`TQG` Telemetry Byte 9)
In packet `TQG: [54 51 47 09 07 51 9D <CUR> <SET> <RELAY_MASK> 00 00 <CRC>]`:
* **Bit 0 (`0x01`):** Heater Relay Active
* **Bit 1 (`0x02`):** Light Relay Active
* **Bit 2 (`0x04`):** Fan Relay Active
