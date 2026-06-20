#pragma once

#include "common.h"

/* PMIC GPIO */
#define PMIC_EN_PIN GPIO_PIN_15

/* I2C Address */
#define PMIC_I2C_ADDR    (0x30 << 1)  

/* Register Addresses */
#define PMIC_REG_TI_DEV_ID              0x00
#define PMIC_REG_NVM_ID                 0x01
#define PMIC_REG_ENABLE_CTRL            0x02
#define PMIC_REG_BUCKS_CONFIG           0x03
#define PMIC_REG_LDO4_VOUT              0x04
#define PMIC_REG_LDO3_VOUT              0x05
#define PMIC_REG_LDO2_VOUT              0x06
#define PMIC_REG_LDO1_VOUT              0x07
#define PMIC_REG_BUCK3_VOUT             0x08
#define PMIC_REG_BUCK2_VOUT             0x09
#define PMIC_REG_BUCK1_VOUT             0x0A
#define PMIC_MFP_CONFIG_1               0x1F
#define PMIC_MFP_CONFIG_2               0x20
#define PMIC_MASK_CONFIG                0x25
#define PMIC_REG_MFP_CTRL               0x29
#define PMIC_DISCHARGE_CONFIG           0x2A
#define PMIC_REG_INT_SOURCE             0x2B
#define PMIC_REG_INT_LDO_3_4            0x2C
#define PMIC_REG_INT_LDO_1_2            0x2D
#define PMIC_REG_INT_BUCK_3             0x2E
#define PMIC_REG_INT_BUCK_1_2           0x2F
#define PMIC_REG_INT_SYSTEM             0x30
#define PMIC_REG_INT_RV                 0x31
#define PMIC_REG_INT_TIMEOUT_RV_SD      0x32
#define PMIC_USER_NVM_CMD_REG           0x34
#define PMIC_REG_POWER_UP_STATUS        0x35

/* Enable Control Register Bits */
#define PMIC_ENABLE_BUCK1_EN            (1 << 0)
#define PMIC_ENABLE_BUCK2_EN            (1 << 1)
#define PMIC_ENABLE_BUCK3_EN            (1 << 2)
#define PMIC_ENABLE_LDO1_EN             (1 << 3)
#define PMIC_ENABLE_LDO2_EN             (1 << 4)
#define PMIC_ENABLE_LDO3_EN             (1 << 5)
#define PMIC_ENABLE_LDO4_EN             (1 << 6)

/* MFP Control Register Bits */
#define PMIC_MFP_I2C_OFF_REQ            (1 << 0)
#define PMIC_MFP_STBY_I2C_CTRL          (1 << 1)
#define PMIC_MFP_COLD_RESET_I2C_CTRL    (1 << 2)
#define PMIC_MFP_WARM_RESET_I2C_CTRL    (1 << 3)
#define PMIC_MFP_GPIO_STATUS            (1 << 4)

#define PMIC_LDO4_SEQUENCE_SLOT         0x0B
#define PMIC_LDO3_SEQUENCE_SLOT         0x0C
#define PMIC_LDO2_SEQUENCE_SLOT         0x0D
#define PMIC_LDO1_SEQUENCE_SLOT         0x0E
#define PMIC_BUCK3_SEQUENCE_SLOT        0x0F
#define PMIC_BUCK2_SEQUENCE_SLOT        0x10
#define PMIC_BUCK1_SEQUENCE_SLOT        0x11
#define PMIC_POWER_UP_SLOT_DURATION_1   0x16

bool pmic_read_reg(uint8_t reg, uint8_t *value);
bool pmic_write_reg(uint8_t reg, uint8_t value);
bool pmic_update_bits(uint8_t reg, uint8_t mask, uint8_t value);

void pmic_print_info(void);
bool pmic_scan_interrupts(void);

void pmic_enable(void);
void pmic_disable(void);
void pmic_configure(bool save_to_nvm);

void soc_reset_low(void);

void soc_reset_high(void);

void pmic_clear_interrupts(void);
