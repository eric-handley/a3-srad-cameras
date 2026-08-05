#include "status_led_thread.h"

#define STATUS_LED_PORT GPIOB
#define STATUS_LED_PIN  GPIO_PIN_12

static void blink(int duration) {
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, 1);
    tx_thread_sleep(MS_TO_TICKS(duration));
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, 0);
    tx_thread_sleep(MS_TO_TICKS(duration));
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
                blink(200);
                tx_thread_sleep(MS_TO_TICKS(5000));
                break;
                
            case LED_ERROR:
                blink(100);
                tx_thread_sleep(MS_TO_TICKS(100));
                break;
                
            case LED_RECORDING:
                blink(800);
                break;
            
            default:
                break;
        }

    }
}
