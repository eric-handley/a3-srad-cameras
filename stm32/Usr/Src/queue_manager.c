#include "queue_manager.h"

TX_QUEUE imu_data_queue_handle;

#define IMU_QUEUE_DEPTH 10
#define IMU_MSG_SIZE_ULONGS ((sizeof(imu_data_t) + sizeof(ULONG) - 1) / sizeof(ULONG))

static ULONG imu_queue_storage[IMU_QUEUE_DEPTH * IMU_MSG_SIZE_ULONGS];

static UINT imu_queue_created = TX_FALSE;

static void cleanup_queues() {
    if (imu_queue_created) tx_queue_delete(&imu_data_queue_handle);
}

bool init_queues() {
    imu_queue_created = tx_queue_create(
                            &imu_data_queue_handle,
                            "IMU Data Queue",
                            IMU_MSG_SIZE_ULONGS,
                            imu_queue_storage,
                            sizeof(imu_queue_storage)
                        ) == TX_SUCCESS;

    if (!imu_queue_created) {
        cleanup_queues();
        return false;
    }

    return true;
}