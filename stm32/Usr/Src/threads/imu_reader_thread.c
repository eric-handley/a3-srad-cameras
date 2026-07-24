#include "imu_reader_thread.h"

// LSM6DSR I2C address (SA0 pin low = 0x6A, high = 0x6B)
#define LSM6DSR_I2C_ADDR    (0x6A << 1)
#define I2C_TIMEOUT_MS      100
#define IMU_INIT_RETRIES    0

// Device context
static stmdev_ctx_t dev_ctx;

int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len) {
    HAL_StatusTypeDef status;
    status = HAL_I2C_Mem_Write(handle, LSM6DSR_I2C_ADDR, reg,
                               I2C_MEMADD_SIZE_8BIT, (uint8_t*)bufp, len, I2C_TIMEOUT_MS);
    if (status != HAL_OK) {
        debug("[IMU]\tI2C Write Error: %d (addr=0x%02X, reg=0x%02X)\r\n", status, LSM6DSR_I2C_ADDR, reg);
    }
    return (status == HAL_OK) ? 0 : -1;
}

int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len) {
    HAL_StatusTypeDef status;
    status = HAL_I2C_Mem_Read(handle, LSM6DSR_I2C_ADDR, reg,
                              I2C_MEMADD_SIZE_8BIT, bufp, len, I2C_TIMEOUT_MS);
    
    #ifdef IMU_DEBUG
        if (status != HAL_OK) {
            debug("[IMU]\tI2C Read Error: %d (addr=0x%02X, reg=0x%02X)\r\n", status, LSM6DSR_I2C_ADDR, reg);

        }
    #endif
    return (status == HAL_OK) ? 0 : -1;
}

void platform_delay(uint32_t millisec) {
    tx_thread_sleep(millisec);
}

bool imu_init(void) {
    uint8_t whoami;

    debug("[IMU]\tInitializing...\r\n");

    // Scan I2C bus
    // debug("[IMU]\tScanning I2C3 bus...\r\n");
    // for (uint8_t addr = 1; addr < 128; addr++) {
    //     if (HAL_I2C_IsDeviceReady(&IMU_I2C, addr << 1, 1, 10) == HAL_OK) {
    //         debug("[IMU]\tDevice found at 0x%02X\r\n", addr);
    //     }
    // }

    // Initialize device context
    dev_ctx.write_reg = platform_write;
    dev_ctx.read_reg = platform_read;
    dev_ctx.mdelay = platform_delay;
    dev_ctx.handle = &IMU_I2C;

    // Check device ID
    ASSERT(0, lsm6dsr_device_id_get(&dev_ctx, &whoami));

    if (whoami != LSM6DSR_ID) {
        debug("[IMU]\tWrong device ID\r\n");
        return false;
    }

    debug("[IMU]\tLSM6DSR detected\r\n");

    // Restore default configuration
    ASSERT(0, lsm6dsr_reset_set(&dev_ctx, PROPERTY_ENABLE));
    
    do {
        lsm6dsr_reset_get(&dev_ctx, &whoami);
    } while (whoami);

    // Enable Block Data Update
    ASSERT(0, lsm6dsr_block_data_update_set(&dev_ctx, PROPERTY_ENABLE));

    // Set accelerometer full scale to ±2g
    ASSERT(0, lsm6dsr_xl_full_scale_set(&dev_ctx, LSM6DSR_2g));

    // Set gyroscope full scale to ±2000dps
    ASSERT(0, lsm6dsr_gy_full_scale_set(&dev_ctx, LSM6DSR_2000dps));

    // Set accelerometer output data rate to 104 Hz
    ASSERT(0, lsm6dsr_xl_data_rate_set(&dev_ctx, LSM6DSR_XL_ODR_104Hz));

    // Set gyroscope output data rate to 104 Hz
    ASSERT(0, lsm6dsr_gy_data_rate_set(&dev_ctx, LSM6DSR_GY_ODR_104Hz));

    debug("[IMU]\tInitialization complete\r\n");
    return true;
}

int32_t imu_read_accel(int16_t *data) {
    return lsm6dsr_acceleration_raw_get(&dev_ctx, data);
}

int32_t imu_read_gyro(int16_t *data) {
    return lsm6dsr_angular_rate_raw_get(&dev_ctx, data);
}

int32_t imu_read_temp(int16_t *data) {
    return lsm6dsr_temperature_raw_get(&dev_ctx, data);
}

VOID imu_reader_thread(ULONG thread_input) {

    int init_attempts = 0;
    while (! imu_init() && (init_attempts < IMU_INIT_RETRIES)) {
        debug("[IMU]\tInitialization failed, retrying...\r\n");
        tx_thread_sleep(MS_TO_TICKS(1000));  // Wait 1 second before retrying
        init_attempts++;
    };

    imu_data_t imu_data = {0};
    int16_t raw_accel[3], raw_gyro[3], raw_temp;

    while (true) {
        imu_read_accel(raw_accel);
        imu_read_gyro(raw_gyro);
        imu_read_temp(&raw_temp);

        // Convert raw LSBs to engineering units using the configured full scales
        // (accel ±2g -> mg, gyro ±2000dps -> mdps); divide by 1000 for g / dps.
        for (int i = 0; i < 3; i++) {
            imu_data.accel[i] = lsm6dsr_from_fs2g_to_mg(raw_accel[i]) / 1000.0f;
            imu_data.gyro[i]  = lsm6dsr_from_fs2000dps_to_mdps(raw_gyro[i]) / 1000.0f;
        }
        imu_data.temp = lsm6dsr_from_lsb_to_celsius(raw_temp);

        #ifdef EN_IMU_DEBUG
            #define FP3(x) ((x) < 0 ? "-" : ""), (int)fabsf(x), (int)(fabsf(x) * 1000.0f) % 1000
            debug("[IMU]\tAccel: %s%d.%03d %s%d.%03d %s%d.%03d g  "
                  "Gyro: %s%d.%03d %s%d.%03d %s%d.%03d dps  Temp: %s%d.%03d C\r\n",
                FP3(imu_data.accel[0]), FP3(imu_data.accel[1]), FP3(imu_data.accel[2]),
                FP3(imu_data.gyro[0]), FP3(imu_data.gyro[1]), FP3(imu_data.gyro[2]),
                FP3(imu_data.temp));
            #undef FP3

            tx_thread_sleep(MS_TO_TICKS(1000));  // Slow down debug prints
        #endif

        // Place data into message queue for sending to SoC
        tx_queue_send(&imu_data_queue_handle, &imu_data, TX_NO_WAIT);

        // Poll at 100Hz to match IMU sample rate
        tx_thread_sleep(MS_TO_TICKS(10));
    }
}
