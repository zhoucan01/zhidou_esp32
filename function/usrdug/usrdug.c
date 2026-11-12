#include "usrdug.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "UsartExchange.h"
#include "DataCenter.h"

typedef struct {
    enum {
        SOY_COMMAND_MODE,
        SOY_COMMAND_VALVE_OPEN,
        SOY_COMMAND_VALVE_CLOSE,
        SOY_COMMAND_HOSE_FORWARD,
        SOY_COMMAND_HOSE_REVERSE,
        SOY_COMMAND_HOSE_STOP,
        SOY_COMMAND_CALIBRATION_START,
        SOY_COMMAND_CALIBRATION_QUERY,
    } type;
    int mode;
    int start;
} soy_command_t;

static QueueHandle_t commands;

static void soy_mode_task(void *arg)
{
    (void)arg;
    int mode = 0, motorNum = 0, heatNum = 0, heatGear = 0;
    soy_command_t command;
    for (;;) {
        if (xQueueReceive(commands, &command,
                          mode ? pdMS_TO_TICKS(1000) : portMAX_DELAY) == pdTRUE) {
            if (command.type == SOY_COMMAND_CALIBRATION_START) {
                if (setPowerCalibration()) {
                    printf("NB+calibrate=STARTED OK\r\n");
                } else {
                    printf("ERROR:NB_BUSY\r\n");
                }
                continue;
            }
            if (command.type == SOY_COMMAND_CALIBRATION_QUERY) {
                switch (getPowerCalibrationStatus()) {
                    case 0:
                        printf("NB+calibrate=IDLE\r\n");
                        break;
                    case 1:
                        printf("NB+calibrate=CALIBRATING\r\n");
                        break;
                    case 2:
                        printf("NB+calibrate=COMPLETE\r\n");
                        break;
                    case 3:
                        printf("NB+calibrate=TIMEOUT\r\n");
                        break;
                    default:
                        printf("ERROR:CALIBRATION_STATUS_INVALID\r\n");
                        break;
                }
                continue;
            }
            if (command.type != SOY_COMMAND_MODE) {
                switch (command.type) {
                    case SOY_COMMAND_VALVE_OPEN:
                        valveOn();
                        printf("NB+VALVE=OPEN OK\r\n");
                        break;
                    case SOY_COMMAND_VALVE_CLOSE:
                        valveOff();
                        printf("NB+VALVE=CLOSE OK\r\n");
                        break;
                    case SOY_COMMAND_HOSE_FORWARD:
                        hosePumpSetSignedSpeed(90);
                        printf("NB+HOSEPUMP=FORWARD OK\r\n");
                        break;
                    case SOY_COMMAND_HOSE_REVERSE:
                        hosePumpSetSignedSpeed(-90);
                        printf("NB+HOSEPUMP=REVERSE OK\r\n");
                        break;
                    case SOY_COMMAND_HOSE_STOP:
                        hosePumpOff();
                        printf("NB+HOSEPUMP=STOP OK\r\n");
                        break;
                    case SOY_COMMAND_CALIBRATION_START:
                    case SOY_COMMAND_CALIBRATION_QUERY:
                    case SOY_COMMAND_MODE:
                        break;
                }
                continue;
            }
            if (command.mode == 0) {
                mode = motorNum = heatNum = 0;
                heatGear = 0;
                NoBushRun(0);
                Heat1Run(0);
                // Heat2Run(0);
                setPowerEnabled(false);
                printf("NB+SOYMODE=0,0 OK HEAT_GEAR=0\r\n");
            } else if (command.start) {
                if (mode) {
                    NoBushRun(0);
                    Heat1Run(0);
                }
                mode = command.mode;
                motorNum = heatNum = 0;
                heatGear = mode == 1 ? 19 :
                           (mode <= 3 ? 10 : (mode <= 5 ? 9 : 8));
                Heat1Run(heatGear);
                NoBushRun(1);
                printf("NB+SOYMODE=%d,1 OK HEAT_GEAR=%d\r\n",
                       mode, heatGear);
            } else if (mode == command.mode) {
                NoBushRun(0);
                Heat1Run(0);
                mode = 0;
                heatGear = 0;
                printf("NB+SOYMODE=%d,0 OK HEAT_GEAR=0\r\n",
                       command.mode);
            } else {
                printf("NB+SOYMODE=%d,0 INACTIVE\r\n", command.mode);
            }
            continue;
        }

        // One second elapsed, equivalent to CheckDelay(1), but interruptible.
        ++motorNum;
        ++heatNum;
        if (motorNum >= 8) {
            motorNum = 0;
        }
        int heatPeriod = mode <= 2 ? 8 : (mode == 4 ? 21 : 10);
        if (heatNum >= heatPeriod) heatNum = 0;
        NoBushRun(motorNum < 2 ? 1 : 0);
        if (mode >= 3) {
            int onGear = mode == 3 ? 10 : (mode <= 5 ? 9 : 8);
            heatGear = heatNum < 6 ? onGear : 0;
            Heat1Run(heatGear);
        }
        printf("NB+SOYMODE=%d motorNum=%d heatNum=%d HEAT_GEAR=%d\r\n",
               mode, motorNum, heatNum, heatGear);
    }
}

bool usrdug_init(void)
{
    if (commands) return true;
    commands = xQueueCreate(8, sizeof(soy_command_t));
    if (!commands) return false;
    if (xTaskCreate(soy_mode_task, "usrdug", 3072, NULL, 8, NULL) != pdPASS) {
        vQueueDelete(commands);
        commands = NULL;
        return false;
    }
    return true;
}

bool usrdug_command(const char *line)
{
    char compact[48];
    size_t used = 0;
    for (; *line; ++line) {
        if (isspace((unsigned char)*line)) continue;
        if (used == sizeof(compact) - 1) return false;
        compact[used++] = (char)toupper((unsigned char)*line);
    }
    compact[used] = '\0';
    if (strncmp(compact, "NB+", 3) != 0) return false;
    soy_command_t command = {0};
    if (strcmp(compact, "NB+VALVE=OPEN") == 0) {
        command.type = SOY_COMMAND_VALVE_OPEN;
    } else if (strcmp(compact, "NB+VALVE=CLOSE") == 0) {
        command.type = SOY_COMMAND_VALVE_CLOSE;
    } else if (strcmp(compact, "NB+HOSEPUMP=FORWARD") == 0) {
        command.type = SOY_COMMAND_HOSE_FORWARD;
    } else if (strcmp(compact, "NB+HOSEPUMP=REVERSE") == 0) {
        command.type = SOY_COMMAND_HOSE_REVERSE;
    } else if (strcmp(compact, "NB+HOSEPUMP=STOP") == 0) {
        command.type = SOY_COMMAND_HOSE_STOP;
    } else if (strcmp(compact, "NB+CALIBRATE=1") == 0) {
        command.type = SOY_COMMAND_CALIBRATION_START;
    } else if (strcmp(compact, "NB+CALIBRATE=?") == 0) {
        command.type = SOY_COMMAND_CALIBRATION_QUERY;
    } else {
        command.type = SOY_COMMAND_MODE;
    }
    if (command.type != SOY_COMMAND_MODE) {
        if (!commands || xQueueSend(commands, &command, 0) != pdTRUE) {
            printf("ERROR:NB_BUSY\r\n");
        }
        return true;
    }
    int consumed = 0;
    if (sscanf(compact, "NB+SOYMODE=%d,%d%n", &command.mode,
               &command.start, &consumed) != 2 || compact[consumed] != '\0' ||
        command.mode < 0 || command.mode > 7 ||
        (command.mode == 0 && command.start != 0) ||
        (command.start != 0 && command.start != 1)) {
        printf("ERROR:NB_FORMAT expected NB+SOYMODE=1..7,0|1 or 0,0\r\n");
    } else if (!commands || xQueueSend(commands, &command, 0) != pdTRUE) {
        printf("ERROR:NB_BUSY\r\n");
    }
    return true;
}
