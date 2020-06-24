#ifndef CLI_H
#define CLI_H

#include "stm32f4xx_hal.h"

#include "flashrom.h"

typedef struct __CLI_SetupTypeDef {
    UART_HandleTypeDef *huart;
    SPI_ROM_ConfigDef spi_rom;
} CLI_SetupTypeDef;

// Run the CLI loop - the UART must be initialised
void cli_loop(CLI_SetupTypeDef *config);

#endif