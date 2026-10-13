#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ttrend.h"
#include "minimalos.h"
#include "window.h"
#include "stdlib.h"
#include "string.h"
#include "settings.h"
#include "password_hash.h"
#include "input.h"
#include "../mosWM/file_icons.h"

#define SETTINGS_WIDTH 520u
#define SETTINGS_HEIGHT 320u
#define NAV_WIDTH 148u
#define NAV_TOP 52
#define NAV_PITCH 44
#define PASSWORD_MAX_LENGTH 63u

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t* pixels;
} settings_surface_t;

static settings_surface_t surface;
static mosdesk_settings_t preferences;
static uint32_t window_id;
static uint32_t category;
static bool password_editing;
static char password_entry[PASSWORD_MAX_LENGTH + 1u];
static uint32_t password_entry_length;
static char settings_message[64];
static ttrend_font_t small_font = {1, 1, 0, 0};
static ttrend_font_t large_font = {2, 1, 1, 0};
static ttrend_font_t loaded_font;
static ttrend_font_t loaded_large_font;

static const uint32_t cursor_colors[] = {
    0xFFFFFFFFu, 0xFFFFE4A6u, 0xFF8EC5FFu, 0xFF55D6BEu
};

static const uint32_t accent_colors[] = {
    0xFF59A6D8u, 0xFF48B8A5u, 0xFFD28C54u
};
static const char* const wallpaper_names[] = {"Blue", "Teal", "Copper"};

static ttrend_surface_t text_target(void) {
    return (ttrend_surface_t){surface.pixels, surface.width,
                              surface.height, surface.width};
}

static bool resize_surface(uint32_t width, uint32_t height) {
    if (width < 360u || height < 220u ||
        (size_t)width > SIZE_MAX / height / sizeof(uint32_t))
        return false;
    uint32_t* pixels = (uint32_t*)realloc(surface.pixels,
        (size_t)width * height * sizeof(uint32_t));
    if (!pixels) return false;
    surface.pixels = pixels;
    surface.width = width;
    surface.height = height;
    return true;
}

static void fill_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                      uint32_t color) {
    int64_t right = (int64_t)x + width;
    int64_t bottom = (int64_t)y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > surface.width) right = surface.width;
    if (bottom > surface.height) bottom = surface.height;
    for (int32_t py = y; py < bottom; py++) {
        uint32_t* row = surface.pixels + (size_t)py * surface.width;
        for (int32_t px = x; px < right; px++) row[px] = color;
    }
}

static void draw_cursor_icon(int32_t x, int32_t y, uint32_t scale,
                             uint32_t fill_color) {
    static const char* const shape[] = {
        "#...........", "##..........", "#x#.........",
        "#xx#........", "#xxx#.......", "#xxxx#......",
        "#xxxxx#.....", "#xxxxxx#....", "#xxxxxxx#...",
        "#xxxxxxxx#..", "#xxxxx###...", "#xx#xx#......",
        "...#..#......", "...#..#......", "....##......."
    };
    for (uint32_t row = 0; row < sizeof(shape) / sizeof(shape[0]); row++) {
        for (uint32_t column = 0; column < 12u; column++) {
            uint32_t color = shape[row][column] == '#'
                ? 0xFF263640u : shape[row][column] == 'x'
                    ? fill_color : 0u;
            if (color)
                fill_rect(x + (int32_t)(column * scale),
                          y + (int32_t)(row * scale),
                          (int32_t)scale, (int32_t)scale, color);
        }
    }
}

static void draw_text(int32_t x, int32_t y, const char* text,
                      uint32_t color, bool large) {
    ttrend_surface_t target = text_target();
    ttrend_draw_text(&target, x, y, text, color,
                     large ? &large_font : &small_font);
}

static void draw_control_labels(void) {
    if (category == 0u) {
        draw_text((int32_t)NAV_WIDTH + 22, 63, "Desktop accent",
                  0xFF34434Cu, true);
        /* One label per swatch: proportional text cannot be aligned with
         * runs of spaces the way the monospace bitmap font was. */
        static const char* const accent_names[] = {"Blue", "Teal", "Copper"};
        for (uint32_t i = 0; i < 3; i++)
            draw_text((int32_t)NAV_WIDTH + 22 + (int32_t)i * 50, 132,
                      accent_names[i], 0xFF667680u, false);
        draw_text((int32_t)NAV_WIDTH + 22, 185, "Wallpaper",
                  0xFF34434Cu, true);
        for (uint32_t i = 0; i < 3u; i++)
            draw_text((int32_t)NAV_WIDTH + 22 + (int32_t)i * 50, 248,
                      wallpaper_names[i], 0xFF667680u, false);
    } else if (category == 1u) {
        char speed[5];
        uint32_t length;
        if (preferences.mouse_speed >= 100u) {
            speed[0] = (char)('0' + preferences.mouse_speed / 100u);
            speed[1] = (char)('0' + (preferences.mouse_speed / 10u) % 10u);
            speed[2] = (char)('0' + preferences.mouse_speed % 10u);
            length = 3u;
        } else {
            speed[0] = (char)('0' + preferences.mouse_speed / 10u);
            speed[1] = (char)('0' + preferences.mouse_speed % 10u);
            length = 2u;
        }
        speed[length] = '%';
        speed[length + 1u] = '\0';
        draw_text((int32_t)NAV_WIDTH + 22, 63, "Pointer speed",
                  0xFF34434Cu, true);
        draw_text((int32_t)NAV_WIDTH + 22, 129, speed,
                  0xFF667680u, false);
        draw_text((int32_t)NAV_WIDTH + 22, 176, "Drag to adjust",
                  0xFF667680u, false);
    } else if (category == 2u) {
        draw_text((int32_t)NAV_WIDTH + 22, 63, "Cursor fill",
                  0xFF34434Cu, true);
        draw_text((int32_t)NAV_WIDTH + 22, 159, "Size",
                  0xFF34434Cu, true);
        draw_text((int32_t)NAV_WIDTH + 29, 201, "100%",
                  0xFF34434Cu, false);
        draw_text((int32_t)NAV_WIDTH + 85, 201, "200%",
                  0xFF34434Cu, false);
        draw_text((int32_t)NAV_WIDTH + 22, 220, "Pack",
                  0xFF34434Cu, false);
        draw_text((int32_t)NAV_WIDTH + 43, 243, "Posy",
                  preferences.cursor_pack == MOSDESK_CURSOR_PACK_POSY
                      ? 0xFFFFFFFFu : 0xFF34434Cu, false);
        draw_text((int32_t)NAV_WIDTH + 139, 243, "Classic",
                  preferences.cursor_pack == MOSDESK_CURSOR_PACK_CLASSIC
                      ? 0xFFFFFFFFu : 0xFF34434Cu, false);
    } else if (category == 3u) {
        draw_text((int32_t)NAV_WIDTH + 22, 63, "Password sign-in",
                  0xFF34434Cu, true);
        if (password_editing) {
            draw_text((int32_t)NAV_WIDTH + 22, 167,
                      "Enter a password of at least 4 characters.",
                      0xFF667680u, false);
            draw_text((int32_t)NAV_WIDTH + 22, 188, "Press Enter to save",
                      0xFF667680u, false);
        } else if (preferences.login_enabled) {
            /* Below the Change/Disable password buttons (y 106-185) so the
             * note no longer draws on top of the button labels. */
            draw_text((int32_t)NAV_WIDTH + 22, 199,
                      "Sign-in is required after restart.",
                      0xFF667680u, false);
        } else {
            draw_text((int32_t)NAV_WIDTH + 22, 161,
                      "The desktop opens automatically.",
                      0xFF667680u, false);
        }
    } else {
        draw_text((int32_t)NAV_WIDTH + 22, 63, "Click once to open / run",
                  0xFF34434Cu, true);
        draw_text((int32_t)NAV_WIDTH + 22, 161,
                  "Off uses double-click to open / run.",
                  0xFF667680u, false);
    }
}

static void draw_window(void) {
    fill_rect(0, 0, (int32_t)surface.width, (int32_t)surface.height,
              0xFFF3F5F6u);
    fill_rect(0, 0, (int32_t)NAV_WIDTH, (int32_t)surface.height,
              0xFFE6EAEDu);
    fill_rect((int32_t)NAV_WIDTH, 0, 1, (int32_t)surface.height,
              0xFFD2D9DEu);
    fill_rect(10, NAV_TOP + (int32_t)category * NAV_PITCH, 128, NAV_PITCH - 4,
              0xFFD4E4EFu);
    fill_rect(10, NAV_TOP + (int32_t)category * NAV_PITCH, 3, NAV_PITCH - 4,
              preferences.accent_color);
    fill_rect((int32_t)NAV_WIDTH + 14, 42,
              (int32_t)surface.width - (int32_t)NAV_WIDTH - 28,
              1, 0xFFD2D9DEu);

    draw_text(16, 15, "MOSdesk Settings", 0xFF273640u, true);
    static const moswm_file_icon_t nav_icons[5] = {
        MOSWM_ICON_APPEARANCE, MOSWM_ICON_MOUSE,
        MOSWM_ICON_POINTER, MOSWM_ICON_SETTINGS, MOSWM_ICON_HOME};
    static const char* const nav_names[5] = {
        "Appearance", "Mouse", "Cursor", "Login", "Desktop"};
    for (int32_t i = 0; i < 5; i++) {
        int32_t row = NAV_TOP + i * NAV_PITCH;
        moswm_draw_file_icon_sized(surface.pixels, surface.width,
                                   surface.height, 18, row + 3,
                                   nav_icons[i], 32u);
        draw_text(58, row + 15, nav_names[i], 0xFF34434Cu, false);
    }

    if (category == 0u) {
        for (uint32_t i = 0; i < 3; i++) {
            int32_t x = (int32_t)NAV_WIDTH + 22 + (int32_t)i * 50;
            fill_rect(x, 96, 30, 30, accent_colors[i]);
            if ((preferences.accent_color & 0x00FFFFFFu) ==
                (accent_colors[i] & 0x00FFFFFFu))
                fill_rect(x - 2, 94, 34, 2, 0xFF34434Cu);
        }
        static const uint32_t wallpaper_colors[3] = {
            0xFF31536Au, 0xFF28645Fu, 0xFF80502Eu
        };
        for (uint32_t i = 0; i < 3; i++) {
            int32_t x = (int32_t)NAV_WIDTH + 22 + (int32_t)i * 50;
            fill_rect(x, 212, 44, 28, wallpaper_colors[i]);
            if (preferences.wallpaper == i)
                fill_rect(x - 2, 210, 48, 2, 0xFF34434Cu);
        }
    } else if (category == 1u) {
        int32_t track_x = (int32_t)NAV_WIDTH + 22;
        int32_t track_y = 101;
        int32_t track_width = (int32_t)surface.width - track_x - 24;
        int32_t marker = track_width *
            (int32_t)(preferences.mouse_speed - 50u) / 150;
        fill_rect(track_x, track_y, track_width, 5, 0xFFBCC6CDu);
        fill_rect(track_x, track_y, marker, 5, preferences.accent_color);
        fill_rect(track_x + marker - 5, track_y - 5, 11, 16,
                  preferences.accent_color);
    } else if (category == 2u) {
        draw_cursor_icon((int32_t)surface.width - 52, 58, 2u,
                         preferences.cursor_fill);
        for (uint32_t i = 0; i < 4; i++) {
            int32_t x = (int32_t)NAV_WIDTH + 22 + (int32_t)i * 37;
            fill_rect(x, 106, 25, 25, cursor_colors[i]);
            if ((preferences.cursor_fill & 0x00FFFFFFu) ==
                (cursor_colors[i] & 0x00FFFFFFu))
                fill_rect(x - 2, 104, 29, 2, preferences.accent_color);
        }
        fill_rect((int32_t)NAV_WIDTH + 22, 191, 48, 27, 0xFFDCE2E6u);
        fill_rect((int32_t)NAV_WIDTH + 78, 191, 48, 27, 0xFFDCE2E6u);
        if (preferences.cursor_scale == 1u)
            fill_rect((int32_t)NAV_WIDTH + 22, 215, 48, 3,
                      preferences.accent_color);
        else
            fill_rect((int32_t)NAV_WIDTH + 78, 215, 48, 3,
                      preferences.accent_color);
        fill_rect((int32_t)NAV_WIDTH + 22, 234, 84, 28,
                  preferences.cursor_pack == MOSDESK_CURSOR_PACK_POSY
                      ? preferences.accent_color : 0xFFDCE2E6u);
        fill_rect((int32_t)NAV_WIDTH + 116, 234, 84, 28,
                  preferences.cursor_pack == MOSDESK_CURSOR_PACK_CLASSIC
                      ? preferences.accent_color : 0xFFDCE2E6u);
    } else if (category == 3u) {
        if (password_editing) {
            fill_rect((int32_t)NAV_WIDTH + 22, 106,
                      (int32_t)surface.width - (int32_t)NAV_WIDTH - 44,
                      38, 0xFFFFFFFFu);
            fill_rect((int32_t)NAV_WIDTH + 22, 106,
                      (int32_t)surface.width - (int32_t)NAV_WIDTH - 44,
                      1, 0xFFBCC6CDu);
            fill_rect((int32_t)NAV_WIDTH + 22, 143,
                      (int32_t)surface.width - (int32_t)NAV_WIDTH - 44,
                      1, 0xFFBCC6CDu);
            char masked[PASSWORD_MAX_LENGTH + 1u];
            for (uint32_t i = 0; i < password_entry_length; i++)
                masked[i] = '*';
            masked[password_entry_length] = '\0';
            draw_text((int32_t)NAV_WIDTH + 34, 118, masked,
                      0xFF34434Cu, false);
        } else if (preferences.login_enabled) {
            fill_rect((int32_t)NAV_WIDTH + 22, 106, 160, 34,
                      preferences.accent_color);
            fill_rect((int32_t)NAV_WIDTH + 22, 151, 160, 34,
                      0xFFDCE2E6u);
        } else {
            fill_rect((int32_t)NAV_WIDTH + 22, 106, 160, 34,
                      preferences.accent_color);
        }
    } else {
        fill_rect((int32_t)NAV_WIDTH + 22, 106, 160, 34,
                  preferences.click_once_open
                      ? preferences.accent_color : 0xFFDCE2E6u);
    }
    draw_control_labels();
    if (category == 3u && !password_editing) {
        draw_text((int32_t)NAV_WIDTH + 33, 117,
                  preferences.login_enabled ? "Change password"
                                            : "Set password",
                  0xFFFFFFFFu, false);
        if (preferences.login_enabled)
            draw_text((int32_t)NAV_WIDTH + 48, 162, "Disable password",
                      0xFF34434Cu, false);
    } else if (category == 4u) {
        draw_text((int32_t)NAV_WIDTH + 33, 117,
                  preferences.click_once_open ? "On" : "Off",
                  preferences.click_once_open ? 0xFFFFFFFFu : 0xFF34434Cu,
                  false);
    }
    if (settings_message[0])
        draw_text((int32_t)NAV_WIDTH + 22,
                  (int32_t)surface.height - 22, settings_message,
                  0xFFB43A32u, false);
}

static void save_preferences(const mosdesk_settings_t* previous) {
    if (!mosdesk_settings_save(&preferences)) {
        preferences = *previous;
        strcpy(settings_message, "Unable to save settings.");
        return;
    }
    settings_message[0] = '\0';
}

static void save_login_password(void) {
    if (password_entry_length < 4u) {
        strcpy(settings_message, "Use at least 4 characters.");
        return;
    }
    mosdesk_settings_t updated = preferences;
    if (mos_syscall(SYS_RANDOM,
            (long)(uintptr_t)updated.password_salt,
            (long)sizeof(updated.password_salt), 0) !=
        (long)sizeof(updated.password_salt)) {
        strcpy(settings_message, "Could not create password salt.");
        return;
    }
    if (!mosdesk_password_hash(updated.password_salt,
            password_entry, updated.password_hash)) {
        strcpy(settings_message, "Could not set password.");
        return;
    }
    updated.login_enabled = 1u;
    updated.onboarding_complete = 1u;
    if (!mosdesk_settings_save(&updated)) {
        strcpy(settings_message, "Unable to save password settings.");
        return;
    }
    preferences = updated;
    password_editing = false;
    password_entry_length = 0;
    memset(password_entry, 0, sizeof(password_entry));
    settings_message[0] = '\0';
}

static void handle_mouse(const syscall_window_event_t* event) {
    if (!(event->buttons & SYSCALL_MOUSE_BTN_LEFT)) return;
    mosdesk_settings_t previous = preferences;
    int32_t x = event->x;
    int32_t y = event->y;
    if (x < (int32_t)NAV_WIDTH) {
        int32_t row = (y - NAV_TOP) / NAV_PITCH;
        if (y < NAV_TOP || row < 0 || row > 4) return;
        category = (uint32_t)row;
        settings_message[0] = '\0';
    } else if (category == 0u && y >= 92 && y < 132) {
        int32_t index = (x - (int32_t)NAV_WIDTH - 22) / 50;
        if (index < 0 || index >= 3) return;
        preferences.accent_color = accent_colors[index];
    } else if (category == 0u && y >= 210 && y < 242) {
        int32_t index = (x - (int32_t)NAV_WIDTH - 22) / 50;
        if (index < 0 || index >= 3) return;
        preferences.wallpaper = (uint32_t)index;
    } else if (category == 1u && y >= 94 && y < 118) {
        int32_t track_x = (int32_t)NAV_WIDTH + 22;
        int32_t track_width = (int32_t)surface.width - track_x - 24;
        if (track_width < 1) return;
        int32_t relative = x - track_x;
        if (relative < 0) relative = 0;
        if (relative > track_width) relative = track_width;
        preferences.mouse_speed = 50u +
            (uint32_t)(relative * 150 / track_width);
    } else if (category == 2u && y >= 102 && y < 138) {
        int32_t index = (x - (int32_t)NAV_WIDTH - 22) / 37;
        if (index < 0 || index >= 4) return;
        preferences.cursor_fill = cursor_colors[index];
    } else if (category == 2u && y >= 188 && y < 222) {
        preferences.cursor_scale = x < (int32_t)NAV_WIDTH + 74 ? 1u : 2u;
    } else if (category == 2u && y >= 234 && y < 262) {
        if (x >= (int32_t)NAV_WIDTH + 22 &&
            x < (int32_t)NAV_WIDTH + 106)
            preferences.cursor_pack = MOSDESK_CURSOR_PACK_POSY;
        else if (x >= (int32_t)NAV_WIDTH + 116 &&
                 x < (int32_t)NAV_WIDTH + 200)
            preferences.cursor_pack = MOSDESK_CURSOR_PACK_CLASSIC;
        else
            return;
    } else if (category == 3u && !password_editing &&
               y >= 104 && y < 144 &&
               x >= (int32_t)NAV_WIDTH + 22 &&
               x < (int32_t)NAV_WIDTH + 182) {
        password_editing = true;
        password_entry_length = 0;
        memset(password_entry, 0, sizeof(password_entry));
        settings_message[0] = '\0';
        return;
    } else if (category == 3u && preferences.login_enabled &&
               !password_editing && y >= 149 && y < 189 &&
               x >= (int32_t)NAV_WIDTH + 22 &&
               x < (int32_t)NAV_WIDTH + 182) {
        preferences.login_enabled = 0u;
        memset(preferences.password_salt, 0,
               sizeof(preferences.password_salt));
        memset(preferences.password_hash, 0,
               sizeof(preferences.password_hash));
    } else if (category == 4u && y >= 104 && y < 144 &&
               x >= (int32_t)NAV_WIDTH + 22 &&
               x < (int32_t)NAV_WIDTH + 182) {
        preferences.click_once_open = !preferences.click_once_open;
    } else {
        return;
    }
    save_preferences(&previous);
}

static void handle_key(const syscall_window_event_t* event) {
    if (category != 3u || !password_editing || !event->buttons) return;
    if (event->key == 40u) {
        save_login_password();
        return;
    }
    if (event->key == 41u) {
        password_editing = false;
        password_entry_length = 0;
        memset(password_entry, 0, sizeof(password_entry));
        settings_message[0] = '\0';
        return;
    }
    if (event->key == 42u) {
        if (password_entry_length)
            password_entry[--password_entry_length] = '\0';
        settings_message[0] = '\0';
        return;
    }
    char character = (char)mos_syscall(SYS_USB,
        SYS_USB_KEY_TO_ASCII, event->key,
        mosdesk_usb_modifiers(event->mods));
    if (character >= 32 && character <= 126 &&
        password_entry_length < PASSWORD_MAX_LENGTH) {
        password_entry[password_entry_length++] = character;
        password_entry[password_entry_length] = '\0';
        settings_message[0] = '\0';
    }
}

static void close_requested(uint32_t id) {
    mos_window_close(id);
    free(surface.pixels);
    surface.pixels = NULL;
    mos_exit(0);
}

static void load_fonts(void) {
    /* Prefer smooth OpenType text (Inter); fall back to the bitmap font.
     * For outline fonts ttrend_draw_text() puts y at the top of capitals, so
     * the layout below is the same for both. */
    if (ttrend_font_load_path(&loaded_font,
            "0:/programs/mosdesk/fonts/Inter-Regular.otf", 12u) == 0) {
        small_font = loaded_font;
        small_font.tracking = 0u;
        if (ttrend_font_load_path(&loaded_large_font,
                "0:/programs/mosdesk/fonts/Inter-Medium.otf", 16u) == 0) {
            large_font = loaded_large_font;
        } else {
            large_font = loaded_font;
            large_font.pixel_size = 16u;
        }
        large_font.tracking = 0u;
    } else if (ttrend_font_load_path(&loaded_font,
            "0:/programs/mosdesk/fonts/default.font", 1u) == 0) {
        small_font = loaded_font;
        large_font = loaded_font;
        large_font.scale = 2u;
        large_font.pixel_size = 2u;
        large_font.tracking = 1u;
        large_font.bold = 1u;
    }
}

int main(void) {
    preferences = mosdesk_settings_load();
    load_fonts();

    window_id = mos_window_create(0, 72, 56, SETTINGS_WIDTH, SETTINGS_HEIGHT,
                                  "MOSdesk Settings");
    if (!window_id || !resize_surface(SETTINGS_WIDTH, SETTINGS_HEIGHT)) {
        if (window_id) mos_window_close(window_id);
        return 1;
    }
    mos_window_set_resizable(window_id, true);
    syscall_window_style_t style = {
        .title_bar = 0xFF263945u,
        .title_text = 0xFFF4F7F8u,
        .close_button = 0xFF263945u,
        .minimize_button = 0xFF263945u,
        .maximize_button = 0xFF263945u,
        .close_icon = 0xFFF4F7F8u,
        .minimize_icon = 0xFFF4F7F8u,
        .maximize_icon = 0xFFF4F7F8u,
        .border = 0xFF536773u,
        .content_background = 0xFFF3F5F6u
    };
    mos_window_set_style(window_id, style);

    for (;;) {
        syscall_window_event_t event = {0};
        while (mos_window_next_event(window_id, &event) == SYS_SUCCESS) {
            if (event.type == SYSCALL_WINDOW_EVENT_CLOSE_REQUEST) {
                close_requested(window_id);
                return 0;
            }
            if (event.type == SYSCALL_WINDOW_EVENT_RESIZED &&
                event.width && event.height)
                resize_surface((uint32_t)event.width,
                               (uint32_t)event.height);
            if (event.type == SYSCALL_WINDOW_EVENT_MOUSE) handle_mouse(&event);
            if (event.type == SYSCALL_WINDOW_EVENT_KEY) handle_key(&event);
        }
        syscall_window_info_t info = {0};
        if (mos_window_get_info(window_id, &info) != SYS_SUCCESS) {
            free(surface.pixels);
            return 1;
        }
        if (surface.width != info.width || surface.height != info.height)
            resize_surface(info.width, info.height);
        draw_window();
        mos_window_present(window_id, surface.pixels,
            surface.width * sizeof(uint32_t), 0, 0,
            surface.width, surface.height);
        mos_sleep(16);
    }
}
