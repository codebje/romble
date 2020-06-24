/**
 * @brief   SPI Flash ROM interface code
 */

#ifndef FLASHROM_H
#define FLASHROM_H

#include "stm32f4xx_hal.h"

#define SPI_ROM_MANUFACTURER_WINBOND        0xEF        // manufacturer ID
#define SPI_ROM_WINBOND_W25Q32xV            0x4016      // device ID

#define SPI_ROM_SECTOR_MASK                 0xFFF       // 4K mask
#define SPI_ROM_BLOCK_MASK                  0x7FFF      // 32K mask
#define SPI_ROM_LARGE_BLOCK_MASK            0xFFFF      // 64K mask

#define SPI_ROM_ERASE_SECTOR                1
#define SPI_ROM_ERASE_BLOCK                 2
#define SPI_ROM_ERASE_LARGE_BLOCK           3

typedef struct __SPI_ROM_ConfigDef {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef* ss_port;
    uint16_t ss_pin;
} SPI_ROM_ConfigDef;

HAL_StatusTypeDef spi_rom_read_jedec_id(const SPI_ROM_ConfigDef *, uint8_t *, uint16_t *);
HAL_StatusTypeDef spi_rom_erase(const SPI_ROM_ConfigDef *, uint32_t, uint8_t);
HAL_StatusTypeDef spi_rom_program(const SPI_ROM_ConfigDef *, uint32_t, const uint8_t *, uint16_t);
HAL_StatusTypeDef spi_rom_read_page(const SPI_ROM_ConfigDef *, uint32_t, uint8_t *);

#endif