#ifndef HARDWARE_SIM_H
#define HARDWARE_SIM_H

#include <stdbool.h>

#define NUM_ZONES   3

void  hw_init(void);
float hw_read_temp(int zone);
void  hw_set_heater(int zone, bool on);
void  hw_set_buzzer(bool on);
void  hw_log(const char *msg);

#endif
