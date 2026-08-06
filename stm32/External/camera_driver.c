// v2, 2026-08-05

#include "camera_driver.h"

static UART_HandleTypeDef *__camera_e_to_huart(camera_e cam)
{
    switch (cam) {
        case CAM1: return &CAM_1_UART;
        case CAM2: return &CAM_2_UART;
        default:   return NULL;
    }
}

static cam_status_t __camera_send_cmd(camera_e cam, cam_command_t cmd, uint32_t timeout_ms)
{
    UART_HandleTypeDef *uart = __camera_e_to_huart(cam);
    if (uart == NULL) {
        return REPLY_ERROR;
    }

    // Discard anything already sitting in the receiver before sending a new command
    // Otherwise replies that are recieved after the timeout from a previous command 
    // will be used instead of the new reply
    __HAL_UART_CLEAR_FLAG(uart, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(uart, UART_RXDATA_FLUSH_REQUEST);

    if (HAL_UART_Transmit(uart, (uint8_t *)&cmd, sizeof(cmd), timeout_ms) != HAL_OK) {
        return REPLY_ERROR;
    }

    cam_status_t reply;
    if (HAL_UART_Receive(uart, (uint8_t *)&reply, sizeof(reply), timeout_ms) != HAL_OK) {
        return REPLY_ERROR;
    }

    if (reply > REPLY_INVALID_CMD) {
        return REPLY_ERROR;
    }

    return reply;
}

cam_status_t camera_start(camera_e cam)
{
    return __camera_send_cmd(cam, CMD_START_CAM, CAM_UART_START_TIMEOUT_MS);
}

cam_status_t camera_stop(camera_e cam)
{
    return __camera_send_cmd(cam, CMD_STOP_CAM, CAM_UART_TIMEOUT_MS);
}

cam_status_t camera_status(camera_e cam)
{
    return __camera_send_cmd(cam, CMD_GET_STATUS, CAM_UART_TIMEOUT_MS);
}
