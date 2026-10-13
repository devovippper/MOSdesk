/* Compiles the real mosWM compositor and renders one frame of it. */
#define main wm_main
#include "mosWM/main.c"
#undef main
extern uint32_t* harness_fb;
extern syscall_window_info_t harness_windows[8];
extern int harness_nwin;
extern uint32_t* harness_frames[8];
extern uint32_t harness_w, harness_h;
uint32_t* wm_frame(int menu_open, int cx, int cy, const char* status) {
    mos_graphics_info(&display);
    canvas = wm_malloc((size_t)display.width * display.height * 4);
    wallpaper = wm_malloc((size_t)display.width * display.height * 4);
    paint_wallpaper(wallpaper);
    cursor_x = cx; cursor_y = cy;
    load_desktop_settings(); load_fonts();
    start_menu_open = menu_open != 0;
    if (status) strcpy(status_message, status);
    syscall_window_info_t windows[MAX_WINDOWS];
    uint64_t count = mos_window_list(windows, MAX_WINDOWS);
    focused_id = 0;
    for (uint64_t i = 0; i < count; i++) {
        cached_window_t* e = cache_for(windows[i].id);
        if (windows[i].flags & SYSCALL_WINDOW_FOCUSED) focused_id = windows[i].id;
        if (e) refresh_frame(e, &windows[i]);
    }
    update_cursor_appearance(windows, count);
    render(windows, count);
    return harness_fb;
}

/* Loads cursor `status` through the real mosWM code path (pack lookup, .cur /
 * .ani decoding, 32x32 size selection) and returns the decoded sprite. */
int wm_cursor_sprite(uint32_t status, const uint32_t** px, uint32_t* w, uint32_t* h,
                     uint32_t* hx, uint32_t* hy, uint32_t* frames, uint32_t* delay_ms) {
    mos_graphics_info(&display);
    load_desktop_settings();
    cached_cursor_status = UINT32_MAX; cached_cursor_pack[0] = 0;
    status_message[0] = 0;
    ensure_cursor_loaded(NULL, status);
    if (!cursor_width || !cursor_height) return 0;
    *px = cursor_animation_pixels ? cursor_animation_pixels : cursor_pixels;
    *w = cursor_width; *h = cursor_height; *hx = cursor_hotspot_x; *hy = cursor_hotspot_y;
    *frames = cursor_animation_pixels ? cursor_animation_frame_count : 1u;
    *delay_ms = cursor_animation_pixels ? cursor_animation_frame_delay_ms : 0u;
    return 1;
}
