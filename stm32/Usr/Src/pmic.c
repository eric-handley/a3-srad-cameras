#include "pmic.h"

// RK809-5 I2C address (7-bit address)
#define PMIC_I2C_ADDR_7BIT 0x20
#define PMIC_TIMEOUT 100

#define PMIC_PWRON_PIN GPIO_PIN_2

#define PWRON_LOW() HAL_GPIO_WritePin(GPIOC, PMIC_PWRON_PIN, GPIO_PIN_RESET)
#define PWRON_HIGH() HAL_GPIO_WritePin(GPIOC, PMIC_PWRON_PIN, GPIO_PIN_SET)

// Bitbang I2C GPIO definitions (swapped pins on hardware)
#define I2C_SCL_PORT GPIOC
#define I2C_SCL_PIN  GPIO_PIN_1  // PC1 = SCL (physically connected to PMIC SCL)
#define I2C_SDA_PORT GPIOC
#define I2C_SDA_PIN  GPIO_PIN_0  // PC0 = SDA (physically connected to PMIC SDA)

// Timing delays for I2C bitbang (adjust if needed for reliability)
#define I2C_DELAY_US 2  // ~100kHz I2C speed

// PMIC power state tracking
static uint8_t pmic_power_state = 0;  // 0 = off, 1 = on

// Register definitions from pmic_registers.csv
typedef struct {
    const char* name;
    uint16_t addr;
    uint8_t write_value;
    uint8_t mask;  // Bits to modify (1 = modify, 0 = preserve). 0xFF = write entire register
} pmic_register_t;

typedef struct {
    const char* name;
    uint8_t value;
} pmic_write_value_t;

static const pmic_register_t pmic_registers[] = {
    {"RTC_SECONDS", 0x0000},
    {"RTC_MINUTES", 0x0001},
    {"RTC_HOURS", 0x0002},
    {"RTC_DAYS", 0x0003},
    {"RTC_MONTHS", 0x0004},
    {"RTC_YEARS", 0x0005},
    {"RTC_WEEKS", 0x0006},
    {"RTC_ALARM_SECONDS", 0x0007},
    {"RTC_ALARM_MINUTES", 0x0008},
    {"RTC_ALARM_HOURS", 0x0009},
    {"RTC_ALARM_DAYS", 0x000a},
    {"RTC_ALARM_MONTHS", 0x000b},
    {"RTC_ALARM_YEARS", 0x000c},
    {"RTC_RTC_CTRL", 0x000d},
    {"RTC_RTC_STATUS", 0x000e},
    {"RTC_RTC_INT", 0x000f},
    {"RTC_RTC_COMP_LSB", 0x0010},
    {"RTC_RTC_COMP_MSB", 0x0011},
    {"CODEC_DTOP_VUCTL", 0x0012},
    {"CODEC_DTOP_VUCTIME", 0x0013},
    {"CODEC_DTOP_LPT_SRST", 0x0014},
    {"CODEC_DTOP_DIGEN_CLKE", 0x0015},
    {"CODEC_AREF_RTCFG0", 0x0016},
    {"CODEC_AREF_RTCFG1", 0x0017},
    {"CODEC_AADC_CFG0", 0x0018},
    {"CODEC_DADC_VOLL", 0x001a},
    {"CODEC_DADC_VOLR", 0x001b},
    {"CODEC_DADC_SR_ACL0", 0x001e},
    {"CODEC_DADC_ALC1", 0x001f},
    {"CODEC_DADC_ALC2", 0x0020},
    {"CODEC_DADC_NG", 0x0021},
    {"CODEC_DADC_HPF", 0x0022},
    {"CODEC_DADC_RVOLL", 0x0023},
    {"CODEC_DADC_RVOLR", 0x0024},
    {"CODEC_AMIC_CFG0", 0x0027},
    {"CODEC_AMIC_CFG1", 0x0028},
    {"CODEC_DMIC_PGA_GAIN", 0x0029},
    {"CODEC_DMIC_LMT1", 0x002a},
    {"CODEC_DMIC_LMT2", 0x002b},
    {"CODEC_DMIC_NG1", 0x002c},
    {"CODEC_DMIC_NG2", 0x002d},
    {"CODEC_ADAC_CFG1", 0x002f},
    {"CODEC_DDAC_POPD_DACST", 0x0030},
    {"CODEC_DDAC_VOLL", 0x0031},
    {"CODEC_DDAC_VOLR", 0x0032},
    {"CODEC_DDAC_SR_LMT0", 0x0035},
    {"CODEC_DDAC_LMT1", 0x0036},
    {"CODEC_DDAC_LMT2", 0x0037},
    {"CODEC_DDAC_MUTE_MIXCTL", 0x0038},
    {"CODEC_DDAC_RVOLL", 0x0039},
    {"CODEC_DDAC_RVOLR", 0x003a},
    {"CODEC_AHP_ANTI0", 0x003b},
    {"CODEC_AHP_ANTI1", 0x003c},
    {"CODEC_AHP_CFG0", 0x003d},
    {"CODEC_AHP_CFG1", 0x003e},
    {"CODEC_AHP_CP", 0x003f},
    {"CODEC_ACLASSD_CFG1", 0x0040},
    {"CODEC_ACLASSD_CFG2", 0x0041},
    {"CODEC_APLL_CFG0", 0x0042},
    {"CODEC_APLL_CFG1", 0x0043},
    {"CODEC_APLL_CFG2", 0x0044},
    {"CODEC_APLL_CFG3", 0x0045},
    {"CODEC_APLL_CFG4", 0x0046},
    {"CODEC_APLL_CFG5", 0x0047},
    {"CODEC_DI2S_CKM", 0x0048},
    {"CODEC_DI2S_RSD", 0x0049},
    {"CODEC_DI2S_RXCR1", 0x004a},
    {"CODEC_DI2S_RXCR2", 0x004b},
    {"CODEC_DI2S_RXCMD_TSD", 0x004c},
    {"CODEC_DI2S_TXCR1", 0x004d},
    {"CODEC_DI2S_TXCR2", 0x004e},
    {"CODEC_DI2S_TXCR3_TXCMD", 0x004f},
    {"gas_gauge_ADC_CONFIG0", 0x0050},
    {"gas_gauge_ADC_CONFIG1", 0x0055},
    {"gas_gauge_GG_CON", 0x0056},
    {"gas_gauge_GG_STS", 0x0057},
    {"gas_gauge_RELAX_THRE_H", 0x0058},
    {"gas_gauge_RELAX_THRE_L", 0x0059},
    {"gas_gauge_RELAX_VOL1_H", 0x005a},
    {"gas_gauge_RELAX_VOL1_L", 0x005b},
    {"gas_gauge_RELAX_VOL2_H", 0x005c},
    {"gas_gauge_RELAX_VOL2_L", 0x005d},
    {"gas_gauge_RELAX_CUR1_H", 0x005e},
    {"gas_gauge_RELAX_CUR1_L", 0x005f},
    {"gas_gauge_RELAX_CUR2_H", 0x0060},
    {"gas_gauge_RELAX_CUR2_L", 0x0061},
    {"gas_gauge_OCV_THRE_VOL", 0x0062},
    {"gas_gauge_OCV_VOL_H", 0x0063},
    {"gas_gauge_OCV_VOL_L", 0x0064},
    {"gas_gauge_OCV_VOL0_H", 0x0065},
    {"gas_gauge_OCV_VOL0_L", 0x0066},
    {"gas_gauge_OCV_CUR_H", 0x0067},
    {"gas_gauge_OCV_CUR_L", 0x0068},
    {"gas_gauge_OCV_CUR0_H", 0x0069},
    {"gas_gauge_OCV_CUR0_L", 0x006a},
    {"gas_gauge_PWRON_VOL_H", 0x006b},
    {"gas_gauge_PWRON_VOL_L", 0x006c},
    {"gas_gauge_PWRON_CUR_H", 0x006d},
    {"gas_gauge_PWRON_CUR_L", 0x006e},
    {"gas_gauge_OFF_CNT", 0x006f},
    {"gas_gauge_Q_INIT_H3", 0x0070},
    {"gas_gauge_Q_INIT_H2", 0x0071},
    {"gas_gauge_Q_INIT_L1", 0x0072},
    {"gas_gauge_Q_INIT_L0", 0x0073},
    {"gas_gauge_Q_PRES_H3", 0x0074},
    {"gas_gauge_Q_PRES_H2", 0x0075},
    {"gas_gauge_Q_PRES_L1", 0x0076},
    {"gas_gauge_Q_PRES_L0", 0x0077},
    {"gas_gauge_BAT_VOL_H", 0x0078},
    {"gas_gauge_BAT_VOL_L", 0x0079},
    {"gas_gauge_BAT_CUR_H", 0x007a},
    {"gas_gauge_BAT_CUR", 0x007b},
    {"gas_gauge_SW2_VOL_H", 0x007e},
    {"gas_gauge_SW2_VOL_L", 0x007f},
    {"gas_gauge_SW1_VOL_H", 0x0080},
    {"gas_gauge_SW1_VOL_L", 0x0081},
    {"gas_gauge_Q_MAX_H3", 0x0082},
    {"gas_gauge_Q_MAX_H2", 0x0083},
    {"gas_gauge_Q_MAX_L1", 0x0084},
    {"gas_gauge_Q_MAX_L0", 0x0085},
    {"gas_gauge_Q_TERM_H3", 0x0086},
    {"gas_gauge_Q_TERM_H2", 0x0087},
    {"gas_gauge_Q_TERM_L1", 0x0088},
    {"gas_gauge_Q_TERM_L0", 0x0089},
    {"gas_gauge_Q_OCV_H3", 0x008a},
    {"gas_gauge_Q_OCV_H2", 0x008b},
    {"gas_gauge_Q_OCV_L1", 0x008c},
    {"gas_gauge_Q_OCV_L0", 0x008d},
    {"gas_gauge_OCV_CNT", 0x008e},
    {"gas_gauge_SLEEP_CON_SAMP_CUR_H", 0x008f},
    {"gas_gauge_SLEEP_CON_SAMP_CUR", 0x0090},
    {"gas_gauge_CAL_OFFSET_H", 0x0091},
    {"gas_gauge_CAL_OFFSET_L", 0x0092},
    {"gas_gauge_VCALIB0_H", 0x0093},
    {"gas_gauge_VCALIB0_L", 0x0094},
    {"gas_gauge_VCALIB1_H", 0x0095},
    {"gas_gauge_VCALIB1_L", 0x0096},
    {"gas_gauge_IOFFSET_H", 0x0097},
    {"gas_gauge_IOFFSET_L", 0x0098},
    {"gas_gauge_BAT_R0", 0x0099},
    {"gas_gauge_BAT_R1", 0x009a},
    {"gas_gauge_BAT_R2", 0x009b},
    {"gas_gauge_BAT_R3", 0x009c},
    {"gas_gauge_DATA0", 0x009d},
    {"gas_gauge_DATA1", 0x009e},
    {"gas_gauge_DATA2", 0x009f},
    {"gas_gauge_DATA3", 0x00a0},
    {"gas_gauge_DATA4", 0x00a1},
    {"gas_gauge_DATA5", 0x00a2},
    {"gas_gauge_DATA6", 0x00a3},
    {"gas_gauge_DATA7", 0x00a4},
    {"gas_gauge_DATA8", 0x00a5},
    {"gas_gauge_DATA9", 0x00a6},
    {"gas_gauge_DATA10", 0x00a7},
    {"gas_gauge_DATA11", 0x00a8},
    {"gas_gauge_VOL_ADC_B3", 0x00a9},
    {"gas_gauge_VOL_ADC_B2", 0x00aa},
    {"gas_gauge_VOL_ADC_B1", 0x00ab},
    {"gas_gauge_VOL_ADC_B_7_0", 0x00ac},
    {"gas_gauge_CUR_ADC_K3", 0x00ad},
    {"gas_gauge_CUR_ADC_K2", 0x00ae},
    {"gas_gauge_CUR_ADC_K1", 0x00af},
    {"gas_gauge_CUR_ADC_K0", 0x00b0},
    {"PMIC_POWER_EN0", 0x00b1},
    {"PMIC_POWER_EN1", 0x00b2},
    {"PMIC_POWER_EN2", 0x00b3},
    {"PMIC_POWER_EN3", 0x00b4},
    {"PMIC_POWER_SLP_EN0", 0x00b5},
    {"PMIC_POWER_SLP_EN1", 0x00b6},
    {"PMIC_POWER_DISCHRG_EN0", 0x00b7},
    {"PMIC_POWER_DISCHRG_EN1", 0x00b8},
    {"PMIC_POWER_CONFIG", 0x00b9},
    {"PMIC_BUCK1_CONFIG", 0x00ba},
    {"PMIC_BUCK1_ON_VSEL", 0x00bb},
    {"PMIC_BUCK1_SLP_VSEL", 0x00bc},
    {"PMIC_BUCK2_CONFIG", 0x00bd},
    {"PMIC_BUCK2_ON_VSEL", 0x00be},
    {"PMIC_BUCK2_SLP_VSEL", 0x00bf},
    {"PMIC_BUCK3_CONFIG", 0x00c0},
    {"PMIC_BUCK3_ON_VSEL", 0x00c1},
    {"PMIC_BUCK3_SLP_VSEL", 0x00c2},
    {"PMIC_BUCK4_CONFIG", 0x00c3},
    {"PMIC_BUCK4_ON_VSEL", 0x00c4},
    {"PMIC_BUCK4_SLP_VSEL", 0x00c5},
    {"PMIC_BUCK4_CMIN", 0x00c6},
    {"PMIC_LDO1_ON_VSEL", 0x00cc},
    {"PMIC_LDO1_SLP_VSEL", 0x00cd},
    {"PMIC_LDO2_ON_VSEL", 0x00ce},
    {"PMIC_LDO2_SLP_VSEL", 0x00cf},
    {"PMIC_LDO3_ON_VSEL", 0x00d0},
    {"PMIC_LDO3_SLP_VSEL", 0x00d1},
    {"PMIC_LDO4_ON_VSEL", 0x00d2},
    {"PMIC_LDO4_SLP_VSEL", 0x00d3},
    {"PMIC_LDO5_ON_VSEL", 0x00d4},
    {"PMIC_LDO5_SLP_VSEL", 0x00d5},
    {"PMIC_LDO6_ON_VSEL", 0x00d6},
    {"PMIC_LDO6_SLP_VSEL", 0x00d7},
    {"PMIC_LDO7_ON_VSEL", 0x00d8},
    {"PMIC_LDO7_SLP_VSEL", 0x00d9},
    {"PMIC_LDO8_ON_VSEL", 0x00da},
    {"PMIC_LDO8_SLP_VSEL", 0x00db},
    {"PMIC_LDO9_ON_VSEL", 0x00dc},
    {"PMIC_LDO9_SLP_VSEL", 0x00dd},
    {"PMIC_BUCK5_SW1_CONFIG0", 0x00de},
    {"PMIC_BUCK5_CONFIG1", 0x00df},
    {"PMIC_CHIP_NAME", 0x00ed},
    {"PMIC_CHIP_VER", 0x00ee},
    {"PMIC_OTP_VER", 0x00ef},
    {"PMIC_SYS_STS", 0x00f0},
    {"PMIC_SYS_CFG0", 0x00f1},
    {"PMIC_SYS_CFG1", 0x00f2},
    {"PMIC_SYS_CFG2", 0x00f3},
    {"PMIC_SYS_CFG3", 0x00f4},
    {"PMIC_ON_SOURCE", 0x00f5},
    {"PMIC_OFF_SOURCE", 0x00f6},
    {"PMIC_PWRON_KEY", 0x00f7},
    {"PMIC_INT_STS0", 0x00f8},
    {"PMIC_INT_MSK0", 0x00f9},
    {"PMIC_INT_STS1", 0x00fa},
    {"PMIC_INT_MSK1", 0x00fb},
    {"PMIC_INT_STS2", 0x00fc},
    {"PMIC_INT_MSK2", 0x00fd},
    {"PMIC_GPIO_INT_CONFIG", 0x00fe},
};

#define NUM_REGISTERS (sizeof(pmic_registers) / sizeof(pmic_register_t))

static uint8_t get_register_by_name(const char* name) {
    for (int i = 0; i < NUM_REGISTERS; i++) {
        if (strcmp(pmic_registers[i].name, name) == 0) {
            return pmic_registers[i].addr;
        }
    }
    return -1; // Not found
}

// ============================================================================
// Bitbang I2C Implementation
// ============================================================================

static void i2c_delay(void) {
    // Simple delay loop for I2C timing
    for (volatile int i = 0; i < I2C_DELAY_US * 10; i++) {
        __NOP();
    }
}

static inline void sda_high(void) {
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
}

static inline void sda_low(void) {
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
}

static inline void scl_high(void) {
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
}

static inline void scl_low(void) {
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
}

static inline uint8_t sda_read(void) {
    return HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN);
}

static void i2c_start(void) {
    sda_high();
    scl_high();
    i2c_delay();
    sda_low();   // SDA goes low while SCL is high
    i2c_delay();
    scl_low();
    i2c_delay();
}

static void i2c_stop(void) {
    sda_low();
    scl_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high();  // SDA goes high while SCL is high
    i2c_delay();
}

static uint8_t i2c_write_byte(uint8_t byte) {
    uint8_t ack;

    // Write 8 bits
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) {
            sda_high();
        } else {
            sda_low();
        }
        i2c_delay();
        scl_high();
        i2c_delay();
        scl_low();
    }

    // Read ACK bit
    sda_high();  // Release SDA for slave to pull low
    i2c_delay();
    scl_high();
    i2c_delay();
    ack = sda_read();  // 0 = ACK, 1 = NACK
    scl_low();
    i2c_delay();

    return ack == 0 ? 1 : 0;  // Return 1 for success (ACK), 0 for failure (NACK)
}

static uint8_t i2c_read_byte(uint8_t send_ack) {
    uint8_t byte = 0;

    sda_high();  // Release SDA for reading

    // Read 8 bits
    for (int i = 7; i >= 0; i--) {
        i2c_delay();
        scl_high();
        i2c_delay();
        if (sda_read()) {
            byte |= (1 << i);
        }
        scl_low();
    }

    // Send ACK or NACK
    if (send_ack) {
        sda_low();   // ACK
    } else {
        sda_high();  // NACK
    }
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    i2c_delay();
    sda_high();  // Release SDA

    return byte;
}

/**
 * Read a single PMIC register
 * Returns true on success, false on failure
 */
static bool read_pmic_register(uint8_t reg_addr, uint8_t *return_buffer)
{
    // START condition
    i2c_start();

    // Write device address with write bit
    if (!i2c_write_byte(PMIC_I2C_ADDR_7BIT << 1)) {
        i2c_stop();
        return false;
    }

    // Write register address
    if (!i2c_write_byte(reg_addr)) {
        i2c_stop();
        return false;
    }

    // REPEATED START for read
    i2c_start();

    // Write device address with read bit
    if (!i2c_write_byte((PMIC_I2C_ADDR_7BIT << 1) | 0x01)) {
        i2c_stop();
        return false;
    }

    // Read data byte and send NACK (last byte)
    *return_buffer = i2c_read_byte(0);

    // STOP condition
    i2c_stop();

    return true;
}

/**
 * Write a single PMIC register
 * Returns true on success, false on failure
 */
static bool write_pmic_register(uint8_t reg_addr, uint8_t value)
{
    // START condition
    i2c_start();

    // Write device address with write bit
    if (!i2c_write_byte(PMIC_I2C_ADDR_7BIT << 1)) {
        i2c_stop();
        return false;
    }

    // Write register address
    if (!i2c_write_byte(reg_addr)) {
        i2c_stop();
        return false;
    }

    // Write data byte
    if (!i2c_write_byte(value)) {
        i2c_stop();
        return false;
    }

    // STOP condition
    i2c_stop();

    return true;
}

/**
 * Power on the PMIC using the PWRON pin sequence
 */
void pmic_pwron(void) {
    // PMIC enable sequence: high -> low (500ms) -> high
    PWRON_HIGH();
    PWRON_LOW();
    HAL_Delay(500);
    PWRON_HIGH();

    pmic_power_state = 1;
    debug("[PMIC]\tPWRON sequence complete\r\n");
}

void pmic_pwroff(void) {
    // PMIC disable sequence: high -> low (6000ms/6s) -> high
    PWRON_HIGH();
    PWRON_LOW();
    HAL_Delay(6000);
    PWRON_HIGH();

    pmic_power_state = 0;
    debug("[PMIC]\tPWROFF sequence complete\r\n");
}

void pmic_toggle_power(void) {
    if (pmic_power_state) {
        pmic_pwroff();
    } else {
        pmic_pwron();
    }
}

/**
 * Read all PMIC registers
 */
void read_all_pmic_registers(void)
{
    uint8_t reg_value;
    bool status;
    uint32_t success_count = 0;
    uint32_t fail_count = 0;

    debug("[PMIC]\tRK809-5 PMIC Register Dump (I2C3)\r\n");

    for (uint32_t i = 0; i < NUM_REGISTERS; i++) {
        status = read_pmic_register(pmic_registers[i].addr, &reg_value);

        if (status) {
            debug("[PMIC]\t[0x%04X] %-30s = 0x%02X\r\n",
                pmic_registers[i].addr,
                pmic_registers[i].name,
                reg_value);
            success_count++;
        } else {
            debug("[PMIC]\t[0x%04X] %-30s = READ FAILED\r\n",
                pmic_registers[i].addr,
                pmic_registers[i].name);
            fail_count++;
        }

        // Small delay between reads
        HAL_Delay(1);
    }

    debug("[PMIC]\tTotal: %u registers, Success: %lu, Failed: %lu\r\n",
        NUM_REGISTERS, success_count, fail_count);
}

/**
 * Scan I2C bus to debug address issues
 */
void scan_i2c_bus(void)
{
    debug("[PMIC]\tScanning I2C bus...\r\n");

    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 128; addr++) {
        // Send START
        i2c_start();

        // Try to write address with write bit
        uint8_t ack = i2c_write_byte(addr << 1);

        // Send STOP
        i2c_stop();

        if (ack) {
            debug("[PMIC]\tDevice found at 0x%02X (7-bit)\r\n", addr);
            found++;
        }

        // Small delay between probes
        HAL_Delay(1);
    }

    debug("[PMIC]\tScan complete - found %d device(s)\r\n", found);
}

static inline const char *byte_to_binary (int x)
{
    static char b[9];
    b[0] = '\0';

    for (int z = 128; z > 0; z >>= 1) {
        strcat(b, ((x & z) == z) ? "1" : "0");
    }

    return b;
}

/**
 * Test function: Read a few safe, non-OTP status registers to verify I2C communication
 * These are all read-only status/info registers, safe to read anytime
 */
void read_pmic_basic_info(void)
{
    const char* test_registers[] = {
        "PMIC_CHIP_NAME",
        "PMIC_CHIP_VER",
        "PMIC_OTP_VER",
        "PMIC_SYS_STS",
        "PMIC_ON_SOURCE",
        "PMIC_INT_STS0",

        // "PMIC_LDO1_ON_VSEL",
        // "PMIC_LDO4_ON_VSEL",
        // "PMIC_LDO9_ON_VSEL",
        // "PMIC_POWER_EN1",
        // "PMIC_POWER_EN3",
    };

    int num_tests = sizeof(test_registers) / sizeof(test_registers[0]);

    uint8_t reg_value;
    bool status;

    debug("[PMIC]\tRK809-5 PMIC Registers:\r\n");

    for (int i = 0; i < num_tests; i++) {
        uint8_t reg_addr = get_register_by_name(test_registers[i]);
        status = read_pmic_register(reg_addr, &reg_value);

        if (status) {
            debug("[PMIC]\t%-20s (0x%02X): 0x%02X (0b%s)\r\n",
                test_registers[i], reg_addr, reg_value, byte_to_binary(reg_value));
        } else {
            debug("[PMIC]\t%-20s (0x%02X): READ FAILED\r\n",
                test_registers[i], reg_addr);
        }
    }
}

void write_register_list(pmic_register_t* register_writes, int num_writes, int delay_ms) {
    bool status;

    for (int i = 0; i < num_writes; i++) {
        uint8_t reg_addr = register_writes[i].addr;
        uint8_t write_val = register_writes[i].write_value;

        // If mask is 0xFF, write entire register. Otherwise, read-modify-write
        if (register_writes[i].mask != 0xFF) {
            uint8_t current_val;
            if (read_pmic_register(reg_addr, &current_val)) {
                write_val = (current_val & ~register_writes[i].mask) | (write_val & register_writes[i].mask);
            } else {
                debug("[PMIC]\tFailed to read %s (0x%02X) for RMW\r\n",
                    register_writes[i].name, reg_addr);
                continue;
            }
        }

        status = write_pmic_register((uint8_t)reg_addr, write_val);
        if (status) {
            debug("[PMIC]\tSuccessfully wrote 0x%02X to %s (0x%02X)\r\n",
                write_val, register_writes[i].name, reg_addr);
        } else {
            debug("[PMIC]\tFailed to write 0x%02X to %s (0x%02X)\r\n",
                write_val, register_writes[i].name, reg_addr);
        }
        if (delay_ms > 0) {
            HAL_Delay(delay_ms);
        }
    }
}

void read_register_list(pmic_register_t* registers, int num_registers) {
    bool status;

    for (int i = 0; i < num_registers; i++) {
        uint8_t reg_addr = registers[i].addr;
        uint8_t reg_value;

        status = read_pmic_register(reg_addr, &reg_value);
        if (status) {
            debug("[PMIC]\tRead 0x%02X (0b%s) from %s (0x%02X)\r\n",
                reg_value, byte_to_binary(reg_value), registers[i].name, reg_addr);
        } else {
            debug("[PMIC]\tFailed to read from %s (0x%02X)\r\n",
                registers[i].name, reg_addr);
        }
    }
}

void pmic_poweron_seq(void) {
    debug("[PMIC]\tStarting power on sequence...\r\n");

    // Define registers that need config so lookup doesn't have to go through the full list every time
    #define PMIC_LDO1_ON_VSEL  0x00cc
    #define PMIC_LDO4_ON_VSEL  0x00d2
    #define PMIC_LDO9_ON_VSEL  0x00dc
    
    #define PMIC_POWER_EN0     0x00b1
    #define PMIC_POWER_EN1     0x00b2
    #define PMIC_POWER_EN2     0x00b3
    #define PMIC_POWER_EN3     0x00b4

    #define PMIC_BUCK1_CONFIG  0x00ba
    #define PMIC_BUCK5_CONFIG1 0x00df

    pmic_register_t register_writes_seq1[] = {
        {"PMIC_BUCK1_CONFIG",  PMIC_BUCK1_CONFIG,  0b00111111, 0b00111111}, // Maximize peak current limit for BUCK1 (VDD_LOGIC)
        {"PMIC_BUCK5_CONFIG1", PMIC_BUCK5_CONFIG1, 0b00111000, 0b00111000}, // Maximize peak current limit for SWOUT2 (VCC_3V3_SD)

        {"PMIC_LDO1_ON_VSEL",  PMIC_LDO1_ON_VSEL,  0b00001100, 0b01111111}, // 0.9V
        {"PMIC_LDO4_ON_VSEL",  PMIC_LDO4_ON_VSEL,  0b11101100, 0b11111111}, // 3.3V and maximize peak current limit for LDO4 (VCCIO_3V3_SD)
        {"PMIC_LDO9_ON_VSEL",  PMIC_LDO9_ON_VSEL,  0b00110000, 0b01111111}, // 1.8V
    };

    PWRON_HIGH();

    // Configure voltages for non-default LDOs before powering on rails
    write_register_list(register_writes_seq1, sizeof(register_writes_seq1) / sizeof(pmic_write_value_t), 0);
    
    // Power on sequence starts after PWRON brought low. Bring LDO1 up before so it starts in 1st slot
    write_pmic_register(PMIC_POWER_EN1, 0b00010001); // Activate LDO1 (VDDA_0V9_IMG)
    
    PWRON_LOW();                                     // Power on sequence starts ~138ms after this
    // LDO1 comes on here (while delaying) at 138ms with rest of 1st slot
    
    HAL_Delay(136);                                  // 140ms to 2nd rail seq slot, 4ms offset for I2C communication (verified on oscilloscope)
    write_pmic_register(PMIC_POWER_EN3, 0b00010001); // Activate LDO9 (VCCA_1V8_IMG) at 140ms
    
    // 0ms delay + 4ms from I2C communication = 4ms to 4th rail seq slot, 144ms total
    
    write_pmic_register(PMIC_POWER_EN1, 0b10001000); // Activate LDO4 (VCCIO_3V3_SD)
    
    HAL_Delay(5);                                    // RESETB goes high at ~154ms after PWRON low, delay another 5ms to place PWRON high 5ms before RESETB high
    PWRON_HIGH();                                    // Sequence complete, bring PWRON back high
    
    debug("[PMIC]\tPower on sequence complete!\r\n");

    read_register_list(register_writes_seq1, sizeof(register_writes_seq1) / sizeof(pmic_write_value_t));
}