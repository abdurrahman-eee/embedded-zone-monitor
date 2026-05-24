# RTOS Zone Temperature Controller

Multi-task temperature control system built on FreeRTOS. 3 zones, each with a sensor, a heater relay, and an independent setpoint. The core challenge: concurrent tasks sharing hardware state, sensor fault handling, and critical condition response — all without deadlocks or race conditions.

Runs on the FreeRTOS POSIX simulator, no hardware required.

---

## Design

Five tasks:

- Three sensor tasks, one per zone — read temperature every 500ms, update shared zone state under a per-zone mutex.
- Controller task — runs at 250ms, evaluates each zone and drives heater relays based on setpoint and hysteresis.
- Alarm task — blocks on a semaphore, activates on any fault or critical temperature, manages the shutdown sequence.
- Watchdog task — collects task notifications from sensor tasks every cycle; any zone that misses the 2.5s window gets flagged as unresponsive.
- Status task — prints a full zone summary every 5 seconds.

Per-zone mutexes instead of a single global lock — sensor tasks run in parallel and have no reason to block each other.

---

## Watchdog

Used `xTaskNotify` for the watchdog rather than polling timestamps. Each sensor task sends a notification bit (`1 << zone_id`) every cycle. The watchdog collects bits over a 2.5s window and checks all three are set. If one isn't, that zone gets marked faulted and the alarm triggers.

This is separate from the NAN check in the sensor task — NAN handles bad hardware readings, the watchdog handles a task that stopped executing entirely. They cover different failure modes.

---

## Hysteresis in the controller

Simple threshold control causes relay chatter — when temperature sits near the setpoint, the heater can toggle hundreds of times per minute. The controller uses a deadband (setpoint ± hysteresis) so the heater only switches state when temperature drifts far enough to warrant it. Values are tunable per zone since each zone has different thermal characteristics.

---

## Build

```bash
git clone --recurse-submodules https://github.com/abdurrahman-eee/embedded-zone-monitor.git
cd embedded-zone-monitor
./setup.sh
./build/rtos_temp_monitor
```

After first build, use `make` to rebuild. Requires gcc and git.

---

## Fault injection

Zone 1 sensor fails at ~10s. Zone 2 temperature spikes to 55°C at ~15s. Both are hardcoded in `hardware_sim.c` to exercise the full alarm and watchdog code paths on every run.

---

## Planned

- Runtime setpoint changes via stdin command parser
- Recoverable sensor fault on Zone 1 to test watchdog clear path
- Circular buffer event log

---

Feedback welcome. Still learning.

