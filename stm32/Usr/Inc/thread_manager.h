#pragma once

#include "common.h"

#include "threads/imu_reader_thread.h"
#include "threads/controller_thread.h"

#define THREAD_STACK_SIZE 1024

#define IMU_PRIORITY        1
#define CONTROLLER_PRIORITY 0

/*
    @brief Initializes user defined threads
    @returns - `TX_FALSE` if any thread fails to start. Successful threads are deleted
    @returns - `TX_TRUE` if all threads are initialized successfully
*/
bool init_threads();
