#include "uart_bulk.h"

#include <stdbool.h>
#include <stdint.h>

#include "debug.h"
#include "eeprom.h"

typedef enum {
    BULK_STATE_IDLE = 0,
    BULK_STATE_RECEIVING,
    BULK_STATE_READY_TO_PROGRAM,
    BULK_STATE_COMPLETE,
    BULK_STATE_ERROR,
} BulkState_t;

typedef struct {
    volatile BulkState_t state;
    uint16_t targetAddr;
    uint32_t expectedLength;
    uint32_t receivedLength;
    uint32_t expectedCrc32;
    uint32_t runningCrc32;
    uint8_t frameBuf[BULK_FLUSH_CHUNK];
    uint32_t frameIdx;
    uint8_t eventReported;
} BulkTransfer_t;

static BulkTransfer_t bulkTransfer;
static uint8_t bulkImage[BULK_IMAGE_SIZE];
static uint8_t bulkImageReady = 0;
static uint16_t bulkImageTargetAddr = 0xFFFFu;
static uint32_t bulkImageLength = 0;

static uint32_t bulkCrc32Update(uint32_t crc, uint8_t data) {
    crc ^= (uint32_t)data;
    for (int i = 0; i < 8; ++i) {
        if ((crc & 1u) != 0u) {
            crc = (crc >> 1) ^ 0xEDB88320u;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static void bulkReset(void) {
    bulkTransfer.state = BULK_STATE_IDLE;
    bulkTransfer.targetAddr = 0;
    bulkTransfer.expectedLength = 0;
    bulkTransfer.receivedLength = 0;
    bulkTransfer.expectedCrc32 = 0;
    bulkTransfer.runningCrc32 = 0xFFFFFFFFu;
    bulkTransfer.frameIdx = 0;
    bulkTransfer.eventReported = 0;
}

bool uartBulk_arm(uint16_t targetAddr, uint32_t length, uint32_t crc32) {
    if (bulkTransfer.state != BULK_STATE_IDLE) return false;
    if (length == 0 || length > BULK_IMAGE_SIZE) return false;

    bulkReset();
    bulkTransfer.targetAddr = targetAddr;
    bulkTransfer.expectedLength = length;
    bulkTransfer.expectedCrc32 = crc32;
    bulkTransfer.runningCrc32 = 0xFFFFFFFFu;
    bulkImageReady = 0;
    bulkImageTargetAddr = targetAddr;
    bulkImageLength = length;
    bulkTransfer.state = BULK_STATE_RECEIVING;
    /* clear image buffer area we'll use */
    for (uint32_t i = 0; i < length; ++i) {
        bulkImage[i] = 0xFFu;
    }
    return true;
}

bool uartBulk_on_rx(const uint8_t* data, uint16_t len) {
    if (bulkTransfer.state != BULK_STATE_RECEIVING) return false;

    for (uint16_t i = 0; i < len; ++i) {
        uint8_t byte = data[i];

        if (bulkTransfer.receivedLength >= bulkTransfer.expectedLength) {
            bulkTransfer.state = BULK_STATE_ERROR;
            return false;
        }

        bulkTransfer.frameBuf[bulkTransfer.frameIdx++] = byte;
        bulkTransfer.runningCrc32 = bulkCrc32Update(bulkTransfer.runningCrc32, byte);
        bulkTransfer.receivedLength++;

        if (bulkTransfer.frameIdx >= BULK_FLUSH_CHUNK || bulkTransfer.receivedLength == bulkTransfer.expectedLength) {
            uint32_t dest = (bulkTransfer.receivedLength - bulkTransfer.frameIdx);
            if ((dest + bulkTransfer.frameIdx) > bulkTransfer.expectedLength) {
                bulkTransfer.state = BULK_STATE_ERROR;
                return false;
            }
            /* copy received frame into RAM buffer, do NOT write to EEPROM here */
            for (uint32_t j = 0; j < bulkTransfer.frameIdx; ++j) {
                bulkImage[dest + j] = bulkTransfer.frameBuf[j];
            }
            bulkTransfer.frameIdx = 0;
        }
    }

    if (bulkTransfer.receivedLength == bulkTransfer.expectedLength) {
        uint32_t imageCrc = bulkTransfer.runningCrc32 ^ 0xFFFFFFFFu;
        if (imageCrc == bulkTransfer.expectedCrc32) {
            /* Image verified — keep it in RAM for later commit */
            bulkImageReady = 1;
            bulkImageTargetAddr = bulkTransfer.targetAddr;
            bulkImageLength = bulkTransfer.expectedLength;
            bulkTransfer.state = BULK_STATE_COMPLETE;
            bulkTransfer.eventReported = 0;
        } else {
            bulkImageReady = 0;
            bulkTransfer.state = BULK_STATE_ERROR;
        }
    }
    return true;
}

bool uartBulk_poll(void) {
    if (bulkTransfer.state == BULK_STATE_COMPLETE && bulkTransfer.eventReported == 0) {
        uprintf("Bulk RX complete and verified (CRC ok), stored RAM bytes: %lu\r\n", bulkTransfer.expectedLength);
        bulkTransfer.eventReported = 1;
        return true;
    }

    if (bulkTransfer.state == BULK_STATE_ERROR) {
        uprintf("Bulk transfer failed (overflow or CRC mismatch)\r\n");
        bulkImageReady = 0;
        bulkImageTargetAddr = 0xFFFFu;
        bulkImageLength = 0;
        bulkReset();
        return true;
    }

    return false;
}

bool uartBulk_isReceiving(void) {
    return bulkTransfer.state == BULK_STATE_RECEIVING;
}

bool uartBulk_hasPendingEvent(void) {
    return (bulkTransfer.state == BULK_STATE_COMPLETE || bulkTransfer.state == BULK_STATE_ERROR) &&
           bulkTransfer.eventReported == 0;
}

HAL_StatusTypeDef uartBulk_commit(void) {
    if (bulkTransfer.state != BULK_STATE_COMPLETE || bulkImageReady == 0) return HAL_ERROR;
    /* Program the buffered image into EEPROM now */
    HAL_StatusTypeDef res = eepromProgramBuffer(bulkTransfer.targetAddr, bulkTransfer.expectedLength, bulkImage);
    if (res == HAL_OK) {
        bulkReset();
    }
    return res;
}

const uint8_t* uartBulk_getImage(uint32_t* outLength) {
    if (bulkImageReady == 0) {
        if (outLength) *outLength = 0;
        return NULL;
    }
    if (outLength) *outLength = bulkImageLength;
    return bulkImage;
}

const uint8_t* uartBulk_getImagePtr(void) {
    if (bulkImageReady == 0) return NULL;
    return bulkImage;
}

uint32_t uartBulk_getImageLength(void) {
    if (bulkImageReady == 0) return 0;
    return bulkImageLength;
}

uint16_t uartBulk_getTargetAddr(void) {
    if (bulkImageReady == 0) return 0xFFFFu;
    return bulkImageTargetAddr;
}
