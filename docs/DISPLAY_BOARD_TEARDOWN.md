# Display (Control Panel) Board – Teardown Findings

**Date of inspection:** 2026-09-25
**Source:** Visual inspection of the opened Harvia Griffin control panel (display) board.
**Status:** Observations recorded verbatim from the inspection. Interpretations are marked as
*hypothesis* where not yet verified.

---

## 1. Board markings

| Marking | Location | Notes |
| :--- | :--- | :--- |
| `SK038972` | Silkscreen / board marking | No public hits on this number. Likely a Harvia or EMS (contract manufacturer) internal board/PCB revision number. |
| `1306023730` | Below a barcode | Likely a serial/batch or order number. No public hits — recorded for future reference against a spare-parts listing (Harvia spare part for control-panel board is listed as **SP451**). |

**Confirmed product identity:** Harvia **Griffin CG170** (model `CG170400`, type code `CG110`,
control panel type *Griffin*, discontinued). The CG170 uses a **separate control panel connected
to the power unit by a data cable** — i.e. the board inspected here is the far end of that data
cable.

---

## 2. Main microcontroller

Marking as read off the package (largest identifiable chip):

```
MC9 08
GT32ACFBE
ON22A
CTGQ1225
```

**Identification (confirmed against distributor listings):**

- `MC9 08` / `GT32ACFBE` → **MC9S08GT32ACFBE**
- **Manufacturer:** Freescale / NXP (now NXP Semiconductors)
- **Core:** HCS08, **8-bit**, up to 40 MHz
- **Flash:** 32 KB
- **Package:** 44-pin QFP (SOT803-2)
- **On-chip comms:** **SCI (UART)**, SPI, I2C
- `ON22A`, `CTGQ1225` → lot / mask / date codes. `1225` is consistent with a 2022 date code
  (week 25) — *unverified*.

### Why this matters for the project

1. **The display side is a simple 8-bit part.** No RTOS, no high-speed comms. Supports the
   master–slave UART-style hypothesis in `HARVIA_CG170_NOTES.md`, and makes bit-banged
   proprietary framing plausible too — either way the timing budget is loose compared to an
   ESP32.
2. **Single SCI.** The MCU has one hardware UART. Whatever talks to the power unit is either
   that SCI or a GPIO bit-bang — both decodable by an ESP32 UART sniffer if the framing is
   standard.
3. **It is reprogrammable in principle.** The HCS08 family ships with a Background Debug Mode
   (see §4), which opens a completely different route to the goal (see §6).

---

## 3. Power and test points

### 3.1 Data cable / RJ45-style connector (4 leads)

- **Resolved (2026-09-25):** the **12 V is measured *across* the two outer leads** — i.e. the
  outer pair is the **power distribution feed to the display** (+12 V and its return), not a
  signal level.
- ⇒ The **two inner conductors are the data pair** (only two conductors left).
- The data pair's voltage is **still unknown** — see §3.3.

**Confirmed:** the 4-conductor cable = 2 conductors power (+12 V / return) + 2 conductors data.

> ⚠️ **Action before connecting anything:** do not treat any wire in this cable as logic level
> until §3.3 measurements are done.

### 3.2 Board test points

- Multiple marked test points giving **3.3 V** — usable as jumper-wire attachment points.
- ⇒ The **MCU supply domain is 3.3 V** (12 V is regulated down on-board).

### 3.3 Is the data pair 3.3 V? — **NOT established**

The 3.3 V test points only prove the *MCU's own* supply rail. **They say nothing about the
voltage on the two data conductors between boards.** Do not assume ESP32-compatible levels.

In a heater-contactor environment the line driver is plausibly one of:

| Candidate line type | Typical idle level | Notes |
| :--- | :--- | :--- |
| Direct 3.3 V GPIO (worst case: 5 V) | 0 V / rail | Simplest; least noise-immune |
| Open-collector pulled up to the 12 V rail | ≈12 V | Very plausible: 12 V is already present, better noise margin |
| Opto-isolated / current loop | 5–12 V, may float | Fully isolated from MCU domain |
| RS-485 differential | ±1.5 V to ±6 V differential | Needs a transceiver regardless |

**Measurements that settle it (multimeter only, ~5 minutes):**

1. **Continuity:** each inner conductor to the +12 V lead, and to the 12 V return / board ground.
   - No continuity to either ⇒ floating or isolated pair (RS-485 / current loop).
   - Continuity to a rail ⇒ single-ended with a shared reference.
2. **Idle DC level** across the inner pair, black probe on the display board's known ground.
3. **Idle level of each inner conductor *relative to the 12 V return*** — this is the number
   that decides whether an ESP32 GPIO can touch it.
4. **Behaviour with the panel active** (press a button): does the level swing, or only current?

**Connection rule from the result:**

| Measured idle | Verdict | How to sniff |
| :--- | :--- | :--- |
| ≤ 3.3 V, referenced to board GND | Direct connect is likely safe | Series ~1 kΩ resistor + ESP32 `rx_pin` |
| ~5 V | Too high for ESP32 | Resistive divider, or 5 V→3.3 V buffer |
| ~12 V or floating | **Never connect directly** | MAX485 / level shifter (as planned) |

> This table is the **only** thing gating Phase 2. Everything else (baud sweep, `uart` `debug`
> logging) is ready to run the moment the level is known.


---

## 4. 2×3 hole array in the corner (likely BDM header)

**Observation:** a block of **6 solder/measuring holes in two rows of three** in one corner of
the board, where 3.3 V was measurable; the square pad identified as **ground**; the opposite
side measurable as positive supply.

**Hypothesis (strong):** this is the **standard Freescale/NXP 6-pin BDM (Background Debug Mode)
connector**. NXP application note AN2497 describes it as *"typically a 2 by 3 pin header with
0.025" square posts on 0.1" centers"* — the HCS08 reference manual shows the standard tool
connector carrying **VDD, GND, RESET, BKGD** plus two no-connect pins. A 2×3 hole pattern in the
corner of an HCS08 board is exactly that footprint, left unpopulated as a production/programming
pad.

**Standard BDM signals to expect:**

| Signal | Role |
| :--- | :--- |
| `VDD` | Target voltage reference (3.3 V here) |
| `GND` | Ground (square pad, per observation) |
| `RESET` | Open-drain reset, pulled up on-chip |
| `BKGD` | **Single-wire** background debug data line |
| `NC` ×2 | Not connected |

> ⚠️ **Do not assume the pin order.** Different vendors number the 6-pin header differently
> (e.g. iSYSTEM maps 1=BGND, 2=GND, 4=RESET, 6=VTref). **Identify each hole with a multimeter
> and by tracing to the MCU before connecting a BDM pod.**

**BDM capabilities if confirmed:** memory/register read and write, flash programming and erase,
and single-wire debug — using a cheap USB BDM pod (P&E USB Multilink, or a home-built/FXTH
style pod) plus CodeWarrior or a Python HCS08 flasher. The HCS08 **security (SEC) byte** in flash
may be set, which would block reads until an erase — erasing destroys the firmware, so read first
and treat this path as read-only until a verified backup exists.

---

## 5. What this changes in the plan

| Item | Before | After this teardown |
| :--- | :--- | :--- |
| Logic voltage on the panel | Unknown (assumed 12/24 V) | **Power pair = 12 V (confirmed). MCU rail = 3.3 V. Data pair level = UNKNOWN** |
| Display MCU | Unknown | **MC9S08GT32ACFBE, 8-bit, 40 MHz, has UART** |
| Sniffing feasibility | Blocked on analyzer | **ESP32 sniffing is viable *subject to* the data-pair level check (§3.3)** |
| Firmware access | Not considered | **Probable BDM header discovered** — possible firmware dump/replace |
| Cable conductor map | Unknown | **Outer pair = +12 V / return (power). Inner pair = data (presumed)** |

### Revised next actions

1. **Do the §3.3 measurements first** (continuity + idle level of the inner pair). This is the
   single gate on connecting an ESP32 — 5 minutes with a multimeter.
2. Wire **ESP32 RX only** to the two inner conductors, per the §3.3 connection rule (direct
   with series resistor, divider, or MAX485 — depending on step 1), then brute-force baud rates
   with an ESPHome `uart` `debug` logger.
3. In parallel, map the 2×3 header against the MC9S08GT32A 44-pin QFP pinout (trace `BKGD` and
   `RESET`) to confirm the BDM hypothesis.
4. Keep the logic analyzer on the shopping list for the case where step 2 returns garbage at
   every baud rate — that outcome means the link is *not* UART-framed and only a waveform view
   will explain it.

---

## 6. Alternative strategy unlocked by BDM

The project goal is (a) remote start/stop and (b) hide E9. Two routes:

- **MitM (current plan):** intercept the cable traffic. Requires knowing the protocol.
- **Firmware replacement (new):** if BDM gives a flash dump, the display firmware could be
  analysed directly — the packet format, checksum and the exact E9 condition would be visible in
  the disassembly rather than guessed from waveforms. The display could even be re-flashed to
  ignore E9 locally.

Route 2 is higher effort and higher risk (bricking the panel), but it converts a reverse-engineering
problem into a code-reading problem. **Get a flash dump before deciding** — a dump is only possible
while the original firmware is intact.

---

## 7. Open questions

- **Q1 — RESOLVED (2026-09-25):** the 12 V is **across the two outer leads** ⇒ the outer pair
  is the power distribution feed (+12 V / return), not a signal level. Polarity of each outer
  lead relative to board ground still to be noted for reference.
- **Q2 (now the critical one):** **what voltage does the inner data pair sit at?** Differential
  (RS-485-like), isolated, or single-ended pulled up to 12 V? See §3.3 — this gates any ESP32
  connection. Polarity of the inner pair is irrelevant if it is differential.
- **Q3:** Is the 2×3 hole array really BDM? Confirm by tracing `BKGD` (pin on the QFP with an
  internal pull-up, active only during debug) before applying any pod.
- **Q4:** Is the HCS08 security byte programmed? Determines whether firmware extraction is
  possible without erasing.
- **Q5:** What do `SK038972` and `1306023730` correspond to in Harvia's spare-parts catalogue
  (candidate: SP451 circuit board for control panel)?
