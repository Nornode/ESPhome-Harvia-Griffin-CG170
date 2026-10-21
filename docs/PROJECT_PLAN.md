# Project: Harvia Sauna Controller MitM (ESP32)

## Objective
Build an ESPHome-based Man-in-the-Middle (MitM) device between the Harvia CG170 control box and the user display.

### Primary goals:
1. **Remote Control:** Enable starting and stopping the sauna via Home Assistant.
2. **Error Filtering:** Intercept and remove "E9" error messages before they reach the display.
3. **Transparency:** Maintain all original functionality of the physical display.

## Architecture Comparison: MitM vs Parallel Slave

| Feature | Man-in-the-Middle (MitM) | Parallel Slave (Sidecar) |
| :--- | :--- | :--- |
| **Physical Wiring** | More Complex (Cut wires) | Simpler (Tapping wires) |
| **Control Ability** | Total (Intercept/Modify/Block) | Additive (Send only) |
| **Hiding E9 Error** | ✅ Possible (Replace packets) | ❌ Impossible (Broadcast nature) |
| **Remote Control** | ✅ Possible | ✅ Possible |
| **System Reliability** | Higher Risk (Potential point of failure) | Lower Risk (Display remains independent) |
| **Stealth** | High | Low |

**Conclusion:** The MitM approach is selected because the requirement to filter "E9" errors cannot be achieved with a parallel slave.

## Technical Hypothesis (Harvia CG170)
- **Protocol:** Likely proprietary, possibly RS-485 or a current-loop system.
- **Logic:** Master-Slave architecture where the CG170 (Master) polls the display (Slave).
- **E9 Error:** Likely a communication timeout or checksum failure.
- **Risk:** Timing is critical. Delays in the MitM proxy could trigger E9 errors.

## Implementation Roadmap

### Phase 1: Hardware Acquisition & Exploration
- [x] Acquire a 24MHz 8-channel Logic Analyzer (Saleae compatible).
- [x] Identify on-board RS-485 transceiver: **SP485EE** (tapped at TTL Pin 1 RO / Pin 4 DI).
- [x] Map physical wiring from display to CG170 (4-conductor cable, 12V outer pair, inner RS-485 pair).
- [x] Measure voltages: power pair = 12 V, MCU rail = 3.3 V, transceiver TTL = 3.3 V.
- [x] Identify display MCU: **MC9S08GT32ACFBE** (HCS08 8-bit, has UART).
- [x] Confirmed the corner 2×3 hole array is the standard **BDM debug header**.

### Phase 2: Protocol Analysis (Sniffing) — COMPLETED
- [x] Capture raw waveforms using Saleae Logic 2 MCP (saved in `harvia_cg170_*.csv`).
- [x] Identify baud rate, parity, and frame structure: **7812.5 baud, 8N1**.
- [x] Map specific byte sequences to actions (Heater ON/OFF, Temp setpoint 80°C/83°C, Timer, Current Temp).
- [x] Verify checksum/CRC implementation: **CRC-16/XMODEM** (Poly 0x1021, Init 0x0000).
- [x] Formalized in [`PROTOCOL_SPEC.md`](PROTOCOL_SPEC.md).

### Phase 3: ESPHome Protyping
- [ ] Build a "Transparent Proxy" to ensure zero-latency packet forwarding.
- [ ] Implement "Packet Filtering" to block/replace E9 error packets.
- [ ] Implement "Packet Injection" for Home Assistant remote controls.
- [ ] Integrate with Home Assistant via ESPHome.

### Phase 4: Validation & Safety
- [ ] Test fail-safe behavior (ensuring system works if ESP32 is bypassed).
- [ ] Verify that remote commands do not conflict with physical display inputs.
