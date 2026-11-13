<p align="right">
  <a href="hardware-quest.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Passport Quest: Hardware Self-Check and Focus Timer Design

> Status: design draft M0, not implemented. Fork-specific product design kept under `docs/assets/`; it is not submitted upstream.

## 1. Goals and non-goals

**Goals**

- One application that exercises every usable peripheral: display, backlight, three-key ADC ladder, I2C bus, fuel gauge, speaker, microphone, BLE, Wi-Fi, Flash/NVS, and deep sleep with wake.
- Repeatable, decidable, exportable self-checks: every level has objective criteria and the run produces one report.
- Deep sleep is both a test and a real feature: the focus timer and the level share a single `power` module, so passing the check means the product path works.

**Non-goals (v1)**

- NFC: the NTAG213 is a passive tag not wired to the MCU, so it cannot be read or written; no level is defined.
- Wi-Fi networking and Bluetooth provisioning: separate follow-up projects; v1 only scans to validate the RF path.
- Dual-device BLE gaming: separate project; v1 only exposes a phone-connectable GATT service.
- Standby current measurement: deep-sleep current is in the tens of microamps, invisible to software; an external USB power meter is required.
- Arbitrary Unicode text: fixed UI copy is Simplified Chinese (through a font subset with a glyph-coverage gate), while dynamic values such as SSIDs, AP names, and device names render as ASCII or placeholders.

## 2. Board constraints (from `bsp_pins.h` and measurements)

| Item | Fact | Consequence for this design |
| --- | --- | --- |
| SoC | ESP32-C3, 160 MHz, single core, no PSRAM | every buffer must be budgeted; no large images |
| Flash | 8 MB, factory partition 8.1 MB, current app 1.53 MB | ample room; v1 ships no image assets |
| Display | ST7789P3 240×320, RGB565, SPI2 80 MHz, inverted panel | full-screen and rectangular updates only; no full-screen animation |
| Backlight | LEDC PWM on GPIO21, 5 kHz, 10 bit | use `bsp_display_backlight(0..100)` |
| Keys | one ADC pin (GPIO0), windows `{0–150, 150–447, 447–1900} mV`, released ≥1900 mV | key identity can only come from voltage; short press 180 ms, long press 500 ms |
| Audio | ES8311, I2S full duplex, 16 kHz / 16 bit / mono, 512-sample chunks | record-and-playback is the most convincing audio check |
| Fuel gauge | CW2017 (0x63), SOC plus mV; the product specification and the BSP profile are both 500 mAh | use 500 mAh; the 520 mAh figure in the handoff document does not match and is not used for conversions |
| Deep sleep | timer wake plus GPIO0 low-level wake; RTC slow clock is the internal RC | duration error must be measured, not assumed |
| LVGL | 24 KB memory pool | few widgets per screen; release everything on exit |

## 3. Application structure

Three entry points on one foundation:

```text
main/
  app_home.c        home screen: check / focus / report
  power.c/.h        shared: arm wake sources, stop peripherals, RTC state, wake reason, duration
  quest.c/.h        self-check state machine and its twelve levels
  focus.c/.h        focus timer state machine (reuses power)
  report.c/.h       report assembly, serialization, serial export, NVS persistence
  ui_common.c/.h    three-key navigation, status bar, fonts, colors
```

Rules:

- `power` knows nothing about levels or pomodoros; it accepts `(mode, duration)` plus an opaque RTC payload, so both features share one deep-sleep path.
- Level code calls only public `bsp_*` APIs, never `esp_lcd` or `esp_codec_dev` directly.
- Pure logic (level state machine, report serialization, remaining-time math) stays independent of LVGL and ESP-IDF and is covered by host tests.

## 4. Hardware coverage matrix

| Hardware | Self-check level | Focus timer | Note |
| --- | --- | --- | --- |
| Display / backlight | L1, L2 | always on | |
| Three-key ADC | L5 | navigation | L5 also records calibration values |
| I2C bus | L3 | indirect (gauge, codec) | |
| Fuel gauge | L4 | recorded per session | |
| Speaker | L6 | end-of-session chime | |
| Microphone | L7 | unused | |
| I2S full duplex | L8 loopback | unused | |
| BLE | L9 | unused | peripheral + broadcaster only |
| Wi-Fi | L10 scan | unused | no networking |
| Flash / NVS | L11 | completion count | |
| Deep sleep / wake | L12 | main path | same `power` code |
| NFC | — | — | not testable; reported as "not applicable" |

## 5. Self-check levels

Each level records start/end time, verdict, and key numbers. Levels that require human judgement ask for an explicit key confirmation instead of pretending to be objective.

| Level | Hardware | Steps | Objective criteria | Failure handling |
| --- | --- | --- | --- | --- |
| L1 Display | panel | full-screen white/black/red/green/blue, 16-step grayscale, 1 px border, one line of small text | user confirms no dead pixels or color cast | allow re-display; record an explicit negative verdict |
| L2 Backlight | LEDC | ten steps 0→100%, one fade, key changes step | user confirms no flicker and distinguishable steps | repeat once |
| L3 I2C | bus | `bsp_i2c_scan()` | both `0x18` and `0x63` must appear | fail immediately, list what was found |
| L4 Battery | CW2017 | read SOC/mV, read again after 10 s | 3000–4300 mV, SOC 0–100, delta ≤2% | retry once, then record |
| L5 Keys | ADC ladder | live mV readout; single presses up/down/OK; one 1 s long press on OK | every reading falls inside its window; record the three centers | show measured values so `BSP_BTN_MV_TABLE` can be adjusted |
| L6 Speaker | ES8311 output | 200 Hz→8 kHz sweep for 3 s at 40%, then five volume steps | user confirms audible, no crackle | replay; skipping is recorded |
| L7 Microphone | ES8311 input | record 2 s with a live level bar | RMS above the noise floor captured at boot | retry once with more gain |
| L8 Loopback | I2S full duplex | play back the L7 recording | written bytes equal recorded bytes and audio is recognisable | record "audio works, loopback abnormal" |
| L9 BLE | NimBLE | advertise `PQ-<MAC2>` with a custom GATT write characteristic | non-empty write received and read-back matches | restart advertising and retry |
| L10 Wi-Fi | STA scan | scan and list AP count plus strongest RSSI | at least one AP found | retry once, record RSSI |
| L11 Storage | NVS | increment a counter, store 16 random bytes and a timestamp | after reboot the counter continues and the bytes match | failure means progress cannot be stored |
| L12 Deep sleep | RTC + GPIO0 | 15 s timer wake; long press OK aborts | wake reason correct, measured duration within ±5%, RTC state intact | record wake reason and measured duration |

L5 must explicitly cover "a long press read as a different key": the ADC ladder sweeps through neighbouring windows when the contact bounces. That is a known defect and must be visible in the report rather than hidden.

## 6. Shared deep-sleep module (`power`)

### 6.1 RTC state

```c
#define PQ_RTC_MAGIC 0x50515553u  /* 'PQUS' */

typedef struct {
    uint32_t magic;
    uint32_t seq;         /* incremented on every sleep */
    uint32_t mode;        /* 1 = quest L12, 2 = focus, 3 = break */
    uint32_t arg;         /* level number or focus round */
    uint64_t arm_us;      /* RTC time at sleep entry */
    uint64_t wake_us;     /* RTC time written on wake */
    uint32_t checksum;    /* guards against uninitialized RTC memory */
} pq_rtc_t;
```

### 6.2 Sleep entry sequence (order matters)

1. Take the LVGL lock, turn the backlight off, delete the page, release the lock.
2. Stop tasks that touch peripherals (audio reads/writes in particular).
3. `bsp_battery_sleep()`.
4. `bsp_audio_sleep()`.
5. `bsp_audio_prepare_deep_sleep()` (I2S pins to high impedance).
6. `bsp_i2c_prepare_deep_sleep()` (release the shared bus).
7. `bsp_display_prepare_deep_sleep()` (panel off, then sleep-in).
8. `bsp_button_prepare_deep_sleep(&level)`: hand GPIO0 back to digital input and confirm the key is released; refuse to sleep otherwise.
9. Arm wake sources: `esp_sleep_enable_timer_wakeup(duration)` and `esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW)`.
10. Write the RTC state, then `esp_deep_sleep_start()`.

Peripheral shutdown failures are logged but do not block sleep; "key still held" is a hard failure that returns to the UI with a message.

### 6.3 Wake and duration measurement

Deep-sleep wake is a reset, so `app_main` starts over; the first thing it does is validate the RTC state.

- `esp_sleep_get_wakeup_cause()` distinguishes `TIMER` from `GPIO`. A GPIO wake only means "GPIO0 went low" and does not identify the key; read the ADC again and separate "user aborted" from "accidental touch".
- Measured sleep duration: capture `esp_clk_rtc_time()` before sleeping and again after waking. Whether that counter is continuous across deep sleep must be verified on hardware in M1; if not, fall back to per-round counters plus an external meter and mark duration as unmeasured.
- Accumulate the ratio `measured / expected` and use it, clamped to ±5%, when arming the next timer so a 25-minute session does not drift further each round.

### 6.4 Pause semantics

Deep sleep cannot pause the RTC, so "pause" means: store the elapsed time in RTC memory and re-arm the remaining duration on wake. Resuming uses the same sleep path; there is no second implementation.

## 7. Focus timer (reuses D)

- Two duration profiles: **self-check 15 s / 5 s** and **product 25 min / 5 min**. Duration is a parameter, never a constant.
- State machine: `IDLE → FOCUS(sleep) → wake → prompt → BREAK(sleep) → wake → IDLE`.
- A key during focus wakes the device and shows the remaining time with two choices (keep sleeping / abandon); after 5 s with no input it resumes sleeping so accidental touches do not cancel a session.
- Completed sessions are counted per day in NVS, written once per round rather than on every wake.
- The report shows measured sleep duration, expected duration, error percentage, SOC before and after, and the cumulative count.

## 8. Data and persistence

| Data | Where | Why |
| --- | --- | --- |
| Sleep state, sequence, timestamps | RTC_DATA_ATTR | needed on every wake; never write Flash here |
| Level progress and verdicts | NVS (`pq_quest`) | survives power loss; one write per level |
| Focus counts and settings | NVS (`pq_focus`) | low write rate |
| Diagnostic report (latest only) | NVS (`pq_report`) | one report plus counters; the NVS partition is only 24 KiB |
| Report history | serial export to a PC | keeps no space on the device |

### 8.1 Retention decision (v1)

The NVS partition is only 24 KiB (`0x9000`–`0xF000`) and shares space with PHY calibration data, so it cannot hold report history. For v1:

- NVS stores only the **latest report** (≤1 KB) and cumulative counters, surviving reboot and power loss;
- history and full reports leave the device through **serial export** (text or JSON); the UI provides an export entry;
- a full merged-image flash fills the NVS region with `0xFF`, so reports and counters **may be reset**. That is known behaviour and the UI must say so instead of promising retention.

If device-side history that survives a full flash is ever required, shrink the app partition and add a dedicated data partition after `0x400000`: the merged image stops at the end of the app and would not touch it. That change touches the partition table, the validation gate, and the flashing flow, so it is deferred to M5 pending a real requirement; v1 keeps the default layout.


## 9. UI and interaction

- 240×320 portrait, one screen at a time; keys: up/down move, OK confirms, long press OK goes back.
- Every non-UI task touching LVGL must hold `bsp_lvgl_lock()`; key callbacks only set state and never block.
- Delete the previous page before creating the next, and stop its timers and callbacks on exit.
- Rebuilding the UI after a deep-sleep wake must finish within 200 ms, showing a static placeholder meanwhile.

### 9.1 Chinese fonts (required for v1)

The default Montserrat 14/20 has no CJK glyphs, and the Source Han Sans SC bundled with LVGL is only a subset; neither can be validated by "it compiles as UTF-8".

- **Probe**: in M1, enable `CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK=y` and set one label to that font explicitly to confirm Chinese actually renders. That built-in subset has a fixed, hand-picked character list that does not cover all UI copy, and it costs about 200 KB of flash, so it stays a probe rather than the production font.
- **Production path (implemented)**: `tools/gen_font.py` rasterises **Noto Sans CJK SC Regular** (SIL OFL 1.1) with Pillow and writes the LVGL PLAIN 4bpp format directly, one subset each for 16 px and 20 px. It then parses its own C output back and compares every glyph pixel by pixel against the rasterised mask. **`lv_font_conv` is not used**: it writes corrupted bitmaps under current Node (see the [reference entry](../reference/shinku-chen/lvgl-cjk-font-subsets.md)).
- The character set lives in two places: `main/ui_strings.h` holds every user-visible string, `tools/font_charset.txt` defines the covered range. Generated `.c` files live in `assets/fonts/` and are added to `main/CMakeLists.txt`; the source OTF is not committed, and family, version, license, and SHA-256 are recorded in `assets/fonts/README.md`.
- **Glyph gate**: `tools/gen_font.py --check` scans `main/ui_strings.h` and reports any character outside the charset as `U+XXXX`, failing `tools/validate.sh --static`.
- **Dynamic text**: SSIDs, AP names, and device names may contain arbitrary Unicode; v1 converts them to ASCII or shows a placeholder (for example "Network 3") rather than feeding unknown characters to the CJK font or silently dropping them.

## 10. Resource budget

| Item | Budget | Note |
| --- | --- | --- |
| Flash (code) | ≤ 150 KB added | excludes image and font assets |
| Flash (fonts) | about 35 KB measured | 16 px + 20 px, 152 code points, 4 bpp; the built-in subset enabled in M1 (about 200 KB) has been removed |
| LVGL pool | 24 KB | one screen at a time, released on exit |
| Audio buffers | 2 × 512 × 2 B | one for record, one for playback, heap-allocated |
| BLE | measured while connected | NimBLE with peripheral + broadcaster only; coexistence with LVGL must be measured |
| Stacks | default task plus one worker | audio and BLE get their own tasks |

## 11. Test plan

**Host tests (no hardware)**

- duration conversion, drift-compensation clamp, and pause/remaining math in `power`;
- level state machine: ordering, retry on failure, skipping only where allowed;
- report serialization round trip;
- ADC voltage to key mapping at window boundaries and in the released state;
- Chinese glyph coverage: every fixed UI code point exists in the generated font, and the known-missing negative control fails.

**On-device acceptance**

1. Build, merge the image, and run the layout check.
2. Flash and run every level, recording values and verdicts.
3. Short deep sleep: confirm timer wake, key abort, wake reason, and duration error.
4. Run one complete short focus round (focus plus break).
5. With no AP available, confirm L10 still reports an explicit "zero APs found" result.

**Explicitly unverified (reported separately)**

- Standby current and battery life: needs an external USB power meter.
- RF range, wall penetration, coexistence: needs distance and interference conditions.
- Acoustic metrics (response, sensitivity, THD): needs instruments.
- Real battery capacity and CW2017 profile accuracy.
- NFC: physically untestable.

## 12. Milestones

| Stage | Content | Acceptance |
| --- | --- | --- |
| M1 | `power`, home screen, L12 short deep sleep | passed on device 2026-11-08, see section 14 |
| M2 | L1–L5 (display, backlight, keys, I2C, battery) | all five passed on device 2026-11-09, see section 15 |
| M2.5 | CJK font pipeline and Chinese UI | done 2026-11-09, glyphs and layout confirmed on device |
| M3 | report page: RAM + NVS + serial export | done 2026-11-09, verified with a simulated power loss |
| M4 | focus timer (configurable durations, on-screen countdown, chimes) | passed on device 2026-11-10, see section 15 |
| M5 | L6–L8 (speaker sweep, microphone recording, loopback) | passed on device 2026-11-12, see section 15 |
| M6 | Wi-Fi scan, setup with captive portal, internet probe and file round trip | passed on device 2026-11-13; BLE skipped by decision |
| M7 | serial screenshot export and a data partition | data partition done with M6; screenshot export not started |

## 13. Risks and open questions

1. Is `esp_clk_rtc_time()` continuous across deep sleep? This is the first M1 experiment and decides the error-measurement approach.
2. Temperature drift of the internal RC slow clock: a 25-minute session may be off by tens of seconds; design for ±1 minute or add a crystal.
3. Heap headroom with BLE, LVGL, and audio running together: the reference measured about 60 KB for a full link; this app only enables two roles, but it still needs measurement.
4. A full merged-image flash resets NVS: only the latest report is kept and history depends on export, which the UI must state (see 8.1).
5. Resolved: the product specification and the BSP profile are both 500 mAh; the 520 mAh figure in the handoff document is not used for conversions and that document should be corrected.
6. Long press versus the deep-sleep guard: sleeping while a key is held wakes the device immediately; both L12 and the focus timer must say so.

## 14. M1 device results (2026-11-08)

Measured on one device (ESP32-C3 rev v1.1, battery 4163 mV / 98%).

| Item | Result |
| --- | --- |
| RTC time across deep sleep | **continuous**, so `esp_clk_rtc_time()` can measure real sleep duration |
| Internal RC error | 15 s requested, 15.39 s measured (**+2.61%**, 26 160 ppm); drift compensation took effect: the second sleep was armed at 15.39 s |
| Timer wake / key wake | both work, counters increment; refusing to sleep while a key is held works |
| Wake destination | after sleeping from the self-check level the device returns straight to the level page (product decision, kept) |
| CW2017 fuel gauge | after wake it stays asleep or reports an implausible SOC (0% at 4.16 V). Added `bsp_battery_recover()`: RESTART/ACTIVE, then validate SOC against a voltage-band floor, rewriting the profile if still inconsistent; recovered to 98% in about 2 s |
| CJK font | built-in subset covers 27/51; 21 missing characters become the M2 subset input |
| I2S | the `channel has not been enabled yet` lines before sleep are IDF driver noise; the BSP already treats them as success |

Missing characters (the M2 subset must include them):

```text
U+724C U+786C U+68C0 U+8304 U+949F U+62A5 U+7761 U+6D4B U+8BD5 U+8BF7
U+677E U+5F00 U+6309 U+952E U+5524 U+9192 U+8BEF U+8D25 U+65E7 U+6F14 U+9488
```

Two engineering conclusions follow:

- keep the drift tolerance and clamp as they are: a measured +2.61% shows the ±5% window is right, and a narrower one would reject normal samples;
- the fuel-gauge recovery belongs in the BSP: any application calling `bsp_battery_sleep()` before deep sleep hits the same state, and `bsp_battery_recover()` should be called unconditionally after initialisation (it returns immediately when readings are sane).

## 15. M2/M3 device results (2026-11-09)

| Item | Result |
| --- | --- |
| L1 display | five colours plus 16-step grayscale, confirmed by eye |
| L2 backlight | ten steps 10–100%, all distinguishable |
| L3 I2C scan | `0x18` and `0x63` both respond |
| L4 fuel gauge | 98% / 4162 mV, two reads agree |
| L5 keys | three clicks plus a long press, voltages inside the `bsp_pins.h` windows |
| Report page | six levels with per-level detail, up/down switching |
| NVS persistence | restored after a simulated power loss |

Defects found on hardware and fixed in code:

1. **Key test sampled the wrong moment**: `BSP_BTN_CLICK` fires after release, when the ADC is
   already back at the released voltage. Sample on `BSP_BTN_PRESS` and use CLICK/LONG only for
   the event type.
2. **Sleep duration included the boot time**: RTC keeps running after wake, but the code read it
   only in `app_main`, so roughly 300 ms of bootloader and init were counted as sleep. The drift
   estimator absorbed that and inflated every request until the 10 s level exceeded tolerance.
   Subtract the awake time and accept only ±0.5% samples (the sleep timer and RTC share a clock).
3. **Results lived in normal RAM**: any L12 deep sleep cleared them. They now live in RTC memory
   with an NVS copy for power loss.
4. **Report page loaded too late**: NVS was only read when the self-check page opened first, so a
   cold boot showed an empty report. Load right after `nvs_flash_init()`.
5. **Testing**: the device runs from a battery, so unplugging USB never loses power. A long press
   on DOWN in the report page simulates a power loss (drops RTC, keeps NVS, restarts).

Interaction conventions in the current build:

- report page: up/down switch, long OK back, long UP clear, long DOWN simulated power loss;
- focus timer: OK starts or resumes, long OK leaves the round, long UP toggles 25/5 and 1/1 minute;
- the report page carries a permanent "flashing the merged image resets the report" note.

### Focus timer (M4, 2026-11-10 hardware)

- Durations are configurable: single click ±1 minute, double click ±5, long UP switches between focus and
  break; values live in RTC (across deep sleep) and NVS (across power loss).
- The bulk of a round still sleeps; **the last 60 seconds run awake with an on-screen countdown**
  (`COUNTDOWN_US`). A round shorter than a minute skips sleep entirely, which also makes testing quick.
- The final five seconds beep once per second; the round ends with three groups of three beeps
  (70 ms inside a group, 700 ms between groups). The whole pattern is streamed as one waveform —
  reopening the codec per beep and separating them with `vTaskDelay` only produced the first group.
- Finished states show `00:00` plus a "next: …" line; a round counts as completed only when both the
  focus and the break have run to the end.
- Power trade-off: the last minute keeps the screen on, costing tens of milliamps more than sleeping all
  the way through. Shrinking `COUNTDOWN_US` is enough to trade it back without touching the state machine.
- Pressing OK on the report page exports it over the serial port, a second retention path next to NVS.

### Audio levels (M5, 2026-11-12 hardware)

| Level | Result |
| --- | --- |
| L6 speaker | 200 Hz→8 kHz sweep for 3 s with no crackle; five volume steps distinguishable |
| L7 microphone | RMS above the threshold (300) while speaking, peak sane |
| L8 loopback | sequential playback recognisable; simultaneous duplex shows **79%** of the 1 kHz energy (threshold 20%) |

Conclusions:

- **The microphone and full-duplex I2S are verified for the first time**; audio input had no user before this.
- Two seconds of 16 kHz mono needs 64 KB of heap, which allocated fine — the practical ceiling without PSRAM.
- The duplex criterion uses Goertzel to estimate the 1 kHz share of the recording, which is steadier than listening;
  79% against a 20% threshold leaves plenty of margin.
- At 20 px the result string exceeded 212 px, so it now spans two lines and every level result label wraps.

### Radio and storage (M6, 2026-11-12 hardware)

| Item | Result |
| --- | --- |
| L9 Wi-Fi scan | several APs found with a sane strongest RSSI; the record survives a power cycle via NVS |
| L10 Wi-Fi setup | open `PQ-Setup` hotspot plus a phone form; the station connects |
| Internet probe | `http://connectivitycheck.gstatic.com/generate_204` returns `204` |
| File round trip | written to `/store/wifi_test.txt` and read back byte for byte |
| Data partition | new 4 MB SPIFFS `storage` at 0x400000; the app partition shrank to 4 MB |

Three problems found on hardware and fixed:

1. **The Wi-Fi stack cannot be re-initialised per visit**: the second visit failed in
   `esp_netif_new` / `esp_wifi_init` and the page showed "connection failed". Initialise once per
   process and only start/stop the AP and HTTP server per visit.
2. **Pages must be opaque**: a transparent page container let the previous page's pixels show
   through as overlapping text; `pq_ui_page_create()` now paints a background.
3. **The page must notice the phone**: watching only HTTP submits left it saying "waiting" while
   the phone sat on the hotspot. It now watches `WIFI_EVENT_AP_STACONNECTED/DISCONNECTED` and
   refreshes every 500 ms.

Other decisions and limits:

- **BLE levels are skipped by decision**; of the two radios only Wi-Fi is exercised by the app;
- leaving the setup page stops the station link (power), credentials stay in NVS and the page
  shows "last connected to …";
- the **captive portal is implemented and verified on hardware**: DHCP points DNS at the device,
  a UDP responder answers every A query with the hotspot address, and the system probes of Android,
  iOS and Windows are redirected to the setup page;
- after the partition change the merged image still resets NVS (the report), but the `storage`
  partition sits after the app, so **files survive a full reflash**.
