// v2, 2026-08-05

#pragma once

// TODO Replace with correct HAL
// #include "stm32u0xx_hal.h"
// #include "stm32u0xx_hal_uart.h"

// TODO Replace with correct UART handles
// UARTs must be set to 115200 baud
// extern UART_HandleTypeDef huart1;
// extern UART_HandleTypeDef huart2;

// TODO Correct UART handles here also
#define CAM_1_UART huart1
#define CAM_2_UART huart2

// Start commands may take up to ~3.1s because we retry several times before
// reporting an error. Other commands respond immediately
#define CAM_UART_TIMEOUT_MS       100
#define CAM_UART_START_TIMEOUT_MS 4000

// FC -> CAM: commands from the flight computer.
typedef enum __attribute__((packed)) {
    CMD_START_CAM  = 1, // Start recording, will automatically record for the full duration set on the camera boards
    CMD_STOP_CAM   = 2, // Disable recording + power down SoC, only used for error recovery from Ground Station
    CMD_GET_STATUS = 3, // Get current cam_status_t from camera
    CMD_START_IDLE = 4, // DEBUG ONLY: start the SoC without recording anything and leave it running. Allows SSH access
} cam_command_t;

// CAM -> FC: responses to the flight computer.
typedef enum __attribute__((packed)) {
    REPLY_STOPPED     = 0, // Camera either has not started recording or is finished recording
    REPLY_RECORDING   = 1, // Camera is recording
    REPLY_ERROR       = 2, // Something has gone wrong! Cameras will try to recover from this automatically but new STOP/START may be needed in some cases
    REPLY_BUSY        = 3, // Camera is in the middle of handling an existing command
    REPLY_STARTING    = 4, // Powered on, SoC still booting / not yet recording
    REPLY_STOPPING    = 5, // Stop requested, camera saving the recording + unmounting the SD card before powering down
    REPLY_INVALID_CMD = 6
} cam_status_t;

// enum for determining which camera should receive commands
typedef enum {
    CAM1,
    CAM2
} camera_e;

// Double check that packets have the correct size so they can be decoded by cameras
_Static_assert(sizeof(cam_command_t) == 1, "cam_command_t must be 1 byte");
_Static_assert(sizeof(cam_status_t)  == 1, "cam_status_t must be 1 byte");

// Start camera c. Should return cam_status_t::REPLY_STARTING
// Should then check status periodically until cam_status_t::REPLY_RECORDING is seen
cam_status_t camera_start  ( camera_e c );

// Stop camera c. Should return cam_status_t::REPLY_STOPPING while the camera saves the
// recording and unmounts the SD card; poll status until cam_status_t::REPLY_STOPPED
cam_status_t camera_stop   ( camera_e c );

// Get camera c status
cam_status_t camera_status ( camera_e c );
