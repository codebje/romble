/**
 * The W25Q32JQ 32Mbit Flash ROM is accessed using SPI. It supports dual and quad SPI, but neither the FPGA nor
 * the STM32F411 support dual or quad SPI natively, so it will be used in standard SPI mode only.
 * 
 * The ROM is arranged as 16,384 pages, with each page being 256 bytes. Erasing operates on sectors (of 16 pages, 4Kb),
 * blocks (128 or 256 pages, 32Kb/64Kb), or the whole memory.
 * 
 * Each operation requires a Write Enable command beforehand.
 */

#include "cmsis_os.h"

#include "flashrom.h"


// SPI constants
#define SPI_CMD_JEDEC_ID            0x9F        // retrieve JEDEC ID data
#define SPI_CMD_PAGE_PROGRAM        0x02        // program a page of data, up to 256 bytes
#define SPI_CMD_READ_STATUS_1       0x05        // read status register 1
#define SPI_CMD_READ_FAST           0x0B        // fast-read a page
#define SPI_CMD_WRITE_ENABLE        0x06        // enable a write operation
#define SPI_CMD_ERASE_SECTOR        0x20        // erase a 4k sector
#define SPI_CMD_ERASE_BLOCK         0x52        // erase a 32k block
#define SPI_CMD_ERASE_LARGE_BLOCK   0xD8        // erase a 64k block

#define SPI_STATUS_1_BUSY           (2 << 0)    // BUSY bit, set to 1 during program/erase operations

#define SPI_TIMEOUT                 100         // nothing should even take this long, really

static HAL_StatusTypeDef spi_rom_write_enable(const SPI_ROM_ConfigDef *config)
{

    uint8_t enable = SPI_CMD_WRITE_ENABLE;
    HAL_StatusTypeDef result;

    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(config->hspi, &enable, 1, SPI_TIMEOUT);
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);

    return result;

}

static HAL_StatusTypeDef spi_rom_busy_wait(const SPI_ROM_ConfigDef *config)
{

    HAL_StatusTypeDef result;
    uint32_t timeout, delay;
    uint8_t cmd;

    // An erase or program needs at least 50ns before SS goes active again, so give it a full tick
    osDelay(1);

    cmd = SPI_CMD_READ_STATUS_1;
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);
    if ((result = HAL_SPI_Transmit(config->hspi, &cmd, 1, SPI_TIMEOUT)) != HAL_OK) {
        HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);
        return result;
    }

    timeout = osKernelGetTickCount();

    do {

        // Show some mercy to the Flash ROM
        osDelay(1);

        // continually read the status register waiting for the BUSY flag to clear
        if ((result = HAL_SPI_TransmitReceive(config->hspi, &cmd, &cmd, 1, SPI_TIMEOUT)) != HAL_OK) {
            HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);
            return result;
        }

        delay = (osKernelGetTickCount() - timeout);

    } while ((cmd & SPI_STATUS_1_BUSY) != 0 && delay < 3 * osKernelGetTickFreq());

    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);

    return (cmd & SPI_STATUS_1_BUSY) == 0 ? HAL_OK : HAL_TIMEOUT;

}

/**
 * @brief   Fetch the Flash ROM's JEDEC ID code.
 * 
 * @param   config        pointer to the flash configuration data
 * @param   manufacturer  pointer to where to store the manufacturer ID
 * @param   device_id     pointer to where to store the device ID
 * @retval  HAL status
 */
HAL_StatusTypeDef spi_rom_read_jedec_id(const SPI_ROM_ConfigDef *config, uint8_t *manufacturer, uint16_t *device_id)
{

    HAL_StatusTypeDef result;
    uint8_t data[4];

    // request JEDEC ID
    data[0] = SPI_CMD_JEDEC_ID;

    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);
    result = HAL_SPI_TransmitReceive(config->hspi, data, data, 4, SPI_TIMEOUT);
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);

    if (manufacturer != NULL) {
        *manufacturer = data[1];
    }

    if (device_id != NULL) {
        *device_id = (uint16_t)data[2] << 8 | data[3];
    }

    return result;

}

/**
 * @brief   Erase a portion of the Flash ROM.
 * 
 * @param   config   pointer to the flash configuration data
 * @param   address  the address to erase - must be appropriately aligned
 * @param   type     the type of erase to do, one of SPI_ROM_ERASE_xxx
 * @retval  HAL status
 */

HAL_StatusTypeDef spi_rom_erase(const SPI_ROM_ConfigDef *config, uint32_t address, uint8_t type)
{

    HAL_StatusTypeDef result;
    uint8_t cmd[4];

    // Load in the type - done before any SPI operations, in case of argument error
    switch (type) {

        case SPI_ROM_ERASE_SECTOR:
            cmd[0] = SPI_CMD_ERASE_SECTOR;
            break;

        case SPI_ROM_ERASE_BLOCK:
            cmd[0] = SPI_CMD_ERASE_BLOCK;
            break;

        case SPI_ROM_ERASE_LARGE_BLOCK:
            cmd[0] = SPI_CMD_ERASE_LARGE_BLOCK;
            break;

        default:
            return HAL_ERROR;

    }


    if ((result = spi_rom_write_enable(config)) != HAL_OK) {
        return result;
    }

    // Load in the address, MSB first
    cmd[1] = (address >> 16) & 0xff;
    cmd[2] = (address >> 8) & 0xff;
    cmd[3] = address & 0xff;

    // Pump out the instruction
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(config->hspi, cmd, 4, SPI_TIMEOUT);
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);

    if (result != HAL_OK) {
        return result;
    }

    return spi_rom_busy_wait(config);

}

/**
 * @brief   Program bytes into the Flash ROM.
 * 
 * This will program all the given bytes into the Flash ROM, which may take multiple program operations. If
 * an error is returned, the state of the Flash ROM is undefined.
 * 
 * @param   config   pointer to the flash configuration data
 * @param   address  the address to begin writing from
 * @param   data     the data to write
 * @param   size     the total size of data to write
 * @retval  HAL status
 */
HAL_StatusTypeDef spi_rom_program(
    const SPI_ROM_ConfigDef *config,
    uint32_t address,
    const uint8_t *data,
    uint16_t size)
{

    HAL_StatusTypeDef result;
    static uint8_t cmd[4];
    uint16_t chunk;

    while (size > 0) {

        if ((result = spi_rom_write_enable(config)) != HAL_OK) {
            return result;
        }
        
        // A page is 256-byte aligned, see how much of the page is left
        chunk = 256 - (address & 0xff);
        if (chunk > size) chunk = size;

        // Load in the command and address, MSB first
        cmd[0] = SPI_CMD_PAGE_PROGRAM;
        cmd[1] = (address >> 16) & 0xff;
        cmd[2] = (address >> 8) & 0xff;
        cmd[3] = address & 0xff;

        // Perform the program
        HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);
        if ((result = HAL_SPI_Transmit(config->hspi, cmd, 4, SPI_TIMEOUT)) != HAL_OK) {
            HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);
            return result;
        }
        result = HAL_SPI_Transmit(config->hspi, (uint8_t *)data, chunk, SPI_TIMEOUT);
        HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);
        if (result != HAL_OK) {
            return result;
        }

        // Shuffle variables along
        size -= chunk;
        address += chunk;
        data += chunk;

    }

    return HAL_OK;

}

/**
 * @brief   Read bytes from the Flash ROM.
 * 
 * spi_rom_read_page() reads one page of data from the ROM. The address passed must be page-aligned.
 * 
 * @param   config   pointer to the flash configuration data
 * @param   address  the address to begin reading from
 * @param   data     where to store the data
 * @retval  HAL status
 */
HAL_StatusTypeDef spi_rom_read_page(const SPI_ROM_ConfigDef *config, uint32_t address, uint8_t *data)
{

    HAL_StatusTypeDef result;
    uint8_t cmd[5];

    // Must be page aligned
    if ((address & 0xff) != 0) {
        return HAL_ERROR;
    }

    // Load in the command and address, MSB first
    cmd[0] = SPI_CMD_READ_FAST;
    cmd[1] = (address >> 16) & 0xff;
    cmd[2] = (address >> 8) & 0xff;
    cmd[3] = address & 0xff;
    cmd[4] = 0xbe;  // dummy byte inserted for fast-read

    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);
    osDelay(1);
    if ((result = HAL_SPI_Transmit(config->hspi, cmd, 5, SPI_TIMEOUT)) != HAL_OK) {
        HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);
        return result;
    }

    result = HAL_SPI_TransmitReceive(config->hspi, data, data, 256, SPI_TIMEOUT);
    osDelay(1);
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);

    return result;

}