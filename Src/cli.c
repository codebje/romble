#include <string.h>
#include <stdio.h>

#include "cmsis_os.h"

#include "main.h"
#include "cli.h"
#include "ymodem.h"

// State machine transitions
#define STATE_IDLE      0           // waiting for a system command

// Input commands
#define CMD_HELLO       'h'         // show hello message
#define CMD_HELP        '?'         // show help message
#define CMD_SPI_INFO    'i'         // retrieve ROM information
#define CMD_SPI_UPLOAD  'u'         // upload ROM image

// SPI constants
#define SPI_CMD_JEDEC_ID    0x9F;   // retrieve JEDEC ID data

void cli_rom_info(CLI_SetupTypeDef *config) {
    static char buffer[128];
    static char *busy = "Error: SPI system is busy\r\n";
    static char *timeout = "Error: SPI timeout\r\n";
    static char *error = "Error: unknown SPI error\r\n";
    uint8_t data[4];
    HAL_StatusTypeDef result;

    // pull SS low
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_RESET);

    // request JEDEC ID
    data[0] = SPI_CMD_JEDEC_ID;
    result = HAL_SPI_TransmitReceive(config->hspi, data, data, 4, 100);

    switch (result) {
        case HAL_OK:
            snprintf(buffer, sizeof(buffer), "Manufacturer: %02x\r\nDevice ID: %02x%02x\r\n",
                data[1], data[2], data[3]);
            HAL_UART_Transmit(config->huart, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);
            break;
        case HAL_BUSY:
            HAL_UART_Transmit(config->huart, (uint8_t *)busy, strlen(busy), HAL_MAX_DELAY);
            break;
        case HAL_TIMEOUT:
            HAL_UART_Transmit(config->huart, (uint8_t *)timeout, strlen(timeout), HAL_MAX_DELAY);
            break;
        case HAL_ERROR: // fall through
        default:
            HAL_UART_Transmit(config->huart, (uint8_t *)error, strlen(error), HAL_MAX_DELAY);
            break;

    }

    // push SS high
    HAL_GPIO_WritePin(config->ss_port, config->ss_pin, GPIO_PIN_SET);

}

// Prepare SPI for file upload
static int cli_open_file(void *arg, const char *filename, uint32_t size) {

    return YMODEM_OK;
}

// Write data to SPI ROM
static int cli_write_data(void *arg, const uint8_t *data, uint16_t size) {

    return YMODEM_OK;
}

// Finalise ROM write
static void cli_close_file(void *arg, uint8_t status) {
    UNUSED(arg);
    UNUSED(status);
}

static void cli_rom_upload(CLI_SetupTypeDef *config) {

    static char *ready = "ROMble ready to receive file... ";
    static char *okay = "OK!\r\n";
    static char *fail = "transfer failed\r\n";

    const YModem_ControlDef ctrl = {
        config->huart,
        (void *)config,
        &cli_open_file,
        &cli_write_data,
        &cli_close_file,
    };

    HAL_UART_Transmit(config->huart, (uint8_t *)ready, strlen(ready), HAL_MAX_DELAY);

    switch (ymodem_receive(&ctrl)) {
        case YMODEM_OK:
            HAL_UART_Transmit(config->huart, (uint8_t *)okay, strlen(okay), HAL_MAX_DELAY);
            break;
        default:
            HAL_UART_Transmit(config->huart, (uint8_t *)fail, strlen(fail), HAL_MAX_DELAY);
            break;
    }

}

void cli_loop(CLI_SetupTypeDef *config) {
    static char cmd;
    static char *welcome = "ROMble programmer UI online\r\n? for help\r\n";
    static char *errmsg = "Unrecognised command\r\n";
    static char *help = "'ROMble programmer commands:\r\n"
                        "  ? - help\r\n"
                        "  i - SPI ROM information\r\n"
                        "  u - Upload SPI ROM data\r\n";
    int state = STATE_IDLE;

    // Infinite loop
    while (1) {

        switch (state) {
            case STATE_IDLE:
                if (HAL_UART_Receive(config->huart, (uint8_t *)&cmd, 1, HAL_MAX_DELAY) == HAL_OK) {
                    switch (cmd) {
                        case CMD_HELLO:
                            HAL_UART_Transmit(config->huart, (uint8_t *)welcome, strlen(welcome), HAL_MAX_DELAY);
                            break;
                        case CMD_HELP:
                            HAL_UART_Transmit(config->huart, (uint8_t *)help, strlen(help), HAL_MAX_DELAY);
                            break;
                        case CMD_SPI_INFO:
                            cli_rom_info(config);
                            break;
                        case CMD_SPI_UPLOAD:
                            cli_rom_upload(config);
                        default:
                            HAL_UART_Transmit(config->huart, (uint8_t *)errmsg, strlen(errmsg), HAL_MAX_DELAY);
                            break;
                    }
                }
                break;
            default:
                state = STATE_IDLE;
                break;
        }

    }

}
