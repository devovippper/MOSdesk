/* Real mosWM compositor, with hooks to drive input and render frames. */
#define main wm_main
#include "mosWM/main.c"
#undef main
extern uint32_t* harness_fb;
void a_init(uint32_t auth) {
    mos_graphics_info(&display);
    canvas = wm_malloc((size_t)display.width * display.height * 4);
    wallpaper = wm_malloc((size_t)display.width * display.height * 4);
    cursor_x = (int32_t)display.width / 2; cursor_y = (int32_t)display.height / 2;
    load_desktop_settings();
    load_desktop_apps();
    auth_state = auth;
    painted_wallpaper_style = UINT32_MAX;
    load_fonts();
}
void a_set_auth(uint32_t s) { auth_state = s; }
uint32_t a_get_auth(void) { return auth_state; }
void a_set_menu(int open) { start_menu_open = open != 0; }
int a_menu(void) { return start_menu_open; }
const char* a_auth_message(void) { return auth_message; }
void a_cursor(int32_t* x, int32_t* y) { *x = cursor_x; *y = cursor_y; }
void a_set_cursor(int32_t x, int32_t y) { cursor_x = x; cursor_y = y; }
void a_input(const syscall_input_event_t* in) {
    syscall_window_info_t windows[MAX_WINDOWS];
    uint64_t count = mos_window_list(windows, MAX_WINDOWS);
    route_input(in, windows, count);
}
uint32_t* a_render(void) {
    static unsigned poll;
    if (++poll >= 30u) { poll = 0; load_desktop_settings(); }   /* same cadence as the real main loop */
    syscall_window_info_t windows[MAX_WINDOWS];
    uint64_t count = mos_window_list(windows, MAX_WINDOWS);
    remove_missing_cache(windows, count);
    sync_window_modes(windows, count);
    focused_id = 0;
    for (uint64_t i = 0; i < count; i++) {
        cached_window_t* e = cache_for(windows[i].id);
        if (windows[i].flags & SYSCALL_WINDOW_FOCUSED) focused_id = windows[i].id;
        refresh_window_icon(e, false);
        if (e) refresh_frame(e, &windows[i]);
    }
    update_cursor_appearance(windows, count);
    render(windows, count);
    return harness_fb;
}
