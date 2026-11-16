# Ext 2: Fault detection and diagnostics (FDD)

## 1. What this phase adds

Two simple, explainable rules that watch the thermostat's own data and flag equipment problems. Their
results appear in Modbus register 8 and in the log. A simulator experiment measures how well they work:
it injects faults and also runs healthy days to count false alarms.

## 2. Why it matters

Connected HVAC platforms try to spot failing equipment *before* the customer notices a warm office or
a spoiled cold store. Rule-based diagnostics are the classic first step: easy to explain to a
technician, cheap to run on the controller itself, and a baseline for any later machine-learning
approach.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `thermostat/include/hvac_fdd.h`, `src/hvac_fdd.c` | The two detectors (pure, like the core) |
| `thermostat/src/hvac_core.c` | Records `last_reading_valid` for FDD (D-028) |
| `thermostat/src/config.c` | `fdd_enabled`, `fdd_ineffective_window_s`, `fdd_ineffective_min_drop_mc`, `fdd_stuck_window_s` |
| `thermostat/src/main.c` | Runs FDD after each step, register 8, logs each bit once |
| `thermostat/sim/room_model.c` | Fault injection: capacity loss, compressor failure, stuck sensor |
| `thermostat/sim/sil_fdd.c` | Detection matrix and false-alarm matrix (`sil fdd OUT_DIR`) |
| `thermostat/tests/test_hvac_fdd.c` | 11 unit tests at and around every threshold |
| `scripts/check_fdd.py` | Checks targets, adds the FDD metrics |
| `tests/host/test_daemon_host.py` | Stuck sensor -> register 8 bit1, logged once |

## 4. How it works

```
 bit0 COOLING_INEFFECTIVE                       bit1 SENSOR_STUCK
 compressor on?  no -> forget the window        valid reading changed? yes -> restart the timer
          yes                                   unchanged for 1800 s?  yes -> set bit1
 start a 1200 s window at temperature T0
 at the end: T0 - T_now < 0.5 C ?  yes -> set bit0
 then start the next window at T_now
```

1. **Cooling ineffective.** A healthy compressor running for 20 minutes cools the room by well over
   0.5 °C. If it doesn't, something is wrong: low refrigerant (capacity loss), a dead compressor, or a
   wide-open window.
2. **Sensor stuck.** A real room never stays constant to 1/16 °C for 30 minutes. Sensor noise alone
   moves the reading, so a perfectly flat value means the sensor or its wiring has frozen.
3. **Latching.** Once set, a bit stays set until `hvacd` restarts, so a technician sees it even if the
   fault came and went.
4. **Experiment.** Faults are injected at 06:00 into 24-hour simulations with the real core.
   Detection matrix: capacity loss, compressor failure and stuck sensor on the hot day, plus a stuck
   sensor on the cold and mild days. False-alarm matrix: all three healthy days, each with 5 noise seeds.

## 5. Run it yourself

```
build/thermostat/sil fdd results/sil && cat results/sil/fdd_metrics.json
python3 tests/common/modbus_client.py --port 5020 read 8 1     # on the demo board: 0 = healthy
```

## 6. Tests and results

| Check | Result |
| --- | --- |
| Gate `make test-all` | host 93/93 (82 unit tests x3: normal, ASan+UBSan, Valgrind; 11 integration), guest 101/101, 0 skipped |
| `hvac_fdd.c` coverage | 100 % lines, 93.5 % branches |
| Detection matrix | 5/5 detected: capacity loss 21.1 min, compressor failure 21.1 min, stuck sensor 29.9 min (all three days) |
| False-alarm matrix | 0 of 15 healthy days (3 scenarios x 5 seeds) |
| `fdd_mean_time_to_detect_min` | 26.4 min |
| Daemon test | stuck sensor sets register 8 bit1 within the 60 s window, logged exactly once |
| Metrics | 45/45 on target |

## 7. Key concepts

- **FDD.** Fault detection (something is wrong) and diagnostics (what is wrong), from operating data.
- **Rule-based vs machine learning.** Rules are transparent and need no training data, but they only find
  what someone thought of. Machine-learning methods can find subtler patterns but need labelled data. A
  next step would be the public fault datasets from Lawrence Berkeley National Laboratory (LBNL).
- **Detection rate, false alarms, time to detect.** The three numbers that decide whether people trust a
  diagnostic. Too many false alarms and operators start ignoring all of them.
- **A fault is only visible when the part is used.** A broken compressor on a day without cooling demand
  stays hidden. That is why the cooling faults are only in the hot-day matrix.

## 8. What could go wrong

- Windows too short give false alarms (the first 20 minutes of a pull-down can be slow at noon).
  Windows too long detect late. The defaults gave 0 false alarms in 15 healthy days.
- A stuck sensor that freezes at a high value keeps the compressor running, so bit0 can also appear.
  That is reported, not counted against the bit1 detection.
- The thresholds are tuned to this simulated room. A real building would need field data to tune them.

## 9. Interview questions

1. *How does your system detect a failing compressor?* If the compressor runs a full 20-minute window and
   the room cools less than 0.5 °C, it sets a diagnostic bit, published over Modbus and logged.
2. *How did you measure the detector?* Injected 5 faults (100 % detected, 26.4 minutes average) and ran
   15 healthy days (0 false alarms).
3. *Rule-based or ML?* Rule-based first: explainable, no data needed, runs on the controller. ML could
   later find subtler faults, given labelled field data.
4. *Why can't a cooling fault be found on a cold day?* The compressor never runs, so there is nothing to
   observe. Detection needs the equipment to be used.
