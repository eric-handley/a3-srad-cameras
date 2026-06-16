#include "common.h"
#include "lsm6dsr_reg.h"

// Platform-specific I/O functions
int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);
int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);
void platform_delay(uint32_t millisec);

// IMU functions
bool imu_init(void);
int32_t imu_read_accel(int16_t *data);
int32_t imu_read_gyro(int16_t *data);
int32_t imu_read_temp(int16_t *temp);

VOID imu_reader_thread(ULONG thread_input);