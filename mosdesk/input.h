#ifndef MOSDESK_INPUT_H
#define MOSDESK_INPUT_H

#include <stdint.h>

#include "syscall.h"

#define MOSDESK_INPUT_MOD_SHIFT (1u << 0u)
#define MOSDESK_USB_MOD_LSHIFT (1u << 1u)

static inline uint8_t mosdesk_usb_modifiers(uint8_t input_modifiers) {
    return (input_modifiers & MOSDESK_INPUT_MOD_SHIFT)
        ? MOSDESK_USB_MOD_LSHIFT : 0u;
}

#endif
