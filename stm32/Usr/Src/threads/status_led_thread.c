#include "status_led_thread.h"

#define STATUS_LED_PORT GPIOB
#define STATUS_LED_PIN  GPIO_PIN_12

static void blink(int duration) {
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, 1);
    HAL_Delay(duration);
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, 0);
    HAL_Delay(duration);
}

VOID status_led_thread(ULONG thread_input) {
    (void)thread_input;

    debug("[LED]\tStatus LED thread started\r\n");
    
    for (int i = 0; i < 3; i++) {
        blink(200);
    }
    
    while(true) {
        switch (LED_STATUS) {
            case LED_NOMINAL:
                tx_thread_sleep(MS_TO_TICKS(5000));
                blink(200);
                break;
                
            case LED_ERROR:
                tx_thread_sleep(MS_TO_TICKS(100));
                blink(100);
                break;
            
            default:
                break;
        }

    }
}
