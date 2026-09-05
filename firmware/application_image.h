#ifndef APPLICATION_IMAGE_H
#define APPLICATION_IMAGE_H

#include <stdint.h>

/* The final 16 bytes of the application section are written last by the
   image stamper. The bootloader only starts an application whose manifest
   matches the bytes stored below it. */
#define APP_MANIFEST_ADDRESS 0x6ff0UL
#define APP_MANIFEST_SIZE 16U
#define APP_MANIFEST_FORMAT 1U
#define APP_PROTOCOL_MAJOR 1U
#define APP_PROTOCOL_MINOR 0U

typedef struct __attribute__((packed)) {
    uint8_t magic[4];       /* "P2FW" */
    uint8_t format;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint8_t reserved;
    uint32_t image_length;
    uint32_t image_crc32;
} application_manifest_t;

#endif
