#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "cmsis_os.h"
#include "task.h"

#include "main.h"
#include "cli.h"
#include "ymodem.h"
#include "sstrom.h"
#include "sdcard.h"

// When writing a ROM image, this structure tracks the work done so far.
typedef struct __CLI_ROM_Upload {
    SPI_ROM_ConfigDef *spi_rom;
    uint32_t address;
    uint32_t erased;
    uint32_t filesize;
} CLI_ROM_Upload;

// State machine transitions
#define STATE_IDLE      0           // waiting for a system command
#define STATE_SDCARD    1           // waiting for an SD card command

// Input commands
#define CMD_HELLO       'h'         // show hello message
#define CMD_HELP        '?'         // show help message
#define CMD_SPI_INFO    'i'         // retrieve ROM information
#define CMD_SPI_UPLOAD  'u'         // upload ROM image
#define CMD_SPI_PEEK    'p'         // dump the first page of the ROM
#define CMD_SST_INFO    'x'         // retrieve parallel ROM information
#define CMD_SST_PEEK    'o'         // dump first 128 bytes of parallel ROM
#define CMD_SST_PANIC   'z'         // dump 128 bytes at 0x12000 of SST ROM
#define CMD_SST_UPLOAD  'r'         // upload parallel ROM image
#define CMD_SD_MODE     's'         // open SD menu

static uint32_t sst_peek_address = 0;

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

    sst_peek_address = 0;

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
        upload_error = "bad SPI device\r\n";
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
    static char *fail = "transfer failed: ";

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

    upload_error = "unknown error\r\n";

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

uint32_t sectors = 0;
uint32_t seclist[256];

static int cli_sst_open_file(void *arg, const char *filename, uint32_t size)
{

    UNUSED(arg);
    UNUSED(filename);
    UNUSED(size);

    sectors = 0;

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);

    return YMODEM_OK;

}

static int cli_sst_write_data(void *arg, const uint8_t *data, uint16_t size)
{

    uint32_t *address = (uint32_t *)arg;
    uint8_t result;

    if (((*address) & 0xfff) == 0 || (((*address) & 0x3f000) != (((*address) + size - 1) & 0x3f000))) {
        seclist[sectors++] = *address;
        result = sst_rom_erase(*address, SST_ROM_ERASE_SECTOR);
        switch (result) {
            case HAL_OK:
                break;
            case HAL_TIMEOUT:
                upload_error = "page erase timeout\r\n";
                return YMODEM_ERROR;
            default:
                upload_error = "page erase error\r\n";
                return YMODEM_ERROR;
        }
    }

    if ((result = sst_rom_program(*address, data, size)) != HAL_OK) {
        upload_error = result == HAL_TIMEOUT ? "page write timeout\r\n" : "page write error\r\n";
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
    static char *fail = "transfer failed: ";

    uint32_t address = 0;
    const YModem_ControlDef ctrl = {
        config->huart,
        (void *)&address,
        &cli_sst_open_file,
        &cli_sst_write_data,
        &cli_sst_close_file,
    };

    HAL_UART_Transmit(config->huart, (uint8_t *)ready, strlen(ready), HAL_MAX_DELAY);

    upload_error = "unknown error\r\n";

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

#define P(c) (isprint(c) ? c : '.')

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
                    "%02X %02X %02X %02X %02X %02X %02X %02X - %02X %02X %02X %02X %02X %02X %02X %02X",
                    page[i*16+0], page[i*16+1], page[i*16+2], page[i*16+3],
                    page[i*16+4], page[i*16+5], page[i*16+6], page[i*16+7],
                    page[i*16+8], page[i*16+9], page[i*16+10], page[i*16+11],
                    page[i*16+12], page[i*16+13], page[i*16+14], page[i*16+15]
            );
            HAL_UART_Transmit(config->huart, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);
            snprintf(buffer, 80, "    %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\r\n",
                    P(page[i*16+0]), P(page[i*16+1]), P(page[i*16+2]), P(page[i*16+3]),
                    P(page[i*16+4]), P(page[i*16+5]), P(page[i*16+6]), P(page[i*16+7]),
                    P(page[i*16+8]), P(page[i*16+9]), P(page[i*16+10]), P(page[i*16+11]),
                    P(page[i*16+12]), P(page[i*16+13]), P(page[i*16+14]), P(page[i*16+15])
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

    sst_rom_read_sector(sst_peek_address, sector);

    // display 512 bytes of data
    for (i = 0; i < 32; i++) {

        snprintf(buffer, 80,
            "%05lX   %02X %02X %02X %02X %02X %02X %02X %02X - %02X %02X %02X %02X %02X %02X %02X %02X",
            sst_peek_address,
            sector[i*16+0],  sector[i*16+1],  sector[i*16+2],  sector[i*16+3],
            sector[i*16+4],  sector[i*16+5],  sector[i*16+6],  sector[i*16+7],
            sector[i*16+8],  sector[i*16+9],  sector[i*16+10], sector[i*16+11],
            sector[i*16+12], sector[i*16+13], sector[i*16+14], sector[i*16+15]
        );
        sst_peek_address += 32;
        HAL_UART_Transmit(config->huart, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);
        snprintf(buffer, 80, "    %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\r\n",
                P(sector[i*16+0]), P(sector[i*16+1]), P(sector[i*16+2]), P(sector[i*16+3]),
                P(sector[i*16+4]), P(sector[i*16+5]), P(sector[i*16+6]), P(sector[i*16+7]),
                P(sector[i*16+8]), P(sector[i*16+9]), P(sector[i*16+10]), P(sector[i*16+11]),
                P(sector[i*16+12]), P(sector[i*16+13]), P(sector[i*16+14]), P(sector[i*16+15])
        );
        HAL_UART_Transmit(config->huart, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);

    }

}

void binprint(char *buf, uint32_t val) {
    buf[16] = '\r';
    buf[17] = '\n';
    buf[18] = '\0';

    for (uint8_t i = 0; i < 16; i++) {
        buf[15-i] = (val & 1) ? '1' : '0';
        val >>= 1;
    }
}
extern uint32_t bpins;

void sd_sendclocks(CLI_SetupTypeDef *config) {
    uint8_t byte = 0xff;
    HAL_GPIO_WritePin(config->spi_rom.ss_port, config->spi_rom.ss_pin, GPIO_PIN_SET);
    for (int i = 0; i < 8; i++) {
        HAL_SPI_Transmit(config->spi_rom.hspi, &byte, 1, 100);
    }
}

uint8_t sd_transfer(CLI_SetupTypeDef *config, uint8_t byte)
{

    uint8_t recv;
    HAL_SPI_TransmitReceive(config->spi_rom.hspi, &byte, &recv, 1, 10);
    return recv;

}

uint8_t sd_command(CLI_SetupTypeDef *config, uint8_t cmd, uint32_t arg, int resp, size_t data, uint8_t *buffer)
{
    int retries;
    uint8_t cmdbuf[6];

    HAL_GPIO_WritePin(config->spi_rom.ss_port, config->spi_rom.ss_pin, GPIO_PIN_SET);
    sd_transfer(config, 0xff);
    HAL_GPIO_WritePin(config->spi_rom.ss_port, config->spi_rom.ss_pin, GPIO_PIN_RESET);

    for (retries = 0; retries < 20; retries++) {
        if (sd_transfer(config, 0xff) == 0xff) break;
    }
    if (retries > 19) {
        HAL_GPIO_WritePin(config->spi_rom.ss_port, config->spi_rom.ss_pin, GPIO_PIN_SET);
        sd_transfer(config, 0xff);
        return 0xff;
    }

    cmdbuf[0] = 0x40 | cmd;
    cmdbuf[1] = (arg >> 24) & 0xff;
    cmdbuf[2] = (arg >> 16) & 0xff;
    cmdbuf[3] = (arg >>  8) & 0xff;
    cmdbuf[4] = (arg >>  0) & 0xff;
    cmdbuf[5] = (cmd == 0 ? 0x95 : (cmd == 8 ? 0x87 : 1));

    HAL_SPI_Transmit(config->spi_rom.hspi, cmdbuf, 6, 10);

    uint8_t r1, r2;

    for (retries = 0; retries < 8; retries++) {
        if ((r1 = sd_transfer(config, 0xff)) != 0xff) break;
    }
    if (retries > 7) {
        HAL_GPIO_WritePin(config->spi_rom.ss_port, config->spi_rom.ss_pin, GPIO_PIN_SET);
        sd_transfer(config, 0xff);
        return 0xff;
    }

    switch (resp) {
        case R1:
            break;
        case R2:
            r2 = sd_transfer(config, 0xff);
            if (buffer != NULL && data >= 1)
                buffer[0] = r2;
            break;
        case R3:
        case R7:
            for (int i = 0; i < 4; i++) {
                r2 = sd_transfer(config, 0xff);
                if (buffer != NULL && data >= i+1)
                    buffer[i] = r2;
            }
            break;
        case RDATA:
            // TODO
            break;
    }

    sd_transfer(config, 0xff);
    HAL_GPIO_WritePin(config->spi_rom.ss_port, config->spi_rom.ss_pin, GPIO_PIN_SET);
    sd_transfer(config, 0xff);

    return r1;

}

void printr1(CLI_SetupTypeDef *config, uint8_t r1)
{
    char r1buf[10];

    snprintf(r1buf, 10, "R1=%02x\r\n", r1);
    HAL_UART_Transmit(config->huart, (uint8_t *)r1buf, strlen(r1buf), HAL_MAX_DELAY);
}

void printr7(CLI_SetupTypeDef *config, uint32_t r7)
{
    char r7buf[14];

    snprintf(r7buf, 14, "R7=%08lx\r\n", r7);
    HAL_UART_Transmit(config->huart, (uint8_t *)r7buf, strlen(r7buf), HAL_MAX_DELAY);
}

void cli_loop(CLI_SetupTypeDef *config) {
    static char cmd;
    static char *welcome = "ROMble programmer v1.0.1 online\r\n? for help\r\n";
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
    static char *sdhelp = "ROMble SD commands:\r\n"
                        " 0 - send 80 clock cycles\r\n"
                        " 1 - send CMD0\r\n"
                        " 2 - set voltage to 3.3v\r\n"
                        " 3 - send ACMD41\r\n"
                        " 4 - get card status\r\n"
                        " 5 - read OCR\r\n"
                        " 6 - read CID\r\n"
                        " 7 - read CSD\r\n"
                        " 8 - read MBR\r\n"
                        " 9 - set block length\r\n"
                        ;

    static char ticker[100];
    int state = STATE_IDLE;

    uint8_t r1;
    uint32_t sdword;

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
                        case CMD_SST_PANIC:
                            sst_peek_address = 0x12000;
                        case CMD_SST_PEEK:
                            cli_sst_peek(config);
                            break;
                        case CMD_SST_UPLOAD:
                            cli_sst_upload(config);
                            break;
                        case 'q':
                            snprintf(ticker, 100, "sectors erased: %lu\r\n", sectors);
                            HAL_UART_Transmit(config->huart, (uint8_t *)ticker, strlen(ticker), HAL_MAX_DELAY);
                            for (uint32_t s = 0; s < sectors; s++) {
                                snprintf(ticker, 100, "  - %lu\r\n", seclist[s]);
                                HAL_UART_Transmit(config->huart, (uint8_t *)ticker, strlen(ticker), HAL_MAX_DELAY);
                            }
                            break;
                        case CMD_SD_MODE:
                            state = STATE_SDCARD;
                            config->spi_rom.hspi->Instance->I2SPR = 128;
                            HAL_UART_Transmit(config->huart, (uint8_t *)sdhelp, strlen(sdhelp), HAL_MAX_DELAY);
                            break;
                        default:
                            HAL_UART_Transmit(config->huart, (uint8_t *)errmsg, strlen(errmsg), HAL_MAX_DELAY);
                            break;
                    }
                }
                break;
            case STATE_SDCARD:
                if (HAL_UART_Receive(config->huart, (uint8_t *)&cmd, 1, HAL_MAX_DELAY) == HAL_OK) {
                    switch (cmd) {
                        case '0':
                            sd_sendclocks(config);
                            break;
                        case '1':
                            r1 = sd_command(config, CMD0, 0, R1, 0, NULL);
                            printr1(config, r1);
                            break;
                        case '2':
                            r1 = sd_command(config, CMD8, 0x1AA, R7, 4, &sdword);
                            printr1(config, r1);
                            if (r1 != 0xff) {
                                printr7(config, sdword);
                            }
                            break;
                        case '3':
                            r1 = sd_command(config, CMD55, 0, R1, 0, NULL);
                            printr1(config, r1);
                            r1 = sd_command(config, ACMD41, 0x40000000, R1, 0, NULL);
                            printr1(config, r1);
                            break;
                        case '4':
                            break;
                        case '5':
                            r1 = sd_command(config, CMD58, 0, R3, 4, &sdword);
                            printr1(config, r1);
                            if (r1 != 0xff) {
                                printr7(config, sdword);
                            }
                            break;
                        case '6':
                            break;
                        case '7':
                            break;
                        case '8':
                            break;
                        case '9':
                            break;
                        case CMD_HELP:
                            HAL_UART_Transmit(config->huart, (uint8_t *)sdhelp, strlen(sdhelp), HAL_MAX_DELAY);
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
