#pragma once

#include "common.h"

/*
    @brief Initializes message queues
    @returns - `TX_FALSE` if any queue fails to create
    @returns - `TX_TRUE` if all queues are initialized successfully
*/
bool init_queues();