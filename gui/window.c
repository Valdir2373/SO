

#include <gui/window.h>
#include <gui/canvas.h>
#include <drivers/framebuffer.h>
#include <proc/process.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>

static window_t windows[MAX_WINDOWS];
static window_t *wm_bottom = 0;
static window_t *wm_top    = 0;
static window_t *wm_focused = 0;
static uint32_t  next_wid   = 1;


static int     mouse_x    = 400;
static int     mouse_y    = 300;
static bool    mouse_ldown = false;




static void update_content(window_t *win) {
    win->content_x = win->x + BORDER_WIDTH;
    win->content_y = win->y + ((win->flags & WIN_NO_TITLEBAR)
                                ? BORDER_WIDTH : TITLEBAR_HEIGHT);
    win->content_w = win->w - 2 * BORDER_WIDTH;
    win->content_h = win->h - ((win->flags & WIN_NO_TITLEBAR)
                                ? 2 * BORDER_WIDTH
                                : TITLEBAR_HEIGHT + BORDER_WIDTH);
    if (win->content_w < 0) win->content_w = 0;
    if (win->content_h < 0) win->content_h = 0;
}


static uint8_t hit_resize_dir(window_t *win, int x, int y) {
    if (!(win->flags & WIN_RESIZABLE)) return RESIZE_NONE;
    if (win->flags & WIN_MAXIMIZED)   return RESIZE_NONE;

    int ex = win->x + win->w;
    int ey = win->y + win->h;
    bool on_l = (x >= win->x && x < win->x + RESIZE_BORDER);
    bool on_r = (x >= ex - RESIZE_BORDER && x < ex);
    bool on_b = (y >= ey - RESIZE_BORDER && y < ey);
    bool in_w = (x >= win->x && x < ex && y >= win->y && y < ey);

    if (!in_w) return RESIZE_NONE;
    if (on_b && on_l) return RESIZE_BL;
    if (on_b && on_r) return RESIZE_BR;
    if (on_b)         return RESIZE_BOTTOM;
    
    if (y > win->y + TITLEBAR_HEIGHT) {
        if (on_l) return RESIZE_LEFT;
        if (on_r) return RESIZE_RIGHT;
    }
    return RESIZE_NONE;
}

static void wm_draw_titlebar(window_t *win) {
    bool focused = (win == wm_focused);
    uint32_t tb_color = focused ? KRYPX_TITLEBAR : KRYPX_TITLEBAR_IN;

    canvas_fill_rect(win->x, win->y, win->w, TITLEBAR_HEIGHT, tb_color);

    
    int tw = canvas_string_width(win->title);
    int tx = win->x + (win->w - tw) / 2;
    if (tx < win->x + 60) tx = win->x + 60;
    canvas_draw_string(tx, win->y + 6, win->title, KRYPX_TEXT, COLOR_TRANSPARENT);

    int by_btn = win->y + (TITLEBAR_HEIGHT - BTN_SIZE) / 2;

    
    int bx = win->x + win->w - BTN_SIZE - BTN_MARGIN;
    canvas_fill_rounded_rect(bx, by_btn, BTN_SIZE, BTN_SIZE, 3, 0x00E74C3C);
    canvas_draw_string(bx + 3, by_btn + 1, "x", COLOR_WHITE, COLOR_TRANSPARENT);

    
    bx -= BTN_SIZE + 4;
    canvas_fill_rounded_rect(bx, by_btn, BTN_SIZE, BTN_SIZE, 3, 0x00F39C12);
    canvas_draw_string(bx + 3, by_btn + 1, "-", COLOR_WHITE, COLOR_TRANSPARENT);

    
    bx -= BTN_SIZE + 4;
    uint32_t max_col = (win->flags & WIN_MAXIMIZED) ? 0x00229954 : 0x0027AE60;
    canvas_fill_rounded_rect(bx, by_btn, BTN_SIZE, BTN_SIZE, 3, max_col);
    canvas_draw_string(bx + 3, by_btn + 1,
                       (win->flags & WIN_MAXIMIZED) ? "v" : "^",
                       COLOR_WHITE, COLOR_TRANSPARENT);
}

static void wm_draw_border(window_t *win) {
    bool focused = (win == wm_focused);
    uint32_t bc = focused ? KRYPX_TITLEBAR : 0x00444444;
    canvas_draw_rect(win->x, win->y, win->w, win->h, bc);
    canvas_draw_rect(win->x + 1, win->y + 1, win->w - 2, win->h - 2, 0x00222222);

    
    if (win->flags & WIN_RESIZABLE && !(win->flags & WIN_MAXIMIZED)) {
        uint8_t rd = hit_resize_dir(win, mouse_x, mouse_y);
        if (rd != RESIZE_NONE) {
            uint32_t rc = 0x0074B9FF;
            
            if (rd == RESIZE_LEFT || rd == RESIZE_BL)
                canvas_fill_rect(win->x, win->y + TITLEBAR_HEIGHT,
                                 RESIZE_BORDER, win->h - TITLEBAR_HEIGHT, rc & 0x00333355);
            if (rd == RESIZE_RIGHT || rd == RESIZE_BR)
                canvas_fill_rect(win->x + win->w - RESIZE_BORDER,
                                 win->y + TITLEBAR_HEIGHT,
                                 RESIZE_BORDER, win->h - TITLEBAR_HEIGHT, rc & 0x00333355);
            if (rd == RESIZE_BOTTOM || rd == RESIZE_BL || rd == RESIZE_BR)
                canvas_fill_rect(win->x, win->y + win->h - RESIZE_BORDER,
                                 win->w, RESIZE_BORDER, rc & 0x00333355);
        }
    }
}

static void wm_draw_window(window_t *win) {
    if (!win->used || !(win->flags & WIN_VISIBLE)) return;
    if (win->flags & WIN_MINIMIZED) return;

    canvas_fill_rect(win->content_x, win->content_y,
                     win->content_w, win->content_h, win->bg_color);

    if (win->on_paint) win->on_paint(win);

    if (!(win->flags & WIN_NO_TITLEBAR)) wm_draw_titlebar(win);
    wm_draw_border(win);
}

static void draw_cursor(int x, int y) {
    
    int i;
    
    for (i = 0; i < 13; i++) {
        int len = 13 - i;
        canvas_fill_rect(x + 1, y + i + 1, len, 1, 0x00000000);
    }
    
    canvas_fill_rect(x + 4 + 1, y + 8 + 1, 3, 4, 0x00000000);

    
    for (i = 0; i < 13; i++) {
        int len = 13 - i;
        canvas_fill_rect(x, y + i, len, 1, COLOR_WHITE);
    }
    
    canvas_fill_rect(x + 4, y + 8, 3, 4, COLOR_WHITE);
}



void wm_init(void) {
    uint32_t i;
    for (i = 0; i < MAX_WINDOWS; i++) windows[i].used = false;
    wm_bottom = wm_top = wm_focused = 0;
}

window_t *wm_create(const char *title, int x, int y, int w, int h, uint32_t flags) {
    uint32_t i;
    window_t *win = 0;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].used) { win = &windows[i]; break; }
    }
    if (!win) return 0;

    memset(win, 0, sizeof(window_t));
    win->id       = next_wid++;
    win->flags    = flags | WIN_VISIBLE;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->bg_color = KRYPX_BG;
    strncpy(win->title, title, 127);

    update_content(win);
    win->used = true;

    
    win->below = wm_top;
    win->above = 0;
    if (wm_top) wm_top->above = win;
    wm_top = win;
    if (!wm_bottom) wm_bottom = win;

    wm_focused = win;
    return win;
}

void wm_close(window_t *win) {
    if (!win || !win->used) return;

    
    if (win->proc_pid != 0) {
        process_kill(win->proc_pid);
        win->proc_pid = 0;
    }

    
    if (win->above) win->above->below = win->below;
    else            wm_top            = win->below;
    if (win->below) win->below->above = win->above;
    else            wm_bottom         = win->above;

    if (wm_focused == win) wm_focused = wm_top;

    if (win->on_close) win->on_close(win);
    win->used = false;
}

void wm_focus(window_t *win) {
    if (!win || !win->used || win == wm_top) {
        if (win) wm_focused = win;
        return;
    }

    if (win->above) win->above->below = win->below;
    else wm_top = win->below;
    if (win->below) win->below->above = win->above;
    else wm_bottom = win->above;

    win->below = wm_top;
    win->above = 0;
    if (wm_top) wm_top->above = win;
    wm_top = win;
    if (!wm_bottom) wm_bottom = win;

    wm_focused = win;
}

void wm_render(void) {
    if (!fb.ready) return;
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    window_t *w = wm_bottom;
    while (w) {
        wm_draw_window(w);
        w = w->above;
    }

    draw_cursor(mouse_x, mouse_y);
    fb_swap();
}

window_t *wm_window_at(int x, int y) {
    window_t *w = wm_top;
    while (w) {
        if (w->used && (w->flags & WIN_VISIBLE) && !(w->flags & WIN_MINIMIZED)) {
            if (x >= w->x && x < w->x + w->w &&
                y >= w->y && y < w->y + w->h) return w;
        }
        w = w->below;
    }
    return 0;
}

void wm_invalidate(window_t *win) {
    (void)win;
    wm_render();
}

int wm_mouse_x(void) { return mouse_x; }
int wm_mouse_y(void) { return mouse_y; }

void wm_mouse_move(int x, int y) {
    mouse_x = x;
    mouse_y = y;

    if (!mouse_ldown || !wm_focused) return;

    if (wm_focused->drag_active) {
        
        wm_focused->x = x - wm_focused->drag_off_x;
        wm_focused->y = y - wm_focused->drag_off_y;
        
        if (wm_focused->y < 0) wm_focused->y = 0;
        update_content(wm_focused);

    } else if (wm_focused->resize_active) {
        int orig_x = wm_focused->x;
        int orig_w = wm_focused->w;

        switch (wm_focused->resize_dir) {
            case RESIZE_RIGHT:
                wm_focused->w = x - wm_focused->x;
                break;
            case RESIZE_BOTTOM:
                wm_focused->h = y - wm_focused->y;
                break;
            case RESIZE_LEFT: {
                int new_w = (orig_x + orig_w) - x;
                if (new_w >= 120) { wm_focused->x = x; wm_focused->w = new_w; }
                break;
            }
            case RESIZE_BL: {
                int new_w = (orig_x + orig_w) - x;
                if (new_w >= 120) { wm_focused->x = x; wm_focused->w = new_w; }
                wm_focused->h = y - wm_focused->y;
                break;
            }
            case RESIZE_BR:
                wm_focused->w = x - wm_focused->x;
                wm_focused->h = y - wm_focused->y;
                break;
        }
        if (wm_focused->w < 120) wm_focused->w = 120;
        if (wm_focused->h < 80)  wm_focused->h = 80;
        update_content(wm_focused);
    }
}

void wm_mouse_down(int x, int y, uint8_t btn) {
    if (btn != 0) return;
    mouse_ldown = true;

    window_t *win = wm_window_at(x, y);
    if (!win) return;

    wm_focus(win);

    
    uint8_t rdir = hit_resize_dir(win, x, y);
    if (rdir != RESIZE_NONE) {
        win->resize_active = true;
        win->resize_dir    = rdir;
        return;
    }

    
    if (!(win->flags & WIN_NO_TITLEBAR) &&
        y >= win->y && y < win->y + TITLEBAR_HEIGHT) {

        int by_btn = win->y + (TITLEBAR_HEIGHT - BTN_SIZE) / 2;

        
        int bx = win->x + win->w - BTN_SIZE - BTN_MARGIN;
        if (x >= bx && x < bx + BTN_SIZE && y >= by_btn && y < by_btn + BTN_SIZE) {
            wm_close(win);
            return;
        }

        
        bx -= BTN_SIZE + 4;
        if (x >= bx && x < bx + BTN_SIZE && y >= by_btn && y < by_btn + BTN_SIZE) {
            win->flags |= WIN_MINIMIZED;
            
            if (wm_focused == win) {
                window_t *next = win->below;
                while (next && (next->flags & WIN_MINIMIZED)) next = next->below;
                wm_focused = next ? next : wm_top;
            }
            return;
        }

        
        bx -= BTN_SIZE + 4;
        if (x >= bx && x < bx + BTN_SIZE && y >= by_btn && y < by_btn + BTN_SIZE) {
            if (win->flags & WIN_MAXIMIZED) {
                
                win->x = win->save_x;
                win->y = win->save_y;
                win->w = win->save_w;
                win->h = win->save_h;
                win->flags &= ~WIN_MAXIMIZED;
            } else {
                
                win->save_x = win->x;
                win->save_y = win->y;
                win->save_w = win->w;
                win->save_h = win->h;
                win->x = 0;
                win->y = 0;
                win->w = (int)fb.width;
                win->h = (int)fb.height - 40;  
                win->flags |= WIN_MAXIMIZED;
            }
            update_content(win);
            return;
        }


        win->drag_active = true;
        win->drag_off_x  = x - win->x;
        win->drag_off_y  = y - win->y;
        return;
    }

    
    if (x >= win->content_x && x < win->content_x + win->content_w &&
        y >= win->content_y && y < win->content_y + win->content_h) {
        if (win->on_click)
            win->on_click(win, x, y);
    }
}

void wm_mouse_up(int x, int y, uint8_t btn) {
    (void)x; (void)y;
    if (btn == 0) {
        mouse_ldown = false;
        if (wm_focused) {
            wm_focused->drag_active   = false;
            wm_focused->resize_active = false;
        }
    }
}

void wm_key_event(char key) {
    if (wm_focused && wm_focused->on_keydown) {
        wm_focused->on_keydown(wm_focused, key);
    }
}



void wm_draw_taskbar_entries(int x, int y, int bw, int bh, int max_x) {
    window_t *w = wm_bottom;
    while (w) {
        if (w->used && (w->flags & WIN_MINIMIZED)) {
            if (x + bw > max_x) break;
            bool hov = (mouse_x >= x && mouse_x < x + bw &&
                        mouse_y >= y && mouse_y < y + bh);
            uint32_t col = hov ? 0x00555566 : 0x00333344;
            canvas_fill_rounded_rect(x, y, bw, bh, 4, col);
            canvas_draw_rounded_rect(x, y, bw, bh, 4, 0x00555555);
            
            canvas_draw_string(x + 6, y + (bh - CHAR_HEIGHT) / 2,
                               w->title, 0x00DFE6E9, COLOR_TRANSPARENT);
            x += bw + 4;
        }
        w = w->above;
    }
}

bool wm_taskbar_entry_click(int mx, int my, int x, int y, int bw, int bh, int max_x) {
    window_t *w = wm_bottom;
    while (w) {
        if (w->used && (w->flags & WIN_MINIMIZED)) {
            if (x + bw > max_x) break;
            if (mx >= x && mx < x + bw && my >= y && my < y + bh) {
                w->flags &= ~WIN_MINIMIZED;
                wm_focus(w);
                return true;
            }
            x += bw + 4;
        }
        w = w->above;
    }
    return false;
}
