#pragma once

// #define EN_DEBUG_PRINT 1
// #define EN_IMU_DEBUG 1
#define EN_THREADS 1
#define EN_FC_COMMS 1

#include "stm32u0xx_hal.h"
#include "stm32u0xx_hal_i2c.h"
#include "stm32u0xx_hal_uart.h"

#include "app_threadx.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;
// extern GPIO_TypeDef/

#define SOC_UART huart1
#define FC_UART  huart2
#define DBG_UART huart2
#define PMIC_I2C hi2c2
#define IMU_I2C  hi2c3

typedef enum LED_Status_T {
    LED_NOMINAL,
    LED_ERROR,
    LED_RECORDING
} LED_Status_T;

extern LED_Status_T LED_STATUS;

/*
    @brief Convert milliseconds to ThreadX ticks (rounds up)
*/
#define MS_TO_TICKS(ms) ((((ms) * TX_TIMER_TICKS_PER_SECOND) + 999) / 1000)

#define TICKS_TO_MS(ticks) (((ticks) * 1000) / TX_TIMER_TICKS_PER_SECOND)

/*
    @brief Error catching macro. Returns stdbool::false if expected value is not returned (exits function)
*/
#define assert_eq(expected, call) \
    do { \
        int32_t ret = (call); \
        if (ret != (expected)) { \
            return false; \
        } \
    } while(false) // Allow usage as single statement

// Allow queue access from threads
extern TX_QUEUE imu_data_queue_handle;

typedef struct imu_data_t {
    float accel[3];  // x, y, z in g
    float gyro[3];   // x, y, z in dps
    float temp;      // degrees C
} imu_data_t;

/*
    @brief Format 8-bit value as binary string with 4-bit grouping
    @param val 8-bit value to format
    @return Pointer to static buffer containing binary string (e.g. "1010 1100")
    @note Uses ring buffer to support up to 4 concurrent calls in single statement
*/
static const char* bin8(uint8_t val) {
    static char bufs[4][10];  // 4 buffers: "xxxx xxxx\0"
    static uint8_t idx = 0;

    char* buf = bufs[idx];
    idx = (idx + 1) & 0x03;  // Wrap around 0-3

    for (int i = 7; i >= 0; i--) {
        buf[7 - i + (i < 4 ? 1 : 0)] = (val & (1 << i)) ? '1' : '0';
    }
    buf[4] = ' ';  // Space separator between nibbles
    buf[9] = '\0';

    return buf;
}

__attribute__((format(printf, 1, 2)))
static void debug(const char* fmt, ...) {
    #ifdef EN_DEBUG_PRINT
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (len > 0) {
            HAL_UART_Transmit(&DBG_UART, (uint8_t*)buf, (len < sizeof(buf)) ? len : sizeof(buf) - 1, HAL_MAX_DELAY);
        }
    #else
        (void)fmt;  // Suppress unused parameter warning
    #endif
}