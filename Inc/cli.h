#ifndef CLI_H
#define CLI_H

typedef struct __CLI_SetupTypeDef {
    UART_HandleTypeDef *huart;
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef* ss_port;
    uint16_t ss_pin;
} CLI_SetupTypeDef;

// Run the CLI loop - the UART must be initialised
void cli_loop(CLI_SetupTypeDef *config);

#endif