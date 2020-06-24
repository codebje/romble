#include <string.h>
#include <stdio.h>

#include "cmsis_os.h"
#include "task.h"

#include "main.h"
#include "cli.h"
#include "ymodem.h"

// When writing a ROM image, this structure tracks the work done so far.
typedef struct __CLI_ROM_Upload {
    SPI_ROM_ConfigDef *spi_rom;
    uint32_t address;
    uint32_t filesize;
} CLI_ROM_Upload;

// State machine transitions
#define STATE_IDLE      0           // waiting for a system command

// Input commands
#define CMD_HELLO       'h'         // show hello message
#define CMD_HELP        '?'         // show help message
#define CMD_SPI_INFO    'i'         // retrieve ROM information
#define CMD_SPI_UPLOAD  'u'         // upload ROM image

void cli_rom_info(CLI_SetupTypeDef *config) {

    static char buffer[128];
    static char *busy = "Error: SPI system is busy\r\n";
    static char *timeout = "Error: SPI timeout\r\n";
    static char *error = "Error: unknown SPI error\r\n";
    HAL_StatusTypeDef result;
    uint8_t manufacturer;
    uint16_t device_id;

    result = spi_rom_read_jedec_id(&config->spi_rom, &manufacturer, &device_id);

    switch (result) {
        case HAL_OK:
            snprintf(buffer, sizeof(buffer), "Manufacturer: %02x\r\nDevice ID: %04x\r\n",
                manufacturer, device_id);
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

}

// Prepare SPI for file upload
static int cli_open_file(void *arg, const char *filename, uint32_t size) {

    CLI_ROM_Upload *upload = (CLI_ROM_Upload *)arg;
    HAL_StatusTypeDef result;
    uint8_t manufacturer;
    uint16_t device_id;

    result = spi_rom_read_jedec_id(upload->spi_rom, &manufacturer, &device_id);

    // Is the SPI device present and correct?
    if (result != HAL_OK || manufacturer != SPI_ROM_MANUFACTURER_WINBOND || device_id != SPI_ROM_WINBOND_W25Q32xV) {
        return YMODEM_ERROR;
    }

    upload->address = 0;
    upload->filesize = size;

    // Flag that ROM programming is in progress
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);

    return YMODEM_OK;

}

// Write data to SPI ROM
static int cli_write_data(void *arg, const uint8_t *data, uint16_t size) {

    CLI_ROM_Upload *upload = (CLI_ROM_Upload *)arg;

    // check to see how much flash memory needs to be erased
    uint32_t remaining = upload->filesize - upload->address;

    if ((upload->address >= upload->filesize || remaining < 32*1024) && (upload->address & SPI_ROM_SECTOR_MASK) == 0) {
        // If the suggested file size is less than the uploaded data so far, or there's less than 32k left..

    } else if (remaining < 64*1024 && (upload->address & SPI_ROM_BLOCK_MASK) == 0) {

    } else if (remaining >= 64*1024 && (upload->address & SPI_ROM_LARGE_BLOCK_MASK) == 0) {

    }

    //      if (size - address) > 64k and address is on a 64k boundary, erase a 64k block
    // else if (size - address) > 32k and address is on a 32k boundary, erase a 32k block
    // else if                            address is on a  4k boundary, erase a 4k sector


    return YMODEM_OK;

}

// Finalise ROM write
static void cli_close_file(void *arg, uint8_t status) {

    UNUSED(arg);
    UNUSED(status);

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

}

static void cli_rom_upload(CLI_SetupTypeDef *config) {

    static char *ready = "ROMble ready to receive file... ";
    static char *okay = "OK!\r\n";
    static char *fail = "transfer failed\r\n";

    CLI_ROM_Upload upload = { &config->spi_rom, 0, 0 };
    const YModem_ControlDef ctrl = {
        config->huart,
        (void *)&upload,
        &cli_open_file,
        &cli_write_data,
        &cli_close_file,
    };

    HAL_UART_Transmit(config->huart, (uint8_t *)ready, strlen(ready), HAL_MAX_DELAY);

    // Wait 5 seconds for user to select the file
    osDelay(configTICK_RATE_HZ * 5);

    uint8_t result = ymodem_receive(&ctrl);

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

    osDelay(configTICK_RATE_HZ * 1);

    switch (result) {
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
    static char *help = "ROMble programmer commands:\r\n"
                        "  ? - help\r\n"
                        "  i - SPI ROM information\r\n"
                        "  u - Upload SPI ROM data\r\n";
    static char ticker[100];
    int state = STATE_IDLE;

    // Infinite loop
    while (1) {

        switch (state) {
            case STATE_IDLE:
                if (HAL_UART_Receive(config->huart, (uint8_t *)&cmd, 1, HAL_MAX_DELAY) == HAL_OK) {
                    switch (cmd) {
                        case CMD_HELLO:
                            snprintf(ticker, 100, "ticks: %lu\r\n", xTaskGetTickCount() / configTICK_RATE_HZ);
                            ticker[99] = '\0';
                            HAL_UART_Transmit(config->huart, (uint8_t *)ticker, strlen(ticker), HAL_MAX_DELAY);

                            snprintf(ticker, 100, "stack mark: %lu\r\n", uxTaskGetStackHighWaterMark(NULL));
                            ticker[99] = '\0';
                            HAL_UART_Transmit(config->huart, (uint8_t *)ticker, strlen(ticker), HAL_MAX_DELAY);

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
                            break;
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
