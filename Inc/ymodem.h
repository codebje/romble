#ifndef YMODEM_H
#define YMODEM_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#define YMODEM_OK       0
#define YMODEM_TIMEOUT  1
#define YMODEM_ERROR    2
#define YMODEM_CANCEL   3

typedef int (*YModem_CB_Open)(void *, const char *, uint32_t);
typedef int (*YModem_CB_Write)(void *, const uint8_t *, uint16_t);
typedef void (*YModem_CB_Close)(void *, uint8_t);

typedef struct __YModem_ControlDef {
    /* The UART to communicate across. */
    UART_HandleTypeDef *huart;

    /* User data argument to pass to all callbacks */
    void *cb_data;
    
    /* 
     * Return a true value to accept the file and begin transfer, or false to reject.
     * The filename will always be supplied, but size may be zero if the sender did
     * not provide the data.
     */
    YModem_CB_Open open;

    /* 
     * Return a true value if the write was successful, or false to cancel the transfer.
     * The size will usually be 128 or 1024, except for the final packet, which will
     * be the truncated data size to end the file without padding bytes.
     */
    YModem_CB_Write write;

    /* 
     * Terminate a transfer.
     * This will be called with YMODEM_OK in the normal course of events, or one of the
     * other YMODEM_XXXX constants if an abnormal termination occurs.
     */
    YModem_CB_Close close;

} YModem_ControlDef;

/* Receive zero or more files using YMODEM. Returns one of the YMODEM_XXXX constants. */
uint8_t ymodem_receive(const YModem_ControlDef *);

#endif