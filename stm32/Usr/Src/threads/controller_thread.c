#include "controller_thread.h"
#include "pmic.h"

// Power on the SoC on the first loop iteration, without waiting for CMD_START_CAM.
// #define EN_AUTOSTART 1

#define UART_TX_TIMEOUT_MS 100
#define UART_TX_RETRIES 3

#define SOC_START_RETRIES 5
#define SOC_START_BACKOFF_MS 100

// Grace period after power-on for the SoC to boot and send its first heartbeat.
#define SOC_BOOT_TIMEOUT_MS 30000
// Max gap between heartbeats once the SoC is up; must exceed the supervisor's
// heartbeat interval.
#define SOC_HEARTBEAT_TIMEOUT_MS 3000
// Upper bound on a graceful stop: we normally cut power when the SoC reports
// SOC_COMPLETE, but if that never comes (some wedged edge case) we cut it anyway
// rather than wait forever.
#define SOC_STOP_TIMEOUT_MS 60000

static bool soc_powered = false;
static soc_mode_t soc_mode = SOC_MODE_RECORD;
static bool heartbeat_seen = false;
static soc_status_t cached_heartbeat = SOC_STOPPED;
static ULONG last_heartbeat_time = 0;
// Set while a graceful stop is in progress: we have stopped streaming IMU frames
// and are waiting for the SoC to finish saving and report SOC_COMPLETE.
static bool soc_stopping = false;
static ULONG stop_request_time = 0;

static volatile HAL_StatusTypeDef last_soc_tx = HAL_OK;
static volatile HAL_StatusTypeDef last_fc_tx  = HAL_OK;

static status_t current_status(void) {
    if (!soc_powered) {
        return REPLY_STOPPED;
    }

    if (soc_stopping) {
        // We asked the SoC to stop and are letting it finalize the card. Report
        // STOPPING regardless of the last heartbeat, unless it faulted; the
        // heartbeat timeout doesn't apply here (SOC_STOP_TIMEOUT_MS bounds it).
        return (cached_heartbeat == SOC_ERROR) ? REPLY_ERROR : REPLY_STOPPING;
    }

    if (soc_mode == SOC_MODE_IDLE) {
        // The supervisor exits immediately in idle mode, so there is no
        // heartbeat to wait for or time out. Powered but not recording.
        return REPLY_STOPPED;
    }

    ULONG elapsed = tx_time_get() - last_heartbeat_time;

    if (!heartbeat_seen) {
        // No heartbeat yet: STARTING while booting, ERROR if boot never completes.
        return elapsed > MS_TO_TICKS(SOC_BOOT_TIMEOUT_MS) ? REPLY_ERROR : REPLY_STARTING;
    }

    if (elapsed > MS_TO_TICKS(SOC_HEARTBEAT_TIMEOUT_MS)) {
        return REPLY_ERROR;
    }

    switch (cached_heartbeat) {
        case SOC_RECORDING: return REPLY_RECORDING;
        case SOC_STOPPING:  return REPLY_STOPPING;
        case SOC_STOPPED:   return REPLY_STOPPED;
        case SOC_ERROR:     return REPLY_ERROR;
        case SOC_INIT:
        default:            return REPLY_STARTING;
    }
}

// Power on the SoC and confirm the PMIC rails came up cleanly, retrying with
// progressive backoff if the interrupt scan reports faults.
static bool soc_start(void) {
    for (int attempt = 0; attempt < SOC_START_RETRIES; attempt++) {
        soc_enable();
        if (!pmic_scan_interrupts()) {
            LED_STATUS = LED_NOMINAL;
            return true;
        }
        LED_STATUS = LED_ERROR;
        soc_disable();
        tx_thread_sleep(MS_TO_TICKS(SOC_START_BACKOFF_MS << attempt));
    }
    return false;
}

VOID controller_thread(ULONG thread_input) {
    imu_data_t   imu_data = {0};
    command_t    fc_cmd;
    soc_status_t soc_hb;

    imu_frame_t  imu_frame = { .sof = IMU_FRAME_SOF };

    while (true) {
        // Drain queue to get latest IMU sample only, even if there are multiple.
        // imu_data keeps its previous value if none arrived this loop.
        while (tx_queue_receive(&imu_data_queue_handle, &imu_data, TX_NO_WAIT) == TX_SUCCESS) {
        }

        // Clear any stale error flags so a prior overrun/framing glitch cannot wedge
        // reception: with a zero timeout HAL_UART_Receive returns before clearing them
        // itself. Clearing OREF leaves a pending byte in RDR to be read below.
        uint32_t rx_err_flags = UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF;
        __HAL_UART_CLEAR_FLAG(&FC_UART,  rx_err_flags);
        __HAL_UART_CLEAR_FLAG(&SOC_UART, rx_err_flags);

        bool new_fc_cmd    = HAL_UART_Receive(&FC_UART,  (uint8_t*)&fc_cmd, sizeof(command_t),    0) == HAL_OK;
        bool new_heartbeat = HAL_UART_Receive(&SOC_UART, (uint8_t*)&soc_hb, sizeof(soc_status_t), 0) == HAL_OK;

        #ifdef EN_AUTOSTART
        static bool autostarted = false;
        if (!autostarted) {
            autostarted = true;
            fc_cmd = CMD_START_CAM;
            new_fc_cmd = true;
        }
        #endif

        if (new_heartbeat && soc_hb >= SOC_INIT && soc_hb <= SOC_COMPLETE) {
            cached_heartbeat = soc_hb;
            last_heartbeat_time = tx_time_get();
            heartbeat_seen = true;

            if (soc_hb == SOC_COMPLETE) {
                // The SoC is done recording and has synced the card, so cut the
                // rails ourselves rather than letting its heartbeat time out
                // (which would look like a fault). Back to baseline state.
                soc_disable();
                soc_powered = false;
                soc_stopping = false;
                heartbeat_seen = false;
                cached_heartbeat = SOC_STOPPED;
            }
        }

        // Bound the graceful stop: if the SoC never reports COMPLETE, cut power
        // once we've waited long enough rather than hang in STOPPING forever.
        if (soc_stopping && (tx_time_get() - stop_request_time) > MS_TO_TICKS(SOC_STOP_TIMEOUT_MS)) {
            soc_disable();
            soc_powered = false;
            soc_stopping = false;
            heartbeat_seen = false;
            cached_heartbeat = SOC_STOPPED;
        }

        status_t fc_reply;
        bool send_fc_reply = false;

        if (new_fc_cmd) {
            send_fc_reply = true;

            switch (fc_cmd) {
                case CMD_START_CAM:
                case CMD_IDLE_CAM:
                    // soc_start() power-cycles the PMIC, so either command recovers
                    // from any state; the only difference is whether we then stream
                    // IMU frames (RECORD) or stay silent (IDLE).
                    soc_mode = (fc_cmd == CMD_IDLE_CAM) ? SOC_MODE_IDLE : SOC_MODE_RECORD;
                    heartbeat_seen = false;
                    soc_stopping = false;
                    cached_heartbeat = SOC_INIT;
                    soc_powered = soc_start();
                    if (soc_powered) {
                        last_heartbeat_time = tx_time_get();
                    }
                    fc_reply = soc_powered ? current_status() : REPLY_ERROR;
                    break;

                case CMD_STOP_CAM:
                    if (!soc_powered) {
                        // Nothing to stop.
                        fc_reply = REPLY_STOPPED;
                    } else if (soc_mode == SOC_MODE_RECORD) {
                        // Graceful stop: stop streaming IMU frames so the SoC sees
                        // the stream go quiet, saves the partial recording and
                        // unmounts the card, then reports COMPLETE (where we cut
                        // power).
                        if (!soc_stopping) {
                            soc_stopping = true;
                            stop_request_time = tx_time_get();
                        }
                        fc_reply = REPLY_STOPPING;
                    } else {
                        // Idle: not recording, and the supervisor already exited at
                        // boot, so nothing will ever send COMPLETE. Cut power now
                        // instead of waiting out the graceful-stop timeout.
                        soc_disable();
                        soc_powered = false;
                        soc_stopping = false;
                        heartbeat_seen = false;
                        cached_heartbeat = SOC_STOPPED;
                        fc_reply = REPLY_STOPPED;
                    }
                    break;

                case CMD_GET_STATUS:
                    fc_reply = current_status();
                    break;

                default:
                    fc_reply = REPLY_INVALID_CMD;
                    break;
            }
        }

        status_t led_status = current_status();
        if (led_status == REPLY_ERROR || (send_fc_reply && fc_reply == REPLY_ERROR)) {
            LED_STATUS = LED_ERROR;
        } else if (led_status == REPLY_RECORDING) {
            LED_STATUS = LED_RECORDING;
        } else {
            LED_STATUS = LED_NOMINAL;
        }

        #ifdef EN_FC_COMMS
        // Streaming IS the record signal, and its absence is the stop signal: we
        // send a frame every loop while recording (even reusing the last sample
        // if none arrived this loop), stay silent in IDLE, and stop entirely once
        // a graceful stop begins so the SoC detects the quiet and finalizes.
        if (soc_powered && soc_mode == SOC_MODE_RECORD && !soc_stopping) {
            imu_frame.imu = imu_data;
            for (int i = 0; i < UART_TX_RETRIES; i++) {
                last_soc_tx = HAL_UART_Transmit(&SOC_UART, (uint8_t*)&imu_frame, sizeof(imu_frame_t), UART_TX_TIMEOUT_MS);
                if (last_soc_tx == HAL_OK) break;
            }
        }

        if (send_fc_reply) {
            for (int i = 0; i < UART_TX_RETRIES; i++) {
                last_fc_tx = HAL_UART_Transmit(&FC_UART, (uint8_t*)&fc_reply, sizeof(status_t), UART_TX_TIMEOUT_MS);
                if (last_fc_tx == HAL_OK) break;
            }
        }
        #endif

        // Yield to allow lower priority threads to run
        tx_thread_sleep(MS_TO_TICKS(10));
    }
}
