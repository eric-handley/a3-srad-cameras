#include "controller_thread.h"

typedef enum {
    SYS_IDLE,
    SYS_WAIT,
    SYS_ERROR
} system_state_t;

#define UART_TX_TIMEOUT_MS 100
#define SOC_TIMEOUT_MS 1000

static system_state_t sys_state = SYS_IDLE;
static status_t cached_soc_status = REPLY_STOPPED;
static bool fc_reply_pending = false;
static ULONG wait_start_time = 0;

VOID controller_thread(ULONG thread_input) {
    imu_data_t imu_data;

    command_t  fc_cmd;
    status_t   fc_reply;

    soc_msg_t  soc_msg;
    status_t   soc_status;

    while (true) {
        
        bool 
            new_imu_data   = false,
            new_fc_cmd     = false,
            new_soc_status = false;

        // Drain queue to get latest IMU sample only, even if there is multiple (shouldn't happen)
        while (tx_queue_receive(&imu_data_queue_handle, &imu_data, TX_NO_WAIT) == TX_SUCCESS) {
            new_imu_data = true;
        }

        new_fc_cmd     = HAL_UART_Receive(&FC_UART,  (uint8_t*)&fc_cmd,     sizeof(command_t), 0) == HAL_OK;
        new_soc_status = HAL_UART_Receive(&SOC_UART, (uint8_t*)&soc_status, sizeof(status_t),  0) == HAL_OK;

        /*
         * Handle state based on new data + current system state:
         *
         * IDLE:
         * - If cached SoC status is REPLY_ERROR -> transition to ERROR, otherwise:
         * - Send IMU data to SoC when available
         * - If FC sends GET_STATUS -> reply immediately with cached SoC status
         * - If FC sends START_CAM or STOP_CAM -> forward to SoC, mark FC reply pending, start timeout, transition to WAITING_FOR_SOC
         * - If FC command invalid (unrecognized or potentially corrupted) reply REPLY_INVALID_CMD
         *
         * WAITING_FOR_SOC:
         * - Keep sending IMU data to SoC
         * - If timeout exceeded (>1 second) -> set cached status to REPLY_ERROR, transition to ERROR
         * - If SoC sends status before timeout:
         *   - Update cached status
         *   - If REPLY_ERROR -> transition to ERROR
         *   - Otherwise -> reply to FC, clear FC reply pending, return to IDLE
         * - If FC sends another command -> reply REPLY_BUSY
         *
         * ERROR:
         * - Stop sending IMU data (won't be received)
         * - Attempt recovery (TODO)
         * - If recovery successful -> update cached status, return to IDLE
         * - If FC sends command -> reply REPLY_ERROR
         * 
         * TODO Error recovery
         * TODO Error-type specific messages?
         */

        switch (sys_state) {
            case SYS_IDLE:
                if (cached_soc_status == REPLY_ERROR) {
                    sys_state = SYS_ERROR;
                    break;
                }

                if (new_imu_data) {
                    soc_msg.msg_type = IMU_DATA;
                    soc_msg.payload.imu = imu_data;
                    HAL_UART_Transmit(&SOC_UART, (uint8_t*)&soc_msg, sizeof(soc_msg_t), UART_TX_TIMEOUT_MS);
                }

                if (new_fc_cmd) {
                    if (fc_cmd == CMD_GET_STATUS) {
                        HAL_UART_Transmit(&FC_UART, (uint8_t*)&cached_soc_status, sizeof(status_t), UART_TX_TIMEOUT_MS);
                    } else if (fc_cmd == CMD_START_CAM || fc_cmd == CMD_STOP_CAM) {
                        soc_msg.msg_type = COMMAND;
                        soc_msg.payload.cmd = fc_cmd;
                        HAL_UART_Transmit(&SOC_UART, (uint8_t*)&soc_msg, sizeof(soc_msg_t), UART_TX_TIMEOUT_MS);

                        fc_reply_pending = true;
                        wait_start_time = tx_time_get();
                        sys_state = SYS_WAIT;
                    } else {
                        status_t invalid = REPLY_INVALID_CMD;
                        HAL_UART_Transmit(&FC_UART, (uint8_t*)&invalid, sizeof(status_t), UART_TX_TIMEOUT_MS);
                    }
                }
                break;

            case SYS_WAIT:
                if (new_imu_data) {
                    soc_msg.msg_type = IMU_DATA;
                    soc_msg.payload.imu = imu_data;
                    HAL_UART_Transmit(&SOC_UART, (uint8_t*)&soc_msg, sizeof(soc_msg_t), UART_TX_TIMEOUT_MS);
                }

                {
                    ULONG elapsed = tx_time_get() - wait_start_time;
                    if (elapsed > MS_TO_TICKS(SOC_TIMEOUT_MS)) {
                        cached_soc_status = REPLY_ERROR;
                        if (fc_reply_pending) {
                            HAL_UART_Transmit(&FC_UART, (uint8_t*)&cached_soc_status, sizeof(status_t), UART_TX_TIMEOUT_MS);
                            fc_reply_pending = false;
                        }
                        sys_state = SYS_ERROR;
                        break;
                    }
                }

                if (new_soc_status) {
                    cached_soc_status = soc_status;

                    if (cached_soc_status == REPLY_ERROR) {
                        if (fc_reply_pending) {
                            HAL_UART_Transmit(&FC_UART, (uint8_t*)&cached_soc_status, sizeof(status_t), UART_TX_TIMEOUT_MS);
                            fc_reply_pending = false;
                        }
                        sys_state = SYS_ERROR;
                    } else {
                        if (fc_reply_pending) {
                            HAL_UART_Transmit(&FC_UART, (uint8_t*)&cached_soc_status, sizeof(status_t), UART_TX_TIMEOUT_MS);
                            fc_reply_pending = false;
                        }
                        sys_state = SYS_IDLE;
                    }
                }

                if (new_fc_cmd) {
                    status_t busy = REPLY_BUSY;
                    HAL_UART_Transmit(&FC_UART, (uint8_t*)&busy, sizeof(status_t), UART_TX_TIMEOUT_MS);
                }
                break;

            case SYS_ERROR:
                if (new_fc_cmd) {
                    status_t error = REPLY_ERROR;
                    HAL_UART_Transmit(&FC_UART, (uint8_t*)&error, sizeof(status_t), UART_TX_TIMEOUT_MS);
                }
                break;
        }

        // Yield to allow lower priority threads to run
        tx_thread_sleep(MS_TO_TICKS(10));
    }
}
