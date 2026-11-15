# Ext 1: Watchdog supervision

## 1. What this phase adds

`hvacd` can arm the Linux watchdog. Once armed, the kernel restarts the board unless `hvacd` proves it
is alive at least every `watchdog_timeout_s` seconds. A clean shutdown disarms it.

## 2. Why it matters

An HVAC controller sits in a plant room for years with nobody watching. If a software bug makes the
control loop hang, the building silently overheats or freezes. A watchdog timer turns "hung forever"
into "restarted within seconds". Nearly every embedded product has one.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `thermostat/include/watchdog.h`, `src/watchdog.c` | `wdt_open`, `wdt_kick`, `wdt_close_clean` |
| `thermostat/src/main.c` | Arms the watchdog at start-up, kicks after every completed tick, magic close on exit |
| `thermostat/src/config.c` | Keys `watchdog_enabled` (0), `watchdog_device` (`/dev/watchdog`), `watchdog_timeout_s` (10, range 2..60) |
| `thermostat/tests/test_watchdog.c` | Error paths without a real watchdog |
| `tests/guest/test_watchdog.py` | Healthy, frozen and clean-shutdown scenarios with `softdog` |
| `tests/host/test_daemon_host.py` | Bad device path -> exit code 1 with a clear message |
| `thermostat/config/hvacd.conf`, `S90hvac-demo` | Board config enables it; demo loads `softdog` |

## 4. How it works

```
 hvacd                                  kernel watchdog (softdog: a timer in software)
 open("/dev/watchdog")   ------------>  armed, timeout 10 s
 every tick: control step, then KICK -> timer restarted
 ... loop hangs (no more kicks) ...     timer reaches 0 -> reset the board
 SIGTERM: write 'V', close  -------->   "magic close": disarmed, no reset
```

1. **Kick only after real work.** The kick comes *after* the control step. A loop stuck inside the step
   (for example a blocked read) therefore stops the kicks. Kicking from a separate timer thread would
   keep the watchdog happy even while control was dead, which defeats the purpose.
2. **Magic close.** If a program closes the device without writing `V`, Linux assumes it crashed and
   lets the timer run out. Writing `V` first says "I am stopping on purpose".
3. **NOWAYOUT.** With `CONFIG_WATCHDOG_NOWAYOUT`, even magic close cannot disarm the watchdog: once
   started, only a reboot stops it. Products that must never run unsupervised use it. Our kernel leaves
   it off, so the tests can stop and start the watchdog freely.
4. **Testing without rebooting.** `softdog` loaded with `soft_noboot=1` logs
   `softdog: Triggered - Reboot ignored` instead of resetting, so the test can see the expiry. The tests
   freeze the daemon with `SIGSTOP`, which is exactly what a hang looks like from the outside.

## 5. Run it yourself

```
make run
grep watchdog /var/log/hvacd.log           # ... watchdog /dev/watchdog armed, timeout 10 s
kill -STOP $(cat /var/run/hvacd.pid)       # the board reboots about 10 s later (real softdog)
```

## 6. Tests and results

Gate `make test-all` on 2026-11-15: host **81 passed** (71 unit tests x3 + 10 integration), guest
**101 passed**, 0 failed, 0 skipped.

| Test | What it checks | Result |
| --- | --- | --- |
| `test_watchdog.c` (4) | Bad arguments, missing device (`ENOENT`), non-watchdog file (`ENOTTY`, closed again), safe kick/close when closed | pass |
| `test_config.c` (extended) | New keys, defaults and ranges | pass |
| `test_bad_watchdog_device_exits_one_with_message` (host) | Exit code 1, `cannot open watchdog ...: No such file or directory` | pass |
| `test_healthy_daemon_keeps_watchdog_quiet` | No expiry during 2.5 x timeout | pass |
| `test_frozen_daemon_triggers_watchdog` | `SIGSTOP` -> expiry within timeout + 3 s | pass |
| `test_clean_shutdown_disarms_watchdog` | SIGTERM -> exit 0, no expiry in the next timeout + 3 s | pass |

Metric `watchdog_trigger_latency_s` = **1.94 s** with a 2 s timeout (target <= 5 s).

The first run showed that a killed daemon leaves the watchdog running, and the kernel refuses to unload
`softdog` until it is disarmed. This is the safety behaviour we want, so the test cleanup now disarms the
watchdog itself.

## 7. Key concepts

- **Watchdog timer.** A countdown that resets the system unless software restarts it in time.
- **Kick / pet / feed.** Restarting the countdown. Do it only when the software has proven it works.
- **Fail-safe default.** A hung controller is worse than a rebooting one: the reboot brings the control
  loop back.

## 8. What could go wrong

- Kicking from the wrong place (a timer or a separate thread) hides hangs.
- Forgetting the magic close makes every clean restart look like a crash, and the board reboots.
- A timeout shorter than the slowest legitimate loop iteration causes false resets. The range is 2..60 s
  against a loop period of at most 10 s.

## 9. Interview questions

1. *What is a watchdog and why kick only after the control step?* A hardware or software timer that resets
   the system if not restarted. Kicking after the step proves the step really ran.
2. *What is magic close?* Writing `V` before closing `/dev/watchdog`, telling the driver the stop is
   intentional.
3. *What does NOWAYOUT do?* It makes the watchdog impossible to stop once started, which is used where
   unsupervised operation is unacceptable.
4. *How did you test it without real resets?* `softdog` with `soft_noboot=1`, plus `SIGSTOP` to simulate a
   hang, and a measured trigger latency.
