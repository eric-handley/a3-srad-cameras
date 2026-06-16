/*
 * read_pmic_i2c.h
 *
 * Read all RK809-5 PMIC registers via I2C3.
 */

#pragma once

#include "common.h"

void pmic_pwron(void);
void pmic_pwroff(void);
void pmic_toggle_power(void);

void read_all_pmic_registers(void);

void read_pmic_basic_info(void);

void config_pmic_registers();

void pmic_poweron_seq(void);

void scan_i2c_bus(void);
