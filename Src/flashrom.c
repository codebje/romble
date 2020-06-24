/**
 * The W25Q32JQ 32Mbit Flash ROM is accessed using SPI. It supports dual and quad SPI, but neither the FPGA nor
 * the STM32F411 support dual or quad SPI natively, so it will be used in standard SPI mode only.
 * 
 * The ROM is arranged as 16,384 pages, with each page being 256 bytes. Erasing operates on sectors (of 16 pages, 4Kb),
 * blocks (128 or 256 pages, 32Kb/64Kb), or the whole memory.
 * 
 * Each operation requires a Write Enable command beforehand.
 */

#include "flashrom.h"

// SPI constants
#define SPI_CMD_JEDEC_ID    0x9F    // retrieve JEDEC ID data
#define SPI_TIMEOUT         500     // half a second is a bit of an eternity really

/**
 * @brief   Fetch the Flash ROM's JEDEC ID code.
 * 
 * @param   config        pointer to the flash configuration data
 * @param   manufacturer  pointer to where to store the manufacturer ID
 * @param   device_id     pointer to where to store the device ID
 * @retval  HAL status
 */
HAL_StatusTypeDef spi_rom_read_jedec_id(SPI_ROM_ConfigDef *config, uint8_t *manufacturer, uint16_t *device_id) {

    HAL_StatusTypeDef result;
    uint8_t data[4];

    // pull SS low
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);

    // request JEDEC ID
    data[0] = SPI_CMD_JEDEC_ID;
    result = HAL_SPI_TransmitReceive(config->hspi, data, data, 4, SPI_TIMEOUT);

    if (manufacturer != NULL) {
        manufacturer = data[1];
    }

    if (device_id != NULL) {
        device_id = ((uint16_t *)data)[1];
    }

    // push SS high
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);

    return result;

}