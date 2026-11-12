#ifndef NTC_TEMP_H
#define NTC_TEMP_H

#include <stdbool.h>
#include <stdint.h>

#ifndef NTC_TEMP_LOG_ENABLE
#define NTC_TEMP_LOG_ENABLE 0
#endif

/* 0: no motor gate; 1: settled-stop sampling or same-channel ADC trend admission. */
#ifndef NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE
#define NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE 1
#endif
#if NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE != 0 && NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE != 1
#error "NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE must be 0 or 1"
#endif

#ifndef NTC_TEMP_MACHINE3_PRIMARY_CHANNEL
#define NTC_TEMP_MACHINE3_PRIMARY_CHANNEL 1
#endif

/* Select the NTC calibration: 1 = 1号机, 2 = 2号机, 3 = 3号机,
 * 4 = 4号机, 6 = 6号机. */
#ifndef MACHINE
#define MACHINE 3
#endif

#if MACHINE != 1 && MACHINE != 2 && MACHINE != 3 && MACHINE != 4 && MACHINE != 6
#error "MACHINE must be 1, 2, 3, 4 or 6"
#endif

#if MACHINE == 3 && (NTC_TEMP_MACHINE3_PRIMARY_CHANNEL < 1 || \
					 NTC_TEMP_MACHINE3_PRIMARY_CHANNEL > 3)
#error "NTC_TEMP_MACHINE3_PRIMARY_CHANNEL must be 1, 2 or 3"
#endif

#if MACHINE == 3
#define NTC_TEMP_MACHINE3_DEFAULT_WATER_VOLUME_ML 1150U

typedef struct {
    uint16_t power_gear;
    int16_t motor_gear;
    float heater_on_seconds;
    float heater_off_seconds;
    float motor_on_seconds;
    float motor_off_seconds;
    uint16_t water_volume_ml; /* Requested water volume in mL; nearest supported group if absent. */
} NtcTempMachine3OperatingConditions;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Return the primary station-2 temperature in Celsius.
 * MACHINE=1 (1号机): reads the effective active heater gear from DataCenter and
 *   uses the calibrated ADC x gear 2D table, with linear interpolation between
 *   ADC nodes and gear anchors. Outside a gear curve's measured ADC span, falls
 *   back to the legacy one-dimensional NTC2 table.
 * MACHINE=2 (2号机): uses its original one-dimensional NTC2 calibration table.
 * MACHINE=3 (3号机): isolates ADC jumps over 200 counts in either direction.
 *   Rejected values do not enter the 20-sample median or train the trend.
 *   A new ADC level needs >=8 consecutive points spanning >=140 ms, with gaps
 *   <=2 s and spread <=40 ADC + 1000 ADC/s times candidate duration; invalid/normal points break the run.
 *   It queries each channel's operating-condition database using the
 *   effective heater gear, water volume (default 1150 mL), configured heater/motor duty cycles, and heating or
 *   cooling phase, then applies robust trend/residual filtering over 100
 *   accepted samples using real microsecond timestamps. A persistent new
 *   temperature requires >=8 coherent points spanning >=140 ms, spread within
 *   0.2 C + 5 C/s times candidate duration, and a change within
 *   0.5 C + 5 C/s times the elapsed accepted-sample gap. Adjacent candidate
 *   changes obey the same base tolerance plus rate times their own gap.
 *   Substituted/held values never enter the trend history. The no-argument
 *   getter derives these conditions by selecting the nearest temperature threshold
 *   for the active gear; callers with explicit conditions can use
 *   ntcTempGetTemperatureWithConditions(). If the database profile or ADC range
 *   is unavailable, the latest valid temperature is held. When a cooling
 *   ADC is colder than the measured cooling range, the machine's measured
 *   low-temperature heating group may supplement it; no unmeasured ADC is
 *   accepted beyond the existing 5 C extrapolation guard. A matched profile permits linear extrapolation
 *   up to 5 C beyond either ADC boundary; farther out it is unavailable.
 *   The heating table remains selected for 10 s after the last confirmed
 *   heater-on feedback; afterward the existing trend latch applies.
 *   Cooling is latched with heaters off when the main temperature reaches
 *   98 C on that channel and its own latest 20 ADC samples trend upward
 *   (the newest 10-sample mean is above the oldest 10-sample mean). Reheating
 *   or total-power-off clears the latch. Motor-cycle cooling has a separate
 *   measured database; while empty it falls back to legacy cooling rows.
 *   It then uses
 *   NTC_TEMP_MACHINE3_PRIMARY_CHANNEL (default 1) as the application temperature;
 *   all three channels remain independently filtered/logged. No automatic
 *   fallback to other channels is performed. A rejected primary sample keeps
 *   that channel's own last valid result, or NAN if none exists. Each channel
 *   owns its phase, profile selection and windows; its phase change resets only
 *   its own windows. Hardware power/motor feedback may be shared, NTC values may not.
 *   Motor-running data needs >=8 own timestamped ADCs spanning >=140 ms,
 *   clean history/residual <=40 ADC, step <=200 ADC and latest age <=2 s.
 *   DataCenter's temperature filter remains downstream.
 * MACHINE=4 (4号机): uses the independent NTC3 calibration table.
 * MACHINE=6 (6号机): uses the independent one-dimensional machine-6 table.
 * All temperature outputs are limited to [25, 100] C; NaN stays unavailable.
 * All primary-temperature modes: [97.9, 98.0) C is reported as 98 C; calculated
 *   values from 98 through 100 C are preserved.
 * Check ntcTempIsInRange() before using the result as calibrated.
 */
float ntcTempGetTemperature(void);
/* True only if the latest primary conversion accepted a new ADC/temperature. */
bool ntcTempPrimarySampleAccepted(void);

#if MACHINE == 3
/** Read the primary temperature using the supplied machine-3 operating group.
 * Select the nearest group by power gear, then water volume, then motor gear;
 * duty-cycle timing distance breaks remaining ties within the same phase/channel.
 * If the selected group cannot cover the ADC, the latest valid temperature is held,
 * except for the measured low-temperature supplement to the cooling group.
 * The motor must still
 * obey the live feedback gate only when motor-stop optimization is enabled.
 */
float ntcTempGetTemperatureWithConditions(
    NtcTempMachine3OperatingConditions conditions);
#endif

/** Reset the machine-3 rolling ADC median window. */
void ntcTempResetFilter(void);

/** Clear pending evidence after a motor/profile boundary, retain the last real
 * temperature/time anchor and trusted ADC median so pauses cannot bypass checks. */
void ntcTempResetTemperatureTrend(void);

/** Temporarily allow machine-3 conversion while the motor is coasting down. */
void ntcTempSetMotorStopSettleOverride(bool enabled);

/** True only inside the measured ADC range, including both endpoints.
 * Zero, open/short circuit values and unmeasured ranges return false.
 */
bool ntcTempIsInRange(uint32_t adc);

/** Read station-2 NTC1 ADC and return its independent calibrated temperature.
 * MACHINE=3 selects NTC1's own heating or cooling curve; other machines retain
 * the legacy NTC1 table.
 */
float ntcTempGetNtc1Temperature(void);

/** Check the NTC1 measured ADC range (separate from NTC2). */
bool ntcTempNtc1IsInRange(uint32_t adc);

#ifdef __cplusplus
}
#endif

#endif
