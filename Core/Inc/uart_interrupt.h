/*
 * uart_interrupt.h
 *
 *  Created on: Apr 23, 2026
 *      Author: troys
 */

#ifndef INC_UART_INTERRUPT_H_
#define INC_UART_INTERRUPT_H_

#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <string.h>

/* Status codes */
typedef enum
{
    UART_IT_OK    = 0,
    UART_IT_ERROR = 1,
    UART_IT_BUSY  = 2,  /* TX still in flight — try again later           */
    UART_IT_EMPTY = 3   /* RX ring buffer has no data available            */
} UART_IT_Status;

/*
 * Init — configure peripheral, initialise rx ring buffer, arm first RX interrupt
 * Call once before any TX/RX functions
 */
UART_IT_Status UART_IT_Init(UART_HandleTypeDef *huart, uint32_t baudrate);

/* ── TX ──────────────────────────────────────────────────────────────────────
 * All TX functions are non-blocking — HAL_UART_Transmit_IT returns immediately
 * Returns UART_IT_BUSY if previous TX is still in progress
 *
 * IMPORTANT: HAL holds a pointer to the caller's buffer until TX complete
 *            The buffer must remain valid until UART_IT_IsTxComplete() returns 1
 *            Exception: TransmitNumber uses an internal static buffer
 */
UART_IT_Status UART_IT_TransmitString(const char *str);
UART_IT_Status UART_IT_TransmitBuffer(const uint8_t *buf, uint16_t len);
UART_IT_Status UART_IT_TransmitNumber(int32_t number);

/* Returns 1 if TX is idle and safe to start a new transmission, 0 if busy */
uint8_t UART_IT_IsTxComplete(void);

/* ── RX ──────────────────────────────────────────────────────────────────────
 * All RX functions read from the ring buffer filled by the ISR
 * Non-blocking — return UART_IT_EMPTY immediately if no data is available
 * Call from the main loop at whatever rate suits the application
 */

/* Read one byte from the ring buffer */
UART_IT_Status UART_IT_ReceiveByte(uint8_t *byte);

/*
 * Read up to len bytes from the ring buffer
 * Actual number of bytes read is written to *read (may be 0)
 */
UART_IT_Status UART_IT_ReceiveBuffer(uint8_t *buf, uint16_t len, uint16_t *read);

/*
 * Read from the ring buffer into buf until terminator byte is found or maxLen-1
 * bytes consumed — whichever comes first — then null-terminates buf
 * Returns UART_IT_OK     if terminator was found
 * Returns UART_IT_EMPTY  if ring buffer drained before terminator was seen
 * Designed to be called repeatedly from the main loop; the caller is responsible
 * for tracking how many bytes have accumulated across calls
 */
UART_IT_Status UART_IT_ReceiveUntil(uint8_t *buf, uint16_t maxLen,
                                     uint8_t terminator, uint16_t *received);

/* Number of bytes currently waiting in the ring buffer */
uint16_t UART_IT_Available(void);

#endif /* INC_UART_INTERRUPT_H_ */
