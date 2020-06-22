/**
 * A probably broken ymodem implementation.
 * 
 * Based on the documentation in XMODEM/YMODEM PROTOCOL REFERENCE
 * 
 *      http://www.blunk-electronic.de/train-z/pdf/xymodem.pdf
 * 
 * Tested against MINICOM's implementation, for receive only. This implementation supports 1024-byte blocks, batch
 * transfers, and of course, CRC-16.
 * 
 * The basics of YMODEM are simple. The sender transmits a control byte, which is either SOH, STX, EOT, or CAN. SOH
 * indicates a 128-byte data packet, and STX indicates a 1024-byte data packet. EOT is a single byte indicating the
 * sender has no more packets to send. CAN followed by another CAN immediately terminates the transfer. The receiver
 * responds to packets with ACK to indicate successful receipt or NAK to indicate failure of some kind.
 * 
 * Data packets contain the command byte, an 8-bit sequence ID, the 1s complement of the sequence ID, 128 or 1024 bytes
 * of data, and a checksum. The checksum is an 8-bit sum unless the receiver indicates it can process CRC-16s, so with
 * no action taken the sender will initially send a metadata packet with an 8-bit checksum. The receiver should respond
 * to this packet with a 'C' instead of a NAK, and it may send the 'C' as soon as it is ready to indicate to the sender
 * that it's available and can process CRC-16.
 * 
 * The metadata packet must be ACKed, and then once more a 'C' should be sent to begin the data transfer. Each data
 * packet must be ACKed for the next to be sent. The last packet will contain padding - if the file size was sent the
 * receiver can truncate it. After the last packet has been ACKed, the sender will send a single EOT byte. This also
 * should be ACKed, and then another 'C' should be sent to get the next metadata block.
 * 
 * A final metadata block with a zero-length filename indicates the end of the batch. The receiver ACKs this final
 * block to complete the session.
 * 
 * This implementation assumes a benign sender. There are several ways that an infinite loop could be triggered, but
 * none are plausible as a result of line noise.
 * 
 */

#include <ctype.h>      // isdigit()
#include <string.h>

#include "ymodem.h"

#define SOH     0x01    // start of a 128-byte packet
#define STX     0x02    // start of a 1024-byte packet
#define EOT     0x04    // end of transmission
#define ACK     0x06    // received ok
#define NAK     0x15    // receive error
#define CAN     0x18    // cancel transmission
#define CRCMODE 0x43    // 'C' to indicate CRC desired

#define YM_OPER_TIMEOUT  (10*1000)      // 10 second timeout for operation byte
#define YM_DATA_TIMEOUT  (1*1000)       // 1 second timeout for packet data

// Check a HAL response code and return a suitable outcome
#define YM_ERRCHECK(stmt) \
switch (stmt) { \
    case HAL_OK: break;\
    case HAL_TIMEOUT: return YMODEM_TIMEOUT; \
    default: return YMODEM_ERROR; \
}

/* Prototypes */
static uint16_t ym_crc(const uint8_t *, uint16_t);
static int ym_read(const YModem_ControlDef *, const uint8_t, uint8_t *);
static uint16_t ym_get_size(const uint8_t *, uint16_t);

/**
 * Receive a YModem packet of data. This will be one control byte, two sequence bytes, 128 or 1024 data bytes, and
 * two CRC bytes.
 * 
 * This will make ten attempts to receive data. After each timeout, the 'retry' byte will be sent to prompt the
 * remote end to have another go. This should be NAK in most cases, or 'C' when metadata or the first data packet
 * is expected.
 * 
 * Data packets will have their CRCs validated. A CRC error will result in a retry.
 * 
 * Returns a YMODEM_XXXX status code.
 */
static int ym_read(const YModem_ControlDef *ctrl, const uint8_t retry, uint8_t *buf) {

    HAL_StatusTypeDef result;
    uint8_t _retry = retry; // HAL doesn't mark parameters const appropriately :()
    uint8_t tries;
    uint16_t size;

    for (tries = 0; tries < 10; tries++) {

        // On the second and subsequent attempts, send the response code again
        if (tries > 1) {

            // clear the line of any pending data
            while (HAL_UART_Receive(ctrl->huart, buf, 1, 100) == HAL_OK) {}

            // send the retry code byte, with a short timeout
            YM_ERRCHECK(HAL_UART_Transmit(ctrl->huart, &_retry, 1, YM_DATA_TIMEOUT));

        }

        // Read the packet control byte, with a full timeout
        result = HAL_UART_Receive(ctrl->huart, buf, 1, YM_OPER_TIMEOUT);

        // A timeout causes a re-transmit of the respones code and another loop.
        if (result == HAL_TIMEOUT) {
            continue;
        } else if (result != HAL_OK) {
            break;
        }

        if (buf[0] == SOH || buf[0] == STX) {   // A SOH or STX packet has a payload

            // How big is the payload?
            size = buf[0] == SOH ? 128 : 1024;

            // Receive data: one second timeouts
            result = HAL_UART_Receive(ctrl->huart, buf + 1, size + 4, YM_DATA_TIMEOUT);

            if (result == HAL_TIMEOUT) {
                continue;
            } else if (result != HAL_OK) {
                break;
            }

            // If the sequence numbers are wonky, retry
            if (buf[1] != ((~buf[2]) & 0xff)) {
                result = HAL_ERROR;
                continue;
            }

            // A CRC error? That's a retryin'.
            if (ym_crc(buf + 3, size + 2)) {
                result = HAL_ERROR;
                continue;
            }

            // A data packet is all good
            return YMODEM_OK;

        } else if (buf[0] == CAN) {         // A CAN might mean we're aborting the whole session

            // Get the next byte along
            result = HAL_UART_Receive(ctrl->huart, buf + 1, 1, YM_DATA_TIMEOUT);

            // If it's a CAN as well, we're done here
            if (result == HAL_OK && buf[1] == CAN) {
                return YMODEM_CANCEL;
            }

            // If it was a UART error, bail out
            if (result != HAL_OK && result != HAL_TIMEOUT) {
                break;
            }

            // Otherwise, retry
            continue;

        } else if (buf[0] == EOT) {     // End of transmission?

            return YMODEM_OK;

        }

        // Any other command code is an error. Retry or cancel out.
        result = HAL_ERROR;

    }

    YM_ERRCHECK(result);
    return YMODEM_OK;

}

static uint16_t ym_get_size(const uint8_t *buffer, uint16_t maxlen) {

    const uint8_t *end = buffer + maxlen;
    uint16_t val = 0;

    while (buffer < end && isdigit(*buffer)) {
        val = val * 10 + (uint16_t)*(buffer++) - '0';
    }

    return val;
}

uint8_t ymodem_receive(const YModem_ControlDef *ctrl) {

    // Static variables mean ymodem_receive is non-reentrant and not thread safe, but will work even with
    // FreeRTOS' typically anemic stack depths.

    // Enough space for command, seq/~seq, 1024-byte payload, and crc16.
    static uint8_t buffer[1024 + 3 + 2];

    // Constants for common messages
    static const uint8_t cancel[2] = { CAN, CAN }; // probably a dancing pun in here somewhere
    static const uint8_t crc[1] = { CRCMODE };
    static const uint8_t ack[1] = { ACK };

    uint32_t remaining = 0xffff;    // bytes left to receive
    uint16_t block_number = 0;      // expected block number
    uint16_t data_size;             // packet's data size
    char *filename;
    int result;

    do {

        // Read a metadata packet, or die trying
        YM_ERRCHECK(HAL_UART_Transmit(ctrl->huart, (uint8_t *)crc, 1, YM_DATA_TIMEOUT));
        if ((result = ym_read(ctrl, 'C', buffer)) != YMODEM_OK) {
            return result;
        }

        // If the ACK was lost after one transfer, the 'C' will result in a re-sent EOT. ACK it again then retry.
        // This potentially creates an infinite loop in which a sender could always respond to a 'C' with EOT.
        // I trust the only sender to this system  not to maliciously deadlock it, so I am not defending against it.
        if (buffer[0] == EOT) {

            YM_ERRCHECK(HAL_UART_Transmit(ctrl->huart, (uint8_t *)ack, 1, YM_DATA_TIMEOUT));
            continue;

        }

        // ym_read() will only return a SOH, STX, or EOT.
        data_size = buffer[0] == STX ? 1024 : 128;

        // Metadata should be block number zero. If not, we're out of sync - cancel.
        if (buffer[1] != 0x00 || buffer[2] != 0xff) {
            HAL_UART_Transmit(ctrl->huart, (uint8_t *)cancel, 2, YM_DATA_TIMEOUT);
            return YMODEM_ERROR;
        }

        // NUL filename means end of transfer session
        if (!buffer[3]) {
            HAL_UART_Transmit(ctrl->huart, (uint8_t *)ack, 1, YM_DATA_TIMEOUT);
            return YMODEM_OK;
        }

        // Try to parse out a length
        filename = (char *)buffer + 3;
        remaining = strnlen(filename, data_size - 1);
        filename[remaining] = '\0';
        remaining = ym_get_size(buffer + 4 + remaining, data_size - remaining - 1);

        // Open the file, or abort the transfer
        if (ctrl->open(ctrl->cb_data, filename, remaining) != YMODEM_OK) {
            HAL_UART_Transmit(ctrl->huart, (uint8_t *)cancel, 2, YM_DATA_TIMEOUT);
            return YMODEM_ERROR;
        }

        // Ack it and begin data transfers
        HAL_UART_Transmit(ctrl->huart, (uint8_t *)ack, 1, YM_DATA_TIMEOUT);
        HAL_UART_Transmit(ctrl->huart, (uint8_t *)crc, 1, YM_DATA_TIMEOUT);
        block_number = 1;
        do {

            // Get the next packet
            if ((result = ym_read(ctrl, block_number == 1 ? CRCMODE : NAK, buffer)) != YMODEM_OK) {
                HAL_UART_Transmit(ctrl->huart, (uint8_t *)cancel, 2, YM_DATA_TIMEOUT);
                ctrl->close(ctrl->cb_data, result);
                return result;
            }

            // Does the remote think we're all done?
            if (buffer[0] == EOT) {

                // Do _we_ think we're all done?
                if (remaining == 0) {

                    // close the transfer off, ACK the EOT, and go back to see if there's another file
                    ctrl->close(ctrl->cb_data, YMODEM_OK);
                    HAL_UART_Transmit(ctrl->huart, (uint8_t *)ack, 1, YM_DATA_TIMEOUT);
                    break;

                } else {

                    // Could be a glitch in the command byte, but let's assume desynch error instead
                    ctrl->close(ctrl->cb_data, YMODEM_ERROR);
                    HAL_UART_Transmit(ctrl->huart, (uint8_t *)cancel, 2, YM_DATA_TIMEOUT);
                    return YMODEM_ERROR;

                }

            }

            // Check sequence numbers
            if (buffer[1] != (block_number & 0xff)) {

                // A repeat of the last block - ACK it again and go back for more.
                // Another infinite loop is possible here.
                if (buffer[1] == ((block_number - 1) & 0xff)) {
                    HAL_UART_Transmit(ctrl->huart, (uint8_t *)ack, 1, YM_DATA_TIMEOUT);
                    continue;
                }

                // Anything else is sadly fatal
                ctrl->close(ctrl->cb_data, YMODEM_ERROR);
                HAL_UART_Transmit(ctrl->huart, (uint8_t *)cancel, 2, YM_DATA_TIMEOUT);
                return YMODEM_ERROR;

            }

            block_number++;

            data_size = buffer[0] == STX ? 1024 : 128;

            // Trim data_size down if a file size was given. Once more, a hostile sender could send an infinite
            // stream of data, this check here only adjusts remaining down to zero, and will work even if no
            // file size was sent.
            if (remaining > 0) {
                data_size = remaining < data_size ? remaining : data_size;
                remaining -= data_size;
            }

            // Consume the packet, or die trying
            if (ctrl->write(ctrl->cb_data, buffer + 3, data_size) != YMODEM_OK) {
                ctrl->close(ctrl->cb_data, YMODEM_CANCEL);
                HAL_UART_Transmit(ctrl->huart, (uint8_t *)cancel, 2, YM_DATA_TIMEOUT);
                return YMODEM_CANCEL;
            }

            // ACK the received packet and go get more
            HAL_UART_Transmit(ctrl->huart, (uint8_t *)ack, 1, YM_DATA_TIMEOUT);

        } while (1);

    } while (1);

}

/**
 * CRC constant lookup.
 * 
 * The XMODEM/YMODEM CRC-16 is based on the polynomial x^16 + x^12 + x^5 + x^0, which is a generator of 0x1021.
 * 
 * A 16-bit CRC value computed byte by byte will xor the next input byte into the high byte of the current
 * value, then perform a polynomial division with the generator. The polynomial division loop operates bit
 * by bit:
 * 
 * for (int bit = 0; bit < 8; bit++) {
 *     if (crc & 0x8000)
 *         crc = (crc << 1) ^ generator;
 *     else
 *         crc = crc << 1;
 * }
 * 
 * But since this is dividing a byte, a 256-entry lookup table will also do the trick - work out the table
 * entry from the current CRC high byte and the next byte to process, xor that entry with the shifted CRC,
 * and that's it.
 * 
 * Going further, that 256-entry lookup table can be computed from two 16-entry lookup tables, one for each
 * nibble of the dividend byte. The shifted CRC is xor'd with each entry to produce the next CRC value.
 * 
 * This table is produced with:
 * 
 *   for (int i = 0; i < 16; i++) {
 *       uint16_t crc = i << 8;
 *       for (int bit = 0; bit < 8; bit++) {
 *           if (crc & 0x8000)
 *               crc = (crc << 1) ^ 0x1021;
 *           else
 *               crc = crc << 1;
 *       }
 *       ym_crc_tab[i] = crc;
 *
 *       crc = i << 12;
 *       for (int bit = 0; bit < 8; bit++) {
 *           if (crc & 0x8000)
 *               crc = (crc << 1) ^ 0x1021;
 *           else
 *               crc = crc << 1;
 *       }
 *       ym_crc_tab[i + 16] = crc;
 *   }
 * 
 * Thanks to the excellent CRC documentation at http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html
 * and Arjen Lentz' 8-bit CRC 32-entry function at https://lentz.com.au/blog/tag/crc-table-generator.
 * 
 * As this is declared static, it will be stored in Flash, not SRAM.
 */

static const uint16_t ym_crc_tab[32] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x0000, 0x1231, 0x2462, 0x3653, 0x48c4, 0x5af5, 0x6ca6, 0x7e97,
    0x9188, 0x83b9, 0xb5ea, 0xa7db, 0xd94c, 0xcb7d, 0xfd2e, 0xef1f,
};


/**
 * Compute a YModem CRC-16.
 * 
 * If the two bytes of the CRC are included in <buf> this will return zero when no errors are detected.
 */
static uint16_t ym_crc(const uint8_t *buf, uint16_t size) {

    uint16_t crc = 0;

    for (uint16_t i = 0; i < size; i++) {
        uint8_t pos = (uint8_t)(crc >> 8) ^ buf[i];
        crc = (crc << 8) ^ ym_crc_tab[pos & 0xf] ^ ym_crc_tab[(pos >> 4) + 16];
    }

    return crc;

}
