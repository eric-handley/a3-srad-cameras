#pragma once

#define EN_DEBUG_PRINT 1
// #define EN_IMU_DEBUG 1
#define EN_THREADS 1
// #define EN_FC_COMMS 1

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
#define PMIC_I2C hi2c2
#define IMU_I2C  hi2c3

/*
    @brief Convert milliseconds to ThreadX ticks (rounds up)
*/
#define MS_TO_TICKS(ms) ((((ms) * TX_TIMER_TICKS_PER_SECOND) + 999) / 1000)

#define TICKS_TO_MS(ticks) (((ticks) * 1000) / TX_TIMER_TICKS_PER_SECOND)

/*
    @brief Error catching macro. Returns stdbool::false if expected value is not returned (exits function)
*/
#define ASSERT(expected, call) \
    do { \
        int32_t ret = (call); \
        if (ret != (expected)) { \
            return false; \
        } \
    } while(false) // Allow usage as single statement

// Allow queue access from threads
extern TX_QUEUE imu_data_queue_handle;

typedef struct imu_data_t {
    int16_t accel[3];  // x, y, z
    int16_t gyro[3];   // x, y, z
    int16_t temp;
} imu_data_t;

__attribute__((format(printf, 1, 2)))
static inline void debug(const char* fmt, ...) {
    #ifdef EN_DEBUG_PRINT
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (len > 0) {
            HAL_UART_Transmit(&huart2, (uint8_t*)buf, (len < sizeof(buf)) ? len : sizeof(buf) - 1, HAL_MAX_DELAY);
        }
    #else
        (void)fmt;  // Suppress unused parameter warning
    #endif
}