#ifndef UART_BULK_H
#define UART_BULK_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32g4xx_hal.h"

#define BULK_IMAGE_SIZE EEPROM_SIZE
#define BULK_FLUSH_CHUNK 512u

bool uartBulk_arm(uint16_t targetAddr, uint32_t length, uint32_t crc32);

// Returns true if data was consumed by the bulk receiver (do not pass to CLI)
bool uartBulk_on_rx(const uint8_t* data, uint16_t len);

// Poll from CLI task; returns true if it handled an event (so CLI should continue loop)
bool uartBulk_poll(void);

// Return true while a bulk transfer is actively receiving data
bool uartBulk_isReceiving(void);

// Returns true once a completed or failed transfer should be reported to the CLI task.
bool uartBulk_hasPendingEvent(void);

// Retrieve pointer to buffered image in RAM and its length after a successful receive.
// Returns NULL if no image is available.
const uint8_t* uartBulk_getImage(uint32_t* outLength);

// Commit the buffered image in RAM to EEPROM. Returns HAL_OK on success.
HAL_StatusTypeDef uartBulk_commit(void);

// Returns pointer to internal RAM buffer (valid until commit/reset)
const uint8_t* uartBulk_getImagePtr(void);

// Returns length of buffered image
uint32_t uartBulk_getImageLength(void);

// Get the target flash address associated with the buffered image. Returns 0xFFFF if unavailable.
uint16_t uartBulk_getTargetAddr(void);

#endif  // UART_BULK_H
