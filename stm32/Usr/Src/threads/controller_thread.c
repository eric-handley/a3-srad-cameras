#include "controller_thread.h"

typedef enum {
    SYS_IDLE,
    SYS_BUSY,
    SYS_ERROR
} system_state_t;

#define UART_TX_TIMEOUT_MS 100
#define SOC_TIMEOUT_MS 1000

static system_state_t sys_state = SYS_IDLE;
static status_t cached_soc_status = REPLY_STOPPED;
static ULONG wait_start_time = 0;

VOID controller_thread(ULONG thread_input) {
    imu_data_t imu_data;

    command_t  fc_cmd;
    status_t   fc_reply;

    soc_msg_t  soc_msg;
    status_t   soc_status;

    #ifdef EN_FC_COMMS
    while (true) {
        
        bool
            new_imu_data   = false,
            new_fc_cmd     = false,
            new_soc_status = false,
            send_fc_reply  = false;

        // Drain queue to get latest IMU sample only, even if there is multiple (shouldn't happen)
        while (tx_queue_receive(&imu_data_queue_handle, &imu_data, TX_NO_WAIT) == TX_SUCCESS) {
            new_imu_data = true;
        }

        new_fc_cmd     = HAL_UART_Receive(&FC_UART,  (uint8_t*)&fc_cmd,     sizeof(command_t), 0) == HAL_OK;
        new_soc_status = HAL_UART_Receive(&SOC_UART, (uint8_t*)&soc_status, sizeof(status_t),  0) == HAL_OK;

        /*
         * Handle state based on new data + current system state:
         *
         * IMU samples are forwarded to the SoC in every state except ERROR, sharing a
         * frame with any command produced this iteration.
         *
         * IDLE:
         * - If FC sends GET_STATUS -> reply immediately with cached SoC status
         * - If FC sends START_CAM or STOP_CAM -> forward to SoC, mark FC reply pending, start timeout, transition to WAITING_FOR_SOC
         * - If FC command invalid (unrecognized or potentially corrupted) reply REPLY_INVALID_CMD
         *
         * WAITING_FOR_SOC:
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

        soc_msg.flags = 0;

        if (new_imu_data && sys_state != SYS_ERROR) {
            soc_msg.imu = imu_data;
            soc_msg.flags |= SOC_HAS_IMU;
        }

        switch (sys_state) {
            case SYS_IDLE:
                if (new_fc_cmd) {
                    if (fc_cmd == CMD_GET_STATUS) {
                        fc_reply = cached_soc_status;
                        send_fc_reply = true;
                    } else if (fc_cmd == CMD_START_CAM || fc_cmd == CMD_STOP_CAM) {
                        soc_msg.cmd = fc_cmd;
                        soc_msg.flags |= SOC_HAS_CMD;

                        wait_start_time = tx_time_get();
                        sys_state = SYS_BUSY;
                    } else {
                        fc_reply = REPLY_INVALID_CMD;
                        send_fc_reply = true;
                    }
                }
                break;

            case SYS_BUSY:
                if (new_soc_status) {
                    cached_soc_status = soc_status;
                    fc_reply = cached_soc_status;
                    send_fc_reply = true;

                    if (cached_soc_status == REPLY_ERROR) {
                        sys_state = SYS_ERROR;
                    } else {
                        sys_state = SYS_IDLE;
                    }
                } else if (tx_time_get() - wait_start_time > MS_TO_TICKS(SOC_TIMEOUT_MS)) {
                    cached_soc_status = REPLY_ERROR;
                    fc_reply = REPLY_ERROR;
                    send_fc_reply = true;
                    sys_state = SYS_ERROR;
                } else if (new_fc_cmd) {
                    fc_reply = REPLY_BUSY;
                    send_fc_reply = true;
                }
                break;

            case SYS_ERROR:
                if (new_fc_cmd) {
                    fc_reply = REPLY_ERROR;
                    send_fc_reply = true;
                }
                break;
        }

        if (soc_msg.flags != 0) {
            HAL_UART_Transmit(&SOC_UART, (uint8_t*)&soc_msg, sizeof(soc_msg_t), UART_TX_TIMEOUT_MS);
        }

        if (send_fc_reply) {
            HAL_UART_Transmit(&FC_UART, (uint8_t*)&fc_reply, sizeof(status_t), UART_TX_TIMEOUT_MS);
        }

        // Yield to allow lower priority threads to run
        tx_thread_sleep(MS_TO_TICKS(10));
    }
    #endif
}
