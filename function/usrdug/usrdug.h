#ifndef USRDUG_H
#define USRDUG_H

#include <stdbool.h>

bool usrdug_init(void);
/* UART0 NB controls: NB+SOYMODE=<1..7>,<0|1> (or 0,0 to stop);
 * NB+VALVE=OPEN|CLOSE; NB+HOSEPUMP=FORWARD|REVERSE|STOP.
 * NB+CALIBRATE=1 starts station 3 calibration with total enable first;
 * NB+CALIBRATE=? reports its cached state. Pump forward/reverse use 90%. */
bool usrdug_command(const char *line);

#endif
