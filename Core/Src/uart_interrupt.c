/*
 * uart_interrupt.c
 *
 *  Created on: Apr 23, 2026
 *      Author: troys
 */

/* Single instance driver — supports one UART peripheral
 * For multiple UART instances use the handle based version
 */

#include "uart_interrupt.h"
#include "ring_buffer.h"
#include <stdio.h>

/* ── Internal state ──────────────────────────────────────────────────────────
 * Stored here so HAL callbacks can access them without extra parameters
 * All ISR-written variables are volatile to prevent compiler optimisation
 */
static UART_HandleTypeDef *s_huart = NULL;

/* TX complete flag — set to 1 by TxCpltCallback, cleared before each TX start */
static volatile uint8_t s_txComplete = 1;

/* Ring buffer that the RX ISR writes into and the main loop reads from */
static RingBuffer_t s_rxRingBuf;
static uint8_t      s_rxStorage[128];

/* Single-byte staging buffer — HAL_UART_Receive_IT writes one byte here,
 * the RxCpltCallback moves it into the ring buffer then re-arms               */
static uint8_t s_rxByte;

/* Static TX buffer used only by UART_IT_TransmitNumber
 * Safe because only one TX can be in flight at a time                         */
static char s_numBuf[16];

/* ─────────────────────────────────────────
 * Init
 * ───────────────────────────────────────── */
UART_IT_Status UART_IT_Init(UART_HandleTypeDef *huart, uint32_t baudrate)
{
    if (huart == NULL) return UART_IT_ERROR;

    s_huart = huart;

    huart->Init.BaudRate     = baudrate;
    huart->Init.WordLength   = UART_WORDLENGTH_8B;
    huart->Init.StopBits     = UART_STOPBITS_1;
    huart->Init.Parity       = UART_PARITY_NONE;
    huart->Init.Mode         = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(huart) != HAL_OK)
    {
        return UART_IT_ERROR;
    }

    /* Initialise ring buffer before arming the interrupt so the ISR has
     * a valid buffer to write into from the first received byte             */
    RingBuffer_Init(&s_rxRingBuf, s_rxStorage, sizeof(s_rxStorage));

    s_txComplete = 1;

    /* Arm first RX interrupt — callback re-arms automatically after each byte */
    if (HAL_UART_Receive_IT(huart, &s_rxByte, 1) != HAL_OK)
    {
        return UART_IT_ERROR;
    }

    return UART_IT_OK;
}

/* ─────────────────────────────────────────
 * HAL callbacks
 * ───────────────────────────────────────── */

/* Called by HAL when all TX bytes have shifted out of the shift register
 * Keep short — no heavy work inside ISR context                              */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == s_huart->Instance)
    {
        s_txComplete = 1;
    }
}

/* Called by HAL each time one RX byte is received
 * Push byte into ring buffer then immediately re-arm for the next byte
 * HAL_UART_Receive_IT fires only once — must re-arm here on every call       */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == s_huart->Instance)
    {
        RingBuffer_Write(&s_rxRingBuf, s_rxByte);

        /* Re-arm — if this fails the driver stops receiving; in production
         * add an error counter or flag to detect and recover                 */
        HAL_UART_Receive_IT(s_huart, &s_rxByte, 1);
    }
}

/* ─────────────────────────────────────────
 * TX
 * ───────────────────────────────────────── */

UART_IT_Status UART_IT_TransmitString(const char *str)
{
    if (str == NULL)      return UART_IT_ERROR;
    if (!s_txComplete)    return UART_IT_BUSY;

    uint16_t len = (uint16_t)strlen(str);
    if (len == 0)         return UART_IT_ERROR;

    s_txComplete = 0;

    /* Cast away const — HAL prototype takes uint8_t *, but does not modify
     * the buffer; the string content is safe                                 */
    if (HAL_UART_Transmit_IT(s_huart, (uint8_t *)str, len) != HAL_OK)
    {
        s_txComplete = 1;   /* reset so caller can retry */
        return UART_IT_ERROR;
    }

    return UART_IT_OK;
}

UART_IT_Status UART_IT_TransmitBuffer(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) return UART_IT_ERROR;
    if (!s_txComplete)           return UART_IT_BUSY;

    s_txComplete = 0;

    if (HAL_UART_Transmit_IT(s_huart, (uint8_t *)buf, len) != HAL_OK)
    {
        s_txComplete = 1;
        return UART_IT_ERROR;
    }

    return UART_IT_OK;
}

/* TransmitNumber uses a static internal buffer so the caller does not need
 * to keep a buffer alive — safe because only one TX is in flight at a time   */
UART_IT_Status UART_IT_TransmitNumber(int32_t number)
{
    if (!s_txComplete) return UART_IT_BUSY;

    int len = snprintf(s_numBuf, sizeof(s_numBuf), "%ld\r\n", number);
    if (len <= 0) return UART_IT_ERROR;

    s_txComplete = 0;

    if (HAL_UART_Transmit_IT(s_huart, (uint8_t *)s_numBuf, (uint16_t)len) != HAL_OK)
    {
        s_txComplete = 1;
        return UART_IT_ERROR;
    }

    return UART_IT_OK;
}

uint8_t UART_IT_IsTxComplete(void)
{
    return s_txComplete;
}

/* ─────────────────────────────────────────
 * RX
 * ───────────────────────────────────────── */

UART_IT_Status UART_IT_ReceiveByte(uint8_t *byte)
{
    if (byte == NULL) return UART_IT_ERROR;

    if (RingBuffer_Read(&s_rxRingBuf, byte) != RING_BUFFER_OK)
    {
        return UART_IT_EMPTY;
    }

    return UART_IT_OK;
}

UART_IT_Status UART_IT_ReceiveBuffer(uint8_t *buf, uint16_t len, uint16_t *read)
{
    if (buf == NULL || len == 0 || read == NULL) return UART_IT_ERROR;

    *read = 0;

    while (*read < len)
    {
        if (RingBuffer_Read(&s_rxRingBuf, &buf[*read]) != RING_BUFFER_OK)
        {
            break;  /* ring buffer empty — return what we have so far */
        }
        (*read)++;
    }

    return (*read > 0) ? UART_IT_OK : UART_IT_EMPTY;
}

/* Reads from ring buffer until terminator found, maxLen-1 bytes consumed, or
 * ring buffer is empty — then null terminates buf
 * Intended to be called repeatedly from the main loop; the caller tracks
 * the write offset between calls and passes the remaining slice of the buffer  */
UART_IT_Status UART_IT_ReceiveUntil(uint8_t *buf, uint16_t maxLen,
                                     uint8_t terminator, uint16_t *received)
{
    if (buf == NULL || maxLen == 0 || received == NULL) return UART_IT_ERROR;

    /* Do NOT reset *received — caller initialises it to 0 and maintains it
     * across calls so bytes accumulate in buf until the terminator arrives   */
    uint8_t byte = 0;

    while (*received < maxLen - 1)
    {
        if (RingBuffer_Read(&s_rxRingBuf, &byte) != RING_BUFFER_OK)
        {
            break;  /* no more data — terminator not yet seen */
        }

        buf[(*received)++] = byte;

        if (byte == terminator)
        {
            buf[*received] = '\0';
            return UART_IT_OK;  /* terminator found — complete line ready */
        }
    }

    buf[*received] = '\0';
    return UART_IT_EMPTY;   /* terminator not yet seen in available data */
}

uint16_t UART_IT_Available(void)
{
    return RingBuffer_Available(&s_rxRingBuf);
}


