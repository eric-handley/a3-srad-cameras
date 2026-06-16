#include "thread_manager.h"

static TX_THREAD
    imu_thread_handle,
    controller_thread_handle,
    status_led_thread_handle;

static UCHAR
    imu_thread_stack[THREAD_STACK_SIZE],
    controller_thread_stack[THREAD_STACK_SIZE],
    status_led_thread_stack[THREAD_STACK_SIZE];

static UINT
    imu_thread_created        = TX_FALSE,
    controller_thread_created = TX_FALSE,
    status_led_thread_created = TX_FALSE;

static void cleanup_threads() {
    if (imu_thread_created)        tx_thread_delete(&imu_thread_handle);
    if (controller_thread_created) tx_thread_delete(&controller_thread_handle);
    if (status_led_thread_created) tx_thread_delete(&status_led_thread_handle);
}

bool init_threads() {
    imu_thread_created = tx_thread_create(
                            &imu_thread_handle,
                            "IMU Reader Thread",
                            imu_reader_thread,
                            0,
                            imu_thread_stack,
                            THREAD_STACK_SIZE,
                            IMU_PRIORITY,
                            IMU_PRIORITY,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START
                        ) == TX_SUCCESS;

    controller_thread_created = tx_thread_create(
                            &controller_thread_handle,
                            "Controller Thread",
                            controller_thread,
                            0,
                            controller_thread_stack,
                            THREAD_STACK_SIZE,
                            CONTROLLER_PRIORITY,
                            CONTROLLER_PRIORITY,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START
                        ) == TX_SUCCESS;

    status_led_thread_created = tx_thread_create(
                            &status_led_thread_handle,
                            "Status LED Thread",
                            status_led_thread,
                            0,
                            status_led_thread_stack,
                            THREAD_STACK_SIZE,
                            STATUS_LED_PRIORITY,
                            STATUS_LED_PRIORITY,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START
                        ) == TX_SUCCESS;

    if (! (imu_thread_created && controller_thread_created && status_led_thread_created) ) {
        cleanup_threads();
        return false;
    }

    return true;
}
