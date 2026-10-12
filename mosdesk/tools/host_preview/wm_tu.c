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
