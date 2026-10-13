#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "window.h"
extern uint32_t* settings_draw(uint32_t w, uint32_t h, uint32_t cat, uint32_t accent);
extern uint32_t* wm_frame(int menu_open, int cx, int cy, const char* status);
extern int wm_cursor_sprite(uint32_t status, const uint32_t** px, uint32_t* w, uint32_t* h,
                            uint32_t* hx, uint32_t* hy, uint32_t* frames, uint32_t* delay_ms);
extern uint32_t harness_cursor_status; extern uint64_t harness_uptime_ms;
extern syscall_window_info_t harness_windows[8];
extern int harness_nwin;
extern uint32_t* harness_frames[8];
extern uint32_t harness_w, harness_h;
static void save(const char* path, const uint32_t* px, uint32_t w, uint32_t h) {
    FILE* o = fopen(path, "wb"); fprintf(o, "P6 %u %u 255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) { fputc((px[i] >> 16) & 255, o); fputc((px[i] >> 8) & 255, o); fputc(px[i] & 255, o); }
    fclose(o);
}
int main(int argc, char** argv) {
    int menu = argc > 2 && atoi(argv[2]);
    const char* out = argc > 1 ? argv[1] : "desktop.ppm";
    harness_w = 1280; harness_h = 720;
    syscall_window_style_t st = { .title_bar = 0xFF263945u, .title_text = 0xFFF4F7F8u, .close_button = 0xFF263945u,
        .minimize_button = 0xFF263945u, .maximize_button = 0xFF263945u, .close_icon = 0xFFF4F7F8u,
        .minimize_icon = 0xFFF4F7F8u, .maximize_icon = 0xFFF4F7F8u, .border = 0xFF536773u, .content_background = 0xFFF3F5F6u };
    uint32_t* a = settings_draw(520, 300, 0, 0xFF59A6D8u);
    static uint32_t fa[520 * 300], fb[520 * 300];
    memcpy(fa, a, sizeof fa);
    uint32_t* b = settings_draw(520, 300, 1, 0xFF59A6D8u);
    memcpy(fb, b, sizeof fb);
    harness_nwin = 2;
    syscall_window_info_t w0 = { .id = 1, .frame_version = 1, .flags = SYSCALL_WINDOW_VISIBLE, .x = 470, .y = 190, .width = 520, .height = 300, .style = st };
    strcpy(w0.title, "MOSdesk Settings - Mouse");
    syscall_window_info_t w1 = { .id = 2, .frame_version = 1, .flags = SYSCALL_WINDOW_VISIBLE | SYSCALL_WINDOW_FOCUSED | SYSCALL_WINDOW_RESIZABLE, .x = 140, .y = 80, .width = 520, .height = 300, .style = st };
    strcpy(w1.title, "MOSdesk Settings");
    harness_windows[0] = w0; harness_frames[0] = fb;
    harness_windows[1] = w1; harness_frames[1] = fa;
    if (argc > 2 && !strcmp(argv[2], "cursors")) {
        /* argv[1] = output directory.  Writes sprites.bin + ctx_<status>.ppm */
        static const struct { int x, y; } at[14] = {
            {930, 150}, {520, 262}, {520, 262}, {352, 179}, {376, 221}, {350, 301}, {331, 340},
            {330, 94}, {289, 262}, {620, 170}, {470, 152}, {655, 404}, {520, 339}, {430, 339} };
        char path[512]; snprintf(path, sizeof path, "%s/sprites.bin", argv[1]);
        FILE* sp = fopen(path, "wb");
        for (uint32_t s = 0; s < 14; s++) {
            const uint32_t* px; uint32_t w, h, hx, hy, n, d;
            uint32_t hdr[8] = {s, 0, 0, 0, 0, 0, 0, 0};
            if (wm_cursor_sprite(s, &px, &w, &h, &hx, &hy, &n, &d)) {
                hdr[1] = w; hdr[2] = h; hdr[3] = hx; hdr[4] = hy; hdr[5] = n; hdr[6] = d;
                fwrite(hdr, 4, 8, sp); fwrite(px, 4, (size_t)n * 32 * 32, sp);
            } else { fwrite(hdr, 4, 8, sp); }
        }
        fclose(sp);
        for (uint32_t s = 0; s < 14; s++) {
            harness_cursor_status = s; harness_uptime_ms = 0;
            /* the WM overrides the status for title bar / corner hits itself */
            uint32_t* f = wm_frame(0, at[s].x, at[s].y, NULL);
            snprintf(path, sizeof path, "%s/ctx_%02u.ppm", argv[1], s);
            save(path, f, harness_w, harness_h);
        }
        printf("cursor dump written to %s\n", argv[1]);
        return 0;
    }
    uint32_t* fbp = wm_frame(menu, 700, 330, NULL);
    save(out, fbp, harness_w, harness_h);
    printf("wrote %s\n", out);
    return 0;
}
