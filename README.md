# RTOS Zone Temperature Controller

Posting a project every week. Embedded systems, low-level C, real problems.

---

Built a multi-task temperature control system using FreeRTOS. The problem I was solving: how do you safely coordinate multiple concurrent tasks that share hardware state, handle sensor failures gracefully, and respond to critical conditions without the whole system falling apart.

The scenario I picked: a 3-zone temperature controller. Three zones, each with a sensor and a heater. The system has to keep each zone at a set temperature without the heater toggling on and off like crazy. And it has to handle two real problems — what if the sensor stops working, and what if the temperature goes dangerously high.

Runs on the FreeRTOS POSIX simulator so no hardware needed to try it.

---

## What I built

Five tasks total:

- Three sensor tasks, one per zone. Each reads temperature and updates shared state.
- A controller task that checks the zone temperatures and decides heater on or off.
- An alarm task that handles faults and critical temperatures.
- A watchdog task that monitors whether the sensor tasks are actually running.
- A status task that prints everything every 5 seconds.

The part I spent the most time on was figuring out the locking. My first attempt used one global mutex for all zones which meant sensor tasks were blocking each other for no reason. Switched to one mutex per zone and it made more sense.

---

## The watchdog thing

This was new to me. I used `xTaskNotify` — each sensor task sends a notification bit to the watchdog every cycle. If a zone doesn't check in within 2.5 seconds the watchdog marks it as faulted and triggers the alarm.

The reason I kept this separate from the NAN check: the NAN check catches bad hardware readings. The watchdog catches a task that stopped running entirely. Different problems, different solutions.

---

## Hysteresis in the controller

First version just did: if temp < setpoint turn on, if temp > setpoint turn off. That causes the heater to switch hundreds of times a minute when the temperature is sitting right at the setpoint. Added a deadband (±hysteresis) so the heater only switches when temperature drifts far enough to actually matter. Small thing but it matters a lot for real relays.

---

## Build

```bash
git clone <this repo>
cd rtos-temp-monitor
./setup.sh
./build/rtos_temp_monitor
```

`setup.sh` clones the FreeRTOS kernel and builds everything. After first run just use `make`.

Needs gcc and git. Tested on macOS.

---

## What happens when you run it

Around 10 seconds in the Zone 1 sensor dies. Around 15 seconds Zone 2 spikes to 55°C. The alarm kicks in, all heaters shut off, buzzer goes on. After 10 seconds it auto-acknowledges and waits for zones to clear.

I hardcoded the fault injection in hardware_sim.c so you can see all the code paths run without having to wait.

---

## What I want to do next

- Add a stdin command parser so I can change setpoints while it's running
- Make the Zone 1 fault temporary so it recovers and I can test the watchdog clearing
- Add an event log with a circular buffer

---

Feedback welcome. Still learning.

