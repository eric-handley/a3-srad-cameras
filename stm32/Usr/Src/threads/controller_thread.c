#include "controller_thread.h"
#include "pmic.h"

// Power on the SoC on the first loop iteration, without waiting for CMD_START_CAM.
// #define EN_AUTOSTART 1

#define UART_TX_TIMEOUT_MS 100
#define UART_TX_RETRIES 3

#define SOC_START_RETRIES 5
#define SOC_START_BACKOFF_MS 100

// Grace period after power-on for the SoC to boot and send its first heartbeat.
#define SOC_BOOT_TIMEOUT_MS 40000
// Max gap between heartbeats once the SoC is up; must exceed the supervisor's
// heartbeat interval.
#define SOC_HEARTBEAT_TIMEOUT_MS 3000
// Upper bound on a graceful stop: we normally cut power when the SoC reports
// SOC_COMPLETE, but if that never comes (some wedged edge case) we cut it anyway
// rather than wait forever.
#define SOC_STOP_TIMEOUT_MS 20000

static bool soc_powered = false;
static soc_mode_t soc_mode = SOC_MODE_RECORD;
static bool heartbeat_seen = false;
static soc_status_t cached_heartbeat = SOC_STOPPED;
static ULONG last_heartbeat_time = 0;
// Set while a graceful stop is in progress: we have stopped streaming IMU frames
// and are waiting for the SoC to finish saving and report SOC_COMPLETE.
static bool soc_stopping = false;
static ULONG stop_request_time = 0;
// Set once the SoC reports SOC_IDLE: it has parked with /data read-only, so we
// stop streaming IDLE frames and just keep watching the heartbeat until power
// is cut. Reset on each new start/idle command.
static bool soc_idle_acked = false;
// A start/idle command received while the SoC is still powered is deferred: we
// gracefully stop the running session first, then power on in this mode once the
// rails are cut. pending_start gates it; pending_mode is the mode to start in.
static bool pending_start = false;
static soc_mode_t pending_mode = SOC_MODE_RECORD;

static volatile HAL_StatusTypeDef last_soc_tx = HAL_OK;
static volatile HAL_StatusTypeDef last_fc_tx  = HAL_OK;

static status_t current_status(void) {
    if (cached_heartbeat == SOC_ERROR) {
        // The SoC faulted, or a power-on attempt failed (controller_start latches
        // SOC_ERROR here). Report ERROR until the next start/idle command clears
        // it, so the direct and deferred-restart paths surface failures the same
        // way. Checked first so it overrides pending_start/soc_stopping below.
        return REPLY_ERROR;
    }

    if (pending_start) {
        // A restart is in progress: we are gracefully stopping the old session
        // before powering back up. Report STARTING for the whole operation so
        // the FC sees its start request as in-flight rather than a stop.
        return REPLY_STARTING;
    }

    if (!soc_powered) {
        return REPLY_STOPPED;
    }

    if (soc_stopping) {
        // We asked the SoC to stop and are letting it finalize the card. Report
        // STOPPING regardless of the last heartbeat; the heartbeat timeout
        // doesn't apply here (SOC_STOP_TIMEOUT_MS bounds it).
        return REPLY_STOPPING;
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
        case SOC_IDLE:      return REPLY_STOPPED;
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

// Cut the rails and return to the baseline stopped state.
static void soc_power_off(void) {
    soc_disable();
    soc_powered = false;
    soc_stopping = false;
    heartbeat_seen = false;
    cached_heartbeat = SOC_STOPPED;
}

// Begin a graceful stop of the running session (idempotent). In RECORD mode we
// tag the frames FRAME_CMD_STOP so the SoC saves the partial recording and
// unmounts the card, then reports COMPLETE (where we cut power). Otherwise there
// is nothing to finalize -- IDLE has already parked, and an off/failed SoC has no
// session at all -- so drop straight to the baseline stopped state (which also
// clears any latched start-failure error).
static void soc_begin_stop(void) {
    if (soc_powered && soc_mode == SOC_MODE_RECORD) {
        if (!soc_stopping) {
            soc_stopping = true;
            stop_request_time = tx_time_get();
        }
    } else {
        soc_power_off();
    }
}

// Power on the SoC in the given mode and return the resulting status. On failure
// we latch SOC_ERROR into cached_heartbeat so current_status() reports the
// failure whether the caller uses the return value (direct start) or polls later
// (deferred restart, whose return value is discarded).
static status_t controller_start(soc_mode_t mode) {
    soc_mode = mode;
    heartbeat_seen = false;
    soc_stopping = false;
    soc_idle_acked = false;
    soc_powered = soc_start();
    cached_heartbeat = soc_powered ? SOC_INIT : SOC_ERROR;
    if (soc_powered) {
        last_heartbeat_time = tx_time_get();
    }
    return current_status();
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

        if (new_heartbeat && soc_hb >= SOC_INIT && soc_hb <= SOC_IDLE) {
            cached_heartbeat = soc_hb;
            last_heartbeat_time = tx_time_get();
            heartbeat_seen = true;

            if (soc_hb == SOC_IDLE) {
                // SoC has parked in idle (/data read-only). Stop streaming IDLE
                // frames; stay powered and keep watching the heartbeat until a
                // CMD_STOP cuts power.
                soc_idle_acked = true;
            }

            if (soc_hb == SOC_COMPLETE) {
                // The SoC is done recording and has synced the card, so cut the
                // rails ourselves rather than letting its heartbeat time out
                // (which would look like a fault). Back to baseline state.
                soc_power_off();
            }
        }

        // Bound the graceful stop: if the SoC never reports COMPLETE, cut power
        // once we've waited long enough rather than hang in STOPPING forever.
        if (soc_stopping && (tx_time_get() - stop_request_time) > MS_TO_TICKS(SOC_STOP_TIMEOUT_MS)) {
            soc_power_off();
        }

        // A start/idle command that arrived while the SoC was still powered was
        // deferred behind a graceful stop. Now that the rails are cut, bring it
        // back up in the requested mode.
        if (pending_start && !soc_powered) {
            pending_start = false;
            controller_start(pending_mode);
        }

        status_t fc_reply;
        bool send_fc_reply = false;

        if (new_fc_cmd) {
            send_fc_reply = true;

            switch (fc_cmd) {
                case CMD_START_CAM:
                case CMD_IDLE_CAM: {
                    soc_mode_t mode = (fc_cmd == CMD_IDLE_CAM) ? SOC_MODE_IDLE : SOC_MODE_RECORD;
                    if (!soc_powered) {
                        // Idle rails: power on directly.
                        fc_reply = controller_start(mode);
                    } else {
                        // Already starting/recording/stopping: don't yank power out
                        // from under an active session. Gracefully stop first, then
                        // let the pending-start dispatch power back up in this mode
                        // once the rails are cut.
                        pending_start = true;
                        pending_mode = mode;
                        soc_begin_stop();
                        fc_reply = current_status();
                    }
                    break;
                }

                case CMD_STOP_CAM:
                    // A stop cancels any deferred restart. soc_begin_stop cuts
                    // power immediately in idle, or starts the graceful stop while
                    // recording; if already stopped it is a no-op.
                    pending_start = false;
                    soc_begin_stop();
                    fc_reply = (soc_powered && soc_mode == SOC_MODE_RECORD) ? REPLY_STOPPING : REPLY_STOPPED;
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
        } else if (led_status == REPLY_RECORDING || led_status == REPLY_STOPPING) {
            LED_STATUS = LED_RECORDING;
        } else {
            LED_STATUS = LED_NOMINAL;
        }

        #ifdef EN_FC_COMMS
        // We stream a tagged IMU frame every loop while powered (reusing the last
        // sample if none arrived this loop). The tag is the record/idle/stop
        // signal: STOP while a graceful stop is in progress, IDLE in idle mode
        // until the SoC acks (SOC_IDLE), otherwise RECORD. In idle we go quiet
        // once acked -- the SoC stays parked and we just watch the heartbeat.
        if (soc_powered) {
            frame_cmd_t tag;
            bool send_frame = true;
            if (soc_stopping) {
                tag = FRAME_CMD_STOP;
            } else if (soc_mode == SOC_MODE_IDLE) {
                tag = FRAME_CMD_IDLE;
                send_frame = !soc_idle_acked;
            } else {
                tag = FRAME_CMD_RECORD;
            }
            if (send_frame) {
                imu_frame.cmd = tag;
                imu_frame.imu = imu_data;
                for (int i = 0; i < UART_TX_RETRIES; i++) {
                    last_soc_tx = HAL_UART_Transmit(&SOC_UART, (uint8_t*)&imu_frame, sizeof(imu_frame_t), UART_TX_TIMEOUT_MS);
                    if (last_soc_tx == HAL_OK) break;
                }
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
