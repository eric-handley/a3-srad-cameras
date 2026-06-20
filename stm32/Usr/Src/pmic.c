#include "pmic.h"

#define I2C_TIMEOUT_MS  100

void pmic_enable(void)
{
    debug("[PMIC]\tPMIC_EN pin enabled\r\n");
    HAL_GPIO_WritePin(GPIOB, PMIC_EN_PIN, 1);
}

void pmic_disable(void)
{
    debug("[PMIC]\tPMIC_EN pin disabled\r\n");
    HAL_GPIO_WritePin(GPIOB, PMIC_EN_PIN, 0);
}

void soc_reset_low(void) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, 0);
}

void soc_reset_high(void) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, 1);
}

void pmic_configure(bool save_to_nvm)
{
    debug("[PMIC]\tConfiguring registers...\r\n");

    pmic_write_reg(PMIC_USER_NVM_CMD_REG, 0x09);

    pmic_update_bits(PMIC_MFP_CONFIG_1, 0b01000000, 0x40); // Disable VSEL controlling LDO1
    pmic_update_bits(PMIC_MFP_CONFIG_2, 0b00111000, 0x00); // EN setting instead of PB

    // Set voltages
    pmic_update_bits(PMIC_REG_BUCK1_VOUT, 0b00111111, 0x0C); // 0.6V
    pmic_update_bits(PMIC_REG_BUCK2_VOUT, 0b00111111, 0x0C); // 0.6V
    pmic_update_bits(PMIC_REG_BUCK3_VOUT, 0b00111111, 0x1E); // 1.35V

    pmic_update_bits(PMIC_REG_LDO1_VOUT,  0b00111111, 0x36); // 3.3V
    pmic_update_bits(PMIC_REG_LDO2_VOUT,  0b00111111, 0x36); // 3.3V
    pmic_update_bits(PMIC_REG_LDO3_VOUT,  0b11111111, 0x18); // 1.8V, turn on fast ramp up

    /* 
        Power up/down sequence (down is in reverse)
        BUCK1, BUCK2 -> BUCK3, LDO3 -> LDO1, LDO2 is what the RV1106 datasheet says
        but this causes residual voltage warnings on LDO1 (backfeed?)
        Order that seems to work is
        BUCK1, BUCK2 -> BUCK3 -> LDO1, LDO3 -> LDO2
        as the problem regulators are in the same slot
    */
    pmic_write_reg(PMIC_BUCK1_SEQUENCE_SLOT, 0x03);
    pmic_write_reg(PMIC_BUCK2_SEQUENCE_SLOT, 0x03);

    pmic_write_reg(PMIC_BUCK3_SEQUENCE_SLOT, 0x12);
    
    pmic_write_reg(PMIC_LDO1_SEQUENCE_SLOT,  0x21);
    pmic_write_reg(PMIC_LDO3_SEQUENCE_SLOT,  0x21);
    
    pmic_write_reg(PMIC_LDO2_SEQUENCE_SLOT,  0x30);

    // More attempts at preventing residual voltage interrupts
    // Somehow these both break it even more??
    // pmic_write_reg(PMIC_MASK_CONFIG, 0x90); // Mask residual voltage interrupts
    // pmic_write_reg(PMIC_DISCHARGE_CONFIG, 0x57); // Disable discharge on LDO1 and LDO3 
    
    // Set power seq. slot duration to 3ms
    pmic_write_reg(PMIC_POWER_UP_SLOT_DURATION_1,  0xAA);

    // Enable everything except LDO4
    pmic_write_reg(
        PMIC_REG_ENABLE_CTRL, 
        PMIC_ENABLE_BUCK1_EN 
        | PMIC_ENABLE_BUCK2_EN 
        | PMIC_ENABLE_BUCK3_EN
        | PMIC_ENABLE_LDO1_EN 
        | PMIC_ENABLE_LDO2_EN
        | PMIC_ENABLE_LDO3_EN
    );

    if (save_to_nvm) {
        HAL_Delay(500);
        pmic_write_reg(PMIC_USER_NVM_CMD_REG, 0x0A);
        HAL_Delay(500);
    }

    pmic_write_reg(PMIC_USER_NVM_CMD_REG, 0x06);
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
 * @brief Scan all PMIC interrupt diagnostic registers and log any errors
 */
bool pmic_scan_interrupts(void)
{
    uint8_t int_source, int_ldo_3_4 = 0, int_ldo_1_2 = 0, int_buck_3 = 0, int_buck_1_2 = 0;
    uint8_t int_system = 0, int_rv = 0, int_timeout_rv_sd = 0;
    bool errors_found = false;

    // Read INT_SOURCE first to see which categories have errors
    if (!pmic_read_reg(PMIC_REG_INT_SOURCE, &int_source)) {
        debug("[PMIC]\tFailed to read INT_SOURCE register\r\n");
        return true;
    }

    if (int_source == 0) {
        debug("[PMIC]\tNo interrupts pending\r\n");
        return false;
    }

    debug("[PMIC]\tInterrupt Scan - INT_SOURCE: 0x%02X\r\n", int_source);

    // Check LDO 3/4 interrupts
    if (int_source & (1 << 6)) {
        if (pmic_read_reg(PMIC_REG_INT_LDO_3_4, &int_ldo_3_4)) {
            if (int_ldo_3_4 & 0x3F) {
                errors_found = true;
                debug("[PMIC]\t  INT_LDO_3_4: 0x%02X\r\n", int_ldo_3_4);
                if (int_ldo_3_4 & (1 << 5)) debug("[PMIC]\t    - LDO4 Undervoltage\r\n");
                if (int_ldo_3_4 & (1 << 4)) debug("[PMIC]\t    - LDO4 Overcurrent\r\n");
                if (int_ldo_3_4 & (1 << 3)) debug("[PMIC]\t    - LDO4 Short Circuit to Ground\r\n");
                if (int_ldo_3_4 & (1 << 2)) debug("[PMIC]\t    - LDO3 Undervoltage\r\n");
                if (int_ldo_3_4 & (1 << 1)) debug("[PMIC]\t    - LDO3 Overcurrent\r\n");
                if (int_ldo_3_4 & (1 << 0)) debug("[PMIC]\t    - LDO3 Short Circuit to Ground\r\n");
            }
        }
    }

    // Check LDO 1/2 interrupts
    if (int_source & (1 << 5)) {
        if (pmic_read_reg(PMIC_REG_INT_LDO_1_2, &int_ldo_1_2)) {
            if (int_ldo_1_2 & 0x3F) {
                errors_found = true;
                debug("[PMIC]\t  INT_LDO_1_2: 0x%02X\r\n", int_ldo_1_2);
                if (int_ldo_1_2 & (1 << 5)) debug("[PMIC]\t    - LDO2 Undervoltage\r\n");
                if (int_ldo_1_2 & (1 << 4)) debug("[PMIC]\t    - LDO2 Overcurrent\r\n");
                if (int_ldo_1_2 & (1 << 3)) debug("[PMIC]\t    - LDO2 Short Circuit to Ground\r\n");
                if (int_ldo_1_2 & (1 << 2)) debug("[PMIC]\t    - LDO1 Undervoltage\r\n");
                if (int_ldo_1_2 & (1 << 1)) debug("[PMIC]\t    - LDO1 Overcurrent\r\n");
                if (int_ldo_1_2 & (1 << 0)) debug("[PMIC]\t    - LDO1 Short Circuit to Ground\r\n");
            }
        }
    }

    // Check BUCK 3 interrupts
    if (int_source & (1 << 4)) {
        if (pmic_read_reg(PMIC_REG_INT_BUCK_3, &int_buck_3)) {
            if (int_buck_3 & 0x0F) {
                errors_found = true;
                debug("[PMIC]\t  INT_BUCK_3: 0x%02X\r\n", int_buck_3);
                if (int_buck_3 & (1 << 3)) debug("[PMIC]\t    - BUCK3 Undervoltage\r\n");
                if (int_buck_3 & (1 << 2)) debug("[PMIC]\t    - BUCK3 Negative Overcurrent\r\n");
                if (int_buck_3 & (1 << 1)) debug("[PMIC]\t    - BUCK3 Positive Overcurrent\r\n");
                if (int_buck_3 & (1 << 0)) debug("[PMIC]\t    - BUCK3 Short Circuit to Ground\r\n");
            }
        }
    }

    // Check BUCK 1/2 interrupts
    if (int_source & (1 << 3)) {
        if (pmic_read_reg(PMIC_REG_INT_BUCK_1_2, &int_buck_1_2)) {
            if (int_buck_1_2) {
                errors_found = true;
                debug("[PMIC]\t  INT_BUCK_1_2: 0x%02X\r\n", int_buck_1_2);
                if (int_buck_1_2 & (1 << 7)) debug("[PMIC]\t    - BUCK2 Undervoltage\r\n");
                if (int_buck_1_2 & (1 << 6)) debug("[PMIC]\t    - BUCK2 Negative Overcurrent\r\n");
                if (int_buck_1_2 & (1 << 5)) debug("[PMIC]\t    - BUCK2 Positive Overcurrent\r\n");
                if (int_buck_1_2 & (1 << 4)) debug("[PMIC]\t    - BUCK2 Short Circuit to Ground\r\n");
                if (int_buck_1_2 & (1 << 3)) debug("[PMIC]\t    - BUCK1 Undervoltage\r\n");
                if (int_buck_1_2 & (1 << 2)) debug("[PMIC]\t    - BUCK1 Negative Overcurrent\r\n");
                if (int_buck_1_2 & (1 << 1)) debug("[PMIC]\t    - BUCK1 Positive Overcurrent\r\n");
                if (int_buck_1_2 & (1 << 0)) debug("[PMIC]\t    - BUCK1 Short Circuit to Ground\r\n");
            }
        }
    }

    // Check SYSTEM interrupts (temperature sensors)
    if (int_source & (1 << 2)) {
        if (pmic_read_reg(PMIC_REG_INT_SYSTEM, &int_system)) {
            if (int_system) {
                errors_found = true;
                debug("[PMIC]\t  INT_SYSTEM: 0x%02X\r\n", int_system);
                if (int_system & (1 << 7)) debug("[PMIC]\t    - Sensor 0 Hot\r\n");
                if (int_system & (1 << 6)) debug("[PMIC]\t    - Sensor 1 Hot\r\n");
                if (int_system & (1 << 5)) debug("[PMIC]\t    - Sensor 2 Hot\r\n");
                if (int_system & (1 << 4)) debug("[PMIC]\t    - Sensor 3 Hot\r\n");
                if (int_system & (1 << 3)) debug("[PMIC]\t    - Sensor 0 Warm\r\n");
                if (int_system & (1 << 2)) debug("[PMIC]\t    - Sensor 1 Warm\r\n");
                if (int_system & (1 << 1)) debug("[PMIC]\t    - Sensor 2 Warm\r\n");
                if (int_system & (1 << 0)) debug("[PMIC]\t    - Sensor 3 Warm\r\n");
            }
        }
    }

    // Check Residual Voltage interrupts
    if (int_source & (1 << 1)) {
        if (pmic_read_reg(PMIC_REG_INT_RV, &int_rv)) {
            if (int_rv & 0x7F) {
                errors_found = true;
                debug("[PMIC]\t  INT_RV: 0x%02X\r\n", int_rv);
                if (int_rv & (1 << 6)) debug("[PMIC]\t    - LDO4 Residual Voltage\r\n");
                if (int_rv & (1 << 5)) debug("[PMIC]\t    - LDO3 Residual Voltage\r\n");
                if (int_rv & (1 << 4)) debug("[PMIC]\t    - LDO2 Residual Voltage\r\n");
                if (int_rv & (1 << 3)) debug("[PMIC]\t    - LDO1 Residual Voltage\r\n");
                if (int_rv & (1 << 2)) debug("[PMIC]\t    - BUCK3 Residual Voltage\r\n");
                if (int_rv & (1 << 1)) debug("[PMIC]\t    - BUCK2 Residual Voltage\r\n");
                if (int_rv & (1 << 0)) debug("[PMIC]\t    - BUCK1 Residual Voltage\r\n");
            }
        }
    }

    // Check Timeout/RV Shutdown interrupts
    if (int_source & (1 << 0)) {
        if (pmic_read_reg(PMIC_REG_INT_TIMEOUT_RV_SD, &int_timeout_rv_sd)) {
            if (int_timeout_rv_sd) {
                errors_found = true;
                debug("[PMIC]\t  INT_TIMEOUT_RV_SD: 0x%02X\r\n", int_timeout_rv_sd);
                if (int_timeout_rv_sd & (1 << 7)) debug("[PMIC]\t    - Shutdown due to Timeout\r\n");
                if (int_timeout_rv_sd & (1 << 6)) debug("[PMIC]\t    - LDO4 RV/Discharge Timeout Shutdown\r\n");
                if (int_timeout_rv_sd & (1 << 5)) debug("[PMIC]\t    - LDO3 RV/Discharge Timeout Shutdown\r\n");
                if (int_timeout_rv_sd & (1 << 4)) debug("[PMIC]\t    - LDO2 RV/Discharge Timeout Shutdown\r\n");
                if (int_timeout_rv_sd & (1 << 3)) debug("[PMIC]\t    - LDO1 RV/Discharge Timeout Shutdown\r\n");
                if (int_timeout_rv_sd & (1 << 2)) debug("[PMIC]\t    - BUCK3 RV/Discharge Timeout Shutdown\r\n");
                if (int_timeout_rv_sd & (1 << 1)) debug("[PMIC]\t    - BUCK2 RV/Discharge Timeout Shutdown\r\n");
                if (int_timeout_rv_sd & (1 << 0)) debug("[PMIC]\t    - BUCK1 RV/Discharge Timeout Shutdown\r\n");
            }
        }
    }

    if (!errors_found) {
        debug("[PMIC]\t  No specific errors found in detail registers\r\n");
    } 

    // Clear all interrupts (R/W1C - write 1 to clear)
    if (int_source & (1 << 6)) {
        pmic_write_reg(PMIC_REG_INT_LDO_3_4, int_ldo_3_4);
    }
    if (int_source & (1 << 5)) {
        pmic_write_reg(PMIC_REG_INT_LDO_1_2, int_ldo_1_2);
    }
    if (int_source & (1 << 4)) {
        pmic_write_reg(PMIC_REG_INT_BUCK_3, int_buck_3);
    }
    if (int_source & (1 << 3)) {
        pmic_write_reg(PMIC_REG_INT_BUCK_1_2, int_buck_1_2);
    }
    if (int_source & (1 << 2)) {
        pmic_write_reg(PMIC_REG_INT_SYSTEM, int_system);
    }
    if (int_source & (1 << 1)) {
        pmic_write_reg(PMIC_REG_INT_RV, int_rv);
    }
    if (int_source & (1 << 0)) {
        pmic_write_reg(PMIC_REG_INT_TIMEOUT_RV_SD, int_timeout_rv_sd);
    }

    return errors_found;
}

/**
 * @brief Print basic PMIC configuration info
 */
void pmic_print_info(void)
{
    uint8_t buff;

    // debug("[PMIC]\tScanning I2C bus...\r\n");
    // uint8_t found = 0;
    // for (uint8_t addr = 1; addr < 128; addr++) {
    //     if (HAL_I2C_IsDeviceReady(&PMIC_I2C, addr << 1, 1, 10) == HAL_OK) {
    //         debug("[PMIC]\tDevice found at 0x%02X\r\n", addr);
    //         found++;
    //     }
    // }
    // debug("[PMIC]\tScan complete - found %d device(s)\r\n", found);

    debug("[PMIC]\tPMIC Configuration:\r\n");

    if (pmic_read_reg(PMIC_REG_TI_DEV_ID, &buff)) {
        debug("[PMIC]\t  Device ID: 0x%02X\r\n", buff);
    } else {
        debug("[PMIC]\t  Device ID: READ FAILED\r\n");
    }

    if (pmic_read_reg(PMIC_REG_NVM_ID, &buff))
        debug("[PMIC]\t  NVM ID: 0x%02X\r\n", buff);

    if (pmic_read_reg(PMIC_REG_ENABLE_CTRL, &buff)) {
        debug("[PMIC]\t  Enable Control: 0x%02X\r\n", buff);
        debug("[PMIC]\t    BUCK1: %s\r\n", (buff & PMIC_ENABLE_BUCK1_EN) ? "ON" : "OFF");
        debug("[PMIC]\t    BUCK2: %s\r\n", (buff & PMIC_ENABLE_BUCK2_EN) ? "ON" : "OFF");
        debug("[PMIC]\t    BUCK3: %s\r\n", (buff & PMIC_ENABLE_BUCK3_EN) ? "ON" : "OFF");
        debug("[PMIC]\t    LDO1: %s\r\n", (buff & PMIC_ENABLE_LDO1_EN) ? "ON" : "OFF");
        debug("[PMIC]\t    LDO2: %s\r\n", (buff & PMIC_ENABLE_LDO2_EN) ? "ON" : "OFF");
        debug("[PMIC]\t    LDO3: %s\r\n", (buff & PMIC_ENABLE_LDO3_EN) ? "ON" : "OFF");
        debug("[PMIC]\t    LDO4: %s\r\n", (buff & PMIC_ENABLE_LDO4_EN) ? "ON" : "OFF");
    }

    debug("[PMIC]\t  LDOx_VOUT Registers\r\n");
    
    if (pmic_read_reg(PMIC_REG_LDO1_VOUT, &buff)) 
        debug("[PMIC]\t    PMIC_REG_LDO1_VOUT: 0x%02X, %s\r\n", buff, bin8(buff));
    else
        debug("[PMIC]\t    ERROR READING PMIC_REG_LDO1_VOUT");
    if (pmic_read_reg(PMIC_REG_LDO2_VOUT, &buff)) 
        debug("[PMIC]\t    PMIC_REG_LDO2_VOUT: 0x%02X, %s\r\n", buff, bin8(buff));
    else
        debug("[PMIC]\t    ERROR READING PMIC_REG_LDO2_VOUT");
    if (pmic_read_reg(PMIC_REG_LDO3_VOUT, &buff)) 
        debug("[PMIC]\t    PMIC_REG_LDO3_VOUT: 0x%02X, %s\r\n", buff, bin8(buff));
    else
        debug("[PMIC]\t    ERROR READING PMIC_REG_LDO3_VOUT");
    if (pmic_read_reg(PMIC_REG_LDO4_VOUT, &buff)) 
        debug("[PMIC]\t    PMIC_REG_LDO4_VOUT: 0x%02X, %s\r\n", buff, bin8(buff));
    else
        debug("[PMIC]\t    ERROR READING PMIC_REG_LDO4_VOUT");
    
        
    if (pmic_read_reg(PMIC_REG_POWER_UP_STATUS, &buff))
        debug("[PMIC]\t  Power-Up Status: 0x%02X, %s\r\n", buff, bin8(buff));
        
    if ((buff & 0xE) == 0xE)
        debug("[PMIC]\t    POWER ON FAILED: Too many retries!\r\n");
        
    if (pmic_read_reg(PMIC_MFP_CONFIG_1, &buff))
        debug("[PMIC]\t  MFP_CONFIG_1: 0x%02X, %s\r\n", buff, bin8(buff));
    if (pmic_read_reg(PMIC_MFP_CONFIG_2, &buff))
        debug("[PMIC]\t  MFP_CONFIG_2: 0x%02X, %s\r\n", buff, bin8(buff));
    if (pmic_read_reg(PMIC_MASK_CONFIG, &buff))
        debug("[PMIC]\t  MASK_CONFIG: 0x%02X, %s\r\n", buff, bin8(buff));
}

void pmic_clear_interrupts(void) {
    pmic_write_reg(PMIC_REG_INT_RV, 0x7F);            // Clear all RV flags                                                      
    pmic_write_reg(PMIC_REG_INT_TIMEOUT_RV_SD, 0xFF); // Clear all timeout flags  
}