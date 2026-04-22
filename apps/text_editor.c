

#include <apps/text_editor.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <drivers/framebuffer.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <proc/process.h>
#include <lib/string.h>
#include <kernel/timer.h>
#include <types.h>

#define ED_W          560
#define ED_H          400
#define ED_TOOLBAR_H   28
#define ED_MAX_LINES  200
#define ED_MAX_COLS   120
#define ED_LNUM_W      32   

static window_t *ed_win = NULL;

static char   ed_lines[ED_MAX_LINES][ED_MAX_COLS + 1];
static int    ed_line_count = 1;
static int    ed_cur_row    = 0;
static int    ed_cur_col    = 0;
static int    ed_scroll_row = 0;
static bool   ed_modified   = false;
static char   ed_filepath[256];


static bool   ed_prompt_active = false;
static bool   ed_prompt_save   = false;
static char   ed_prompt_buf[256];
static int    ed_prompt_len    = 0;

static void ed_clear(void) {
    int i;
    for (i = 0; i < ED_MAX_LINES; i++) ed_lines[i][0] = 0;
    ed_line_count = 1;
    ed_cur_row = 0; ed_cur_col = 0;
    ed_scroll_row = 0;
    ed_modified = false;
}

static void ed_load_file(const char *path) {
    ed_clear();
    strncpy(ed_filepath, path, 255); ed_filepath[255] = 0;

    vfs_node_t *node = vfs_resolve(path);
    if (!node || (node->flags & 0x7) == VFS_DIRECTORY) return;

    uint32_t sz = node->size;
    if (sz == 0) return;
    if (sz > (uint32_t)(ED_MAX_LINES * ED_MAX_COLS)) sz = (uint32_t)(ED_MAX_LINES * ED_MAX_COLS);

    uint8_t *buf = (uint8_t *)kmalloc(sz + 1);
    if (!buf) return;
    uint32_t rd = vfs_read(node, 0, sz, buf);
    buf[rd] = 0;

    int row = 0, col = 0;
    uint32_t i;
    for (i = 0; i < rd && row < ED_MAX_LINES; i++) {
        char c = (char)buf[i];
        if (c == '\n' || c == '\r') {
            ed_lines[row][col] = 0;
            if (c == '\n') { row++; col = 0; }
        } else if (col < ED_MAX_COLS) {
            ed_lines[row][col++] = c;
            ed_lines[row][col]   = 0;
        }
    }
    ed_line_count = row + 1;
    if (ed_line_count < 1) ed_line_count = 1;
    kfree(buf);
    ed_modified = false;
}

static void ed_save_file(const char *path) {
    if (!path || !path[0]) return;
    strncpy(ed_filepath, path, 255); ed_filepath[255] = 0;

    
    uint32_t total = 0;
    int r;
    for (r = 0; r < ed_line_count; r++) total += strlen(ed_lines[r]) + 1;
    if (total == 0) total = 1;

    uint8_t *buf = (uint8_t *)kmalloc(total);
    if (!buf) return;

    uint32_t off = 0;
    for (r = 0; r < ed_line_count; r++) {
        int l = strlen(ed_lines[r]);
        memcpy(buf + off, ed_lines[r], (uint32_t)l); off += l;
        buf[off++] = '\n';
    }

    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir) { kfree(buf); return; }

    vfs_node_t *node = vfs_resolve(path);
    if (!node) { vfs_create(dir, name, 0644); node = vfs_resolve(path); }
    if (!node) { kfree(buf); return; }

    vfs_write(node, 0, off, buf);
    node->size = off;
    kfree(buf);
    ed_modified = false;
}

static void ed_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;
    int bw = win->content_w, bh = win->content_h;

    canvas_fill_rect(bx, by, bw, bh, 0x001E1E2E);

    
    canvas_fill_rect(bx, by, bw, ED_TOOLBAR_H, 0x00252535);
    canvas_fill_rounded_rect(bx + 4, by + 3, 50, 22, 4, 0x00333355);
    canvas_draw_string(bx + 8, by + 8, "Abrir", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_fill_rounded_rect(bx + 58, by + 3, 50, 22, 4, 0x00333355);
    canvas_draw_string(bx + 62, by + 8, "Salvar", 0x0000B894, COLOR_TRANSPARENT);

    
    {
        const char *fname = ed_filepath[0] ? ed_filepath : "(sem titulo)";
        canvas_draw_string(bx + 116, by + 8, fname, 0x00636E72, COLOR_TRANSPARENT);
        if (ed_modified)
            canvas_draw_string(bx + bw - 20, by + 8, "*", 0x00FD9644, COLOR_TRANSPARENT);
    }

    
    int ta_y = by + ED_TOOLBAR_H;
    int ta_h = bh - ED_TOOLBAR_H - CHAR_HEIGHT - 4;
    int vis_rows = ta_h / CHAR_HEIGHT;

    
    canvas_fill_rect(bx, ta_y, ED_LNUM_W, ta_h, 0x00171728);

    int r;
    for (r = 0; r < vis_rows; r++) {
        int ridx = ed_scroll_row + r;
        if (ridx >= ed_line_count) break;
        int ry = ta_y + r * CHAR_HEIGHT;

        
        char lnum[8]; itoa(ridx + 1, lnum, 10);
        canvas_draw_string(bx + 2, ry, lnum, 0x00444466, COLOR_TRANSPARENT);

        
        if (ridx == ed_cur_row)
            canvas_fill_rect(bx + ED_LNUM_W, ry, bw - ED_LNUM_W, CHAR_HEIGHT, 0x00252540);

        canvas_draw_string(bx + ED_LNUM_W + 4, ry, ed_lines[ridx], 0x00CCCCCC, COLOR_TRANSPARENT);

        
        if (ridx == ed_cur_row && (timer_get_ticks() / 500) % 2 == 0) {
            int cx = bx + ED_LNUM_W + 4 + ed_cur_col * CHAR_WIDTH;
            canvas_fill_rect(cx, ry, 2, CHAR_HEIGHT, 0x00FFFFFF);
        }
    }

    
    int sb_y = by + bh - CHAR_HEIGHT - 2;
    canvas_fill_rect(bx, sb_y, bw, CHAR_HEIGHT + 2, 0x00252535);
    char status[64];
    status[0] = 'L'; status[1] = 'n'; status[2] = ':';
    char tmp[12]; itoa(ed_cur_row + 1, tmp, 10);
    strcpy(status + 3, tmp);
    int sl = strlen(status);
    status[sl] = ' '; status[sl+1] = 'C'; status[sl+2] = 'o'; status[sl+3] = 'l'; status[sl+4] = ':';
    itoa(ed_cur_col + 1, status + sl + 5, 10);
    canvas_draw_string(bx + 4, sb_y + 1, status, 0x00636E72, COLOR_TRANSPARENT);
    canvas_draw_string(bx + bw - 200, sb_y + 1,
        "Ctrl+O:abrir  Ctrl+S:salvar  Ctrl+W:fechar",
        0x00444444, COLOR_TRANSPARENT);

    
    if (ed_prompt_active) {
        int ox = bx + bw/2 - 160, oy = by + bh/2 - 35;
        canvas_fill_rounded_rect(ox, oy, 320, 70, 6, 0x00263545);
        canvas_draw_rounded_rect(ox, oy, 320, 70, 6, 0x004A90D9);
        canvas_draw_string(ox + 8, oy + 8,
            ed_prompt_save ? "Salvar como:" : "Abrir arquivo:",
            0x00DFE6E9, COLOR_TRANSPARENT);
        canvas_fill_rect(ox + 8, oy + 30, 304, 20, 0x00141E26);
        canvas_draw_string(ox + 12, oy + 34, ed_prompt_buf, 0x00FFFFFF, COLOR_TRANSPARENT);
        if ((timer_get_ticks() / 500) % 2 == 0) {
            canvas_fill_rect(ox + 12 + ed_prompt_len * CHAR_WIDTH, oy + 34, 2, 14, 0x00FFFFFF);
        }
        canvas_draw_string(ox + 8, oy + 56, "Enter:confirmar  Esc:cancelar", 0x00444444, COLOR_TRANSPARENT);
    }
}

static void ed_insert_char(char c) {
    char *line = ed_lines[ed_cur_row];
    int len = strlen(line);
    if (len >= ED_MAX_COLS) return;
    memmove(line + ed_cur_col + 1, line + ed_cur_col, (uint32_t)(len - ed_cur_col + 1));
    line[ed_cur_col] = c;
    ed_cur_col++;
    ed_modified = true;
}

static void ed_delete_char(void) {
    char *line = ed_lines[ed_cur_row];
    int len = strlen(line);
    if (ed_cur_col < len) {
        memmove(line + ed_cur_col, line + ed_cur_col + 1, (uint32_t)(len - ed_cur_col));
        ed_modified = true;
    }
}

static void ed_backspace(void) {
    if (ed_cur_col > 0) {
        char *line = ed_lines[ed_cur_row];
        int len = strlen(line);
        memmove(line + ed_cur_col - 1, line + ed_cur_col, (uint32_t)(len - ed_cur_col + 1));
        ed_cur_col--;
        ed_modified = true;
    } else if (ed_cur_row > 0) {
        
        char *prev = ed_lines[ed_cur_row - 1];
        char *cur  = ed_lines[ed_cur_row];
        int plen = strlen(prev);
        int clen = strlen(cur);
        if (plen + clen <= ED_MAX_COLS) {
            memcpy(prev + plen, cur, (uint32_t)(clen + 1));
            ed_cur_col = plen;
            
            int i;
            for (i = ed_cur_row; i < ed_line_count - 1; i++)
                memcpy(ed_lines[i], ed_lines[i+1], (uint32_t)(strlen(ed_lines[i+1]) + 1));
            ed_lines[ed_line_count-1][0] = 0;
            ed_line_count--;
            ed_cur_row--;
            ed_modified = true;
        }
    }
}

static void ed_newline(void) {
    if (ed_line_count >= ED_MAX_LINES) return;
    char *line = ed_lines[ed_cur_row];
    int len = strlen(line);
    
    int i;
    for (i = ed_line_count; i > ed_cur_row + 1; i--)
        memcpy(ed_lines[i], ed_lines[i-1], (uint32_t)(strlen(ed_lines[i-1]) + 1));
    
    strncpy(ed_lines[ed_cur_row + 1], line + ed_cur_col, (uint32_t)(len - ed_cur_col));
    ed_lines[ed_cur_row + 1][len - ed_cur_col] = 0;
    line[ed_cur_col] = 0;
    ed_line_count++;
    ed_cur_row++;
    ed_cur_col = 0;
    ed_modified = true;
    
    int vis = (ed_win ? (ed_win->content_h - ED_TOOLBAR_H - CHAR_HEIGHT - 4) / CHAR_HEIGHT : 20);
    if (ed_cur_row >= ed_scroll_row + vis) ed_scroll_row = ed_cur_row - vis + 1;
}

static void ed_on_keydown(window_t *win, char key) {
    (void)win;

    if (ed_prompt_active) {
        if (key == '\n') {
            ed_prompt_buf[ed_prompt_len] = 0;
            if (ed_prompt_save) ed_save_file(ed_prompt_buf);
            else                ed_load_file(ed_prompt_buf);
            ed_prompt_active = false;
            ed_prompt_len = 0; ed_prompt_buf[0] = 0;
        } else if (key == 27) {
            ed_prompt_active = false;
            ed_prompt_len = 0; ed_prompt_buf[0] = 0;
        } else if (key == '\b') {
            if (ed_prompt_len > 0) ed_prompt_buf[--ed_prompt_len] = 0;
        } else if (key >= 32 && key < 127 && ed_prompt_len < 255) {
            ed_prompt_buf[ed_prompt_len++] = key;
            ed_prompt_buf[ed_prompt_len]   = 0;
        }
        return;
    }

    if (key == 15 ) {
        ed_prompt_active = true; ed_prompt_save = false;
        ed_prompt_len = 0;
        if (ed_filepath[0]) { strncpy(ed_prompt_buf, ed_filepath, 255); ed_prompt_len = strlen(ed_prompt_buf); }
        else { ed_prompt_buf[0] = 0; }
        return;
    }
    if (key == 19 ) {
        if (ed_filepath[0]) { ed_save_file(ed_filepath); }
        else { ed_prompt_active = true; ed_prompt_save = true; ed_prompt_len = 0; ed_prompt_buf[0] = 0; }
        return;
    }
    if (key == 23 ) {
        if (ed_win) wm_close(ed_win);
        return;
    }

    
    if (key == 16 ) {
        if (ed_cur_row > 0) {
            ed_cur_row--;
            int l = strlen(ed_lines[ed_cur_row]);
            if (ed_cur_col > l) ed_cur_col = l;
            if (ed_cur_row < ed_scroll_row) ed_scroll_row = ed_cur_row;
        }
    } else if (key == 14 ) {
        if (ed_cur_row < ed_line_count - 1) {
            ed_cur_row++;
            int l = strlen(ed_lines[ed_cur_row]);
            if (ed_cur_col > l) ed_cur_col = l;
            int vis = (ed_win->content_h - ED_TOOLBAR_H - CHAR_HEIGHT - 4) / CHAR_HEIGHT;
            if (ed_cur_row >= ed_scroll_row + vis) ed_scroll_row = ed_cur_row - vis + 1;
        }
    } else if (key == 2 ) {
        if (ed_cur_col > 0) ed_cur_col--;
        else if (ed_cur_row > 0) { ed_cur_row--; ed_cur_col = strlen(ed_lines[ed_cur_row]); }
    } else if (key == 6 ) {
        int l = strlen(ed_lines[ed_cur_row]);
        if (ed_cur_col < l) ed_cur_col++;
        else if (ed_cur_row < ed_line_count - 1) { ed_cur_row++; ed_cur_col = 0; }
    } else if (key == 1 ) {
        ed_cur_col = 0;
    } else if (key == 5 ) {
        ed_cur_col = strlen(ed_lines[ed_cur_row]);
    } else if (key == '\n') {
        ed_newline();
    } else if (key == '\b') {
        ed_backspace();
    } else if (key == 127 ) {
        ed_delete_char();
    } else if ((unsigned char)key >= 32 && key != 127) {
        ed_insert_char(key);
    }
}

void text_editor_open(void) {
    text_editor_open_file("");
}

void text_editor_open_file(const char *path) {
    if (ed_win && ed_win->used) {
        wm_focus(ed_win);
        if (path && path[0]) ed_load_file(path);
        return;
    }

    ed_clear();
    ed_filepath[0] = 0;
    if (path && path[0]) ed_load_file(path);

    ed_win = wm_create("Editor de Texto",
                        fb.width/2 - ED_W/2, fb.height/2 - ED_H/2,
                        ED_W, ED_H, WIN_RESIZABLE);
    if (!ed_win) return;
    ed_win->bg_color   = 0x001E1E2E;
    ed_win->on_paint   = ed_on_paint;
    ed_win->on_keydown = ed_on_keydown;

    { process_t *p = process_create_app("TextEdit", 64 * 1024);
      if (p) ed_win->proc_pid = p->pid; }
}
