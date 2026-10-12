#ifndef MOSDESK_SETTINGS_H
#define MOSDESK_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "syscall.h"

#ifndef SYS_CREATE
#define SYS_CREATE 34u
#endif
#ifndef SYS_O_RDWR
#define SYS_O_RDWR 1u
#endif
#ifndef MOSDESK_SYSCALL
#define MOSDESK_SYSCALL mos_syscall
#endif

#define MOSDESK_SETTINGS_PATH "0:/programs/mosdesk/settings.dat"
#define MOSDESK_SETTINGS_MAGIC 0x4D445331u
#define MOSDESK_CURSOR_PACK_POSY 0u
#define MOSDESK_CURSOR_PACK_CLASSIC 1u

typedef struct {
    uint32_t magic;
    uint32_t mouse_speed;
    uint32_t accent_color;
    uint32_t cursor_fill;
    uint32_t cursor_scale;
    uint32_t cursor_pack;
} mosdesk_settings_t;

typedef struct {
    uint32_t magic;
    uint32_t mouse_speed;
    uint32_t accent_color;
    uint32_t cursor_fill;
    uint32_t cursor_scale;
} mosdesk_settings_legacy_t;

static inline mosdesk_settings_t mosdesk_settings_default(void) {
    return (mosdesk_settings_t){
        MOSDESK_SETTINGS_MAGIC, 100u, 0xFF3DAEE9u, 0xFFFFFFFFu, 1u,
        MOSDESK_CURSOR_PACK_POSY
    };
}

static inline mosdesk_settings_t mosdesk_settings_load(void) {
    mosdesk_settings_t settings = mosdesk_settings_default();
    long fd = (long)MOSDESK_SYSCALL(SYS_OPEN,
        (uintptr_t)MOSDESK_SETTINGS_PATH, SYS_O_RDONLY, 0);
    if (fd < 0) return settings;
    long size = (long)MOSDESK_SYSCALL(SYS_SIZE, (uintptr_t)fd, 0, 0);
    if (size == (long)sizeof(settings)) {
        if ((long)MOSDESK_SYSCALL(SYS_READ, (uintptr_t)fd,
                (uintptr_t)&settings, sizeof(settings)) !=
            (long)sizeof(settings))
            settings = mosdesk_settings_default();
    } else if (size == (long)sizeof(mosdesk_settings_legacy_t)) {
        mosdesk_settings_legacy_t legacy;
        if ((long)MOSDESK_SYSCALL(SYS_READ, (uintptr_t)fd,
                (uintptr_t)&legacy, sizeof(legacy)) ==
            (long)sizeof(legacy)) {
            settings = (mosdesk_settings_t){
                legacy.magic, legacy.mouse_speed, legacy.accent_color,
                legacy.cursor_fill, legacy.cursor_scale,
                MOSDESK_CURSOR_PACK_POSY
            };
        } else {
            settings = mosdesk_settings_default();
        }
    }
    MOSDESK_SYSCALL(SYS_CLOSE, (uintptr_t)fd, 0, 0);
    if (settings.magic != MOSDESK_SETTINGS_MAGIC ||
        settings.mouse_speed < 50u || settings.mouse_speed > 200u ||
        (settings.cursor_scale != 1u && settings.cursor_scale != 2u) ||
        settings.cursor_pack > MOSDESK_CURSOR_PACK_CLASSIC)
        return mosdesk_settings_default();
    settings.accent_color |= 0xFF000000u;
    settings.cursor_fill |= 0xFF000000u;
    return settings;
}

static inline bool mosdesk_settings_save(const mosdesk_settings_t* settings) {
    if (!settings) return false;
    long fd = (long)MOSDESK_SYSCALL(SYS_OPEN,
        (uintptr_t)MOSDESK_SETTINGS_PATH, SYS_O_RDWR, 0);
    if (fd < 0) {
        MOSDESK_SYSCALL(SYS_CREATE,
            (uintptr_t)MOSDESK_SETTINGS_PATH,
            (uintptr_t)"data", (uintptr_t)"bin");
        fd = (long)MOSDESK_SYSCALL(SYS_OPEN,
            (uintptr_t)MOSDESK_SETTINGS_PATH, SYS_O_RDWR, 0);
    }
    if (fd < 0) return false;
    MOSDESK_SYSCALL(SYS_SEEK, (uintptr_t)fd, 0, 0);
    long written = (long)MOSDESK_SYSCALL(SYS_FWRITE, (uintptr_t)fd,
        (uintptr_t)settings, sizeof(*settings));
    MOSDESK_SYSCALL(SYS_CLOSE, (uintptr_t)fd, 0, 0);
    return written == (long)sizeof(*settings);
}

#endif
