#include "pmic.h"

#define I2C_TIMEOUT_MS  100

void pmic_enable(void)
{
    HAL_GPIO_WritePin(GPIOB, PMIC_EN_PIN, 1);
}

void pmic_configure(void)
{
    return; // TODO
}

/**
 * @brief Read a single register from PMIC
 * @param reg Register address
 * @param value Pointer to store the read value
 * @return true if successful, false otherwise
 */
bool pmic_read_reg(uint8_t reg, uint8_t *value)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(&PMIC_I2C, PMIC_I2C_ADDR, reg,
                              I2C_MEMADD_SIZE_8BIT, value, 1, I2C_TIMEOUT_MS);

    return (status == HAL_OK);
}

/**
 * @brief Write a single register to PMIC
 * @param reg Register address
 * @param value Value to write
 * @return true if successful, false otherwise
 */
bool pmic_write_reg(uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Write(&PMIC_I2C, PMIC_I2C_ADDR, reg,
                               I2C_MEMADD_SIZE_8BIT, &value, 1, I2C_TIMEOUT_MS);

    return (status == HAL_OK);
}

/**
 * @brief Update specific bits in a register (read-modify-write)
 * @param reg Register address
 * @param mask Bit mask for bits to modify
 * @param value New value for masked bits
 * @return true if successful, false otherwise
 */
bool pmic_update_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t reg_val;

    if (!pmic_read_reg(reg, &reg_val)) {
        return false;
    }

    reg_val = (reg_val & ~mask) | (value & mask);

    return pmic_write_reg(reg, reg_val);
}

/**
 * @brief Print basic PMIC configuration info
 */
void pmic_print_info(void)
{
    uint8_t dev_id, nvm_id, enable_ctrl, power_status;

    debug("[PMIC]\tScanning I2C bus...\r\n");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 128; addr++) {
        if (HAL_I2C_IsDeviceReady(&PMIC_I2C, addr << 1, 1, 10) == HAL_OK) {
            debug("[PMIC]\tDevice found at 0x%02X\r\n", addr);
            found++;
        }
    }
    debug("[PMIC]\tScan complete - found %d device(s)\r\n\r\n", found);

    debug("[PMIC]\tPMIC Configuration:\r\n");

    if (pmic_read_reg(PMIC_REG_TI_DEV_ID, &dev_id)) {
        debug("[PMIC]\tDevice ID: 0x%02X\r\n", dev_id);
    } else {
        debug("[PMIC]\tDevice ID: READ FAILED\r\n");
    }

    if (pmic_read_reg(PMIC_REG_NVM_ID, &nvm_id)) {
        debug("[PMIC]\tNVM ID: 0x%02X\r\n", nvm_id);
    }

    if (pmic_read_reg(PMIC_REG_ENABLE_CTRL, &enable_ctrl)) {
        debug("[PMIC]\tEnable Control: 0x%02X\r\n", enable_ctrl);
        debug("[PMIC]\t  BUCK1: %s\r\n", (enable_ctrl & PMIC_ENABLE_BUCK1_EN) ? "ON" : "OFF");
        debug("[PMIC]\t  BUCK2: %s\r\n", (enable_ctrl & PMIC_ENABLE_BUCK2_EN) ? "ON" : "OFF");
        debug("[PMIC]\t  BUCK3: %s\r\n", (enable_ctrl & PMIC_ENABLE_BUCK3_EN) ? "ON" : "OFF");
        debug("[PMIC]\t  LDO1: %s\r\n", (enable_ctrl & PMIC_ENABLE_LDO1_EN) ? "ON" : "OFF");
        debug("[PMIC]\t  LDO2: %s\r\n", (enable_ctrl & PMIC_ENABLE_LDO2_EN) ? "ON" : "OFF");
        debug("[PMIC]\t  LDO3: %s\r\n", (enable_ctrl & PMIC_ENABLE_LDO3_EN) ? "ON" : "OFF");
        debug("[PMIC]\t  LDO4: %s\r\n", (enable_ctrl & PMIC_ENABLE_LDO4_EN) ? "ON" : "OFF");
    }

    if (pmic_read_reg(PMIC_REG_POWER_UP_STATUS, &power_status)) {
        debug("[PMIC]\tPower-Up Status: 0x%02X\r\n", power_status);
    }
}

void configure_omuic