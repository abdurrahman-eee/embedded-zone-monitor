#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "hardware_sim.h"

/* ------------------------------------------------------------------ */
/*  Config                                                              */
/* ------------------------------------------------------------------ */

#define CRITICAL_TEMP_C       50.0f
#define WATCHDOG_TIMEOUT_MS   2500
#define SENSOR_PERIOD_MS       500
#define CONTROLLER_PERIOD_MS   250
#define STATUS_PERIOD_MS      5000
#define ALARM_ACK_TIMEOUT_MS 10000

#define STACK_WORDS  (configMINIMAL_STACK_SIZE * 4)

/* ------------------------------------------------------------------ */
/*  Zone State                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    float       temp;
    float       setpoint;
    float       hysteresis;
    bool        heater_on;
    bool        fault;
    bool        critical;
} Zone_t;

static Zone_t zones[NUM_ZONES] = {
    { "Server Room", .setpoint = 22.0f, .hysteresis = 1.0f },
    { "Warehouse",   .setpoint = 18.0f, .hysteresis = 2.0f },
    { "Lab",         .setpoint = 25.0f, .hysteresis = 0.5f },
};

static SemaphoreHandle_t zone_mutex[NUM_ZONES];

typedef enum {
    ALARM_IDLE,
    ALARM_ACTIVE,
    ALARM_ACKNOWLEDGED,
} AlarmState_t;

static volatile AlarmState_t alarm_state = ALARM_IDLE;
static SemaphoreHandle_t alarm_trigger;
static TaskHandle_t watchdog_handle = NULL;

static SemaphoreHandle_t print_mutex;

static void safe_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    xSemaphoreTake(print_mutex, portMAX_DELAY);
    hw_log(buf);
    xSemaphoreGive(print_mutex);
}

static void vSensorTask(void *pvParams) {
    int zone = (int)(uintptr_t)pvParams;

    for (;;) {
        float temp = hw_read_temp(zone);

        if (isnan(temp)) {
            xSemaphoreTake(zone_mutex[zone], portMAX_DELAY);
            if (!zones[zone].fault) {
                zones[zone].fault = true;
                safe_printf("[SENSOR] Zone %d (%s): sensor read FAILED\r\n",
                            zone, zones[zone].name);
                xSemaphoreGive(zone_mutex[zone]);
                xSemaphoreGive(alarm_trigger);
            } else {
                xSemaphoreGive(zone_mutex[zone]);
            }
            vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
            continue;
        }

        xSemaphoreTake(zone_mutex[zone], portMAX_DELAY);
        zones[zone].temp     = temp;
        zones[zone].fault    = false;
        zones[zone].critical = (temp >= CRITICAL_TEMP_C);
        bool went_critical   = zones[zone].critical;
        xSemaphoreGive(zone_mutex[zone]);

        if (watchdog_handle)
            xTaskNotify(watchdog_handle, (1u << zone), eSetBits);

        if (went_critical) {
            safe_printf("[SENSOR] Zone %d (%s): CRITICAL! %.1f C >= %.1f C\r\n",
                        zone, zones[zone].name, temp, CRITICAL_TEMP_C);
            xSemaphoreGive(alarm_trigger);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

static void vControllerTask(void *pvParams) {
    (void)pvParams;

    for (;;) {
        bool system_safe = (alarm_state == ALARM_IDLE);

        for (int i = 0; i < NUM_ZONES; i++) {
            xSemaphoreTake(zone_mutex[i], portMAX_DELAY);
            Zone_t *z = &zones[i];

            bool want_on = z->heater_on;

            if (!system_safe || z->fault || z->critical) {
                want_on = false;
            } else {
                float lo = z->setpoint - z->hysteresis;
                float hi = z->setpoint + z->hysteresis;
                if      (z->temp < lo) want_on = true;
                else if (z->temp > hi) want_on = false;
            }

            if (want_on != z->heater_on) {
                z->heater_on = want_on;
                hw_set_heater(i, want_on);
            }

            xSemaphoreGive(zone_mutex[i]);
        }

        vTaskDelay(pdMS_TO_TICKS(CONTROLLER_PERIOD_MS));
    }
}

static void vAlarmTask(void *pvParams) {
    (void)pvParams;

    for (;;) {
        xSemaphoreTake(alarm_trigger, portMAX_DELAY);

        if (alarm_state != ALARM_IDLE)
            continue;

        alarm_state = ALARM_ACTIVE;
        hw_set_buzzer(true);
        safe_printf("\r\n[ALARM] *** ALARM ACTIVE *** Heaters disabled.\r\n");

        for (int i = 0; i < NUM_ZONES; i++) {
            xSemaphoreTake(zone_mutex[i], portMAX_DELAY);
            if (zones[i].critical)
                safe_printf("[ALARM]   Zone %d (%s): CRITICAL TEMP = %.1f C\r\n",
                            i, zones[i].name, zones[i].temp);
            if (zones[i].fault)
                safe_printf("[ALARM]   Zone %d (%s): SENSOR FAULT\r\n",
                            i, zones[i].name);
            xSemaphoreGive(zone_mutex[i]);
        }

        safe_printf("[ALARM] Auto-acknowledging in %d seconds...\r\n",
                    ALARM_ACK_TIMEOUT_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(ALARM_ACK_TIMEOUT_MS));

        alarm_state = ALARM_ACKNOWLEDGED;
        hw_set_buzzer(false);
        safe_printf("[ALARM] Acknowledged. Waiting for zones to clear...\r\n");

        for (;;) {
            bool all_clear = true;

            for (int i = 0; i < NUM_ZONES; i++) {
                xSemaphoreTake(zone_mutex[i], portMAX_DELAY);
                if (zones[i].critical || zones[i].fault) all_clear = false;
                xSemaphoreGive(zone_mutex[i]);
            }

            if (all_clear) {
                alarm_state = ALARM_IDLE;
                safe_printf("[ALARM] All zones clear. System NORMAL.\r\n\r\n");
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

static void vWatchdogTask(void *pvParams) {
    (void)pvParams;

    for (;;) {
        uint32_t live = 0;

        xTaskNotifyWait(0, 0xFFFFFFFF, &live, pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS));

        for (int i = 0; i < NUM_ZONES; i++) {
            bool checked_in = (live & (1u << i)) != 0;

            xSemaphoreTake(zone_mutex[i], portMAX_DELAY);
            bool was_fault = zones[i].fault;

            if (!checked_in && !zones[i].fault) {
                zones[i].fault = true;
                xSemaphoreGive(zone_mutex[i]);

                safe_printf("[WATCHDOG] Zone %d (%s): task UNRESPONSIVE!\r\n",
                            i, zones[i].name);
                xSemaphoreGive(alarm_trigger);

            } else if (checked_in && was_fault) {
                zones[i].fault = false;
                xSemaphoreGive(zone_mutex[i]);

                safe_printf("[WATCHDOG] Zone %d (%s): task recovered.\r\n",
                            i, zones[i].name);
            } else {
                xSemaphoreGive(zone_mutex[i]);
            }
        }
    }
}

static const char *alarm_str[] = { "NORMAL", "ACTIVE", "ACKNOWLEDGED" };

static void vStatusTask(void *pvParams) {
    (void)pvParams;
    char buf[512];

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));

        int  pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "\r\n+-------+----------------+--------+--------+--------+----------+\r\n"
            "| Zone  | Name           |  Temp  |  Set   | Heater | State    |\r\n"
            "+-------+----------------+--------+--------+--------+----------+\r\n");

        for (int i = 0; i < NUM_ZONES && pos < (int)sizeof(buf) - 80; i++) {
            xSemaphoreTake(zone_mutex[i], portMAX_DELAY);

            const char *state = "ok";
            if      (zones[i].critical) state = "CRITICAL";
            else if (zones[i].fault)    state = "FAULT";

            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "|  %d    | %-14s | %5.1fC | %5.1fC |  %-3s   | %-8s |\r\n",
                i, zones[i].name,
                zones[i].temp, zones[i].setpoint,
                zones[i].heater_on ? "ON" : "OFF",
                state);

            xSemaphoreGive(zone_mutex[i]);
        }

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "+-------+----------------+--------+--------+--------+----------+\r\n"
            "  Alarm: %s\r\n\r\n",
            alarm_str[alarm_state]);

        xSemaphoreTake(print_mutex, portMAX_DELAY);
        hw_log(buf);
        xSemaphoreGive(print_mutex);
    }
}

int main(void) {
    hw_init();

    for (int i = 0; i < NUM_ZONES; i++) {
        zone_mutex[i] = xSemaphoreCreateMutex();
        configASSERT(zone_mutex[i]);
    }
    alarm_trigger = xSemaphoreCreateBinary();
    print_mutex   = xSemaphoreCreateMutex();
    configASSERT(alarm_trigger && print_mutex);

    xTaskCreate(vWatchdogTask, "Watchdog", STACK_WORDS, NULL, 4, &watchdog_handle);

    for (int i = 0; i < NUM_ZONES; i++) {
        xTaskCreate(vSensorTask, "Sensor", STACK_WORDS,
                    (void *)(uintptr_t)i, 3, NULL);
    }

    xTaskCreate(vControllerTask, "Controller", STACK_WORDS, NULL, 3, NULL);
    xTaskCreate(vAlarmTask,      "Alarm",      STACK_WORDS, NULL, 2, NULL);
    xTaskCreate(vStatusTask,     "Status",     STACK_WORDS, NULL, 1, NULL);

    printf("[MAIN] Starting scheduler. Faults will be injected automatically.\r\n\n");

    vTaskStartScheduler();

    printf("[MAIN] Scheduler returned — out of heap?\r\n");
    return 1;
}
