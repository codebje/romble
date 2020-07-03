/**
 * @brief   SST39LF020 Flash ROM interface code
 */

#ifndef SSTROM_H
#define SSTROM_H

#include "stm32f4xx_hal.h"

#define SST_ROM_ERASE_SECTOR        0
#define SST_ROM_ERASE_ALL           1

HAL_StatusTypeDef sst_rom_read_id(uint8_t *, uint8_t *);
HAL_StatusTypeDef sst_rom_erase(uint32_t, uint8_t);
HAL_StatusTypeDef sst_rom_program(uint32_t, const uint8_t *, uint32_t);
HAL_StatusTypeDef sst_rom_read_sector(uint32_t, uint8_t *);

#endif