#pragma once

#include "common.h"

// FC -> stm32: commands from the flight computer.
typedef enum __attribute__((packed)) {
    CMD_START_CAM  = 1,
    CMD_STOP_CAM   = 2,
    CMD_GET_STATUS = 3,
    // Power the SoC up but leave it idle, e.g. to pull recordings over USB.
    CMD_IDLE_CAM   = 4
} command_t;

// stm32 -> FC: responses to the flight computer.
typedef enum __attribute__((packed)) {
    REPLY_STOPPED     = 0,
    REPLY_RECORDING   = 1,
    REPLY_ERROR       = 2,
    REPLY_BUSY        = 3,
    REPLY_STARTING    = 4,   // powered on, SoC still booting / not yet recording
    REPLY_STOPPING    = 5,   // stop requested, SoC finalizing/unmounting the card
    REPLY_INVALID_CMD = 6
} status_t;

#define IMU_FRAME_SOF 0xAA

// stm32-internal record/idle state; drives which command tag the frames carry.
typedef enum __attribute__((packed)) {
    SOC_MODE_RECORD = 1,
    SOC_MODE_IDLE   = 2
} soc_mode_t;

// stm32 -> SoC: per-frame command carried in every IMU frame. The stream is
// continuous now; this tag is the record/idle/stop signal. The SoC treats no
// frames or an unrecognized tag as RECORD (fail toward capturing).
typedef enum __attribute__((packed)) {
    FRAME_CMD_RECORD = 1,
    FRAME_CMD_IDLE   = 2,
    FRAME_CMD_STOP   = 3
} frame_cmd_t;

// stm32 -> SoC: continuous IMU stream, one frame per controller loop. The sync
// byte lets the SoC realign to a frame boundary after any dropped byte; cmd
// carries the current frame_cmd_t.
typedef struct __attribute__((packed)) {
    uint8_t    sof;   // IMU_FRAME_SOF
    uint8_t    cmd;   // frame_cmd_t
    imu_data_t imu;
} imu_frame_t;

// SoC -> stm32: periodic heartbeat reporting what the SoC is already doing. The
// camera app auto-starts on power-up (PMIC controlled), so there are no
// start/stop commands. Values start at 1 so a stray 0x00 is never a valid state.
typedef enum __attribute__((packed)) {
    SOC_INIT      = 1,
    SOC_RECORDING = 2,
    SOC_STOPPED   = 3,
    SOC_ERROR     = 4,
    // The IMU stream stopped (FC requested a stop, or the recording ran to its
    // frame limit): the SoC is saving the partial file and unmounting the card.
    SOC_STOPPING  = 5,
    // Sent once that is done, as the supervisor's last act: everything is flushed
    // to the SD card, so the rails can be cut safely.
    SOC_COMPLETE  = 6,
    // Idle mode is active: /data is remounted read-only and the SoC is parked,
    // safe to cut power at any time. The stm32 stops streaming once it sees this.
    // Highest value, so a range check up to it accepts every state.
    SOC_IDLE      = 7
} soc_status_t;

_Static_assert(sizeof(command_t)    == 1, "command_t must be 1 byte");
_Static_assert(sizeof(status_t)     == 1, "status_t must be 1 byte");
_Static_assert(sizeof(soc_status_t) == 1, "soc_status_t must be 1 byte");
_Static_assert(sizeof(frame_cmd_t)  == 1, "frame_cmd_t must be 1 byte");
