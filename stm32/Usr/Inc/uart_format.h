#pragma once

#include "common.h"

typedef enum __attribute__((packed)) {
    CMD_START_CAM  = 1,
    CMD_STOP_CAM   = 2,
    CMD_GET_STATUS = 3
} command_t;

typedef enum __attribute__((packed)) {
    REPLY_STOPPED     = 0,
    REPLY_RECORDING   = 1,
    REPLY_ERROR       = 2,
    REPLY_BUSY        = 3,
    REPLY_INVALID_CMD = 4
} status_t;

typedef enum __attribute__((packed)) {
    SOC_HAS_CMD = 1 << 0,
    SOC_HAS_IMU = 1 << 1
} soc_flags_t;

// A frame may carry a command and/or an IMU sample. At least one flag is always
// set (empty frames are never sent), so the leading byte is never 0x00 and the
// SoC can use it as an alignment guard.
typedef struct __attribute__((packed)) {
    uint8_t    flags;   // OR of soc_flags_t
    command_t  cmd;     // valid iff (flags & SOC_HAS_CMD)
    imu_data_t imu;     // valid iff (flags & SOC_HAS_IMU)
} soc_msg_t;

_Static_assert(sizeof(command_t) == 1, "command_t must be 1 byte");
_Static_assert(sizeof(status_t)  == 1, "status_t must be 1 byte");
