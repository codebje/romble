#include "cmsis_os.h"

#include "main.h"
#include "sstrom.h"

#define SST_COMMAND_WRITE   0xA0        // write one byte
#define SST_COMMAND_ERASE   0x80        // sector/chip erase
#define SST_COMMAND_IDMODE  0x90        // access software ID
#define SST_COMMAND_EXIT    0xF0        // exit software ID mode

// data is written directly to PORTC bits 0..7
static inline void sst_set_data(uint8_t data) {
    *((volatile uint8_t *)&GPIOC->ODR) = data;
}

static inline uint8_t sst_get_data() {
    return *((volatile uint8_t *)&GPIOC->IDR);
}

uint32_t bpins = 0;

// address lines are all over the joint
static inline void sst_set_address(uint32_t address)
{

    uint32_t bsrr;

    // port A: A9=pa7, A10=pa10, A11=pa9, A17=pa1
    bsrr = ((address & (1<<9)) >> 2)
         | (address & (1<<10))
         | ((address & (1<<11)) >> 2)
         | ((address & (1<<17)) >> 16);
    bsrr |= (~bsrr & 0b0000011010000010) << 16;
    GPIOA->BSRR = bsrr;

    // port B: A0=0, A1=1, A2=2, A3=4, A4=5, A5=6, A6=7, A7=8, A8=13, A12=9, A13=14, A14=15, A15=10, A16=12
    bsrr = (address & 0b111)                        // A0, A1, A2
         | ((address & 0b110000011111000) << 1)     // A3-A7, A13, A14
         | ((address & (1<<8)) << 5)                // A8
         | ((address & (1<<12)) >> 3)               // A12
         | ((address & (1<<15)) >> 5)               // A15
         | ((address & (1<<16)) >> 4);              // A16
    bsrr |= (~bsrr & 0b1111011111110111) << 16;
    GPIOB->BSRR = bsrr;
    bpins = bsrr;

}

/**
 * Perform a write cycle.
 * 
 * The SST39LF020's timing constraints are:
 *  T(AS) - address setup time          0ns
 *  T(AH) - address hold time           30ns
 *  T(CS) - /WE, /CS setup time         0ns
 *  T(CH) - /WE, /CS hold time          0ns
 *  T(CP) - /CE pulse width             40ns
 *  T(WP) - /WE pulse width             40ns
 *  T(CPH) - /CE pulse width high       30ns
 *  T(WPH) - /WE pulse width high       30ns
 * 
 * Write, erase, and software ID modes require a command sequence to be written to the memory.
 * 
 * After address lines are set, no time is required before /WE and /CE are brought low. It doesn't matter which of
 * those two is changed first, there's no required delay between them and the memory understands both /CE and /WE
 * controlled write cycles. Data lines must be set 40ns before /WE or /CE rises, and the control lines must be low
 * for 40ns. Between each write, the control lines must stay high for 30ns.
 * 
 * Set address, set data, lower /CE, lower /WE, wait 40ns or longer, raise /WE, raise /CE, wait 30ns or longer.
 */
static inline void sst_write(uint32_t address, uint8_t data)
{

    sst_set_address(address);
    sst_set_data(data);

    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_WE_Pin, GPIO_PIN_RESET);

    // 40ns is not very long. The STM32F411 runs at 100MHz, so each clock cycle is 10ns. Four NOPs guarantees a minimum
    // of 40ns delay, even if HAL_GPIO_WritePin optimises away to just a register write.
    __NOP();
    __NOP();
    __NOP();
    __NOP();

    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_WE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_SET);

}

static inline uint8_t sst_read(uint32_t address)
{

    uint8_t data;

    sst_set_address(address);

    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_OE_Pin, GPIO_PIN_RESET);

    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();

    data = sst_get_data();

    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_OE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_SET);

    return data;

}

/**
 * @brief   Fetch the SST39F ROM's product identification data
 * 
 * @param   manufacturer  pointer to where to store the manufacturer ID
 * @param   device_id     pointer to where to store the device ID
 * @retval  HAL status
 */

HAL_StatusTypeDef sst_rom_read_id(uint8_t *manufacturer, uint8_t *device_id)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Deselect ROM
    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_OE_GPIO_Port, SST_OE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_WE_Pin, GPIO_PIN_SET);

    // Ensure data lines are High-Z before enabling outputs
    osDelay(1);

    // Configure data lines for push-pull
    GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                        |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // Avoid interrupts mucking with timing too much
    portENTER_CRITICAL();

    // Enter Software ID mode: write 0xAA to 0x5555, write 0x55 to 0x2AAA, write 0x90 to 0x5555
    sst_write(0x5555, 0xaa);
    sst_write(0x2aaa, 0x55);
    sst_write(0x5555, SST_COMMAND_IDMODE);

    // At least 150ns, almost certainly more
    for (int i = 0; i < 15; i++) __NOP();

    // Reconfigure data lines for input
    GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                        |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    *manufacturer = sst_read(0);
    *device_id = sst_read(1);

    // At least 150ns, almost certainly more
    for (int i = 0; i < 15; i++) __NOP();

    // Configure data lines for push-pull
    GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                        |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // exit ID mode
    sst_write(0x5555, 0xaa);
    sst_write(0x2aaa, 0x55);
    sst_write(0x5555, SST_COMMAND_EXIT);

    // At least 150ns, almost certainly more
    for (int i = 0; i < 15; i++) __NOP();

    // Reconfigure data lines for input
    GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                        |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    portEXIT_CRITICAL();

    return HAL_OK;

}

/**
 * @brief   Erase part or all of the ROM.
 * 
 * This will erase either a 4K sector or the entire ROM. When erasing a sector, the address will be masked to a 4K
 * boundary. The address is ignored for a full erase.
 * 
 * @param   address  the address to erase
 * @param   type     one of SST_ERASE_xxxx constants
 * @retval  HAL status
 */
HAL_StatusTypeDef sst_rom_erase(uint32_t address, uint8_t type)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint32_t byte;
    int max_tries;
    int timeout;

    switch (type) {

        case SST_ROM_ERASE_SECTOR:
            address &= 0x3f000;
            byte = 0x30;
            max_tries = 25000000 / 40;
            break;

        case SST_ROM_ERASE_ALL:
            address = 0x5555;
            byte = 0x10;
            max_tries = 100000000 / 40;
            break;

        default:
            return HAL_ERROR;

    }

    // Deselect ROM
    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_OE_GPIO_Port, SST_OE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_WE_Pin, GPIO_PIN_SET);

    // Ensure data lines are High-Z before enabling outputs
    osDelay(1);

    // Configure data lines for push-pull
    GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                        |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    portENTER_CRITICAL();

    sst_write(0x5555, 0xaa);
    sst_write(0x2aaa, 0x55);
    sst_write(0x5555, SST_COMMAND_ERASE);
    sst_write(0x5555, 0xaa);
    sst_write(0x2aaa, 0x55);
    sst_write(address, byte);

    portEXIT_CRITICAL();

    // Reconfigure data lines for input
    GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                        |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    for (timeout = 0; timeout < max_tries; timeout++) {
        // data bit 7 will be inverted until programming is complete
        if ((sst_read(address) & 0x80) == 0x80) {
            break;
        }
    }

    if ((sst_read(address) & 0x80) == 0x00) {
        return HAL_TIMEOUT;
    }

    return HAL_OK;

}

/**
 * @brief   Program bytes into the ROM.
 * 
 * This will program all the given bytes into the ROM, one by one. Sectors will not be erased.
 * 
 * @param   address  the address to begin writing from
 * @param   data     the data to write
 * @param   size     the total size of data to write
 * @retval  HAL status
 */
HAL_StatusTypeDef sst_rom_program(uint32_t address, const uint8_t *data, uint32_t size)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint32_t byte;
    int timeout;

    // Deselect ROM
    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_OE_GPIO_Port, SST_OE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_WE_Pin, GPIO_PIN_SET);

    // Ensure data lines are High-Z before enabling outputs
    osDelay(1);

    for (byte = 0; byte < size; byte++) {

        // Configure data lines for push-pull
        GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                            |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        // Avoid interrupts mucking with timing too much
        portENTER_CRITICAL();

        // Program the byte
        sst_write(0x5555, 0xaa);
        sst_write(0x2aaa, 0x55);
        sst_write(0x5555, SST_COMMAND_WRITE);
        sst_write(byte + address, data[byte]);

        // At least 100ns, almost certainly more
        for (int i = 0; i < 10; i++) __NOP();

        portEXIT_CRITICAL();

        // Reconfigure data lines for input
        GPIO_InitStruct.Pin = SST_D0_Pin|SST_D1_Pin|SST_D2_Pin|SST_D3_Pin 
                            |SST_D4_Pin|SST_D5_Pin|SST_D6_Pin|SST_D7_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        // each read is at least 40ns, 2000x40ns = 8µs
        for (timeout = 0; timeout < 2000; timeout++) {

            // data bit 7 will be inverted until programming is complete
            if ((sst_read(byte + address) & 0x80) == (data[byte] & 0x80)) {
                break;
            }

        }

        if ((sst_read(byte + address) & 0x80) != (data[byte] & 0x80)) {
            return HAL_TIMEOUT;
        }

        // At least 50ns, almost certainly more
        for (int i = 0; i < 5; i++) __NOP();

    }

    return HAL_OK;
}

/**
 * sector = 4k block to read, data points to 4k memory region
 */
HAL_StatusTypeDef sst_rom_read_sector(uint32_t sector, uint8_t *data)
{

    uint32_t address;

    HAL_GPIO_WritePin(SST_CE_GPIO_Port, SST_CE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_OE_GPIO_Port, SST_OE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SST_WE_GPIO_Port, SST_WE_Pin, GPIO_PIN_SET);

    osDelay(1);

    for (address = 0; address < (1<<12); address++) {
        portENTER_CRITICAL();
        data[address] = sst_read(address + sector);
        portEXIT_CRITICAL();
    }

    return HAL_OK;

}
