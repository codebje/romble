#include <string.h>
#include <stdio.h>

#include "cmsis_os.h"
#include "task.h"

#include "main.h"
#include "cli.h"
#include "ymodem.h"
#include "sstrom.h"

// When writing a ROM image, this structure tracks the work done so far.
typedef struct __CLI_ROM_Upload {
    SPI_ROM_ConfigDef *spi_rom;
    uint32_t address;
    uint32_t erased;
    uint32_t filesize;
} CLI_ROM_Upload;

// State machine transitions
#define STATE_IDLE      0           // waiting for a system command

// Input commands
#define CMD_HELLO       'h'         // show hello message
#define CMD_HELP        '?'         // show help message
#define CMD_SPI_INFO    'i'         // retrieve ROM information
#define CMD_SPI_UPLOAD  'u'         // upload ROM image
#define CMD_SPI_PEEK    'p'         // dump the first page of the ROM
#define CMD_SST_INFO    'x'         // retrieve parallel ROM information
#define CMD_SST_PEEK    'o'         // dump first 128 bytes of parallel ROM
#define CMD_SST_UPLOAD  'r'         // upload parallel ROM image

void cli_rom_info(const CLI_SetupTypeDef *config)
{

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

void cli_prom_info(const CLI_SetupTypeDef *config)
{

    static char buffer[128];

    uint8_t manufacturer;
    uint8_t device_id;

    sst_rom_read_id(&manufacturer, &device_id);

    snprintf(buffer, sizeof(buffer), "Manufacturer: %02x\r\nDevice ID: %02x\r\n",
        manufacturer, device_id);
    HAL_UART_Transmit(config->huart, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);

}

static char *upload_error = "unknown error\r\n";

// Prepare SPI for file upload
static int cli_open_file(void *arg, const char *filename, uint32_t size)
{

    CLI_ROM_Upload *upload = (CLI_ROM_Upload *)arg;
    HAL_StatusTypeDef result;
    uint8_t manufacturer;
    uint16_t device_id;

    result = spi_rom_read_jedec_id(upload->spi_rom, &manufacturer, &device_id);

    // Is the SPI device present and correct?
    if (result != HAL_OK || manufacturer != SPI_ROM_MANUFACTURER_WINBOND || device_id != SPI_ROM_WINBOND_W25Q32xV) {
        upload_error = "bad SPI device";
        return YMODEM_ERROR;
    }

    upload->address = 0;
    upload->erased = 0;
    upload->filesize = size;

    // Flag that ROM programming is in progress
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);

    return YMODEM_OK;

}

// Write data to SPI ROM
static int cli_write_data(void *arg, const uint8_t *data, uint16_t size)
{

    CLI_ROM_Upload *upload = (CLI_ROM_Upload *)arg;
    uint32_t remaining;

    // upload->erased will hold the next address requiring erasing
    if (upload->erased <= upload->address) {

        // check to see how much flash memory needs to be erased
        if (upload->address >= upload->filesize) {
            // no filesize given, or it wasn't right - remaining is set to the current write size
            remaining = size;
        } else {
            remaining = upload->filesize - upload->address;
        }

        if (remaining > 64 * 1024) {

            // Erase a 64k block, which will leave upload->erased on a 64k boundary, safe for 64k/32k/4k erases
            if (spi_rom_erase(upload->spi_rom, upload->address, SPI_ROM_ERASE_LARGE_BLOCK) != HAL_OK) {

                upload_error = "bad ROM erase 64k\r\n";
                return YMODEM_ERROR;

            }

            upload->erased += 64 * 1024;

        } else if (remaining > 32 * 1024) {

            // Erase a 32k block, which will leave upload->erased on a 32k boundary, safe for 32k/4k erases
            if (spi_rom_erase(upload->spi_rom, upload->address, SPI_ROM_ERASE_BLOCK) != HAL_OK) {

                upload_error = "bad ROM erase 32k\r\n";
                return YMODEM_ERROR;

            }

            upload->erased += 32 * 1024;

        } else {

            // Erase a 4k sector, only 4k erases will be safe from here to the end of the transfer
            if (spi_rom_erase(upload->spi_rom, upload->address, SPI_ROM_ERASE_SECTOR) != HAL_OK) {

                upload_error = "bad ROM erase 4k\r\n";
                return YMODEM_ERROR;

            }

            upload->erased += 4 * 1024;

        }

    }

    // This will write in at most 256-byte chunks
    spi_rom_program(upload->spi_rom, upload->address, data, size);

    upload->address += size;

    return YMODEM_OK;

}

// Finalise ROM write
static void cli_close_file(void *arg, uint8_t status)
{

    UNUSED(arg);
    UNUSED(status);

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

}

static void cli_rom_upload(CLI_SetupTypeDef *config)
{

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
            HAL_UART_Transmit(config->huart, (uint8_t *)upload_error, strlen(upload_error), HAL_MAX_DELAY);
            break;
    }

}

static int cli_sst_open_file(void *arg, const char *filename, uint32_t size)
{

    UNUSED(arg);
    UNUSED(filename);
    UNUSED(size);

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);

    return YMODEM_OK;

}

static int cli_sst_write_data(void *arg, const uint8_t *data, uint16_t size)
{

    uint32_t *address = (uint32_t *)arg;

    if ((*address & 0xfff) == 0 || ((*address & 0x3f000) != ((*address + size) & 0x3f000))) {
        sst_rom_erase(*address, SST_ROM_ERASE_SECTOR);
    }

    if (sst_rom_program(*address, data, size) != HAL_OK) {
        return YMODEM_ERROR;
    }

    *address += size;

    return YMODEM_OK;

}


static void cli_sst_close_file(void *arg, uint8_t status)
{

    UNUSED(arg);
    UNUSED(status);

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

}


static void cli_sst_upload(CLI_SetupTypeDef *config)
{

    static char *ready = "ROMble ready to receive file... ";
    static char *okay = "OK!\r\n";
    static char *fail = "transfer failed\r\n";

    uint32_t address = 0;
    const YModem_ControlDef ctrl = {
        config->huart,
        (void *)&address,
        &cli_sst_open_file,
        &cli_sst_write_data,
        &cli_sst_close_file,
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
            HAL_UART_Transmit(config->huart, (uint8_t *)upload_error, strlen(upload_error), HAL_MAX_DELAY);
            break;
    }

}

static void cli_rom_peek(CLI_SetupTypeDef *config)
{

    static char *error = "Error reading from Flash ROM\r\n";
    static char buffer[80];
    static uint8_t page[256];
    uint8_t i;

    // read a page from the ROM
    if (spi_rom_read_page(&config->spi_rom, 0, page) == HAL_OK) {

        for (i = 0; i < 16; i++) {

            snprintf(buffer, 80,
                    "%02X %02X %02X %02X %02X %02X %02X %02X - %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                    page[i*16+0], page[i*16+1], page[i*16+2], page[i*16+3],
                    page[i*16+4], page[i*16+5], page[i*16+6], page[i*16+7],
                    page[i*16+8], page[i*16+9], page[i*16+10], page[i*16+11],
                    page[i*16+12], page[i*16+13], page[i*16+14], page[i*16+15]
            );
            HAL_UART_Transmit(config->huart, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);

        }

    } else {

        HAL_UART_Transmit(config->huart, (uint8_t *)error, strlen(error), HAL_MAX_DELAY);

    }

}

static void cli_sst_peek(CLI_SetupTypeDef *config)
{

    static char buffer[80];
    static uint8_t sector[4096];
    uint8_t i;

    sst_rom_read_sector(0, sector);

    // display 512 bytes of data
    for (i = 0; i < 32; i++) {

        snprintf(buffer, 80,
            "%02X %02X %02X %02X %02X %02X %02X %02X - %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            sector[i*16+0],  sector[i*16+1],  sector[i*16+2],  sector[i*16+3],
            sector[i*16+4],  sector[i*16+5],  sector[i*16+6],  sector[i*16+7],
            sector[i*16+8],  sector[i*16+9],  sector[i*16+10], sector[i*16+11],
            sector[i*16+12], sector[i*16+13], sector[i*16+14], sector[i*16+15]
        );
        HAL_UART_Transmit(config->huart, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);

    }

}

void cli_loop(CLI_SetupTypeDef *config) {
    static char cmd;
    static char *welcome = "ROMble programmer UI online\r\n? for help\r\n";
    static char *errmsg = "Unrecognised command\r\n";
    static char *help = "ROMble programmer commands:\r\n"
                        "  ? - help\r\n"
                        "  h - hello & debug info\r\n"
                        "  i - SPI ROM information\r\n"
                        "  p - Peek SPI ROM data\r\n"
                        "  u - Upload SPI ROM data\r\n"
                        "  x - Parallel ROM information\r\n"
                        "  o - Peek parallel ROM data\r\n"
                        "  r - Upload parallel ROM data\r\n"
                        ;

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
                        case CMD_SPI_PEEK:
                            cli_rom_peek(config);
                            break;
                        case CMD_SST_INFO:
                            cli_prom_info(config);
                            break;
                        case CMD_SST_PEEK:
                            cli_sst_peek(config);
                            break;
                        case CMD_SST_UPLOAD:
                            cli_sst_upload(config);
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
