# Ext 3: MQTT telemetry

## 1. What this phase adds

`hvacd` can now send its readings to an MQTT broker, the way connected HVAC (heating, ventilation and
air conditioning) and cold-chain products report to the cloud. Every `mqtt_period_s` it publishes one
JSON message, and a status topic tells subscribers whether the thermostat is `online` or `offline`.
The main engineering rule: **network trouble must never disturb control.**

## 2. Why it matters

A building owner wants to see every unit on one dashboard, and a service company wants data for fault
detection. MQTT (Message Queuing Telemetry Transport) is the usual protocol for that: small messages,
a simple publish/subscribe model, and it works over unreliable links. But the network *is* unreliable,
and a thermostat that stops controlling because Wi-Fi dropped would be a dangerous product. So the
design question is not "how do we send data" but "how do we send data without ever waiting for it".

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `thermostat/include/telemetry_json.h`, `src/telemetry_json.c` | Pure JSON formatter, fixed 512-byte buffer, reports truncation |
| `thermostat/include/telemetry_mqtt.h`, `src/telemetry_mqtt.c` | libmosquitto client: non-blocking step, 5 s reconnect back-off, last will |
| `thermostat/src/main.c` | Calls the MQTT step every loop iteration; publishes after each control tick; debug `tick` line |
| `thermostat/src/config.c` | `mqtt_enabled`, `mqtt_host`, `mqtt_port`, `mqtt_device_id`, `mqtt_period_s` |
| `thermostat/src/modbus_regs.c` | Bug fix found by the new tests: `mb_mc_to_cc()` overflowed at the int32 limits |
| `thermostat/tests/test_telemetry_json.c` | 7 formatter tests (values, negatives, null, extremes, truncation) |
| `tests/common/mqtt_helpers.py` | Private broker on port 18830 and a `mosquitto_sub` subscriber for tests |
| `tests/host/test_telemetry_host.py` | 5 host tests (payload, late broker, last will, clean shutdown, bad id) |
| `tests/guest/test_mqtt.py` | 5 board tests (period, payload, kill -9, broker outage, reconnection) |
| `br2-external/...`, `thermostat/config/hvacd.conf` | mosquitto in the image; the board sends to its own broker |

## 4. How it works

```
             one select() loop, one thread
  +------------------------------------------------------------+
  | every iteration: mosquitto_loop(mosq, 0, 1)   <- timeout 0 |
  |                  (never waits; reconnect every 5 s if down)|
  | every 200 ms:    read sensor -> hvac_core -> Modbus regs   |
  | every period:    regs snapshot -> JSON -> publish (QoS 1)  |
  +------------------------------------------------------------+
        |                                   ^
        v  hvac/hvac-01/telemetry           |  hvac/hvac-01/status
   +----------+                        "online" (retained, after connect)
   |  broker  | ---> subscribers       "offline" (last will, or clean stop)
   +----------+
```

1. **Publish/subscribe.** `hvacd` never talks to a dashboard directly. It publishes to a *topic* on a
   broker; anyone subscribed to that topic gets a copy. Adding a second dashboard needs no change here.
2. **QoS 1 (quality of service "at least once").** The broker acknowledges every message, and the
   library resends any message that was not acknowledged. A subscriber may see a duplicate, never a
   silent loss while the connection is up.
3. **Retained message.** The broker keeps the last `online`/`offline` and hands it to every new
   subscriber at once, so a dashboard that starts later still knows the state.
4. **Last will.** At connect time `hvacd` tells the broker: "if I vanish without saying goodbye,
   publish `offline` for me". `kill -9` or a power cut therefore still produces `offline`.
5. **Never blocking.** The connection is opened with `mosquitto_connect_async()`, and each loop runs
   one `mosquitto_loop(mosq, 0, 1)` step with a zero timeout. If the broker is down, the step fails at
   once, and the next connection attempt waits 5 s. While disconnected, messages are skipped, not
   queued. The message number `seq` still advances, so a subscriber can see exactly what it missed.
6. **One source of truth.** The JSON is built from the same status as the Modbus registers, with the
   same temperature rounding, so the two interfaces can never disagree.

## 5. Run it yourself

```
make run                                         # boots the board; hvacd sends to the local broker
mosquitto_sub -t 'hvac/#' -v                     # on the board: status and a message every 5 s
hvac/hvac-01/status online
hvac/hvac-01/telemetry {"device":"hvac-01","seq":0,"uptime_s":5,"temp_c":25.00,"state":"IDLE",...}
```

Demo mode itself is not part of the automated tests. The same daemon, library and broker are
tested on the board by `tests/guest/test_mqtt.py`, and the boot log shows `Starting mosquitto: OK`.

## 6. Tests and results

| Check | Result |
| --- | --- |
| Gate `make test-all` | host 107/107 (91 unit tests x3: normal, ASan+UBSan, Valgrind; 16 integration), guest 106/106, 0 skipped |
| `telemetry_json.c` coverage | 96.2 % lines, 94.4 % branches (`telemetry_mqtt.c` is covered by host and guest integration tests) |
| `mqtt_messages_received_ratio` | 1.0 (11 of 11 messages over 10 periods; every gap within 20 % of 1 s) |
| `control_loop_jitter_broker_down_ms` | 4 ms (200 ms loop, broker stopped for 12 s, Modbus answered every read) |
| `mqtt_reconnect_s` | 4.78 s from broker restart to `online` (limit 15 s; back-off 5 s) |
| Last will | `kill -9` gives retained `offline` (host and board); clean stop also publishes `offline` |
| Valgrind (daemon with MQTT on) | 0 errors, 0 bytes definitely or indirectly lost |
| Metrics | 48/48 on target |

## 7. Key concepts

- **Broker, topic, publish, subscribe.** The broker is a post office; topics are addresses such as
  `hvac/hvac-01/telemetry`; `#` subscribes to everything below a level.
- **QoS 0 / 1 / 2.** At most once / at least once / exactly once. QoS 1 is the usual choice for
  telemetry: reliable, and duplicates are harmless because every message carries `seq`.
- **Retained message and last will.** Together they give a reliable "is this device online?" signal.
- **Decoupling control and telemetry.** Control is safety-critical and must run on time; telemetry is
  "nice to have". The loop never waits for the network, and the test proves it: with the broker
  stopped, the 200 ms loop period changed by at most 4 ms.

## 8. What could go wrong

- **A host name instead of an IP address.** Looking up a name (DNS) can block for seconds, so the board
  configuration uses `127.0.0.1`. A product would resolve names outside the control loop (D-029).
- **Messages while offline are lost, not stored.** That keeps memory use bounded. A product that must
  not lose data would buffer a limited number of messages on disk.
- **No security.** The test and demo brokers accept anyone on localhost. A real deployment needs TLS
  (Transport Layer Security) and a password or client certificate.
- **The bug the formatter tests found.** Feeding the extreme value `INT32_MIN` into `mb_mc_to_cc()` made
  `-mc` overflow, which is undefined behavior in C. Real sensor values never get near it, but the
  sanitizer build caught it at once. The fix computes in 64 bits; a regression test keeps it fixed.

## 9. Interview questions

1. *How do you make sure telemetry cannot break control?* One thread, a zero-timeout network step, a
   non-blocking connect, a fixed 5 s reconnect back-off, and no queueing while offline. A test stops
   the broker and measures the control-loop period: it changed by at most 4 ms (limit 100 ms).
2. *What is a last will?* A message the broker publishes for a client that disappears without a
   clean disconnect. Here it is a retained `offline` on the status topic.
3. *Why QoS 1 and not 2?* At-least-once is enough, because `seq` lets the receiver drop duplicates.
   QoS 2 costs two more packets per message.
4. *How would you scale this to 1,000 units?* One topic per device (`hvac/<id>/...`), a broker
   cluster in the cloud, TLS with per-device certificates, and subscribers that use wildcards.
