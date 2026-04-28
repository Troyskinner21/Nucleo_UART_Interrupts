#include "ring_buffer.h"

void RingBuffer_Init(RingBuffer_t *rb, uint8_t *storage, uint16_t size)
{
    rb->buffer = storage;
    rb->size   = size;
    rb->head   = 0;
    rb->tail   = 0;
}

RingBuffer_Status RingBuffer_Write(RingBuffer_t *rb, uint8_t byte)
{
    uint16_t nextHead = (rb->head + 1) % rb->size;

    if (nextHead == rb->tail)
    {
        return RING_BUFFER_FULL; // buffer full — byte not written
    }

    rb->buffer[rb->head] = byte;
    rb->head = nextHead;
    return RING_BUFFER_OK;
}

RingBuffer_Status RingBuffer_Read(RingBuffer_t *rb, uint8_t *byte)
{
    if (rb->head == rb->tail)
    {
        return RING_BUFFER_EMPTY;
    }

    *byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return RING_BUFFER_OK;
}

uint16_t RingBuffer_Available(RingBuffer_t *rb)
{
    return (rb->head - rb->tail + rb->size) % rb->size;
}
