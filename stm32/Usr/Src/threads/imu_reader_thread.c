#include "imu_reader_thread.h"

// LSM6DSL I2C address (SA0 pin low = 0x6A, high = 0x6B)
#define LSM6DSL_I2C_ADDR    (0x6A << 1)
#define I2C_TIMEOUT_MS      100
#define IMU_INIT_ATTEMPTS    5

// Device context
static stmdev_ctx_t dev_ctx;

int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len) {
    HAL_StatusTypeDef status;
    status = HAL_I2C_Mem_Write(handle, LSM6DSL_I2C_ADDR, reg,
                               I2C_MEMADD_SIZE_8BIT, (uint8_t*)bufp, len, I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? 0 : -1;
}

int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len) {
    HAL_StatusTypeDef status;
    status = HAL_I2C_Mem_Read(handle, LSM6DSL_I2C_ADDR, reg,
                              I2C_MEMADD_SIZE_8BIT, bufp, len, I2C_TIMEOUT_MS);
    return (status == HAL_OK) ? 0 : -1;
}

void platform_delay(uint32_t millisec) {
    tx_thread_sleep(millisec);
}

bool imu_init(void) {
    uint8_t whoami;

    debug_print("[IMU]\tInitializing...\r\n");

    // Initialize device context
    dev_ctx.write_reg = platform_write;
    dev_ctx.read_reg = platform_read;
    dev_ctx.mdelay = platform_delay;
    dev_ctx.handle = &IMU_I2C;

    // Check device ID
    ASSERT(0, lsm6dsl_device_id_get(&dev_ctx, &whoami));

    if (whoami != LSM6DSL_ID) {
        debug_print("[IMU]\tWrong device ID\r\n");
        return false;
    }

    debug_print("[IMU]\tLSM6DSL detected\r\n");

    // Restore default configuration
    ASSERT(0, lsm6dsl_reset_set(&dev_ctx, PROPERTY_ENABLE));
    
    do {
        lsm6dsl_reset_get(&dev_ctx, &whoami);
    } while (whoami);

    // Enable Block Data Update
    ASSERT(0, lsm6dsl_block_data_update_set(&dev_ctx, PROPERTY_ENABLE));

    // Set accelerometer full scale to ±2g
    ASSERT(0, lsm6dsl_xl_full_scale_set(&dev_ctx, LSM6DSL_2g));

    // Set gyroscope full scale to ±2000dps
    ASSERT(0, lsm6dsl_gy_full_scale_set(&dev_ctx, LSM6DSL_2000dps));

    // Set accelerometer output data rate to 104 Hz
    ASSERT(0, lsm6dsl_xl_data_rate_set(&dev_ctx, LSM6DSL_XL_ODR_104Hz));

    // Set gyroscope output data rate to 104 Hz
    ASSERT(0, lsm6dsl_gy_data_rate_set(&dev_ctx, LSM6DSL_GY_ODR_104Hz));

    debug_print("[IMU]\tInitialization complete\r\n");
    return true;
}

int32_t imu_read_accel(int16_t *data) {
    return lsm6dsl_acceleration_raw_get(&dev_ctx, data);
}

int32_t imu_read_gyro(int16_t *data) {
    return lsm6dsl_angular_rate_raw_get(&dev_ctx, data);
}

int32_t imu_read_temp(int16_t *temp) {
    return lsm6dsl_temperature_raw_get(&dev_ctx, temp);
}

VOID imu_reader_thread(ULONG thread_input) {

    int init_attempts = 0;
    while (! imu_init() && (init_attempts < IMU_INIT_ATTEMPTS)) {
        debug_print("[IMU]\tInitialization failed, retrying...\r\n");
        tx_thread_sleep(MS_TO_TICKS(1000));  // Wait 1 second before retrying
        // init_attempts++;
    };

    imu_data_t imu_data = {0};

    while (true) {
        imu_read_accel(imu_data.accel);
        imu_read_gyro(imu_data.gyro);
        imu_read_temp(&imu_data.temp);

        #ifdef EN_IMU_DEBUG
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "[IMU]\tAccel: %6d %6d %6d  Gyro: %6d %6d %6d  Temp: %6d\r\n",
                     imu_data.accel[0], imu_data.accel[1], imu_data.accel[2],
                     imu_data.gyro[0], imu_data.gyro[1], imu_data.gyro[2],
                     imu_data.temp);
            debug_print(buf);

            tx_thread_sleep(MS_TO_TICKS(1000));  // Slow down debug prints
        #endif

        // Place data into message queue for sending to SoC
        tx_queue_send(&imu_data_queue_handle, &imu_data, TX_NO_WAIT);

        // Poll at 100Hz to match IMU sample rate
        tx_thread_sleep(MS_TO_TICKS(10));
    }
}
