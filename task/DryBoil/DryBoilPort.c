#include "DryBoilPort.h"

#include <string.h>
#include "DataCenter.h"
#include "ModbusMap.h"

static const dry_boil_heater_mode_t heater_modes[] = {
    DRY_BOIL_HEATER_1, DRY_BOIL_HEATER_2, DRY_BOIL_HEATERS_SAME_GEAR
};

static bool read_sample(dry_boil_sample_t *sample, void *context)
{
    const dry_boil_heater_mode_t mode = *(const dry_boil_heater_mode_t *)context;
    uint16_t power[MB_POWER_INPUT_COUNT];
    if (!ModbusCopyCachedInputRegisters(MODBUS_SLAVE_POWER, 0,
            MB_POWER_INPUT_COUNT, power)) {
        return false;
    }
    sample->temperature_c = getTemp();
    sample->temperature_valid = true;

    /* Use board feedback, not requested gear or SystemMoniter watt values. */
    uint16_t p1 = power[MB_POWER_INPUT_HEATER_1_GEAR];
    uint16_t p2 = power[MB_POWER_INPUT_HEATER_2_GEAR];
    if (p1 > 13u || p2 > 13u) {
        return false;
    }
    if (!power[MB_POWER_INPUT_ENABLE]) {
        p1 = p2 = 0;
    } else {
        if (!power[MB_POWER_INPUT_HEATER_1_ENABLED]) p1 = 0;
        if (!power[MB_POWER_INPUT_HEATER_2_ENABLED]) p2 = 0;
    }
    switch (mode) {
        case DRY_BOIL_HEATER_1:
            /* Lower-heater calibration uses only its actual feedback gear.
             * The upper heater may run independently in existing recipes. */
            sample->actual_power_0_13 = (uint8_t)p1;
            break;
        case DRY_BOIL_HEATER_2:
            if (p1 != 0u) return false;
            sample->actual_power_0_13 = (uint8_t)p2;
            break;
        case DRY_BOIL_HEATERS_SAME_GEAR:
            if (p1 != p2) return false;
            sample->actual_power_0_13 = (uint8_t)p1;
            break;
        default:
            return false;
    }
    return true;
}

static void stop_heating(void *context)
{
    (void)context;
    setPowerHeater1(0);
    setPowerHeater2(0);
}

static bool input_ready(void *context)
{
    (void)context;
    uint16_t probe[2];
    return ModbusCopyCachedInputRegisters(MODBUS_SLAVE_POWER, 0, 1, probe);
}

static uint32_t heating_sequence(void *context)
{
    (void)context;
    return DataCenterHeatingCommandSequence();
}

static void stop_for_sequence(uint32_t sequence, void *context)
{
    (void)context;
    (void)DataCenterStopHeatersIfUnchanged(sequence);
}

void DryBoilPortInit(dry_boil_port_t *port)
{
    *port = (dry_boil_port_t) {
        .read_sample = read_sample,
        .stop_heating = stop_heating,
        .context = (void *)&heater_modes[0],
        .ready = input_ready,
        .heating_sequence = heating_sequence,
        .stop_for_sequence = stop_for_sequence,
    };
}

esp_err_t DryBoilTaskStart(dry_boil_heater_mode_t mode)
{
    if (mode < DRY_BOIL_HEATER_1 || mode > DRY_BOIL_HEATERS_SAME_GEAR) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!input_ready(NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    const dry_boil_port_t port = {
        .read_sample = read_sample,
        .stop_heating = stop_heating,
        .context = (void *)&heater_modes[mode - 1],
        .heating_sequence = heating_sequence,
        .stop_for_sequence = stop_for_sequence,
    };
    return DryBoilTaskStartWithPort(&port);
}
