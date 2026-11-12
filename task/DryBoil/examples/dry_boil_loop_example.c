#include "dry_boil_detector.h"

/* Replace these functions with the product HAL/control functions. */
extern float board_read_temperature_c(void);
extern uint8_t heater_get_actual_power_0_13(void);
extern void heater_set_power_0_13(uint8_t power);
extern void board_delay_ms(uint32_t milliseconds);
extern void report_dry_boil_alarm(void);

void heating_process(void)
{
    db_simple_reset(); /* Once at the beginning of each heating cycle. */

    for (;;) {
        if (db_is_dry_boil(board_read_temperature_c(),
                           heater_get_actual_power_0_13())) {
            heater_set_power_0_13(0u); /* Cut power immediately; result is latched. */
            report_dry_boil_alarm();
            break;
        }
        board_delay_ms(500u);
    }
}
