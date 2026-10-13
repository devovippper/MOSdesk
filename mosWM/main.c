#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics.h"
#include "string.h"
#include "window.h"
#include "file_icons.h"
#include "wm_text.h"
#define MOSDESK_SYSCALL mos_syscall3
#include "../mosdesk/settings.h"
#include "../mosdesk/password_hash.h"
#include "../mosdesk/input.h"

#ifndef SYS_EXEC
#define SYS_EXEC 33u
#endif

#define SYS_HEAP_ALLOC 1u
#define SYS_HEAP_FREE 2u
#define MAX_WINDOWS SYSCALL_WINDOW_MAX_WINDOWS
#define TASKBAR_HEIGHT 48u
#define TITLE_HEIGHT 28u
#define CORNER_RADIUS 9u
#define SNAP_DISTANCE 24
#define CURSOR_MAX_SIZE 32u
#define DRAG_NONE 0u
#define DRAG_MOVE 1u
#define DRAG_RESIZE 2u
#define CURSOR_MAX_FRAMES 80u
#define CURSOR_MAX_FILE_SIZE (8u * 1024u * 1024u)
#define MAX_DESKTOP_APPS 32u
#define DESKTOP_APP_PATH_SIZE 192u
#define DESKTOP_APP_NAME_SIZE 48u
#define DESKTOP_DIR_ENTRIES 128u
#define DESKTOP_ICON_SIZE SYSCALL_WINDOW_ICON_SIZE
#define DESKTOP_ICON_PIXELS SYSCALL_WINDOW_ICON_PIXELS
#define RUN_ICON_MAX_SIZE (1024u * 1024u)
#define MODE_NORMAL 0u
#define MODE_MAXIMIZED 1u
#define MODE_FULLSCREEN 2u
#define MODE_MINIMIZED 3u
#define AUTH_NONE 0u
#define AUTH_FIRST_CHOICE 1u
#define AUTH_FIRST_PASSWORD 2u
#define AUTH_LOGIN 3u
#define AUTH_PASSWORD_MAX 63u

#ifndef SYSCALL_CURSOR_COUNT
/* This SDK does not expose per-window cursor state; keep the shell cursor local. */
#define SYSCALL_CURSOR_COUNT 14u
#define SYSCALL_CURSOR_ARROW 0u
#define SYSCALL_CURSOR_MOVE 7u
#define SYSCALL_CURSOR_HAND 4u
#define SYSCALL_CURSOR_RESIZE_NWSE 11u
#define SYSCALL_CURSOR_PACK_PATH_SIZE 128u
typedef struct {
    uint32_t status;
    char pack[SYSCALL_CURSOR_PACK_PATH_SIZE];
} moswm_cursor_state_t;
#else
typedef syscall_cursor_state_t moswm_cursor_state_t;
#endif

typedef struct {
    uint32_t id;
    uint32_t frame_version;
    uint32_t width;
    uint32_t height;
    uint32_t* pixels;
    uint32_t last_mode;
    uint32_t saved_x;
    uint32_t saved_y;
    uint32_t saved_width;
    uint32_t saved_height;
    bool icon_checked;
    bool has_icon;
    uint32_t icon_pixels[SYSCALL_WINDOW_ICON_PIXELS];
} cached_window_t;

typedef struct {
    char path[DESKTOP_APP_PATH_SIZE];
    char name[DESKTOP_APP_NAME_SIZE];
    bool has_icon;
    uint32_t icon_pixels[DESKTOP_ICON_PIXELS];
} desktop_app_t;

static syscall_graphics_info_t display;
static uint32_t* canvas;
static cached_window_t cache[MAX_WINDOWS];
static desktop_app_t desktop_apps[MAX_DESKTOP_APPS];
static uint32_t desktop_app_count;
static int32_t cursor_x;
static int32_t cursor_y;
static int32_t mouse_remainder_x;
static int32_t mouse_remainder_y;
static uint32_t focused_id;
static uint32_t cursor_pixels[CURSOR_MAX_SIZE * CURSOR_MAX_SIZE];
static uint32_t* cursor_animation_pixels;
static uint32_t cursor_animation_frame_count;
static uint32_t cursor_animation_frame_delay_ms;
static uint64_t cursor_animation_elapsed_ms;
static uint32_t cursor_width;
static uint32_t cursor_height;
static uint32_t cursor_hotspot_x;
static uint32_t cursor_hotspot_y;
static uint32_t cached_cursor_status = UINT32_MAX;
static char cached_cursor_pack[SYSCALL_CURSOR_PACK_PATH_SIZE];
static uint32_t cursor_fill_color = 0xFFFFFFFFu;
static uint32_t cursor_scale = 1u;
static uint32_t cursor_pack = MOSDESK_CURSOR_PACK_POSY;
static uint32_t mouse_speed = 100u;
static uint32_t accent_color = 0xFF3DAEE9u;
static uint32_t wallpaper_style;
static uint32_t painted_wallpaper_style = UINT32_MAX;
static uint32_t auth_state;
static mosdesk_settings_t desktop_settings;
static char auth_password[AUTH_PASSWORD_MAX + 1u];
static uint32_t auth_password_length;
static char auth_message[64];
static uint32_t dragging_id;
static uint32_t drag_mode;
static uint32_t drag_start_x;
static uint32_t drag_start_y;
static uint32_t drag_start_width;
static uint32_t drag_start_height;
static uint32_t drag_start_window_x;
static uint32_t drag_start_window_y;
static uint8_t previous_buttons;
static bool start_menu_open;
static char status_message[64];
static bool screen_owned;
static uint32_t settings_poll_frames;
static uint32_t icon_poll_frames;

static uint8_t* wm_read_file(const char* path, size_t max_size, size_t* size);
static void paint_wallpaper(uint32_t* target);
static void log_message(const char* message);

static void* wm_malloc(size_t size) {
    uint64_t result = mos_syscall3(SYS_HEAP, SYS_HEAP_ALLOC, 0, size);
    int64_t signed_result = (int64_t)result;
    if (signed_result < 0 && signed_result >= -16) return NULL;
    return (void*)(uintptr_t)result;
}

static void wm_free(void* pointer) {
    if (pointer)
        mos_syscall3(SYS_HEAP, SYS_HEAP_FREE,
                     (uint64_t)(uintptr_t)pointer, 0);
}

static void load_desktop_settings(void) {
    mosdesk_settings_t settings = mosdesk_settings_load();
    desktop_settings = settings;
    mouse_speed = settings.mouse_speed;
    accent_color = settings.accent_color;
    cursor_fill_color = settings.cursor_fill;
    cursor_scale = settings.cursor_scale;
    cursor_pack = settings.cursor_pack;
    wallpaper_style = settings.wallpaper;
}

static uint16_t read_u16le(const uint8_t* bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u);
}

static uint32_t read_u32le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static bool decode_cursor(const uint8_t* data, size_t size,
                          uint32_t* pixels, uint32_t* width,
                          uint32_t* height, uint32_t* hotspot_x,
                          uint32_t* hotspot_y) {
    if (size < 22u || read_u16le(data) != 0u ||
        read_u16le(data + 2u) != 2u)
        return false;
    uint16_t count = read_u16le(data + 4u);
    if (!count || count > (size - 6u) / 16u) return false;

    uint32_t best_offset = 0;
    uint32_t best_size = 0;
    uint32_t best_width = 0;
    uint32_t best_height = 0;
    uint32_t best_hotspot_x = 0;
    uint32_t best_hotspot_y = 0;
    uint32_t best_distance = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        size_t entry_offset = 6u + (size_t)i * 16u;
        uint32_t image_width = data[entry_offset]
            ? data[entry_offset] : 256u;
        uint32_t image_height = data[entry_offset + 1u]
            ? data[entry_offset + 1u] : 256u;
        uint32_t resource_size = read_u32le(data + entry_offset + 8u);
        uint32_t image_offset = read_u32le(data + entry_offset + 12u);
        if (!image_width || !image_height ||
            image_width > CURSOR_MAX_SIZE ||
            image_height > CURSOR_MAX_SIZE ||
            image_offset > size || resource_size > size - image_offset ||
            resource_size < 40u)
            continue;
        uint32_t distance = CURSOR_MAX_SIZE - image_width +
                            CURSOR_MAX_SIZE - image_height;
        if (distance < best_distance) {
            best_distance = distance;
            best_offset = image_offset;
            best_size = resource_size;
            best_width = image_width;
            best_height = image_height;
            best_hotspot_x = read_u16le(data + entry_offset + 4u);
            best_hotspot_y = read_u16le(data + entry_offset + 6u);
        }
    }
    if (!best_offset) return false;

    const uint8_t* dib = data + best_offset;
    uint32_t header_size = read_u32le(dib);
    int32_t dib_width = (int32_t)read_u32le(dib + 4u);
    int32_t dib_height = (int32_t)read_u32le(dib + 8u);
    uint16_t bits_per_pixel = read_u16le(dib + 14u);
    uint32_t compression = read_u32le(dib + 16u);
    size_t pixel_bytes = (size_t)best_width * best_height * 4u;
    if (header_size < 40u || dib_width != (int32_t)best_width ||
        dib_height != (int32_t)(best_height * 2u) ||
        bits_per_pixel != 32u || compression != 0u ||
        header_size > best_size || pixel_bytes > best_size - header_size)
        return false;

    memset(pixels, 0, CURSOR_MAX_SIZE * CURSOR_MAX_SIZE * sizeof(uint32_t));
    const uint8_t* source = dib + header_size;
    for (uint32_t y = 0; y < best_height; y++) {
        uint32_t source_y = best_height - 1u - y;
        for (uint32_t x = 0; x < best_width; x++) {
            size_t source_index =
                ((size_t)source_y * best_width + x) * 4u;
            uint8_t blue = source[source_index];
            uint8_t green = source[source_index + 1u];
            uint8_t red = source[source_index + 2u];
            uint8_t alpha = source[source_index + 3u];
            pixels[(size_t)y * CURSOR_MAX_SIZE + x] =
                ((uint32_t)alpha << 24u) | ((uint32_t)red << 16u) |
                ((uint32_t)green << 8u) | blue;
        }
    }
    *width = best_width;
    *height = best_height;
    *hotspot_x = best_hotspot_x < best_width ? best_hotspot_x : 0u;
    *hotspot_y = best_hotspot_y < best_height ? best_hotspot_y : 0u;
    return true;
}

static bool decode_animated_cursor(const uint8_t* data, size_t size) {
    if (size < 20u || memcmp(data, "RIFF", 4u) ||
        memcmp(data + 8u, "ACON", 4u))
        return false;
    uint32_t riff_size = read_u32le(data + 4u);
    if (riff_size > size - 8u) return false;
    size_t riff_end = 8u + riff_size;
    uint32_t declared_frames = 0;
    uint32_t frame_delay = 6u;
    size_t frames_start = 0;
    size_t frames_end = 0;
    for (size_t offset = 12u; offset + 8u <= riff_end;) {
        const uint8_t* chunk = data + offset;
        uint32_t chunk_size = read_u32le(chunk + 4u);
        size_t chunk_data = offset + 8u;
        if (chunk_size > riff_end - chunk_data) return false;
        if (!memcmp(chunk, "anih", 4u)) {
            if (chunk_size < 36u) return false;
            declared_frames = read_u32le(data + chunk_data + 4u);
            uint32_t rate = read_u32le(data + chunk_data + 28u);
            if (rate)
                frame_delay = rate > UINT32_MAX / 10u
                    ? UINT32_MAX : rate * 10u;
        } else if (!memcmp(chunk, "LIST", 4u) && chunk_size >= 4u &&
                   !memcmp(data + chunk_data, "fram", 4u)) {
            frames_start = chunk_data + 4u;
            frames_end = chunk_data + chunk_size;
        }
        offset = chunk_data + chunk_size + (chunk_size & 1u);
    }
    if (!declared_frames || declared_frames > CURSOR_MAX_FRAMES ||
        !frames_start || frames_end < frames_start)
        return false;
    uint32_t* frames = wm_malloc((size_t)declared_frames *
        CURSOR_MAX_SIZE * CURSOR_MAX_SIZE * sizeof(uint32_t));
    if (!frames) return false;
    memset(frames, 0, (size_t)declared_frames * CURSOR_MAX_SIZE *
           CURSOR_MAX_SIZE * sizeof(uint32_t));

    uint32_t loaded_frames = 0;
    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    uint32_t hotspot_x = 0;
    uint32_t hotspot_y = 0;
    for (size_t offset = frames_start; offset + 8u <= frames_end;) {
        const uint8_t* chunk = data + offset;
        uint32_t chunk_size = read_u32le(chunk + 4u);
        size_t chunk_data = offset + 8u;
        if (chunk_size > frames_end - chunk_data) {
            wm_free(frames);
            return false;
        }
        if (!memcmp(chunk, "icon", 4u)) {
            if (loaded_frames >= declared_frames) {
                wm_free(frames);
                return false;
            }
            uint32_t width, height, hot_x, hot_y;
            uint32_t* target = frames + (size_t)loaded_frames *
                CURSOR_MAX_SIZE * CURSOR_MAX_SIZE;
            if (!decode_cursor(data + chunk_data, chunk_size, target,
                    &width, &height, &hot_x, &hot_y) ||
                (loaded_frames && (width != frame_width ||
                                   height != frame_height ||
                                   hot_x != hotspot_x || hot_y != hotspot_y))) {
                wm_free(frames);
                return false;
            }
            frame_width = width;
            frame_height = height;
            hotspot_x = hot_x;
            hotspot_y = hot_y;
            loaded_frames++;
        }
        offset = chunk_data + chunk_size + (chunk_size & 1u);
    }
    if (loaded_frames != declared_frames) {
        wm_free(frames);
        return false;
    }
    cursor_animation_pixels = frames;
    cursor_animation_frame_count = loaded_frames;
    cursor_animation_frame_delay_ms = frame_delay ? frame_delay : 60u;
    cursor_width = frame_width;
    cursor_height = frame_height;
    cursor_hotspot_x = hotspot_x;
    cursor_hotspot_y = hotspot_y;
    cursor_animation_elapsed_ms = 0;
    return true;
}

static bool path_has_suffix(const char* path, const char* suffix) {
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    return path_length >= suffix_length &&
        !strcmp(path + path_length - suffix_length, suffix);
}

static bool load_cursor_resource(const char* path) {
    wm_free(cursor_animation_pixels);
    cursor_animation_pixels = NULL;
    cursor_animation_frame_count = 0;
    cursor_width = 0;
    cursor_height = 0;
    cursor_hotspot_x = 0;
    cursor_hotspot_y = 0;
    size_t size = 0;
    uint8_t* data = wm_read_file(path, CURSOR_MAX_FILE_SIZE, &size);
    if (!data) return false;
    bool loaded;
    if (path_has_suffix(path, ".ani")) {
        loaded = decode_animated_cursor(data, size);
    } else {
        loaded = decode_cursor(data, size, cursor_pixels, &cursor_width,
            &cursor_height, &cursor_hotspot_x, &cursor_hotspot_y);
    }
    wm_free(data);
    return loaded;
}

static bool join_cursor_path(char* output, size_t capacity,
                             const char* pack_path, const char* filename) {
    size_t used = 0;
    while (pack_path[used] && used + 1u < capacity) {
        output[used] = pack_path[used];
        used++;
    }
    if (pack_path[used]) return false;
    if (!used || output[used - 1u] != '/') {
        if (used + 1u >= capacity) return false;
        output[used++] = '/';
    }
    for (size_t i = 0; filename[i]; i++) {
        if (used + 1u >= capacity) return false;
        output[used++] = filename[i];
    }
    output[used] = '\0';
    return true;
}

static const char* cursor_filename(uint32_t status) {
    static const char* const filenames[SYSCALL_CURSOR_COUNT] = {
        "arrow.cur", "busy.ani", "working.ani", "text.cur",
        "hand.cur", "help.cur", "forbidden.cur", "move.cur",
        "resize-ew.cur", "resize-nesw.cur", "resize-ns.cur",
        "resize-nwse.cur", "precise.cur", "pen.cur"
    };
    return status < SYSCALL_CURSOR_COUNT ? filenames[status] : filenames[0];
}

static bool try_cursor_from_pack(const char* pack_path, uint32_t status) {
    char path[SYSCALL_CURSOR_PACK_PATH_SIZE + 32u];
    if (join_cursor_path(path, sizeof(path), pack_path,
                         cursor_filename(status)) &&
        load_cursor_resource(path))
        return true;
    if (status && join_cursor_path(path, sizeof(path), pack_path,
                                   "arrow.cur") &&
        load_cursor_resource(path))
        return true;
    return false;
}

static const char* default_cursor_pack(void) {
    return cursor_pack == MOSDESK_CURSOR_PACK_CLASSIC
        ? "0:/programs/mosWM/assets/cursors/Classic"
        : "0:/programs/mosWM/assets/cursors/Posy";
}

static void ensure_cursor_loaded(const moswm_cursor_state_t* cursor_state,
                                 uint32_t status) {
    const char* system_pack = default_cursor_pack();
    const char* requested_pack = cursor_state && cursor_state->pack[0]
        ? cursor_state->pack : system_pack;
    if (status >= SYSCALL_CURSOR_COUNT) status = SYSCALL_CURSOR_ARROW;
    if (cached_cursor_status == status &&
        !strcmp(cached_cursor_pack, requested_pack))
        return;

    bool loaded = try_cursor_from_pack(requested_pack, status);
    if (!loaded && strcmp(requested_pack, system_pack)) {
        strcpy(status_message, "Cursor pack could not be loaded");
        loaded = try_cursor_from_pack(system_pack, status);
    }
    if (!loaded && status != SYSCALL_CURSOR_ARROW)
        loaded = try_cursor_from_pack(system_pack, SYSCALL_CURSOR_ARROW);
    if (!loaded) {
        cursor_width = 0;
        cursor_height = 0;
        strcpy(status_message, "Cursor resource could not be loaded");
    }
    strcpy(cached_cursor_pack, requested_pack);
    cached_cursor_status = status;
}

/* Reads a whole file into a wm_malloc'd buffer (caller wm_free()s it). */
static uint8_t* wm_read_file(const char* path, size_t max_size, size_t* size) {
    long fd = (long)mos_syscall3(SYS_OPEN, (uint64_t)(uintptr_t)path,
                                 SYS_O_RDONLY, 0);
    if (fd < 0) return NULL;
    long file_size = (long)mos_syscall3(SYS_SIZE, (uint64_t)fd, 0, 0);
    if (file_size <= 0 || (size_t)file_size > max_size) {
        mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return NULL;
    }
    uint8_t* data = (uint8_t*)wm_malloc((size_t)file_size);
    if (!data) {
        mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return NULL;
    }
    size_t total = 0;
    while (total < (size_t)file_size) {
        long got = (long)mos_syscall3(SYS_READ, (uint64_t)fd,
            (uint64_t)(uintptr_t)(data + total),
            (uint64_t)((size_t)file_size - total));
        if (got <= 0) break;
        total += (size_t)got;
    }
    mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (total != (size_t)file_size) {
        wm_free(data);
        return NULL;
    }
    *size = total;
    return data;
}

static bool wm_read_exact(uint64_t fd, void* output, size_t size) {
    size_t total = 0;
    while (total < size) {
        long got = (long)mos_syscall3(SYS_READ, fd,
            (uint64_t)(uintptr_t)((uint8_t*)output + total), size - total);
        if (got <= 0) return false;
        total += (size_t)got;
    }
    return true;
}

static bool decode_dib_icon(const uint8_t* dib, size_t size,
                            size_t pixel_offset, bool icon_file,
                            uint32_t* output) {
    if (!dib || !output || size < 40u) return false;
    uint32_t header_size = read_u32le(dib);
    int32_t width = (int32_t)read_u32le(dib + 4u);
    int32_t dib_height = (int32_t)read_u32le(dib + 8u);
    uint16_t bits = read_u16le(dib + 14u);
    uint32_t compression = read_u32le(dib + 16u);
    if (header_size < 40u || header_size > size || width <= 0 ||
        width > 256 || dib_height == 0 || dib_height == INT32_MIN ||
        (bits != 24u && bits != 32u) || compression != 0u ||
        read_u16le(dib + 12u) != 1u)
        return false;
    int32_t height = dib_height < 0 ? -dib_height : dib_height;
    if (icon_file) {
        if (dib_height <= 0 || (height & 1)) return false;
        height /= 2;
    }
    if (height <= 0 || height > 256) return false;
    uint32_t row_bytes = ((uint32_t)width * bits + 31u) / 32u * 4u;
    size_t pixel_bytes = (size_t)row_bytes * (uint32_t)height;
    if (pixel_offset < header_size || pixel_offset > size ||
        pixel_bytes > size - pixel_offset)
        return false;

    size_t mask_offset = pixel_offset + pixel_bytes;
    uint32_t mask_stride = ((uint32_t)width + 31u) / 32u * 4u;
    size_t mask_bytes = (size_t)mask_stride * (uint32_t)height;
    bool has_mask = icon_file && mask_offset <= size &&
                    mask_bytes <= size - mask_offset;
    bool has_alpha = false;
    if (bits == 32u) {
        for (int32_t y = 0; y < height && !has_alpha; y++) {
            const uint8_t* row = dib + pixel_offset +
                                 (size_t)y * row_bytes;
            for (int32_t x = 0; x < width; x++) {
                if (row[(size_t)x * 4u + 3u]) {
                    has_alpha = true;
                    break;
                }
            }
        }
    }

    memset(output, 0, DESKTOP_ICON_PIXELS * sizeof(uint32_t));
    uint32_t target_width = (uint32_t)width;
    uint32_t target_height = (uint32_t)height;
    if (target_width > target_height)
        target_height = target_height * DESKTOP_ICON_SIZE / target_width;
    else
        target_width = target_width * DESKTOP_ICON_SIZE / target_height;
    if (!target_width) target_width = 1u;
    if (!target_height) target_height = 1u;
    uint32_t offset_x = (DESKTOP_ICON_SIZE - target_width) / 2u;
    uint32_t offset_y = (DESKTOP_ICON_SIZE - target_height) / 2u;

    for (uint32_t y = 0; y < target_height; y++) {
        uint32_t source_y = y * (uint32_t)height / target_height;
        uint32_t source_row = dib_height < 0 && !icon_file
            ? source_y : (uint32_t)height - 1u - source_y;
        const uint8_t* row =
            dib + pixel_offset + (size_t)source_row * row_bytes;
        for (uint32_t x = 0; x < target_width; x++) {
            uint32_t source_x = x * (uint32_t)width / target_width;
            const uint8_t* pixel = row +
                (size_t)source_x * (bits / 8u);
            uint8_t alpha = bits == 32u && has_alpha ? pixel[3] : 255u;
            if (has_mask) {
                const uint8_t* mask_row = dib + mask_offset +
                    (size_t)((uint32_t)height - 1u - source_y) * mask_stride;
                if (mask_row[source_x / 8u] & (0x80u >> (source_x & 7u)))
                    alpha = 0u;
            }
            output[(size_t)(offset_y + y) * DESKTOP_ICON_SIZE +
                   offset_x + x] =
                (uint32_t)alpha << 24u |
                (uint32_t)pixel[2] << 16u |
                (uint32_t)pixel[1] << 8u | pixel[0];
        }
    }
    return true;
}

static bool decode_bmp_icon(const uint8_t* data, size_t size,
                            uint32_t* output) {
    if (!data || size < 54u || data[0] != 'B' || data[1] != 'M')
        return false;
    uint32_t pixel_offset = read_u32le(data + 10u);
    uint32_t header_size = read_u32le(data + 14u);
    if (header_size < 40u || pixel_offset < 14u + header_size ||
        pixel_offset > size)
        return false;
    return decode_dib_icon(data + 14u, size - 14u,
                           pixel_offset - 14u, false, output);
}

static bool decode_ico_icon(const uint8_t* data, size_t size,
                            uint32_t* output) {
    if (!data || size < 22u || read_u16le(data) != 0u ||
        read_u16le(data + 2u) != 1u)
        return false;
    uint16_t count = read_u16le(data + 4u);
    if (!count || count > (size - 6u) / 16u) return false;
    uint32_t best_index = UINT32_MAX;
    uint32_t best_score = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        size_t entry = 6u + (size_t)i * 16u;
        uint32_t width = data[entry] ? data[entry] : 256u;
        uint32_t height = data[entry + 1u] ? data[entry + 1u] : 256u;
        uint32_t image_size = read_u32le(data + entry + 8u);
        uint32_t image_offset = read_u32le(data + entry + 12u);
        if (!image_size || image_offset > size ||
            image_size > size - image_offset)
            continue;
        uint32_t distance = width > DESKTOP_ICON_SIZE
            ? width - DESKTOP_ICON_SIZE : DESKTOP_ICON_SIZE - width;
        distance += height > DESKTOP_ICON_SIZE
            ? height - DESKTOP_ICON_SIZE : DESKTOP_ICON_SIZE - height;
        if (distance < best_score) {
            best_index = i;
            best_score = distance;
        }
    }
    if (best_index == UINT32_MAX) return false;
    size_t entry = 6u + (size_t)best_index * 16u;
    uint32_t image_size = read_u32le(data + entry + 8u);
    uint32_t image_offset = read_u32le(data + entry + 12u);
    const uint8_t* dib = data + image_offset;
    uint32_t header_size = read_u32le(dib);
    if (header_size < 40u || header_size > image_size) return false;
    return decode_dib_icon(dib, image_size, header_size, true, output);
}

static bool decode_icon(const uint8_t* data, size_t size, bool ico,
                        uint32_t* output) {
    return ico ? decode_ico_icon(data, size, output)
               : decode_bmp_icon(data, size, output);
}

static bool read_bundle_icon(const char* path, uint32_t* output) {
    long fd = (long)mos_syscall3(SYS_OPEN, (uint64_t)(uintptr_t)path,
                                 SYS_O_RDONLY, 0);
    if (fd < 0) return false;
    long file_size = (long)mos_syscall3(SYS_SIZE, (uint64_t)fd, 0, 0);
    uint8_t header[16];
    if (file_size < (long)sizeof(header) || file_size > UINT32_MAX ||
        !wm_read_exact((uint64_t)fd, header, sizeof(header)) ||
        memcmp(header, "MINIRUN1", 8u) ||
        read_u32le(header + 8u) != 1u) {
        mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return false;
    }
    uint32_t count = read_u32le(header + 12u);
    if (!count || count > 512u ||
        count > ((uint32_t)file_size - sizeof(header)) / 72u) {
        mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return false;
    }
    size_t table_size = (size_t)count * 72u;
    uint8_t* table = (uint8_t*)wm_malloc(table_size);
    if (!table ||
        mos_syscall3(SYS_SEEK, (uint64_t)fd, sizeof(header), 0) !=
            SYS_SUCCESS ||
        !wm_read_exact((uint64_t)fd, table, table_size)) {
        wm_free(table);
        mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return false;
    }

    static const char* const icon_names[] = {
        "icon.bmp", "icon.ico", "Resources/icon.bmp", "Resources/icon.ico"
    };
    bool loaded = false;
    for (uint32_t priority = 0; priority < 4u && !loaded; priority++) {
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* entry = table + (size_t)i * 72u;
            if (strncmp((const char*)entry, icon_names[priority], 64u))
                continue;
            uint32_t offset = read_u32le(entry + 64u);
            uint32_t size = read_u32le(entry + 68u);
            uint32_t table_end = 16u + count * 72u;
            if (offset < table_end || offset > (uint32_t)file_size ||
                size < 4u || size > RUN_ICON_MAX_SIZE ||
                size > (uint32_t)file_size - offset)
                continue;
            uint8_t* image = (uint8_t*)wm_malloc(size);
            if (!image) continue;
            if (mos_syscall3(SYS_SEEK, (uint64_t)fd, offset, 0) ==
                    SYS_SUCCESS &&
                wm_read_exact((uint64_t)fd, image, size))
                loaded = decode_icon(image, size, priority == 1u ||
                    priority == 3u, output);
            wm_free(image);
            if (loaded) break;
        }
    }
    wm_free(table);
    mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
    return loaded;
}

static bool path_join(char* output, size_t capacity,
                      const char* directory, const char* name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    if (!directory_length || !name_length ||
        directory_length + name_length + 2u > capacity)
        return false;
    memcpy(output, directory, directory_length);
    output[directory_length] = '/';
    memcpy(output + directory_length + 1u, name, name_length + 1u);
    return true;
}

static bool is_run_name(const char* name) {
    size_t length = strlen(name);
    return length > 4u && !strcmp(name + length - 4u, ".run");
}

static void add_desktop_app(const char* path, const char* run_name) {
    if (desktop_app_count >= MAX_DESKTOP_APPS ||
        !is_run_name(run_name) || !strcmp(run_name, "mosWM.run"))
        return;
    desktop_app_t* app = &desktop_apps[desktop_app_count];
    size_t path_length = strlen(path);
    size_t name_length = strlen(run_name) - 4u;
    if (path_length >= sizeof(app->path) ||
        !name_length || name_length >= sizeof(app->name))
        return;
    memcpy(app->path, path, path_length + 1u);
    memcpy(app->name, run_name, name_length);
    app->name[name_length] = '\0';
    app->has_icon = read_bundle_icon(app->path, app->icon_pixels);
    desktop_app_count++;
}

static void load_desktop_apps(void) {
    syscall_dirent_t entries[DESKTOP_DIR_ENTRIES];
    const char* programs = "0:/programs";
    uint64_t count = mos_syscall3(SYS_LISTDIR,
        (uint64_t)(uintptr_t)programs, (uint64_t)(uintptr_t)entries,
        DESKTOP_DIR_ENTRIES);
    if ((int64_t)count < 0) {
        log_message("mosWM: unable to list 0:/programs\n");
        return;
    }
    if (count > DESKTOP_DIR_ENTRIES) count = DESKTOP_DIR_ENTRIES;
    for (uint64_t i = 0; i < count &&
         desktop_app_count < MAX_DESKTOP_APPS; i++) {
        if (entries[i].hidden) continue;
        char path[DESKTOP_APP_PATH_SIZE];
        if (entries[i].type == SYSCALL_DIRENT_TYPE_FILE ||
            entries[i].type == SYSCALL_DIRENT_TYPE_EXECUTABLE) {
            if (is_run_name(entries[i].name) &&
                path_join(path, sizeof(path), programs, entries[i].name))
                add_desktop_app(path, entries[i].name);
            continue;
        }
        if (entries[i].type != SYSCALL_DIRENT_TYPE_DIR ||
            !path_join(path, sizeof(path), programs, entries[i].name))
            continue;
        syscall_dirent_t children[DESKTOP_DIR_ENTRIES];
        uint64_t child_count = mos_syscall3(SYS_LISTDIR,
            (uint64_t)(uintptr_t)path,
            (uint64_t)(uintptr_t)children, DESKTOP_DIR_ENTRIES);
        if ((int64_t)child_count < 0) continue;
        if (child_count > DESKTOP_DIR_ENTRIES)
            child_count = DESKTOP_DIR_ENTRIES;
        for (uint64_t child = 0; child < child_count &&
             desktop_app_count < MAX_DESKTOP_APPS; child++) {
            if (children[child].hidden ||
                (children[child].type != SYSCALL_DIRENT_TYPE_FILE &&
                 children[child].type != SYSCALL_DIRENT_TYPE_EXECUTABLE) ||
                !is_run_name(children[child].name))
                continue;
            char run_path[DESKTOP_APP_PATH_SIZE];
            if (path_join(run_path, sizeof(run_path), path,
                          children[child].name))
                add_desktop_app(run_path, children[child].name);
        }
    }
}

static uint8_t* font_regular_data;
static uint8_t* font_medium_data;

/* Smooth OpenType text; silently stays on the bitmap font if the files are
 * missing or unusable. */
static void load_fonts(void) {
    size_t regular_size = 0, medium_size = 0;
    font_regular_data = wm_read_file(
        "0:/programs/mosWM/assets/fonts/Inter-Regular.otf",
        2u * 1024u * 1024u, &regular_size);
    font_medium_data = wm_read_file(
        "0:/programs/mosWM/assets/fonts/Inter-Medium.otf",
        2u * 1024u * 1024u, &medium_size);
    if (!moswm_text_load_fonts(font_regular_data, regular_size,
                               font_medium_data, medium_size))
        log_message("mosWM: no outline fonts, using the bitmap font\n");
}

static void log_message(const char* message) {
    uint32_t length = 0;
    while (message[length]) length++;
    mos_write(message, length);
}

static uint32_t clamp_u32(uint32_t value, uint32_t low, uint32_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static uint32_t alpha_blend(uint32_t source, uint32_t destination) {
    uint32_t alpha = source >> 24;
    if (alpha == 255u) return source;
    if (!alpha) return destination;
    uint32_t inverse = 255u - alpha;
    uint32_t red = (((source >> 16) & 255u) * alpha +
                    ((destination >> 16) & 255u) * inverse) / 255u;
    uint32_t green = (((source >> 8) & 255u) * alpha +
                      ((destination >> 8) & 255u) * inverse) / 255u;
    uint32_t blue = ((source & 255u) * alpha +
                     (destination & 255u) * inverse) / 255u;
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

static void put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= display.width ||
        (uint32_t)y >= display.height)
        return;
    uint32_t* pixel = canvas + (size_t)y * display.width + (uint32_t)x;
    *pixel = alpha_blend(color, *pixel);
}

static void fill_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                      uint32_t color) {
    int64_t right = (int64_t)x + width;
    int64_t bottom = (int64_t)y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > display.width) right = display.width;
    if (bottom > display.height) bottom = display.height;
    for (int32_t py = y; py < bottom; py++) {
        for (int32_t px = x; px < right; px++) put_pixel(px, py, color);
    }
}

static void draw_wallpaper_line(int32_t x0, int32_t y0, int32_t x1,
                                int32_t y1, uint32_t color) {
    int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void wm_text(int32_t x, int32_t y, const char* text, uint32_t color,
                    uint32_t shadow, uint32_t scale) {
    moswm_draw_text(canvas, display.width, display.height, x, y, text, color,
                    shadow, scale);
}

static void stroke_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                        uint32_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

/* Copies up to `max_chars` of `src` into `dst`, ending in ".." if cut. */
static void fit_text(char* dst, size_t dst_size, const char* src,
                     uint32_t max_chars) {
    uint32_t len = 0;
    while (src[len]) len++;
    if (max_chars + 1u > dst_size) max_chars = (uint32_t)dst_size - 1u;
    if (len <= max_chars) {
        for (uint32_t i = 0; i <= len; i++) dst[i] = src[i];
        return;
    }
    if (max_chars < 3u) {
        dst[0] = '\0';
        return;
    }
    for (uint32_t i = 0; i < max_chars - 2u; i++) dst[i] = src[i];
    dst[max_chars - 2u] = '.';
    dst[max_chars - 1u] = '.';
    dst[max_chars] = '\0';
}

/* Like fit_text, but cuts to `max_px` pixels of rendered width (proportional
 * smooth text is much narrower than the 9 px/char bitmap font). */
static void fit_text_px(char* dst, size_t dst_size, const char* src,
                        int32_t max_px, uint32_t scale) {
    size_t len = 0;
    while (src[len] && len + 1u < dst_size) {
        dst[len] = src[len];
        len++;
    }
    dst[len] = '\0';
    if (max_px <= 0) {
        dst[0] = '\0';
        return;
    }
    if ((int32_t)moswm_text_width(dst, scale) <= max_px && !src[len]) return;
    while (len > 0) {
        /* never cut inside a UTF-8 sequence */
        size_t cut = len - 1u;
        while (cut > 0 && ((uint8_t)dst[cut] & 0xC0u) == 0x80u) cut--;
        len = cut;
        dst[len] = '\0';
        if (len + 3u < dst_size) {
            dst[len] = '.';
            dst[len + 1u] = '.';
            dst[len + 2u] = '\0';
            if ((int32_t)moswm_text_width(dst, scale) <= max_px) return;
            dst[len] = '\0';
        }
    }
}

static uint32_t isqrt_u64(uint64_t value) {
    uint64_t result = 0, bit = 1ull << 62;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

/* Each pixel is painted exactly once, so translucent colours stay even. */
static void fill_rounded_rect(int32_t x, int32_t y, int32_t width,
                              int32_t height, uint32_t radius,
                              uint32_t color) {
    if (width <= 0 || height <= 0) return;
    if (radius * 2u > (uint32_t)width) radius = (uint32_t)width / 2u;
    if (radius * 2u > (uint32_t)height) radius = (uint32_t)height / 2u;
    if (!radius) {
        fill_rect(x, y, width, height, color);
        return;
    }
    int32_t r = (int32_t)radius;
    for (int32_t row = 0; row < height; row++) {
        int32_t inset = 0;
        int32_t from_edge = row < r ? row : (row >= height - r ? height - 1 - row : r);
        if (from_edge < r) {
            int64_t dy2 = 2 * (int64_t)r - 2 * (int64_t)from_edge - 1;
            int64_t span2 = 4 * (int64_t)r * r - dy2 * dy2;
            inset = r - (int32_t)((isqrt_u64((uint64_t)span2) + 1u) / 2u);
            if (inset < 0) inset = 0;
        }
        fill_rect(x + inset, y + row, width - inset * 2, 1, color);
    }
}

static uint32_t* wallpaper;

static uint32_t lerp_channel(uint32_t from, uint32_t to, uint32_t t) {
    return (from * (255u - t) + to * t) / 255u;
}

/* Anti-aliased thick line with round caps, written as max-coverage into an
 * 8-bit mask so crossing strokes never double-darken where they meet.
 * Coordinates are in mask space; half16 is half the stroke width in 1/16 px. */
static void mask_thick_line(uint8_t* mask, int32_t mw, int32_t mh,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t half16) {
    int64_t dx = x1 - x0, dy = y1 - y0;
    int64_t len2 = dx * dx + dy * dy;
    if (!len2) return;
    int32_t pad = half16 / 16 + 2;
    int32_t min_x = (x0 < x1 ? x0 : x1) - pad, max_x = (x0 > x1 ? x0 : x1) + pad;
    int32_t min_y = (y0 < y1 ? y0 : y1) - pad, max_y = (y0 > y1 ? y0 : y1) + pad;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= mw) max_x = mw - 1;
    if (max_y >= mh) max_y = mh - 1;
    int64_t reach = (int64_t)(half16 / 16 + 2);
    for (int32_t y = min_y; y <= max_y; y++) {
        for (int32_t x = min_x; x <= max_x; x++) {
            int64_t px = x - x0, py = y - y0;
            int64_t t = px * dx + py * dy, d2;
            if (t <= 0) {
                d2 = px * px + py * py;
            } else if (t >= len2) {
                int64_t qx = x - x1, qy = y - y1;
                d2 = qx * qx + qy * qy;
            } else {
                int64_t cross = px * dy - py * dx;
                d2 = cross * cross / len2;
            }
            if (d2 > reach * reach) continue;
            int32_t distance16 = (int32_t)isqrt_u64((uint64_t)d2 * 256u);
            int32_t coverage = half16 + 8 - distance16;
            if (coverage <= 0) continue;
            if (coverage > 16) coverage = 16;
            uint8_t value = (uint8_t)(coverage * 255 / 16);
            uint8_t* cell = mask + (size_t)y * (uint32_t)mw + (uint32_t)x;
            if (value > *cell) *cell = value;
        }
    }
}

/* The Minimal-OS hourglass: top bar, bottom bar, one long diagonal, and two
 * short diagonals that stop just before the centre. Units follow the
 * 370x353 logo artwork so every stroke meets its neighbours exactly. */
static void draw_logo_mask(uint8_t* mask, int32_t mw, int32_t mh,
                           int32_t pad, int32_t scale_num, int32_t scale_den,
                           int32_t half16) {
#define LX(v) (pad + (v) * scale_num / scale_den)
    mask_thick_line(mask, mw, mh, LX(0), LX(0), LX(370), LX(0), half16);
    mask_thick_line(mask, mw, mh, LX(0), LX(353), LX(370), LX(353), half16);
    mask_thick_line(mask, mw, mh, LX(370), LX(0), LX(0), LX(353), half16);
    mask_thick_line(mask, mw, mh, LX(0), LX(0), LX(167), LX(155), half16);
    mask_thick_line(mask, mw, mh, LX(370), LX(353), LX(203), LX(197), half16);
#undef LX
}

static void paint_wallpaper(uint32_t* target) {
    uint32_t* saved = canvas;
    canvas = target;
    uint32_t w = display.width, h = display.height;
    static const uint8_t colors[3][6] = {
        {9u, 20u, 38u, 31u, 104u, 158u},
        {7u, 27u, 31u, 20u, 111u, 105u},
        {35u, 19u, 12u, 139u, 79u, 39u}
    };
    const uint8_t* palette = colors[wallpaper_style <= 2u
        ? wallpaper_style : 0u];

    /* Diagonal glass gradients with Plasma-style angled light bands. */
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t t = (x * 255u / w + y * 255u / h) / 2u;
            uint32_t red, green, blue;
            if (t < 140u) {
                uint32_t k = t * 255u / 140u;
                red = lerp_channel(palette[0], palette[3] * 3u / 5u, k);
                green = lerp_channel(palette[1], palette[4] / 2u, k);
                blue = lerp_channel(palette[2], palette[5] / 2u, k);
            } else {
                uint32_t k = (t - 140u) * 255u / 115u;
                red = lerp_channel(palette[3] * 3u / 5u, palette[3], k);
                green = lerp_channel(palette[4] / 2u, palette[4], k);
                blue = lerp_channel(palette[5] / 2u, palette[5], k);
            }
            uint32_t nx = x * 1000u / w, ny = y * 1000u / h;
            uint32_t band = 2u * nx + ny;
            uint32_t lift = 0;
            if (band > 1350u && band < 2150u && ny > 140u) lift += 7;
            if (band > 1800u && band < 2600u) lift += 6;
            if (band > 2250u && band < 2750u && ny < 900u) lift += 5;
            red += lift; green += lift * 2u; blue += lift * 2u;
            if (red > 255u) red = 255u;
            if (green > 255u) green = 255u;
            if (blue > 255u) blue = 255u;
            target[(size_t)y * w + x] =
                0xFF000000u | red << 16 | green << 8 | blue;
        }
    }

    /* Hourglass logo, right of centre, built from one connected stroke set. */
    int32_t logo_h = (int32_t)(h * 46u / 100u);
    int32_t scale_num = logo_h, scale_den = 353;
    int32_t logo_w = 370 * scale_num / scale_den;
    int32_t pad = 12;
    int32_t mw = logo_w + pad * 2, mh = logo_h + pad * 2;
    int32_t half16 = (int32_t)(14 * scale_num * 8 / scale_den);
    if (half16 < 24) half16 = 24;
    uint8_t* mask = wm_malloc((size_t)mw * (size_t)mh);
    if (mask) {
        memset(mask, 0, (size_t)mw * (size_t)mh);
        draw_logo_mask(mask, mw, mh, pad, scale_num, scale_den, half16);
        int32_t ox = (int32_t)(w * 74u / 100u) - mw / 2;
        int32_t oy = (int32_t)(h * 47u / 100u) - mh / 2;
        for (int32_t y = 0; y < mh; y++) {
            for (int32_t x = 0; x < mw; x++) {
                uint32_t cover = mask[(size_t)y * (uint32_t)mw + (uint32_t)x];
                if (!cover) continue;
                put_pixel(ox + x, oy + y,
                          ((cover * 0x34u / 255u) << 24) | 0x00DCEBF7u);
            }
        }
        wm_free(mask);
    }
    canvas = saved;
}

static void draw_wallpaper(void) {
    if (painted_wallpaper_style != wallpaper_style) {
        paint_wallpaper(wallpaper ? wallpaper : canvas);
        painted_wallpaper_style = wallpaper_style;
    }
    if (wallpaper)
        memcpy(canvas, wallpaper,
               (size_t)display.width * display.height * sizeof(uint32_t));
}

static void draw_custom_icon(const uint32_t* icon, int32_t x, int32_t y,
                             uint32_t size) {
    if (!icon || !size) return;
    for (uint32_t dy = 0; dy < size; dy++) {
        uint32_t source_y = dy * DESKTOP_ICON_SIZE / size;
        for (uint32_t dx = 0; dx < size; dx++) {
            uint32_t source_x = dx * DESKTOP_ICON_SIZE / size;
            uint32_t color = icon[source_y * DESKTOP_ICON_SIZE + source_x];
            if (color >> 24u)
                put_pixel(x + (int32_t)dx, y + (int32_t)dy, color);
        }
    }
}

static void desktop_app_position(uint32_t index, int32_t* x, int32_t* y) {
    uint32_t columns = display.width > 112u
        ? (display.width - 112u) / 88u : 1u;
    uint32_t rows = display.height > TASKBAR_HEIGHT + 24u
        ? (display.height - TASKBAR_HEIGHT - 24u) / 98u : 1u;
    if (!columns) columns = 1u;
    if (!rows) rows = 1u;
    uint32_t column = index / rows;
    uint32_t row = index % rows;
    *x = 112 + (int32_t)column * 88;
    *y = 14 + (int32_t)row * 98;
}

static bool desktop_app_at(int32_t px, int32_t py, uint32_t* app_index) {
    for (uint32_t i = 0; i < desktop_app_count; i++) {
        int32_t x, y;
        desktop_app_position(i, &x, &y);
        if (px >= x && px < x + (int32_t)DESKTOP_ICON_SIZE &&
            py >= y && py < y + 90) {
            if (app_index) *app_index = i;
            return true;
        }
    }
    return false;
}

static void draw_desktop_icons(void) {
    static const struct {
        moswm_file_icon_t icon;
        const char* label;
    } items[] = {
        {MOSWM_ICON_HOME, "Home"},
        {MOSWM_ICON_COMPUTER, "System"}
    };
    for (uint32_t i = 0; i < sizeof(items) / sizeof(items[0]); i++)
    {
        int32_t icon_y = 14 + (int32_t)i * 98;
        moswm_draw_file_icon_sized(canvas, display.width, display.height,
                                   24, icon_y, items[i].icon, 64u);
        int32_t label_x = 56 - (int32_t)moswm_text_width(items[i].label, 2) / 2;
        if (label_x < 4) label_x = 4;
        wm_text(label_x, icon_y + 68, items[i].label, 0xFFF2F6F8u,
                0x99000000u, 2);
    }
    for (uint32_t i = 0; i < desktop_app_count; i++) {
        int32_t icon_x, icon_y;
        desktop_app_position(i, &icon_x, &icon_y);
        if (desktop_apps[i].has_icon)
            draw_custom_icon(desktop_apps[i].icon_pixels,
                             icon_x, icon_y, DESKTOP_ICON_SIZE);
        else
            moswm_draw_file_icon_sized(canvas, display.width, display.height,
                icon_x, icon_y, MOSWM_ICON_CODE, DESKTOP_ICON_SIZE);
        char label[DESKTOP_APP_NAME_SIZE];
        fit_text_px(label, sizeof(label), desktop_apps[i].name, 84u, 1u);
        int32_t label_x = icon_x +
            ((int32_t)DESKTOP_ICON_SIZE -
             (int32_t)moswm_text_width(label, 1u)) / 2;
        wm_text(label_x, icon_y + 68, label,
                0xFFF2F6F8u, 0x99000000u, 1);
    }
}

static cached_window_t* cache_for(uint32_t id) {
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (cache[i].id == id) return &cache[i];
    }
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (!cache[i].id) {
            cache[i].id = id;
            return &cache[i];
        }
    }
    return NULL;
}

static void remove_missing_cache(const syscall_window_info_t* windows,
                                 uint64_t count) {
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        cached_window_t* entry = &cache[i];
        if (!entry->id) continue;
        bool found = false;
        for (uint64_t j = 0; j < count; j++) {
            if (windows[j].id == entry->id) {
                found = true;
                break;
            }
        }
        if (!found) {
            wm_free(entry->pixels);
            memset(entry, 0, sizeof(*entry));
        }
    }
}

static bool refresh_frame(cached_window_t* entry,
                          const syscall_window_info_t* info) {
    if (entry->width != info->width || entry->height != info->height) {
        if (info->width > SIZE_MAX / info->height / sizeof(uint32_t))
            return false;
        size_t bytes = (size_t)info->width * info->height * sizeof(uint32_t);
        uint32_t* pixels = wm_malloc(bytes);
        if (!pixels) return false;
        memset(pixels, 0, bytes);
        wm_free(entry->pixels);
        entry->pixels = pixels;
        entry->width = info->width;
        entry->height = info->height;
        entry->frame_version = UINT32_MAX;
    }
    if (entry->frame_version == info->frame_version) return true;
    syscall_window_frame_t frame = {
        .width = info->width,
        .height = info->height,
        .pitch = info->width * sizeof(uint32_t),
        .pixels = entry->pixels
    };
    if (mos_window_get_frame(info->id, &frame) != SYS_SUCCESS) return false;
    entry->frame_version = info->frame_version;
    return true;
}

static void refresh_window_icon(cached_window_t* entry, bool force) {
    if (!entry || (entry->icon_checked && !force)) return;
    uint64_t result = mos_window_get_icon(entry->id, entry->icon_pixels);
    if (result == SYS_SUCCESS) {
        entry->has_icon = true;
    } else if (result == SYS_ERR_NOTFOUND) {
        entry->has_icon = false;
    } else {
        strcpy(status_message, "Unable to read application icon");
        entry->has_icon = false;
    }
    entry->icon_checked = true;
}

static uint32_t window_mode(const syscall_window_info_t* info) {
    if (info->flags & SYSCALL_WINDOW_MINIMIZED) return MODE_MINIMIZED;
    if (info->flags & SYSCALL_WINDOW_FULLSCREEN) return MODE_FULLSCREEN;
    if (info->flags & SYSCALL_WINDOW_MAXIMIZED) return MODE_MAXIMIZED;
    return MODE_NORMAL;
}

static void save_bounds(cached_window_t* entry,
                        const syscall_window_info_t* info) {
    entry->saved_x = info->x;
    entry->saved_y = info->y;
    entry->saved_width = info->width;
    entry->saved_height = info->height;
}

static uint32_t mode_event(uint32_t mode) {
    if (mode == MODE_MINIMIZED) return SYSCALL_WINDOW_EVENT_MINIMIZED;
    if (mode == MODE_MAXIMIZED) return SYSCALL_WINDOW_EVENT_MAXIMIZED;
    if (mode == MODE_NORMAL) return SYSCALL_WINDOW_EVENT_RESTORED;
    return 0;
}

static void post_simple_event(uint32_t id, uint32_t type) {
    syscall_window_event_t event = {.type = type};
    uint64_t result = mos_window_post_event(id, &event);
    if (result == SYS_ERR_BUSY)
        strcpy(status_message, "Window event queue is full");
}

static bool set_window_mode(const syscall_window_info_t* info,
                            cached_window_t* entry, uint32_t mode) {
    if (!info || !entry) return false;
    uint32_t previous_mode = entry->last_mode;
    uint32_t flags = info->flags & ~(SYSCALL_WINDOW_MINIMIZED |
                                     SYSCALL_WINDOW_MAXIMIZED |
                                     SYSCALL_WINDOW_FULLSCREEN);
    if (mode == MODE_MINIMIZED) flags |= SYSCALL_WINDOW_MINIMIZED;
    else if (mode == MODE_MAXIMIZED) flags |= SYSCALL_WINDOW_MAXIMIZED;
    else if (mode == MODE_FULLSCREEN) flags |= SYSCALL_WINDOW_FULLSCREEN;

    if (mode != MODE_NORMAL && previous_mode == MODE_NORMAL)
        save_bounds(entry, info);
    if (mos_window_set_flags(info->id, flags) != SYS_SUCCESS) return false;
    entry->last_mode = mode;

    if (mode == MODE_MAXIMIZED || mode == MODE_FULLSCREEN) {
        uint32_t work_height = mode == MODE_FULLSCREEN
            ? display.height : display.height - TASKBAR_HEIGHT;
        if (display.width <= 2u || work_height <= TITLE_HEIGHT + 2u)
            return false;
        return mos_window_set_bounds(info->id, 0, 0, display.width - 2u,
                                     work_height - TITLE_HEIGHT - 2u) ==
               SYS_SUCCESS;
    }
    if (mode == MODE_NORMAL && previous_mode != MODE_NORMAL &&
        entry->saved_width && entry->saved_height) {
        bool restored = mos_window_set_bounds(info->id, entry->saved_x,
            entry->saved_y, entry->saved_width, entry->saved_height) ==
            SYS_SUCCESS;
        entry->saved_width = 0;
        entry->saved_height = 0;
        return restored;
    }
    return true;
}

static void sync_window_modes(syscall_window_info_t* windows, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        syscall_window_info_t* info = &windows[i];
        cached_window_t* entry = cache_for(info->id);
        if (!entry) continue;
        uint32_t requested = window_mode(info);
        if (requested == entry->last_mode) continue;
        uint32_t event = mode_event(requested);
        if (!set_window_mode(info, entry, requested)) {
            strcpy(status_message, "Window state update failed");
            continue;
        }
        if (event) post_simple_event(info->id, event);
    }
}

static void draw_chrome_text(const syscall_window_info_t* info,
                             const cached_window_t* entry);

static void composite_window(const syscall_window_info_t* info,
                             cached_window_t* entry) {
    int32_t x = (int32_t)info->x;
    int32_t y = (int32_t)info->y;
    int32_t outer_width = (int32_t)info->width + 2;
    int32_t outer_height = (int32_t)info->height + (int32_t)TITLE_HEIGHT + 2;
    if (x >= (int32_t)display.width || y >= (int32_t)display.height ||
        x + outer_width <= 0 || y + outer_height <= 0)
        return;

    bool window_focused = info->id == focused_id;
    fill_rounded_rect(x - 4, y - 1, outer_width + 8, outer_height + 10,
                      CORNER_RADIUS + 4u, 0x16000000u);
    fill_rounded_rect(x - 1, y + 2, outer_width + 2, outer_height + 4,
                      CORNER_RADIUS + 1u, window_focused ? 0x40000000u
                                                         : 0x24000000u);
    fill_rounded_rect(x, y, outer_width, outer_height,
                      CORNER_RADIUS, info->style.border);
    fill_rounded_rect(x + 1, y + 1, outer_width - 2, outer_height - 2,
                      CORNER_RADIUS - 1u, info->style.content_background);
    fill_rect(x + 1, y + (int32_t)TITLE_HEIGHT, outer_width - 2, 2,
              info->style.title_bar);
    fill_rounded_rect(x + 1, y + 1, outer_width - 2,
                      (int32_t)TITLE_HEIGHT, CORNER_RADIUS - 1u,
                      info->style.title_bar);
    fill_rect(x + 1,
              y + 1 + (int32_t)TITLE_HEIGHT -
                  (int32_t)(CORNER_RADIUS - 1u),
              outer_width - 2, (int32_t)(CORNER_RADIUS - 1u),
              info->style.title_bar);
    fill_rect(x + 1, y + (int32_t)TITLE_HEIGHT - 1, outer_width - 2, 1,
              0x30000000u);
    fill_rect(x + (int32_t)CORNER_RADIUS, y + 1,
              outer_width - (int32_t)CORNER_RADIUS * 2, 1, 0x1CFFFFFFu);

    if (entry && entry->pixels) {
        int32_t content_x = x + 1;
        int32_t content_y = y + (int32_t)TITLE_HEIGHT + 1;
        for (uint32_t py = 0; py < info->height; py++) {
            int32_t dy = content_y + (int32_t)py;
            if (dy < 0 || (uint32_t)dy >= display.height) continue;
            for (uint32_t px = 0; px < info->width; px++) {
                int32_t dx = content_x + (int32_t)px;
                if (dx < 0 || (uint32_t)dx >= display.width) continue;
                if (py >= info->height - CORNER_RADIUS &&
                    (px < CORNER_RADIUS ||
                     px + CORNER_RADIUS >= info->width)) {
                    int32_t corner_x = px < CORNER_RADIUS
                        ? (int32_t)CORNER_RADIUS - 1
                        : (int32_t)info->width - (int32_t)CORNER_RADIUS;
                    int32_t corner_y =
                        (int32_t)info->height - (int32_t)CORNER_RADIUS;
                    int32_t dxr = (int32_t)px - corner_x;
                    int32_t dyr = (int32_t)py - corner_y;
                    if (dxr * dxr + dyr * dyr >
                        (int32_t)(CORNER_RADIUS * CORNER_RADIUS))
                        continue;
                }
                put_pixel(dx, dy,
                    entry->pixels[(size_t)py * info->width + px]);
            }
        }
    }
    draw_chrome_text(info, entry);
}

static uint32_t opaque(uint32_t color) {
    return 0xFF000000u | (color & 0x00FFFFFFu);
}

static void draw_chrome_text(const syscall_window_info_t* info,
                             const cached_window_t* entry) {
    int32_t x = (int32_t)info->x;
    int32_t y = (int32_t)info->y;
    int32_t outer_width = (int32_t)info->width + 2;
    int32_t right = x + (int32_t)info->width + 1;
    int32_t by = y + 5;
    int32_t close_x = right - 24;
    int32_t max_x = right - 48;
    int32_t min_x = right - 72;
    bool focused = info->id == focused_id;

    /* Breeze-style centred title, dimmed when the window is not focused. */
    char title[SYSCALL_WINDOW_MAX_TITLE];
    int32_t title_left = x + (entry && entry->has_icon ? 32 : 12);
    int32_t title_right = min_x - 10;
    int32_t room = title_right - title_left;
    fit_text_px(title, sizeof(title), info->title, room, 2);
    int32_t text_width = (int32_t)moswm_text_width(title, 2);
    int32_t text_x = title_left + (room - text_width) / 2;
    if (entry && entry->has_icon)
        draw_custom_icon(entry->icon_pixels, x + 7, y + 4, 20u);
    uint32_t title_color = (info->style.title_text & 0x00FFFFFFu) |
                           (focused ? 0xFF000000u : 0xA0000000u);
    wm_text(text_x, y + ((int32_t)TITLE_HEIGHT - 16) / 2, title,
            title_color, 0, 2);

    /* Round window buttons with crisp glyphs. */
    fill_rounded_rect(close_x, by, 18, 18, 9, opaque(info->style.close_button));
    draw_wallpaper_line(close_x + 6, by + 6, close_x + 11, by + 11,
                        opaque(info->style.close_icon));
    draw_wallpaper_line(close_x + 7, by + 6, close_x + 12, by + 11,
                        opaque(info->style.close_icon));
    draw_wallpaper_line(close_x + 11, by + 6, close_x + 6, by + 11,
                        opaque(info->style.close_icon));
    draw_wallpaper_line(close_x + 12, by + 6, close_x + 7, by + 11,
                        opaque(info->style.close_icon));
    fill_rounded_rect(max_x, by, 18, 18, 9,
                      opaque(info->style.maximize_button));
    stroke_rect(max_x + 5, by + 5, 8, 8, opaque(info->style.maximize_icon));
    fill_rect(max_x + 5, by + 5, 8, 2, opaque(info->style.maximize_icon));
    fill_rounded_rect(min_x, by, 18, 18, 9,
                      opaque(info->style.minimize_button));
    fill_rect(min_x + 5, by + 11, 8, 2, opaque(info->style.minimize_icon));
}

static void draw_cursor(void) {
    if (!cursor_width || !cursor_height) {
        static const char* fallback[] = {
            "#...........", "##..........", "#x#.........",
            "#xx#........", "#xxx#.......", "#xxxx#......",
            "#xxxxx#.....", "#xxxxxx#....", "#xxxxxxx#...",
            "#xxxxxxxx#..", "#xxxxx###...", "#xx#xx#......"
        };
        cursor_width = 12;
        cursor_height = 12;
        memset(cursor_pixels, 0, sizeof(cursor_pixels));
        for (uint32_t y = 0; y < cursor_height; y++) {
            for (uint32_t x = 0; x < cursor_width; x++) {
                char pixel = fallback[y][x];
                if (pixel == '#')
                    cursor_pixels[y * CURSOR_MAX_SIZE + x] = 0xFF10191Du;
                else if (pixel == 'x')
                    cursor_pixels[y * CURSOR_MAX_SIZE + x] = 0xFFFFFFFFu;
            }
        }
    }
    const uint32_t* pixels = cursor_animation_pixels
        ? cursor_animation_pixels +
            (size_t)((cursor_animation_elapsed_ms /
                      cursor_animation_frame_delay_ms) %
                     cursor_animation_frame_count) *
                CURSOR_MAX_SIZE * CURSOR_MAX_SIZE
        : cursor_pixels;
    int32_t origin_x = cursor_x - (int32_t)(cursor_hotspot_x * cursor_scale);
    int32_t origin_y = cursor_y - (int32_t)(cursor_hotspot_y * cursor_scale);
    for (uint32_t y = 0; y < cursor_height; y++) {
        for (uint32_t x = 0; x < cursor_width; x++) {
            uint32_t color = pixels[y * CURSOR_MAX_SIZE + x];
            if (!(color >> 24u)) continue;
            if (((color >> 16u) & 255u) > 210u &&
                ((color >> 8u) & 255u) > 210u && (color & 255u) > 210u)
                color = (color & 0xFF000000u) | (cursor_fill_color & 0x00FFFFFFu);
            for (uint32_t sy = 0; sy < cursor_scale; sy++) {
                for (uint32_t sx = 0; sx < cursor_scale; sx++) {
                    put_pixel(origin_x + (int32_t)(x * cursor_scale + sx),
                              origin_y + (int32_t)(y * cursor_scale + sy), color);
                }
            }
        }
    }
}

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} moswm_datetime_t;

static moswm_datetime_t clock_time;
static bool clock_valid;
static uint32_t clock_frames = 30u;

static void update_clock(void) {
    if (++clock_frames < 30u) return;
    clock_frames = 0;
    moswm_datetime_t now;
    memset(&now, 0, sizeof(now));
    clock_valid = mos_syscall1(SYS_GETTIME, (uint64_t)(uintptr_t)&now) ==
                  SYS_SUCCESS && now.month >= 1 && now.month <= 12;
    if (clock_valid) clock_time = now;
}

static bool pointer_in(int32_t x, int32_t y, int32_t w, int32_t h) {
    return cursor_x >= x && cursor_x < x + w && cursor_y >= y &&
           cursor_y < y + h;
}

/* The Minimal-OS hourglass at panel size: same geometry as the wallpaper. */
static void draw_mini_logo(int32_t x, int32_t y, uint32_t color) {
    for (int32_t i = 0; i < 2; i++) {
        draw_wallpaper_line(x, y + i, x + 24, y + i, color);
        draw_wallpaper_line(x, y + 24 - i, x + 24, y + 24 - i, color);
        draw_wallpaper_line(x + 24 - i, y, x - i, y + 24, color);
        draw_wallpaper_line(x + i, y, x + 11 + i, y + 11, color);
        draw_wallpaper_line(x + 24 - i, y + 24, x + 13 - i, y + 13, color);
    }
}

static void draw_taskbar(const syscall_window_info_t* windows,
                         uint64_t count) {
    static const char* const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int32_t top = (int32_t)display.height - (int32_t)TASKBAR_HEIGHT;
    int32_t width = (int32_t)display.width;
    uint32_t accent = 0xFF000000u | (accent_color & 0x00FFFFFFu);
    update_clock();

    /* Panel: dark, slightly translucent, with a faint highlight edge. */
    fill_rect(0, top, width, (int32_t)TASKBAR_HEIGHT, 0xEC1B1E20u);
    fill_rect(0, top, width, 1, 0x2CFFFFFFu);

    /* Application launcher button. */
    bool menu_hover = pointer_in(0, top, 44, (int32_t)TASKBAR_HEIGHT);
    if (start_menu_open || menu_hover)
        fill_rounded_rect(4, top + 4, 40, 40, 8,
                          start_menu_open ? (0x66000000u | (accent & 0xFFFFFFu))
                                          : 0x26FFFFFFu);
    draw_mini_logo(12, top + 12, 0xFFEAF2F8u);
    if (start_menu_open) fill_rect(8, top + 45, 32, 2, accent);

    /* Task manager: one rounded entry per window, Plasma-style indicator. */
    int32_t x = 60;
    for (uint64_t i = 0; i < count && (uint32_t)x + 184u < display.width - 126u;
         i++) {
        const syscall_window_info_t* info = &windows[i];
        bool focused = info->id == focused_id;
        bool minimized = (info->flags & SYSCALL_WINDOW_MINIMIZED) != 0;
        bool hovered = pointer_in(x, top + 5, 176, 38);
        if (focused)
            fill_rounded_rect(x, top + 5, 176, 38, 6, 0x38FFFFFFu);
        else if (hovered)
            fill_rounded_rect(x, top + 5, 176, 38, 6, 0x24FFFFFFu);
        if (focused)
            fill_rect(x + 6, top + 44, 164, 3, accent);
        else
            fill_rect(x + 78, top + 45, 20, 2,
                      minimized ? 0x50FFFFFFu : 0xA0FFFFFFu);
        char label[SYSCALL_WINDOW_MAX_TITLE];
        cached_window_t* entry = cache_for(info->id);
        bool has_icon = entry && entry->has_icon;
        fit_text_px(label, sizeof(label), info->title,
                    has_icon ? 126u : 156u, 2);
        if (has_icon)
            draw_custom_icon(entry->icon_pixels, x + 8, top + 12, 24u);
        wm_text(x + (has_icon ? 40 : 10), top + 16, label,
                minimized ? 0xFF8C9BA6u : 0xFFEAF2F8u, 0, 2);
        x += 180;
    }
    if (!count)
        wm_text(60, top + 16, "MOSDESK", 0x80EAF2F8u, 0, 2);

    /* System tray: separator + two-line clock. */
    if (clock_valid) {
        char hhmm[6] = { (char)('0' + clock_time.hour / 10u),
                         (char)('0' + clock_time.hour % 10u), ':',
                         (char)('0' + clock_time.minute / 10u),
                         (char)('0' + clock_time.minute % 10u), '\0' };
        char date[8];
        uint32_t di = 0;
        if (clock_time.day >= 10u)
            date[di++] = (char)('0' + clock_time.day / 10u);
        date[di++] = (char)('0' + clock_time.day % 10u);
        date[di++] = ' ';
        for (uint32_t k = 0; k < 3u; k++)
            date[di++] = months[clock_time.month - 1u][k];
        date[di] = '\0';
        int32_t center = width - 62;
        fill_rect(width - 124, top + 10, 1, 28, 0x24FFFFFFu);
        wm_text(center - (int32_t)moswm_text_width(hhmm, 2) / 2, top + 9,
                hhmm, 0xFFEAF2F8u, 0, 2);
        wm_text(center - (int32_t)moswm_text_width(date, 1) / 2, top + 30,
                date, 0xFF9BAFBCu, 0, 1);
    }

    if (start_menu_open) {
        /* Kickoff-style launcher: header, category, item list, footer. */
        fill_rounded_rect(7, top - 271, 296, 268, 12, 0x2E000000u);
        fill_rounded_rect(12, top - 268, 286, 258, 8, 0xFF3A4249u);
        fill_rounded_rect(13, top - 267, 284, 256, 7, 0xFF1D2125u);
        fill_rounded_rect(24, top - 260, 30, 30, 15, accent);
        wm_text(32, top - 253, "M", 0xFFFFFFFFu, 0, 2);
        wm_text(64, top - 262, "MOSdesk", 0xFFF2F6F9u, 0, 2);
        wm_text(64, top - 242, "Minimal-OS", 0xFF9BAFBCu, 0, 1);
        fill_rect(30, top - 223, 250, 1, 0xFF353C42u);
        wm_text(30, top - 212, "SYSTEM", accent, 0, 1);
        bool item_hover = pointer_in(18, top - 192, 274, 36);
        if (item_hover)
            fill_rounded_rect(20, top - 192, 270, 36, 6,
                              0x55000000u | (accent & 0xFFFFFFu));
        moswm_draw_file_icon_sized(canvas, display.width, display.height,
                                   28, top - 190, MOSWM_ICON_SETTINGS, 32u);
        wm_text(80, top - 182, "Settings", 0xFFF2F6F9u, 0, 2);
        fill_rect(30, top - 46, 250, 1, 0xFF353C42u);
        wm_text(30, top - 33, "Open windows from the panel", 0xFF8A9AA6u, 0, 1);
    }
    if (status_message[0])
        wm_text(8, 8, status_message, 0xFFFFE696u, 0xAA000000u, 2);
}

static void auth_dialog_bounds(int32_t* x, int32_t* y,
                               int32_t* width, int32_t* height) {
    *width = display.width > 552u ? 520 : (int32_t)display.width - 32;
    *height = display.height > 332u ? 300 : (int32_t)display.height - 32;
    *x = ((int32_t)display.width - *width) / 2;
    *y = ((int32_t)display.height - *height) / 2;
}

static void draw_auth_screen(void) {
    draw_wallpaper();
    fill_rect(0, 0, (int32_t)display.width, (int32_t)display.height,
              0x660A1218u);
    int32_t x, y, width, height;
    auth_dialog_bounds(&x, &y, &width, &height);
    fill_rounded_rect(x, y, width, height, 12, 0xFF3A4249u);
    fill_rounded_rect(x + 2, y + 2, width - 4, height - 4, 10,
                      0xFFF3F5F6u);
    fill_rect(x + 24, y + 64, width - 48, 1, 0xFFD2D9DEu);
    fill_rounded_rect(x + 24, y + 19, 30, 30, 15, accent_color);
    wm_text(x + 32, y + 25, "M", 0xFFFFFFFFu, 0, 2);
    wm_text(x + 68, y + 22, "MOSdesk", 0xFF273640u, 0, 2);

    if (auth_state == AUTH_FIRST_CHOICE) {
        wm_text(x + 28, y + 86, "Welcome to MOSdesk", 0xFF34434Cu, 0, 2);
        wm_text(x + 28, y + 116, "Would you like password sign-in?",
                0xFF667680u, 0, 1);
        int32_t button_y = y + height - 58;
        int32_t button_width = (width - 84) / 2;
        int32_t first_button_x = x + 28;
        int32_t second_button_x = x + width - 28 - button_width;
        fill_rounded_rect(first_button_x, button_y, button_width, 36, 7,
                          accent_color);
        fill_rounded_rect(second_button_x, button_y, button_width, 36, 7,
                          0xFFDCE2E6u);
        wm_text(first_button_x + (button_width -
                (int32_t)moswm_text_width("Set password", 1u)) / 2,
                button_y + 11, "Set password",
                0xFFFFFFFFu, 0, 1);
        wm_text(second_button_x + (button_width -
                (int32_t)moswm_text_width("Auto login", 1u)) / 2,
                button_y + 11, "Auto login",
                0xFF34434Cu, 0, 1);
    } else {
        bool logging_in = auth_state == AUTH_LOGIN;
        wm_text(x + 28, y + 86,
                logging_in ? "Sign in to MOSdesk" : "Set a password",
                0xFF34434Cu, 0, 2);
        wm_text(x + 28, y + 116,
                logging_in ? "Enter your password to continue."
                           : "Use 4 to 63 characters, then press Enter.",
                0xFF667680u, 0, 1);
        fill_rounded_rect(x + 28, y + 140, width - 56, 38, 6,
                          0xFFFFFFFFu);
        fill_rect(x + 28, y + 140, width - 56, 1, 0xFFBCC6CDu);
        fill_rect(x + 28, y + 177, width - 56, 1, 0xFFBCC6CDu);
        char masked[AUTH_PASSWORD_MAX + 1u];
        uint32_t count = auth_password_length;
        if (count > AUTH_PASSWORD_MAX) count = AUTH_PASSWORD_MAX;
        for (uint32_t i = 0; i < count; i++) masked[i] = '*';
        masked[count] = '\0';
        wm_text(x + 42, y + 153, masked, 0xFF34434Cu, 0, 1);
        wm_text(x + 28, y + height - 51,
                "Enter to continue   Esc to go back",
                0xFF667680u, 0, 1);
    }
    if (auth_message[0])
        wm_text(x + 28, y + height - 78, auth_message,
                0xFFB43A32u, 0, 1);
    cursor_animation_elapsed_ms = mos_syscall1(SYS_UPTIME, 0);
    draw_cursor();
    if (mos_graphics_blit_surface(canvas, display.width, display.height,
            display.width * sizeof(uint32_t), 0, 0,
            (int32_t)display.width, (int32_t)display.height, 0, 0,
            SYS_GRAPHICS_BLIT_OPAQUE) != SYS_SUCCESS) {
        strcpy(status_message, "Framebuffer update failed");
        return;
    }
    if (mos_graphics_present(0, 0, (int32_t)display.width,
                             (int32_t)display.height) != SYS_SUCCESS)
        strcpy(status_message, "Display present failed");
}

static bool save_auth_preferences(bool password_enabled) {
    desktop_settings.onboarding_complete = 1u;
    desktop_settings.login_enabled = password_enabled ? 1u : 0u;
    if (!password_enabled) {
        memset(desktop_settings.password_salt, 0,
               sizeof(desktop_settings.password_salt));
        memset(desktop_settings.password_hash, 0,
               sizeof(desktop_settings.password_hash));
    }
    if (!mosdesk_settings_save(&desktop_settings)) {
        strcpy(auth_message, "Could not save setup. Please try again.");
        return false;
    }
    auth_state = AUTH_NONE;
    auth_message[0] = '\0';
    auth_password_length = 0;
    memset(auth_password, 0, sizeof(auth_password));
    return true;
}

static void submit_auth_password(void) {
    if (auth_state == AUTH_FIRST_PASSWORD) {
        if (auth_password_length < 4u) {
            strcpy(auth_message, "Use at least 4 characters.");
            return;
        }
        if (mos_syscall3(SYS_RANDOM,
                (uint64_t)(uintptr_t)desktop_settings.password_salt,
                sizeof(desktop_settings.password_salt), 0) !=
            sizeof(desktop_settings.password_salt)) {
            strcpy(auth_message, "Could not create a password salt.");
            return;
        }
        if (!mosdesk_password_hash(desktop_settings.password_salt,
                auth_password, desktop_settings.password_hash)) {
            strcpy(auth_message, "Could not set this password.");
            return;
        }
        save_auth_preferences(true);
    } else if (auth_state == AUTH_LOGIN) {
        uint8_t actual[MOSDESK_PASSWORD_HASH_SIZE];
        if (mosdesk_password_hash(desktop_settings.password_salt,
                auth_password, actual)) {
            uint8_t difference = 0;
            for (uint32_t i = 0; i < sizeof(actual); i++)
                difference |= actual[i] ^ desktop_settings.password_hash[i];
            memset(actual, 0, sizeof(actual));
            if (difference == 0) {
                auth_state = AUTH_NONE;
                auth_message[0] = '\0';
                auth_password_length = 0;
                memset(auth_password, 0, sizeof(auth_password));
                return;
            }
        }
        strcpy(auth_message, "Password did not match.");
        memset(auth_password, 0, sizeof(auth_password));
        auth_password_length = 0;
    }
}

static void handle_auth_key(const syscall_input_event_t* input) {
    if (input->type != SYSCALL_INPUT_KEY_DOWN) return;
    if (input->key == 40u) {
        if (auth_state != AUTH_FIRST_CHOICE) submit_auth_password();
        return;
    }
    if (input->key == 41u) {
        if (auth_state == AUTH_FIRST_PASSWORD) {
            auth_state = AUTH_FIRST_CHOICE;
            auth_message[0] = '\0';
            auth_password_length = 0;
            memset(auth_password, 0, sizeof(auth_password));
        }
        return;
    }
    if (input->key == 42u) {
        if (auth_password_length) {
            auth_password[--auth_password_length] = '\0';
            auth_message[0] = '\0';
        }
        return;
    }
    char character = (char)mos_syscall3(SYS_USB,
        SYS_USB_KEY_TO_ASCII, input->key,
        mosdesk_usb_modifiers(input->mods));
    if (character >= 32 && character <= 126 &&
        auth_password_length < AUTH_PASSWORD_MAX) {
        auth_password[auth_password_length++] = character;
        auth_password[auth_password_length] = '\0';
        auth_message[0] = '\0';
    }
}

static void handle_auth_click(void) {
    int32_t x, y, width, height;
    auth_dialog_bounds(&x, &y, &width, &height);
    if (auth_state == AUTH_FIRST_CHOICE) {
        int32_t button_y = y + height - 58;
        int32_t button_width = (width - 84) / 2;
        if (cursor_y < button_y || cursor_y >= button_y + 36) return;
        if (cursor_x >= x + 28 && cursor_x < x + 28 + button_width) {
            auth_state = AUTH_FIRST_PASSWORD;
            auth_password_length = 0;
            memset(auth_password, 0, sizeof(auth_password));
            auth_message[0] = '\0';
        } else if (cursor_x >= x + width - 28 - button_width &&
                   cursor_x < x + width - 28) {
            save_auth_preferences(false);
        }
    } else if (auth_state == AUTH_LOGIN) {
        submit_auth_password();
    }
}

static void render(const syscall_window_info_t* windows, uint64_t count) {
    if (auth_state != AUTH_NONE) {
        draw_auth_screen();
        return;
    }
    draw_wallpaper();
    draw_desktop_icons();
    for (uint32_t focus_pass = 0; focus_pass < 2; focus_pass++) {
        for (uint64_t i = 0; i < count; i++) {
            const syscall_window_info_t* info = &windows[i];
            bool focused = info->id == focused_id;
            if ((focus_pass == 0 && focused) || (focus_pass == 1 && !focused))
                continue;
            if (!(info->flags & SYSCALL_WINDOW_VISIBLE) ||
                (info->flags & SYSCALL_WINDOW_MINIMIZED))
                continue;
            cached_window_t* entry = cache_for(info->id);
            if (entry) composite_window(info, entry);
        }
    }
    draw_taskbar(windows, count);
    cursor_animation_elapsed_ms = mos_syscall1(SYS_UPTIME, 0);
    draw_cursor();
    if (mos_graphics_blit_surface(canvas, display.width, display.height,
            display.width * sizeof(uint32_t), 0, 0,
            (int32_t)display.width, (int32_t)display.height, 0, 0,
            SYS_GRAPHICS_BLIT_OPAQUE) != SYS_SUCCESS) {
        strcpy(status_message, "Framebuffer update failed");
        return;
    }
    if (mos_graphics_present(0, 0, (int32_t)display.width,
                             (int32_t)display.height) != SYS_SUCCESS)
        strcpy(status_message, "Display present failed");
}

static syscall_window_info_t* window_at(syscall_window_info_t* windows,
                                        uint64_t count, int32_t px,
                                        int32_t py) {
    for (uint32_t focus_pass = 0; focus_pass < 2; focus_pass++) {
        for (uint64_t i = count; i > 0; i--) {
            syscall_window_info_t* info = &windows[i - 1u];
            if (!(info->flags & SYSCALL_WINDOW_VISIBLE) ||
                (info->flags & SYSCALL_WINDOW_MINIMIZED) ||
                ((info->id == focused_id) != (focus_pass == 0)))
                continue;
            int32_t right = (int32_t)info->x + (int32_t)info->width + 2;
            int32_t bottom = (int32_t)info->y + (int32_t)info->height +
                             (int32_t)TITLE_HEIGHT + 2;
            if (px >= (int32_t)info->x && px < right &&
                py >= (int32_t)info->y && py < bottom)
                return info;
        }
    }
    return NULL;
}

static void update_cursor_appearance(syscall_window_info_t* windows,
                                     uint64_t count) {
    syscall_window_info_t* under =
        window_at(windows, count, cursor_x, cursor_y);
    syscall_window_info_t* app_window =
        under && under->id == focused_id ? under : NULL;
    moswm_cursor_state_t cursor_state = {0};
    uint32_t status = SYSCALL_CURSOR_ARROW;

    if (dragging_id) {
        status = drag_mode == DRAG_RESIZE
            ? SYSCALL_CURSOR_RESIZE_NWSE : SYSCALL_CURSOR_MOVE;
        app_window = NULL;
    } else if (under) {
        int32_t x = cursor_x - (int32_t)under->x;
        int32_t y = cursor_y - (int32_t)under->y;
        int32_t right = (int32_t)under->width + 1;
        if (y < (int32_t)TITLE_HEIGHT) {
            if (x >= right - 72) status = SYSCALL_CURSOR_HAND;
            else status = SYSCALL_CURSOR_MOVE;
            app_window = NULL;
        } else if ((under->flags & SYSCALL_WINDOW_RESIZABLE) &&
                   x >= right - 14 &&
                   y >= (int32_t)under->height +
                            (int32_t)TITLE_HEIGHT - 14) {
            status = SYSCALL_CURSOR_RESIZE_NWSE;
            app_window = NULL;
        } else if (!app_window) {
            status = SYSCALL_CURSOR_ARROW;
        } else {
            status = cursor_state.status;
        }
    }
    ensure_cursor_loaded(app_window ? &cursor_state : NULL, status);
}

static void focus_window(syscall_window_info_t* info) {
    if (!info || focused_id == info->id) return;
    if (mos_window_set_flags(info->id,
            info->flags | SYSCALL_WINDOW_FOCUSED) != SYS_SUCCESS) {
        strcpy(status_message, "Unable to focus window");
        return;
    }
    focused_id = info->id;
}

static void request_close(syscall_window_info_t* info) {
    if (!info) return;
    post_simple_event(info->id, SYSCALL_WINDOW_EVENT_CLOSE_REQUEST);
}

static void maximize_or_restore(syscall_window_info_t* info,
                                cached_window_t* entry) {
    if (!info || !entry) return;
    uint32_t mode = window_mode(info) == MODE_MAXIMIZED
        ? MODE_NORMAL : MODE_MAXIMIZED;
    if (!set_window_mode(info, entry, mode))
        strcpy(status_message, "Unable to change window size");
    else if (mode == MODE_MAXIMIZED)
        post_simple_event(info->id, SYSCALL_WINDOW_EVENT_MAXIMIZED);
    else
        post_simple_event(info->id, SYSCALL_WINDOW_EVENT_RESTORED);
}

static void minimize_window(syscall_window_info_t* info) {
    if (!info) return;
    cached_window_t* entry = cache_for(info->id);
    if (!entry || !set_window_mode(info, entry, MODE_MINIMIZED)) {
        strcpy(status_message, "Unable to minimize window");
        return;
    }
    post_simple_event(info->id, SYSCALL_WINDOW_EVENT_MINIMIZED);
    if (focused_id == info->id) focused_id = 0;
}

static void start_move(syscall_window_info_t* info, bool resize) {
    if (!info || (!(info->flags & SYSCALL_WINDOW_RESIZABLE) && resize)) return;
    cached_window_t* entry = cache_for(info->id);
    syscall_window_info_t current = *info;
    if (entry && window_mode(info) != MODE_NORMAL) {
        if (!set_window_mode(info, entry, MODE_NORMAL) ||
            mos_window_get_info(info->id, &current) != SYS_SUCCESS)
            return;
        post_simple_event(info->id, SYSCALL_WINDOW_EVENT_RESTORED);
    }
    dragging_id = info->id;
    drag_mode = resize ? DRAG_RESIZE : DRAG_MOVE;
    drag_start_x = (uint32_t)cursor_x;
    drag_start_y = (uint32_t)cursor_y;
    drag_start_width = current.width;
    drag_start_height = current.height;
    drag_start_window_x = current.x;
    drag_start_window_y = current.y;
    if (entry) save_bounds(entry, &current);
}

static void move_dragged_window(syscall_window_info_t* windows,
                                uint64_t count) {
    if (!dragging_id || drag_mode == DRAG_NONE) return;
    for (uint64_t i = 0; i < count; i++) {
        syscall_window_info_t* info = &windows[i];
        if (info->id != dragging_id) continue;
        int32_t dx = cursor_x - (int32_t)drag_start_x;
        int32_t dy = cursor_y - (int32_t)drag_start_y;
        uint32_t x = info->x;
        uint32_t y = info->y;
        uint32_t width = drag_start_width;
        uint32_t height = drag_start_height;
        if (drag_mode == DRAG_MOVE) {
            int32_t nx = (int32_t)drag_start_window_x + dx;
            int32_t ny = (int32_t)drag_start_window_y + dy;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            x = (uint32_t)nx;
            y = (uint32_t)ny;
        } else {
            int32_t nw = (int32_t)drag_start_width + dx;
            int32_t nh = (int32_t)drag_start_height + dy;
            if (nw < 64) nw = 64;
            if (nh < 48) nh = 48;
            width = (uint32_t)nw;
            height = (uint32_t)nh;
        }
        if (mos_window_set_bounds(info->id, x, y, width, height) != SYS_SUCCESS)
            strcpy(status_message, "Window resize or move failed");
        break;
    }
}

static void on_mouse_down(syscall_window_info_t* windows, uint64_t count) {
    uint32_t taskbar_y = display.height - TASKBAR_HEIGHT;
    if (start_menu_open && cursor_x >= 12 && cursor_x < 298 &&
        cursor_y >= (int32_t)taskbar_y - 268 &&
        cursor_y < (int32_t)taskbar_y - 10) {
        if (cursor_y >= (int32_t)taskbar_y - 192 &&
            cursor_y < (int32_t)taskbar_y - 156) {
            const char* settings_path = "0:/programs/mosdesk/mosdesk.run";
            int64_t process_id = (int64_t)mos_syscall3(SYS_EXEC,
                (uint64_t)(uintptr_t)settings_path, 0, 0);
            if (process_id < 0)
                strcpy(status_message, "Unable to open MOSdesk Settings");
            start_menu_open = false;
        }
        return;
    }
    if ((uint32_t)cursor_y >= taskbar_y) {
        if (cursor_x < 44) {
            start_menu_open = !start_menu_open;
            return;
        }
        uint32_t index = cursor_x >= 60
            ? ((uint32_t)cursor_x - 60u) / 180u : UINT32_MAX;
        if (cursor_x >= 60 && index < count) {
            syscall_window_info_t* info = &windows[index];
            if (info->flags & SYSCALL_WINDOW_MINIMIZED) {
                cached_window_t* entry = cache_for(info->id);
                if (entry && set_window_mode(info, entry, MODE_NORMAL))
                    post_simple_event(info->id, SYSCALL_WINDOW_EVENT_RESTORED);
                else
                    strcpy(status_message, "Unable to restore window");
            }
            syscall_window_info_t current;
            if (mos_window_get_info(info->id, &current) == SYS_SUCCESS)
                focus_window(&current);
        }
        return;
    }

    syscall_window_info_t* info =
        window_at(windows, count, cursor_x, cursor_y);
    if (!info) {
        start_menu_open = false;
        uint32_t app_index;
        if (desktop_app_at(cursor_x, cursor_y, &app_index)) {
            int64_t process_id = (int64_t)mos_syscall3(SYS_EXEC,
                (uint64_t)(uintptr_t)desktop_apps[app_index].path, 0, 0);
            if (process_id < 0)
                strcpy(status_message, "Unable to open desktop application");
        }
        return;
    }
    focus_window(info);
    int32_t x = cursor_x - (int32_t)info->x;
    int32_t y = cursor_y - (int32_t)info->y;
    int32_t right = (int32_t)info->width + 1;
    if (y < (int32_t)TITLE_HEIGHT) {
        if (x >= right - 24) {
            request_close(info);
        } else if (x >= right - 48) {
            maximize_or_restore(info, cache_for(info->id));
        } else if (x >= right - 72) {
            minimize_window(info);
        } else {
            start_move(info, false);
        }
    } else if (x >= right - 14 &&
               y >= (int32_t)info->height + (int32_t)TITLE_HEIGHT - 14) {
        start_move(info, true);
    }
}

static void on_mouse_up(syscall_window_info_t* windows, uint64_t count) {
    if (!dragging_id) return;
    for (uint64_t i = 0; i < count; i++) {
        syscall_window_info_t* info = &windows[i];
        if (info->id != dragging_id) continue;
        if (drag_mode == DRAG_MOVE) {
            uint32_t work_height = display.height - TASKBAR_HEIGHT;
            cached_window_t* entry = cache_for(info->id);
            if (cursor_x < SNAP_DISTANCE && cursor_y < SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, 0, 0,
                    display.width / 2u - 2u,
                    work_height / 2u - TITLE_HEIGHT - 2u);
            } else if (cursor_x >= (int32_t)display.width - SNAP_DISTANCE &&
                       cursor_y < SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, display.width / 2u, 0,
                    display.width / 2u - 2u,
                    work_height / 2u - TITLE_HEIGHT - 2u);
            } else if (cursor_x < SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, 0, 0,
                    display.width / 2u - 2u,
                    work_height - TITLE_HEIGHT - 2u);
            } else if (cursor_x >= (int32_t)display.width - SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, display.width / 2u, 0,
                    display.width / 2u - 2u,
                    work_height - TITLE_HEIGHT - 2u);
            } else if (cursor_y < SNAP_DISTANCE) {
                set_window_mode(info, entry, MODE_MAXIMIZED);
            } else if (window_mode(info) != MODE_NORMAL) {
                set_window_mode(info, entry, MODE_NORMAL);
            }
        }
        break;
    }
    dragging_id = 0;
    drag_mode = DRAG_NONE;
}

static void route_input(const syscall_input_event_t* input,
                        syscall_window_info_t* windows, uint64_t count) {
    if (auth_state != AUTH_NONE) {
        if (input->type == SYSCALL_INPUT_MOUSE_MOVE) {
            int32_t scaled_x =
                input->dx * (int32_t)mouse_speed + mouse_remainder_x;
            int32_t scaled_y =
                input->dy * (int32_t)mouse_speed + mouse_remainder_y;
            cursor_x += scaled_x / 100;
            cursor_y += scaled_y / 100;
            mouse_remainder_x = scaled_x % 100;
            mouse_remainder_y = scaled_y % 100;
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_y < 0) cursor_y = 0;
            if ((uint32_t)cursor_x >= display.width)
                cursor_x = (int32_t)display.width - 1;
            if ((uint32_t)cursor_y >= display.height)
                cursor_y = (int32_t)display.height - 1;
        } else if (input->type == SYSCALL_INPUT_MOUSE_BUTTON) {
            bool was_down =
                (previous_buttons & SYSCALL_INPUT_BTN_LEFT) != 0;
            bool is_down =
                (input->buttons & SYSCALL_INPUT_BTN_LEFT) != 0;
            previous_buttons = input->buttons;
            if (!was_down && is_down) handle_auth_click();
        } else if (input->type == SYSCALL_INPUT_KEY_DOWN) {
            handle_auth_key(input);
        }
        return;
    }
    if (input->type == SYSCALL_INPUT_MOUSE_MOVE) {
        int32_t scaled_x = input->dx * (int32_t)mouse_speed + mouse_remainder_x;
        int32_t scaled_y = input->dy * (int32_t)mouse_speed + mouse_remainder_y;
        cursor_x += scaled_x / 100;
        cursor_y += scaled_y / 100;
        mouse_remainder_x = scaled_x % 100;
        mouse_remainder_y = scaled_y % 100;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if ((uint32_t)cursor_x >= display.width)
            cursor_x = (int32_t)display.width - 1;
        if ((uint32_t)cursor_y >= display.height)
            cursor_y = (int32_t)display.height - 1;
        move_dragged_window(windows, count);
        syscall_window_info_t* under =
            window_at(windows, count, cursor_x, cursor_y);
        if (under) {
            syscall_window_event_t event = {
                .type = SYSCALL_WINDOW_EVENT_MOUSE,
                .x = cursor_x - (int32_t)under->x - 1,
                .y = cursor_y - (int32_t)under->y -
                     (int32_t)TITLE_HEIGHT - 1,
                .buttons = input->buttons,
                .mods = input->mods,
                .time_ms = input->time_ms
            };
            if (event.y >= 0 &&
                mos_window_post_event(under->id, &event) != SYS_SUCCESS)
                strcpy(status_message, "Unable to deliver mouse movement");
        }
    } else if (input->type == SYSCALL_INPUT_MOUSE_BUTTON) {
        bool was_down =
            (previous_buttons & SYSCALL_INPUT_BTN_LEFT) != 0;
        bool is_down = (input->buttons & SYSCALL_INPUT_BTN_LEFT) != 0;
        previous_buttons = input->buttons;
        syscall_window_info_t* under =
            window_at(windows, count, cursor_x, cursor_y);
        if (under) {
            focus_window(under);
            syscall_window_event_t event = {
                .type = SYSCALL_WINDOW_EVENT_MOUSE,
                .x = cursor_x - (int32_t)under->x - 1,
                .y = cursor_y - (int32_t)under->y -
                     (int32_t)TITLE_HEIGHT - 1,
                .buttons = input->buttons,
                .mods = input->mods,
                .time_ms = input->time_ms
            };
            if (event.y >= 0) mos_window_post_event(under->id, &event);
        }
        if (!was_down && is_down) on_mouse_down(windows, count);
        else if (was_down && !is_down) on_mouse_up(windows, count);
    } else if (input->type == SYSCALL_INPUT_MOUSE_WHEEL) {
        syscall_window_info_t* under =
            window_at(windows, count, cursor_x, cursor_y);
        if (under) {
            syscall_window_event_t event = {
                .type = SYSCALL_WINDOW_EVENT_MOUSE_WHEEL,
                .x = cursor_x - (int32_t)under->x - 1,
                .y = cursor_y - (int32_t)under->y -
                     (int32_t)TITLE_HEIGHT - 1,
                .wheel = input->wheel,
                .buttons = input->buttons,
                .mods = input->mods,
                .time_ms = input->time_ms
            };
            if (event.y >= 0) mos_window_post_event(under->id, &event);
        }
    } else if (input->type == SYSCALL_INPUT_KEY_DOWN ||
               input->type == SYSCALL_INPUT_KEY_UP) {
        if (!focused_id) return;
        syscall_window_event_t event = {
            .type = SYSCALL_WINDOW_EVENT_KEY,
            .key = input->key,
            .buttons = input->type == SYSCALL_INPUT_KEY_DOWN,
            .mods = input->mods,
            .time_ms = input->time_ms
        };
        if (mos_window_post_event(focused_id, &event) == SYS_ERR_BUSY)
            strcpy(status_message, "Window event queue is full");
    }
}

static void cleanup(void) {
    if (screen_owned) {
        mos_graphics_release();
        screen_owned = false;
    }
    wm_free(canvas);
    canvas = NULL;
    wm_free(wallpaper);
    wallpaper = NULL;
    wm_free(font_regular_data);
    font_regular_data = NULL;
    wm_free(font_medium_data);
    font_medium_data = NULL;
    wm_free(cursor_animation_pixels);
    cursor_animation_pixels = NULL;
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        wm_free(cache[i].pixels);
        memset(&cache[i], 0, sizeof(cache[i]));
    }
}

static void cleanup_and_exit(void) {
    cleanup();
    mos_exit();
}

int main(void) {
    if (mos_graphics_acquire() != SYS_SUCCESS) {
        log_message("mosWM: another process owns the display\n");
        return 1;
    }
    screen_owned = true;
    if (mos_window_register_manager() != SYS_SUCCESS) {
        mos_graphics_release();
        screen_owned = false;
        log_message("mosWM: this executable is not the registered window server\n");
        return 1;
    }
    mos_register_cleanup(cleanup_and_exit);
    if (mos_graphics_info(&display) != SYS_SUCCESS ||
        display.pixel_format != SYS_GRAPHICS_PIXEL_FORMAT_ARGB8888 ||
        display.width < 320 || display.height < TASKBAR_HEIGHT + 120u ||
        (size_t)display.width > SIZE_MAX / display.height / sizeof(uint32_t)) {
        log_message("mosWM: unsupported display size\n");
        cleanup();
        return 1;
    }
    canvas = wm_malloc((size_t)display.width * display.height *
                       sizeof(uint32_t));
    if (!canvas) {
        log_message("mosWM: unable to allocate compositor surface\n");
        cleanup();
        return 1;
    }
    wallpaper = wm_malloc((size_t)display.width * display.height *
                          sizeof(uint32_t));
    cursor_x = (int32_t)(display.width / 2u);
    cursor_y = (int32_t)(display.height / 2u);
    load_desktop_settings();
    if (mos_syscall3(SYS_IS_DIR,
            (uint64_t)(uintptr_t)MOSDESK_USERS_PATH, 0, 0) != 1u &&
        mos_syscall3(SYS_MKDIR,
            (uint64_t)(uintptr_t)MOSDESK_USERS_PATH, 0, 0) != SYS_SUCCESS) {
        log_message("mosWM: unable to create 0:/users\n");
        cleanup();
        return 1;
    }
    load_desktop_apps();
    if (!desktop_settings.onboarding_complete)
        auth_state = AUTH_FIRST_CHOICE;
    else if (desktop_settings.login_enabled)
        auth_state = AUTH_LOGIN;
    painted_wallpaper_style = UINT32_MAX;
    load_fonts();
    syscall_window_info_t windows[MAX_WINDOWS];
    syscall_input_event_t input[32];

    for (;;) {
        bool refresh_icons = ++icon_poll_frames >= 30u;
        if (refresh_icons) icon_poll_frames = 0;
        if (++settings_poll_frames >= 30u) {
            settings_poll_frames = 0;
            load_desktop_settings();
        }
        uint64_t count = mos_window_list(windows, MAX_WINDOWS);
        if (count > MAX_WINDOWS) {
            strcpy(status_message, "Unable to enumerate windows");
            count = 0;
        }
        remove_missing_cache(windows, count);
        sync_window_modes(windows, count);
        focused_id = 0;
        for (uint64_t i = 0; i < count; i++) {
            cached_window_t* entry = cache_for(windows[i].id);
            if (windows[i].flags & SYSCALL_WINDOW_FOCUSED)
                focused_id = windows[i].id;
            refresh_window_icon(entry, refresh_icons);
            if (entry && !refresh_frame(entry, &windows[i]))
                strcpy(status_message, "Unable to read a window surface");
        }

        uint64_t event_count = mos_input_read(input, 32, 0, NULL);
        if (event_count <= 32) {
            for (uint64_t i = 0; i < event_count; i++)
                route_input(&input[i], windows, count);
        } else if (event_count != SYS_ERR_AGAIN) {
            strcpy(status_message, "Input read failed");
        }
        update_cursor_appearance(windows, count);
        render(windows, count);
        mos_sleep(16);
    }
}