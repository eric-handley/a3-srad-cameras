#include "status_led_thread.h"

#define STATUS_LED_PORT GPIOB
#define STATUS_LED_PIN  GPIO_PIN_12

VOID status_led_thread(ULONG thread_input) {
    (void)thread_input;

    debug("[LED]\tStatus LED thread started\r\n");

    for (int i = 0; i < 6; i++) {
        HAL_GPIO_TogglePin(STATUS_LED_PORT, STATUS_LED_PIN);
        HAL_Delay(200);
    }
}
