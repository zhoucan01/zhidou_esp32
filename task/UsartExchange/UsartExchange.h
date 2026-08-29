#ifndef UIINTERACTION__H
#define UIINTERACTION__H
#include "stdint.h"
#include "stdbool.h"
#include "sys.h"


typedef enum 
{
    HEATDOUBLE=0,
    NOBUSHMOTER=1,
    HIGHHEAT1=2,
    HIGHHEAT2=3
}SpeedHeatControl_t;


void SpeedHeatControl(SpeedHeatControl_t SpeedHeatControl,int gear,int dir);
void NoBushRun(int gear);
void Heat1Run(int gear);
void Heat2Run(int gear);

#endif
