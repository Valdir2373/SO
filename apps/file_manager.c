

#include <apps/file_manager.h>
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

#define FM_W            480
#define FM_H            360
#define FM_TOOLBAR_H     32
#define FM_ITEM_H        20
#define FM_MAX_ENTRIES   64

#define FM_COL_DIR   0x004A90D9
#define FM_COL_FILE  0x00DFE6E9
#define FM_COL_SEL   0x000984E3

static window_t *fm_win = NULL;

static char     fm_cwd[256];
static char     fm_names[FM_MAX_ENTRIES][256];
static uint8_t  fm_is_dir[FM_MAX_ENTRIES];
static int      fm_count   = 0;
static int      fm_sel     = 0;
static int      fm_scroll  = 0;


static bool     fm_input_active = false;
static char     fm_input_buf[128];
static int      fm_input_len    = 0;

static void fm_load_dir(const char *path) {
    strncpy(fm_cwd, path, 255); fm_cwd[255] = 0;
    fm_count  = 0;
    fm_sel    = 0;
    fm_scroll = 0;

    vfs_node_t *dir = vfs_resolve(path);
    if (!dir || (dir->flags & 0x7) != VFS_DIRECTORY) return;

    uint32_t i = 0;
    dirent_t *e;
    while ((e = vfs_readdir(dir, i++)) != 0 && fm_count < FM_MAX_ENTRIES) {
        if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) { kfree(e); continue; }
        strncpy(fm_names[fm_count], e->name, 255);
        fm_names[fm_count][255] = 0;
        fm_is_dir[fm_count] = (e->type == VFS_DIRECTORY) ? 1 : 0;
        fm_count++;
        kfree(e);
    }
}

static void fm_go_up(void) {
    int l = strlen(fm_cwd);
    if (l <= 1) return;
    if (fm_cwd[l-1] == '/') l--;
    while (l > 0 && fm_cwd[l-1] != '/') l--;
    if (l == 0) l = 1;
    fm_cwd[l] = 0;
    fm_load_dir(fm_cwd);
}

static void fm_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;
    int bw = win->content_w, bh = win->content_h;

    canvas_fill_rect(bx, by, bw, bh, 0x001A2533);

    
    canvas_fill_rect(bx, by, bw, FM_TOOLBAR_H, 0x00263545);
    canvas_fill_rounded_rect(bx + 4, by + 4, 44, 24, 4, 0x00333333);
    canvas_draw_string(bx + 10, by + 10, "< Up", 0x00DFE6E9, COLOR_TRANSPARENT);
    canvas_fill_rounded_rect(bx + 52, by + 4, 44, 24, 4, 0x00333333);
    canvas_draw_string(bx + 58, by + 10, "+ Dir", 0x0000B894, COLOR_TRANSPARENT);
    canvas_fill_rounded_rect(bx + 100, by + 4, 44, 24, 4, 0x00333333);
    canvas_draw_string(bx + 106, by + 10, "Del", 0x00D63031, COLOR_TRANSPARENT);
    canvas_fill_rounded_rect(bx + 148, by + 4, 44, 24, 4, 0x00333333);
    canvas_draw_string(bx + 152, by + 10, "Edit", 0x004A90D9, COLOR_TRANSPARENT);

    
    int pw = bw - 200;
    canvas_fill_rect(bx + 196, by + 6, pw, 20, 0x00141E26);
    canvas_draw_rect(bx + 196, by + 6, pw, 20, 0x00636E72);
    canvas_draw_string(bx + 200, by + 10, fm_cwd, 0x00636E72, COLOR_TRANSPARENT);

    
    int ly = by + FM_TOOLBAR_H;
    int vis = (bh - FM_TOOLBAR_H) / FM_ITEM_H;
    int i;
    for (i = 0; i < vis && (fm_scroll + i) < fm_count; i++) {
        int idx = fm_scroll + i;
        int iy  = ly + i * FM_ITEM_H;
        if (idx == fm_sel) canvas_fill_rect(bx, iy, bw, FM_ITEM_H, FM_COL_SEL);
        else if (i & 1)    canvas_fill_rect(bx, iy, bw, FM_ITEM_H, 0x001E2D3E);
        else               canvas_fill_rect(bx, iy, bw, FM_ITEM_H, 0x001A2533);

        uint32_t fc = fm_is_dir[idx] ? FM_COL_DIR : FM_COL_FILE;
        canvas_draw_string(bx + 20, iy + 2, fm_is_dir[idx] ? "[DIR]" : "     ", fc, COLOR_TRANSPARENT);
        canvas_draw_string(bx + 72, iy + 2, fm_names[idx], fc, COLOR_TRANSPARENT);
    }

    
    if (fm_count == 0) {
        canvas_draw_string(bx + bw/2 - 40, by + FM_TOOLBAR_H + 60,
                           "(vazio)", 0x00636E72, COLOR_TRANSPARENT);
    }

    
    if (fm_input_active) {
        int ox = bx + bw/2 - 140, oy = by + bh/2 - 30;
        canvas_fill_rounded_rect(ox, oy, 280, 60, 6, 0x00263545);
        canvas_draw_rounded_rect(ox, oy, 280, 60, 6, 0x004A90D9);
        canvas_draw_string(ox + 8, oy + 8, "Nome do diretorio:", 0x00DFE6E9, COLOR_TRANSPARENT);
        canvas_fill_rect(ox + 8, oy + 28, 264, 20, 0x00141E26);
        canvas_draw_string(ox + 12, oy + 32, fm_input_buf, 0x00FFFFFF, COLOR_TRANSPARENT);
        if ((timer_get_ticks() / 500) % 2 == 0) {
            canvas_fill_rect(ox + 12 + fm_input_len * CHAR_WIDTH, oy + 32, 2, 14, 0x00FFFFFF);
        }
    }

    
    canvas_draw_string(bx + 4, by + bh - CHAR_HEIGHT - 2,
        "Setas:navegar  Enter:abrir  Backspace:voltar  F2:novo dir  Del:apagar  E:editar",
        0x00444444, COLOR_TRANSPARENT);
}

static void fm_on_keydown(window_t *win, char key) {
    (void)win;

    if (fm_input_active) {
        if (key == '\n') {
            if (fm_input_len > 0) {
                fm_input_buf[fm_input_len] = 0;
                char newpath[256];
                int clen = strlen(fm_cwd);
                strncpy(newpath, fm_cwd, 255);
                if (clen > 1 && newpath[clen-1] != '/') { newpath[clen]='/'; newpath[clen+1]=0; }
                strncat(newpath, fm_input_buf, 255 - strlen(newpath));
                char name[256];
                vfs_node_t *dir = vfs_resolve_parent(newpath, name);
                if (dir && name[0]) vfs_mkdir(dir, name, 0755);
                fm_load_dir(fm_cwd);
            }
            fm_input_active = false;
            fm_input_len = 0; fm_input_buf[0] = 0;
        } else if (key == 27) {
            fm_input_active = false;
            fm_input_len = 0; fm_input_buf[0] = 0;
        } else if (key == '\b') {
            if (fm_input_len > 0) fm_input_buf[--fm_input_len] = 0;
        } else if (key >= 32 && key < 127 && fm_input_len < 127) {
            fm_input_buf[fm_input_len++] = key;
            fm_input_buf[fm_input_len]   = 0;
        }
        return;
    }

    if (key == 'j' || key == 2) {
        if (fm_sel < fm_count - 1) {
            fm_sel++;
            int vis = (win->content_h - FM_TOOLBAR_H) / FM_ITEM_H;
            if (fm_sel >= fm_scroll + vis) fm_scroll++;
        }
    } else if (key == 'k' || key == 16 ) {
        if (fm_sel > 0) {
            fm_sel--;
            if (fm_sel < fm_scroll) fm_scroll = fm_sel;
        }
    } else if (key == '\n') {
        
        if (fm_sel >= 0 && fm_sel < fm_count) {
            if (fm_is_dir[fm_sel]) {
                int clen = strlen(fm_cwd);
                char newpath[256];
                strncpy(newpath, fm_cwd, 255);
                if (clen > 1 && newpath[clen-1] != '/') { newpath[clen]='/'; newpath[clen+1]=0; }
                strncat(newpath, fm_names[fm_sel], 255 - strlen(newpath));
                fm_load_dir(newpath);
            } else {
                
                char fpath[256];
                int clen = strlen(fm_cwd);
                strncpy(fpath, fm_cwd, 255);
                if (clen > 1 && fpath[clen-1] != '/') { fpath[clen]='/'; fpath[clen+1]=0; }
                strncat(fpath, fm_names[fm_sel], 255 - strlen(fpath));
                text_editor_open_file(fpath);
            }
        }
    } else if (key == '\b') {
        fm_go_up();
    } else if (key == 6  || key == 'n') {
        fm_input_active = true;
        fm_input_len = 0; fm_input_buf[0] = 0;
    } else if (key == 4  || key == 127) {
        if (fm_sel >= 0 && fm_sel < fm_count) {
            char dpath[256];
            int clen = strlen(fm_cwd);
            strncpy(dpath, fm_cwd, 255);
            if (clen > 1 && dpath[clen-1] != '/') { dpath[clen]='/'; dpath[clen+1]=0; }
            strncat(dpath, fm_names[fm_sel], 255 - strlen(dpath));
            char name[256];
            vfs_node_t *dir = vfs_resolve_parent(dpath, name);
            if (dir && name[0]) vfs_unlink(dir, name);
            fm_load_dir(fm_cwd);
        }
    } else if (key == 'e' || key == 5 ) {
        if (fm_sel >= 0 && fm_sel < fm_count && !fm_is_dir[fm_sel]) {
            char fpath[256];
            int clen = strlen(fm_cwd);
            strncpy(fpath, fm_cwd, 255);
            if (clen > 1 && fpath[clen-1] != '/') { fpath[clen]='/'; fpath[clen+1]=0; }
            strncat(fpath, fm_names[fm_sel], 255 - strlen(fpath));
            text_editor_open_file(fpath);
        }
    }
}

void file_manager_open(void) {
    if (fm_win && fm_win->used) {
        wm_focus(fm_win);
        return;
    }

    fm_load_dir("/");
    fm_input_active = false;
    fm_input_len    = 0;
    fm_input_buf[0] = 0;

    fm_win = wm_create("Gerenciador de Arquivos",
                        fb.width/2 - FM_W/2, fb.height/2 - FM_H/2,
                        FM_W, FM_H, WIN_RESIZABLE);
    if (!fm_win) return;
    fm_win->bg_color   = 0x001A2533;
    fm_win->on_paint   = fm_on_paint;
    fm_win->on_keydown = fm_on_keydown;

    { process_t *p = process_create_app("FileMgr", 64 * 1024);
      if (p) fm_win->proc_pid = p->pid; }
}
