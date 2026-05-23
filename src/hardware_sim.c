#include "hardware_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

#define SIM_DT_S    0.5f

typedef struct {
    float temp;
    float ambient;
    float heater_target;
    float tau;
    bool  heater_on;
    int   fault_at_tick;
    int   spike_at_tick;
    int   tick;
} ZoneSim_t;

static ZoneSim_t sims[NUM_ZONES] = {
    { .temp=15.0f, .ambient=15.0f, .heater_target=30.0f, .tau=15.0f,
      .fault_at_tick=-1, .spike_at_tick=-1 },
    { .temp=16.0f, .ambient=16.0f, .heater_target=28.0f, .tau=20.0f,
      .fault_at_tick=20, .spike_at_tick=-1 },
    { .temp=20.0f, .ambient=20.0f, .heater_target=35.0f, .tau=8.0f,
      .fault_at_tick=-1, .spike_at_tick=30 },
};

void hw_init(void) {
    srand((unsigned int)time(NULL));
    printf("[SIM]  3-zone HVAC simulation started.\n");
    printf("[SIM]  Zone 1 sensor fault  @ ~10s\n");
    printf("[SIM]  Zone 2 temp spike    @ ~15s\n\n");
}

float hw_read_temp(int zone) {
    ZoneSim_t *s = &sims[zone];
    s->tick++;

    float target = s->heater_on ? s->heater_target : s->ambient;
    s->temp += (target - s->temp) * (SIM_DT_S / s->tau);

    if (s->spike_at_tick > 0 && s->tick == s->spike_at_tick) {
        s->temp = 55.0f;
        printf("[SIM]  Zone %d: SPIKE injected -> %.1f C\n", zone, s->temp);
    }

    if (s->fault_at_tick > 0 && s->tick == s->fault_at_tick) {
        printf("[SIM]  Zone %d: sensor fault injected\n", zone);
    }
    if (s->fault_at_tick > 0 && s->tick >= s->fault_at_tick) {
        return NAN;
    }

    float noise = ((float)(rand() % 10) - 5) * 0.05f;
    return s->temp + noise;
}

void hw_set_heater(int zone, bool on) {
    if (sims[zone].heater_on != on) {
        printf("[HW]   Zone %d heater: %s\n", zone, on ? "ON" : "OFF");
        sims[zone].heater_on = on;
    }
}

void hw_set_buzzer(bool on) {
    static bool state = false;
    if (state != on) {
        printf("[HW]   Buzzer: %s\n", on ? "*** ALARM ON ***" : "off");
        state = on;
    }
}

void hw_log(const char *msg) {
    printf("%s", msg);
    fflush(stdout);
}
