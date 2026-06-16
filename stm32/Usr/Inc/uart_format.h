#pragma once

#include "common.h"

typedef enum {
    CMD_START_CAM  = 0,
    CMD_STOP_CAM   = 1,
    CMD_GET_STATUS = 2
} command_t;

typedef enum {
    REPLY_STOPPED     = 0,
    REPLY_RECORDING   = 1,
    REPLY_ERROR       = 2,
    REPLY_BUSY        = 3,
    REPLY_INVALID_CMD = 4
} status_t;

// typedef enum {

// } system_state_t;

// STM32 -> SoC
typedef enum {
    COMMAND  = 0,
    IMU_DATA = 1
} msg_type_t;

typedef struct __attribute__((packed)) {
    msg_type_t msg_type;
    union {
        command_t cmd;
        imu_data_t imu;
    } payload;
} soc_msg_t;
