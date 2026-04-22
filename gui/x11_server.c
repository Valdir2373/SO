
#include <gui/x11_server.h>
#include <compat/linux_compat64.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <gui/canvas.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <mm/heap.h>
#include <kernel/timer.h>
#include <types.h>


#define X11_MAX_WINDOWS   64
#define X11_MAX_GCS       64
#define X11_MAX_ATOMS    512
#define X11_RESOURCE_BASE 0x00200000UL
#define X11_RESOURCE_MASK 0x001FFFFFUL


static uint32_t g_next_xid = X11_RESOURCE_BASE | 0x10;

static uint32_t alloc_xid(void) { return g_next_xid++; }


typedef struct {
    uint32_t id;
    int32_t  x, y;
    uint32_t w, h;
    uint32_t border;
    bool     mapped;
    uint32_t bg_pixel;
    uint32_t event_mask;
    uint32_t parent;
} x11_win_t;

static x11_win_t g_wins[X11_MAX_WINDOWS];
static int       g_nwins = 0;

static x11_win_t *find_win(uint32_t id) {
    int i;
    for (i = 0; i < g_nwins; i++) if (g_wins[i].id == id) return &g_wins[i];
    return 0;
}
static x11_win_t *alloc_win(uint32_t id) {
    if (g_nwins >= X11_MAX_WINDOWS) return 0;
    memset(&g_wins[g_nwins], 0, sizeof(x11_win_t));
    g_wins[g_nwins].id = id;
    return &g_wins[g_nwins++];
}


#define X11_MAX_PIXMAPS   32
#define X11_PIXMAP_MAX_SZ (4*1024*1024)  

typedef struct {
    uint32_t  id;
    uint16_t  w, h;
    uint8_t   depth;
    bool      used;
    uint32_t *pixels;   
} x11_pixmap_t;

static x11_pixmap_t g_pixmaps[X11_MAX_PIXMAPS];
static int          g_npixmaps = 0;

static x11_pixmap_t *find_pixmap(uint32_t id) {
    int i;
    for (i = 0; i < g_npixmaps; i++) if (g_pixmaps[i].id == id && g_pixmaps[i].used) return &g_pixmaps[i];
    return 0;
}

static x11_pixmap_t *alloc_pixmap(uint32_t id, uint16_t w, uint16_t h, uint8_t depth) {
    if (g_npixmaps >= X11_MAX_PIXMAPS) return 0;
    x11_pixmap_t *pm = &g_pixmaps[g_npixmaps++];
    memset(pm, 0, sizeof(*pm));
    pm->id = id; pm->w = w; pm->h = h; pm->depth = depth; pm->used = true;
    uint32_t sz = (uint32_t)w * h * 4;
    pm->pixels = (sz > 0 && sz <= X11_PIXMAP_MAX_SZ) ? (uint32_t*)kmalloc(sz) : 0;
    if (pm->pixels) memset(pm->pixels, 0, sz);
    return pm;
}


#define X11_MAX_PICTURES 64

typedef struct {
    uint32_t id;
    uint32_t drawable;  
    bool     used;
} x11_pic_t;

static x11_pic_t g_pics[X11_MAX_PICTURES];
static int       g_npics = 0;

static x11_pic_t *find_pic(uint32_t id) {
    int i;
    for (i = 0; i < g_npics; i++) if (g_pics[i].id == id && g_pics[i].used) return &g_pics[i];
    return 0;
}

static x11_pic_t *alloc_pic(uint32_t id, uint32_t drawable) {
    if (g_npics >= X11_MAX_PICTURES) return 0;
    g_pics[g_npics].id = id; g_pics[g_npics].drawable = drawable; g_pics[g_npics].used = true;
    return &g_pics[g_npics++];
}


typedef struct {
    uint32_t id;
    uint32_t foreground, background;
    int      line_width;
    uint32_t font;
} x11_gc_t;

static x11_gc_t g_gcs[X11_MAX_GCS];
static int      g_ngcs = 0;

static x11_gc_t *find_gc(uint32_t id) {
    int i;
    for (i = 0; i < g_ngcs; i++) if (g_gcs[i].id == id) return &g_gcs[i];
    return 0;
}
static x11_gc_t *alloc_gc(uint32_t id) {
    if (g_ngcs >= X11_MAX_GCS) return 0;
    memset(&g_gcs[g_ngcs], 0, sizeof(x11_gc_t));
    g_gcs[g_ngcs].id = id;
    g_gcs[g_ngcs].foreground = 0xFFFFFFFF;
    g_gcs[g_ngcs].background = 0xFF000000;
    return &g_gcs[g_ngcs++];
}



static const char *predefined_atoms[] = {
    "",              
    "PRIMARY",       
    "SECONDARY",     
    "ARC",           
    "ATOM",          
    "BITMAP",        
    "CARDINAL",      
    "COLORMAP",      
    "CURSOR",        
    "CUT_BUFFER0",   
    "CUT_BUFFER1",   
    "CUT_BUFFER2",   
    "CUT_BUFFER3",   
    "CUT_BUFFER4",   
    "CUT_BUFFER5",   
    "CUT_BUFFER6",   
    "CUT_BUFFER7",   
    "DRAWABLE",      
    "FONT",          
    "INTEGER",       
    "PIXMAP",        
    "POINT",         
    "RECTANGLE",     
    "RESOURCE_MANAGER",
    "RGB_COLOR_MAP", 
    "RGB_BEST_MAP",  
    "RGB_BLUE_MAP",  
    "RGB_DEFAULT_MAP",
    "RGB_GRAY_MAP",  
    "RGB_GREEN_MAP", 
    "RGB_RED_MAP",   
    "STRING",        
    "VISUALID",      
    "WINDOW",        
    "WM_COMMAND",    
    "WM_HINTS",      
    "WM_CLIENT_MACHINE",
    "WM_ICON_NAME",  
    "WM_ICON_SIZE",  
    "WM_NAME",       
    "WM_NORMAL_HINTS",
    "WM_SIZE_HINTS", 
    "WM_ZOOM_HINTS", 
    "MIN_SPACE",     
    "NORM_SPACE",    
    "MAX_SPACE",     
    "END_SPACE",     
    "SUPERSCRIPT_X", 
    "SUPERSCRIPT_Y", 
    "SUBSCRIPT_X",   
    "SUBSCRIPT_Y",   
    "UNDERLINE_POSITION",
    "UNDERLINE_THICKNESS",
    "STRIKEOUT_ASCENT",
    "STRIKEOUT_DESCENT",
    "ITALIC_ANGLE",  
    "X_HEIGHT",      
    "QUAD_WIDTH",    
    "WEIGHT",        
    "POINT_SIZE",    
    "RESOLUTION",    
    "COPYRIGHT",     
    "NOTICE",        
    "FONT_NAME",     
    "FAMILY_NAME",   
    "FULL_NAME",     
    "CAP_HEIGHT",    
    "WM_CLASS",      
    "WM_TRANSIENT_FOR",
};
#define N_PREDEF_ATOMS 69

static char   g_atom_names[X11_MAX_ATOMS][64];
static int    g_natoms = N_PREDEF_ATOMS;

static uint32_t intern_atom(const char *name, int only_if_exists) {
    
    uint32_t i;
    for (i = 1; i < N_PREDEF_ATOMS; i++)
        if (strcmp(name, predefined_atoms[i]) == 0) return i;
    
    for (i = N_PREDEF_ATOMS; i < (uint32_t)g_natoms; i++)
        if (strcmp(name, g_atom_names[i]) == 0) return i;
    if (only_if_exists) return 0;
    if (g_natoms >= X11_MAX_ATOMS) return 0;
    strncpy(g_atom_names[g_natoms], name, 63);
    return (uint32_t)(g_natoms++);
}

static const char *atom_name(uint32_t atom) {
    if (atom == 0) return "None";
    if (atom < N_PREDEF_ATOMS) return predefined_atoms[atom];
    if (atom < (uint32_t)g_natoms) return g_atom_names[atom];
    return "";
}


static int  g_svc = -1;     
static uint32_t g_seq = 0;  


static uint8_t  g_outbuf[65536];
static uint32_t g_outlen = 0;

static void out_u8(uint8_t v)  { if (g_outlen<65535) g_outbuf[g_outlen++]=v; }
static void out_u16(uint16_t v){ out_u8((uint8_t)(v&0xFF)); out_u8((uint8_t)(v>>8)); }
static void out_u32(uint32_t v){ out_u16((uint16_t)(v&0xFFFF)); out_u16((uint16_t)(v>>16)); }
static void out_zero(uint32_t n){ uint32_t i; for(i=0;i<n;i++) out_u8(0); }
static void out_pad4(uint32_t len) {
    uint32_t p = (4 - (len & 3)) & 3;
    out_zero(p);
}
static void out_str(const char *s, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++) out_u8((uint8_t)s[i]);
}

static void flush_out(void) {
    if (g_outlen == 0 || g_svc < 0) return;
    lx64_ksvc_write(g_svc, g_outbuf, g_outlen);
    g_outlen = 0;
}


static uint8_t  g_inbuf[65536];
static uint32_t g_inlen = 0;

static uint8_t  in_u8(void)  { return (g_inlen > 0) ? g_inbuf[0] : 0; }
static uint16_t in_u16_at(const uint8_t *b) { return (uint16_t)(b[0] | (b[1]<<8)); }
static uint32_t in_u32_at(const uint8_t *b) {
    return (uint32_t)(b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24));
}


#define ROOT_WIN_ID   (X11_RESOURCE_BASE | 1)
#define ROOT_CMAP_ID  (X11_RESOURCE_BASE | 2)
#define ROOT_VISUAL   (X11_RESOURCE_BASE | 3)


static void send_connection_accepted(void) {
    extern framebuffer_t fb;
    uint16_t fw = fb.width  ? (uint16_t)fb.width  : 1024;
    uint16_t fh = fb.height ? (uint16_t)fb.height : 768;

    const char *vendor = "Krypx X Server";
    uint16_t vlen = (uint16_t)strlen(vendor);
    uint16_t vpad = (uint16_t)((4 - (vlen & 3)) & 3);

    
    uint32_t extra = 32 + vlen + vpad + 8 + 68;
    uint16_t extra_words = (uint16_t)(extra / 4);

    
    out_u8(1);               
    out_u8(0);               
    out_u16(11);             
    out_u16(0);              
    out_u16(extra_words);

    
    out_u32(1);
    out_u32(X11_RESOURCE_BASE);
    out_u32(X11_RESOURCE_MASK);
    out_u32(256);

    
    out_u16(vlen);
    out_u16(65535);

    
    out_u8(1); out_u8(1);
    out_u8(0); out_u8(0);     
    out_u8(32); out_u8(32);   
    out_u8(8); out_u8(255);   
    out_u32(0);               

    
    out_str(vendor, vlen);
    out_zero(vpad);

    
    out_u8(24); out_u8(32); out_u8(32); out_zero(5);

    
    out_u32(ROOT_WIN_ID);
    out_u32(ROOT_CMAP_ID);
    out_u32(0x00FFFFFF);   
    out_u32(0x00000000);   
    out_u32(0);            
    out_u16(fw); out_u16(fh);
    out_u16(305); out_u16(178); 
    out_u16(1); out_u16(1);     
    out_u32(ROOT_VISUAL);
    out_u8(0); out_u8(0); out_u8(24); out_u8(1); 

    
    out_u8(24); out_u8(0); out_u16(1); out_u32(0);

    
    out_u32(ROOT_VISUAL);
    out_u8(4);    
    out_u8(8);    
    out_u16(256); 
    out_u32(0x00FF0000); 
    out_u32(0x0000FF00); 
    out_u32(0x000000FF); 
    out_u32(0);

    flush_out();
}


static void send_error(uint8_t err_code, uint32_t bad_val, uint16_t minor_op, uint8_t major_op) {
    out_u8(0);         
    out_u8(err_code);
    out_u16((uint16_t)g_seq);
    out_u32(bad_val);
    out_u16(minor_op);
    out_u8(major_op);
    out_zero(21);
    flush_out();
}


static void reply_start(uint8_t extra) {
    out_u8(1);          
    out_u8(extra);
    out_u16((uint16_t)g_seq);
}




static void handle_intern_atom(const uint8_t *req, uint32_t ) {
    uint8_t only_if_exists = req[1];
    uint16_t name_len = in_u16_at(req + 4);
    const char *name = (const char*)(req + 8);
    char tmp[128];
    uint32_t n = name_len < 127 ? name_len : 127;
    memcpy(tmp, name, n); tmp[n] = '\0';
    uint32_t atom = intern_atom(tmp, only_if_exists);

    reply_start(0);
    out_u32(0);    
    out_u32(atom);
    out_zero(20);
    flush_out();
}


static void handle_get_atom_name(const uint8_t *req) {
    uint32_t atom = in_u32_at(req + 4);
    const char *name = atom_name(atom);
    uint16_t nlen = (uint16_t)strlen(name);
    uint16_t npad = (uint16_t)((4 - (nlen & 3)) & 3);

    reply_start(0);
    out_u32((uint32_t)(nlen + npad) / 4);
    out_u16(nlen);
    out_zero(22);
    out_str(name, nlen);
    out_zero(npad);
    flush_out();
}


static void handle_create_window(const uint8_t *req, uint32_t len) {
    uint32_t wid    = in_u32_at(req + 4);
    uint32_t parent = in_u32_at(req + 8);
    int32_t  x      = (int32_t)in_u16_at(req + 12);
    int32_t  y      = (int32_t)in_u16_at(req + 14);
    uint16_t w      = in_u16_at(req + 16);
    uint16_t h      = in_u16_at(req + 18);
    (void)len;
    x11_win_t *win = alloc_win(wid);
    if (!win) { send_error(5 , wid, 0, 1); return; }
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    win->parent = parent;
    win->bg_pixel = 0xFF2D3436;
    
}


static void handle_change_window_attrs(const uint8_t *req) {
    uint32_t wid  = in_u32_at(req + 4);
    uint32_t mask = in_u32_at(req + 8);
    const uint8_t *vp = req + 12;
    x11_win_t *win = find_win(wid);
    if (!win) return;
    
    uint32_t bit; const uint8_t *p = vp;
    for (bit = 0; bit < 32; bit++) {
        if (!(mask & (1u << bit))) continue;
        uint32_t v = in_u32_at(p); p += 4;
        if (bit == 1)  win->bg_pixel   = v;
        if (bit == 11) win->event_mask = v;
    }
}


static void handle_get_window_attrs(const uint8_t *req) {
    uint32_t wid = in_u32_at(req + 4);
    x11_win_t *win = find_win(wid);
    if (!win) { send_error(3 , wid, 0, 3); return; }

    reply_start(win->mapped ? 1 : 0);
    out_u32(3);               
    out_u32(ROOT_VISUAL);
    out_u16(1);               
    out_u8(0); out_u8(0);     
    out_u32(win->event_mask);
    out_u32(win->event_mask); 
    out_u8(0);                
    out_u8(win->mapped ? 2 : 0); 
    out_zero(10);
    out_u32(ROOT_CMAP_ID);
    out_u32(1);               
    flush_out();
}


static void handle_map_window(const uint8_t *req) {
    uint32_t wid = in_u32_at(req + 4);
    x11_win_t *win = find_win(wid);
    if (!win) return;
    win->mapped = true;
    
    out_u8(12);              
    out_u8(0);
    out_u16((uint16_t)g_seq);
    out_u32(wid);
    out_u16((uint16_t)win->x); out_u16((uint16_t)win->y);
    out_u16((uint16_t)win->w); out_u16((uint16_t)win->h);
    out_u16(0);              
    out_zero(14);
    flush_out();
}


static void handle_unmap_window(const uint8_t *req) {
    uint32_t wid = in_u32_at(req + 4);
    x11_win_t *win = find_win(wid);
    if (win) win->mapped = false;
}


static void handle_get_geometry(const uint8_t *req) {
    uint32_t drawable = in_u32_at(req + 4);
    x11_win_t *win = find_win(drawable);
    extern framebuffer_t fb;
    uint16_t w = win ? (uint16_t)win->w : (uint16_t)(fb.width  ? fb.width  : 1024);
    uint16_t h = win ? (uint16_t)win->h : (uint16_t)(fb.height ? fb.height : 768);

    reply_start(24); 
    out_u32(0);
    out_u32(ROOT_WIN_ID);
    out_u16(win ? (uint16_t)win->x : 0);
    out_u16(win ? (uint16_t)win->y : 0);
    out_u16(w); out_u16(h);
    out_u16(0); 
    out_zero(10);
    flush_out();
}


static void handle_configure_window(const uint8_t *req) {
    uint32_t wid  = in_u32_at(req + 4);
    uint16_t mask = in_u16_at(req + 8);
    const uint8_t *vp = req + 12;
    x11_win_t *win = find_win(wid);
    uint16_t bit;
    const uint8_t *p = vp;
    for (bit = 0; bit < 7; bit++) {
        if (!(mask & (1u << bit))) continue;
        uint32_t v = in_u32_at(p); p += 4;
        if (!win) continue;
        if (bit == 0) win->x = (int32_t)v;
        if (bit == 1) win->y = (int32_t)v;
        if (bit == 2) win->w = v;
        if (bit == 3) win->h = v;
    }
}


static void handle_create_gc(const uint8_t *req) {
    uint32_t gc_id = in_u32_at(req + 4);
    uint32_t mask  = in_u32_at(req + 12);
    const uint8_t *vp = req + 16;
    x11_gc_t *gc = alloc_gc(gc_id);
    if (!gc) return;
    const uint8_t *p = vp;
    uint32_t bit;
    for (bit = 0; bit < 32; bit++) {
        if (!(mask & (1u << bit))) continue;
        uint32_t v = in_u32_at(p); p += 4;
        if (bit == 2)  gc->foreground = v;
        if (bit == 3)  gc->background = v;
        if (bit == 4)  gc->line_width = (int)v;
        if (bit == 14) gc->font = v;
    }
}


static void handle_change_gc(const uint8_t *req) {
    uint32_t gc_id = in_u32_at(req + 4);
    uint32_t mask  = in_u32_at(req + 8);
    const uint8_t *vp = req + 12;
    x11_gc_t *gc = find_gc(gc_id);
    if (!gc) return;
    const uint8_t *p = vp;
    uint32_t bit;
    for (bit = 0; bit < 32; bit++) {
        if (!(mask & (1u << bit))) continue;
        uint32_t v = in_u32_at(p); p += 4;
        if (bit == 2)  gc->foreground = v;
        if (bit == 3)  gc->background = v;
        if (bit == 14) gc->font = v;
    }
}


static void handle_free_gc(const uint8_t *req) {
    uint32_t gc_id = in_u32_at(req + 4);
    int i;
    for (i = 0; i < g_ngcs; i++) {
        if (g_gcs[i].id == gc_id) {
            g_gcs[i] = g_gcs[--g_ngcs];
            break;
        }
    }
}


static void handle_fill_rect(const uint8_t *req, uint32_t len) {
    uint32_t drawable = in_u32_at(req + 4);
    uint32_t gc_id    = in_u32_at(req + 8);
    x11_gc_t *gc = find_gc(gc_id);
    uint32_t color = gc ? gc->foreground : 0xFFFFFFFF;
    (void)drawable;
    const uint8_t *rp = req + 12;
    uint32_t n = (len - 12) / 8;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int16_t rx = (int16_t)in_u16_at(rp);
        int16_t ry = (int16_t)in_u16_at(rp+2);
        uint16_t rw = in_u16_at(rp+4);
        uint16_t rh = in_u16_at(rp+6);
        rp += 8;
        fb_fill_rect((int)rx, (int)ry, (int)rw, (int)rh, color);
    }
}


static bool put_image_to_pixmap(uint32_t drawable, int16_t dx, int16_t dy,
                                  uint16_t w, uint16_t h, const uint8_t *data, uint8_t bpp);


static void handle_put_image(const uint8_t *req) {
    uint8_t  format   = req[1];
    uint32_t drawable = in_u32_at(req + 4);
    uint32_t gc_id    = in_u32_at(req + 8);
    uint16_t width    = in_u16_at(req + 12);
    uint16_t height   = in_u16_at(req + 14);
    int16_t  dst_x    = (int16_t)in_u16_at(req + 16);
    int16_t  dst_y    = (int16_t)in_u16_at(req + 18);
    uint8_t  bpp      = req[21];  
    (void)gc_id;
    if (format != 2 || width == 0 || height == 0) return;
    const uint8_t *pixels = req + 24;
    
    if (put_image_to_pixmap(drawable, dst_x, dst_y, width, height, pixels, bpp)) return;
    
    if (bpp == 32)
        fb_blit((int)dst_x, (int)dst_y, (int)width, (int)height, (uint32_t*)pixels);
}


static void handle_text8(const uint8_t *req) {
    uint32_t drawable = in_u32_at(req + 4);
    uint32_t gc_id    = in_u32_at(req + 8);
    int16_t  x        = (int16_t)in_u16_at(req + 12);
    int16_t  y        = (int16_t)in_u16_at(req + 14);
    x11_gc_t *gc = find_gc(gc_id);
    uint32_t fg = gc ? gc->foreground : 0xFFFFFFFF;
    uint32_t bg = gc ? gc->background : 0xFF000000;
    (void)drawable;
    
    uint8_t slen = req[4];   
    
    const char *s = (const char*)(req + 16);
    if (req[0] == 76) { 
        slen = req[1];
        s    = (const char*)(req + 16);
    } else {
        
        uint8_t icount = req[16];
        if (icount == 0) return;
        s    = (const char*)(req + 18);
        slen = icount;
    }
    canvas_draw_string((int)x, (int)y - 12, s, fg, bg);
}


static void handle_alloc_color(const uint8_t *req) {
    uint16_t r = in_u16_at(req + 8);
    uint16_t g = in_u16_at(req + 10);
    uint16_t b = in_u16_at(req + 12);
    uint32_t pixel = ((uint32_t)(r>>8) << 16) | ((uint32_t)(g>>8) << 8) | (uint32_t)(b>>8);

    reply_start(0);
    out_u32(0);
    out_u16(r); out_u16(g); out_u16(b);
    out_u16(0);
    out_u32(pixel);
    out_zero(12);
    flush_out();
}


static void handle_query_extension(const uint8_t *req) {
    uint16_t name_len = in_u16_at(req + 4);
    const char *name  = (const char*)(req + 8);
    char tmp[64];
    uint32_t n = name_len < 63 ? name_len : 63;
    memcpy(tmp, name, n); tmp[n] = '\0';

    
    uint8_t present = 0, major_op = 0, first_event = 0, first_error = 0;
    if (strcmp(tmp, "BIG-REQUESTS") == 0)    { present=1; major_op=133; }
    if (strcmp(tmp, "RENDER") == 0)          { present=1; major_op=139; }
    if (strcmp(tmp, "Composite") == 0)       { present=1; major_op=142; }
    if (strcmp(tmp, "XFIXES") == 0)          { present=1; major_op=138; }
    if (strcmp(tmp, "DAMAGE") == 0)          { present=1; major_op=143; first_event=91; }
    if (strcmp(tmp, "RANDR") == 0)           { present=1; major_op=140; first_event=89; }
    if (strcmp(tmp, "XINERAMA") == 0)        { present=1; major_op=141; }
    if (strcmp(tmp, "XInputExtension") == 0) { present=1; major_op=131; first_event=80; }
    if (strcmp(tmp, "SYNC") == 0)            { present=1; major_op=134; first_event=83; }
    if (strcmp(tmp, "SHAPE") == 0)           { present=1; major_op=129; first_event=64; }
    if (strcmp(tmp, "DOUBLE-BUFFER") == 0)   { present=1; major_op=135; }

    reply_start(0);
    out_u32(0);
    out_u8(present); out_u8(major_op); out_u8(first_event); out_u8(first_error);
    out_zero(20);
    flush_out();
}


static void handle_get_property(const uint8_t *req) {
    
    reply_start(0);  
    out_u32(0);      
    out_u32(0);      
    out_u32(0);      
    out_u32(0);      
    out_zero(12);
    flush_out();
}


static void handle_get_keyboard_mapping(const uint8_t *req) {
    uint8_t first = req[4];
    uint8_t count = req[5];
    uint8_t keysyms_per = 2;

    reply_start(keysyms_per);
    out_u32((uint32_t)(count * keysyms_per));  
    out_zero(24);

    
    uint8_t i;
    for (i = 0; i < count; i++) {
        uint8_t kc = first + i;
        
        uint32_t ks = 0;
        if (kc >= 10 && kc <= 18) ks = 0x0031 + (kc - 10);  
        if (kc == 19) ks = 0x0030;                             
        if (kc >= 24 && kc <= 32) ks = 0x0071 + (kc - 24);  
        if (kc >= 38 && kc <= 46) ks = 0x0061 + (kc - 38);  
        if (kc == 65) ks = 0x0020;                             
        if (kc == 36) ks = 0xFF0D;                             
        if (kc == 9)  ks = 0xFF1B;                             
        out_u32(ks);
        out_u32(ks ? ks - 0x20 : 0);  
    }
    flush_out();
}


static void handle_query_keymap(void) {
    reply_start(0);
    out_u32(2);    
    out_zero(24);
    out_zero(32);  
    flush_out();
}


static void handle_get_input_focus(void) {
    reply_start(1); 
    out_u32(0);
    out_u32(ROOT_WIN_ID); 
    out_zero(20);
    flush_out();
}


static void handle_big_requests(const uint8_t *req) {
    (void)req;
    reply_start(0);
    out_u32(0);
    out_u32(0x40000000UL);  
    out_zero(20);
    flush_out();
}


static void handle_extension_stub(void) {
    reply_start(0);
    out_u32(0); out_zero(24);
    flush_out();
}


static void blit_pixmap_to_screen(x11_pixmap_t *pm, int16_t dx, int16_t dy,
                                   int16_t sx, int16_t sy, uint16_t w, uint16_t h) {
    if (!pm || !pm->pixels) return;
    extern framebuffer_t fb;
    int i, j;
    for (j = 0; j < h; j++) {
        int sy2 = (int)sy + j;
        int dy2 = (int)dy + j;
        if (sy2 < 0 || sy2 >= (int)pm->h) continue;
        if (dy2 < 0 || dy2 >= (int)fb.height) continue;
        for (i = 0; i < w; i++) {
            int sx2 = (int)sx + i;
            int dx2 = (int)dx + i;
            if (sx2 < 0 || sx2 >= (int)pm->w) continue;
            if (dx2 < 0 || dx2 >= (int)fb.width) continue;
            uint32_t pixel = pm->pixels[(uint32_t)sy2 * pm->w + (uint32_t)sx2];
            fb_putpixel(dx2, dy2, pixel);
        }
    }
}


static void handle_create_pixmap(const uint8_t *req) {
    uint8_t  depth = req[1];
    uint32_t pid   = in_u32_at(req + 4);
    uint16_t w     = in_u16_at(req + 12);
    uint16_t h     = in_u16_at(req + 14);
    (void)in_u32_at(req + 8); 
    alloc_pixmap(pid, w, h, depth);
}


static void handle_free_pixmap(const uint8_t *req) {
    uint32_t pid = in_u32_at(req + 4);
    int i;
    for (i = 0; i < g_npixmaps; i++) {
        if (g_pixmaps[i].id == pid && g_pixmaps[i].used) {
            if (g_pixmaps[i].pixels) kfree(g_pixmaps[i].pixels);
            g_pixmaps[i] = g_pixmaps[--g_npixmaps];
            break;
        }
    }
}


static void handle_copy_area(const uint8_t *req) {
    uint32_t src = in_u32_at(req +  4);
    uint32_t dst = in_u32_at(req +  8);
    int16_t  sx  = (int16_t)in_u16_at(req + 16);
    int16_t  sy  = (int16_t)in_u16_at(req + 18);
    int16_t  dx  = (int16_t)in_u16_at(req + 20);
    int16_t  dy  = (int16_t)in_u16_at(req + 22);
    uint16_t w   = in_u16_at(req + 24);
    uint16_t h   = in_u16_at(req + 26);
    (void)dst;  
    x11_pixmap_t *pm = find_pixmap(src);
    if (pm) blit_pixmap_to_screen(pm, dx, dy, sx, sy, w, h);
}


static bool put_image_to_pixmap(uint32_t drawable, int16_t dx, int16_t dy,
                                  uint16_t w, uint16_t h, const uint8_t *data, uint8_t bpp) {
    x11_pixmap_t *pm = find_pixmap(drawable);
    if (!pm || !pm->pixels) return false;
    if (bpp != 32) return false;
    int i, j;
    for (j = 0; j < h; j++) {
        int dy2 = dy + j;
        if (dy2 < 0 || dy2 >= (int)pm->h) continue;
        for (i = 0; i < w; i++) {
            int dx2 = dx + i;
            if (dx2 < 0 || dx2 >= (int)pm->w) continue;
            uint32_t pixel = in_u32_at(data + ((uint32_t)j * w + (uint32_t)i) * 4);
            pm->pixels[(uint32_t)dy2 * pm->w + (uint32_t)dx2] = pixel;
        }
    }
    return true;
}


static void handle_render_ext(const uint8_t *req, uint32_t len) {
    uint8_t minor = req[1];
    (void)len;
    switch (minor) {
    case 0:  {
        reply_start(0);
        out_u32(0);
        out_u32(0); out_u32(11);  
        out_zero(16);
        flush_out();
        break;
    }
    case 1:  {
        
        uint32_t nformats = 1;
        uint32_t nscreens = 1;
        uint32_t ndepths  = 1;
        uint32_t nvisuals = 1;
        uint32_t data_len = 7  +
                            2 ;
        reply_start(0);
        out_u32(data_len);
        out_u32(nformats);
        out_u32(nscreens);
        out_u32(0);  
        out_u32(0);  
        out_u32(0);  
        
        out_u32(0x50);      
        out_u8(1);          
        out_u8(32);         
        out_u16(0);         
        
        out_u16(24); out_u16(0xFF); 
        out_u16(16); out_u16(0xFF); 
        out_u16(8);  out_u16(0xFF); 
        out_u16(0);  out_u16(0xFF); 
        out_u32(0);  
        
        out_u32(ROOT_WIN_ID); 
        out_u32(0x50);        
        out_u32(ndepths);
        
        out_u8(32); out_u8(0); out_u16((uint16_t)nvisuals); out_u32(0);
        
        out_u32(ROOT_VISUAL);
        out_u32(0x50); 
        flush_out();
        break;
    }
    case 4:  {
        uint32_t pic     = in_u32_at(req + 4);
        uint32_t drawable = in_u32_at(req + 8);
        alloc_pic(pic, drawable);
        
        break;
    }
    case 5: 
    case 7:  {
        uint32_t pic = in_u32_at(req + 4);
        int i;
        for (i = 0; i < g_npics; i++)
            if (g_pics[i].id == pic) { g_pics[i] = g_pics[--g_npics]; break; }
        break;
    }
    case 8:  {
        
        uint8_t op = req[4];  
        uint32_t src_pic = in_u32_at(req + 8);
        
        uint32_t dst_pic = in_u32_at(req + 16);
        int16_t src_x = (int16_t)in_u16_at(req + 20);
        int16_t src_y = (int16_t)in_u16_at(req + 22);
        int16_t dst_x = (int16_t)in_u16_at(req + 28);
        int16_t dst_y = (int16_t)in_u16_at(req + 30);
        uint16_t w = in_u16_at(req + 32);
        uint16_t h = in_u16_at(req + 34);
        (void)op;
        x11_pic_t *sp = find_pic(src_pic);
        x11_pic_t *dp = find_pic(dst_pic);
        if (sp && dp) {
            
            x11_pixmap_t *pm = find_pixmap(sp->drawable);
            if (pm) blit_pixmap_to_screen(pm, dst_x, dst_y, src_x, src_y, w, h);
        }
        break;
    }
    case 23: 
    case 26:  {
        
        uint32_t dst_pic = in_u32_at(req + 8);
        x11_pic_t *dp = find_pic(dst_pic);
        
        uint16_t cr = in_u16_at(req + 14);
        uint16_t cg = in_u16_at(req + 16);
        uint16_t cb = in_u16_at(req + 18);
        uint32_t color = ((uint32_t)(cr>>8)<<16)|((uint32_t)(cg>>8)<<8)|(uint32_t)(cb>>8);
        
        uint32_t nrects = (len - 20) / 8;
        uint32_t i;
        const uint8_t *rp = req + 20;
        for (i = 0; i < nrects; i++) {
            int16_t rx = (int16_t)in_u16_at(rp);
            int16_t ry = (int16_t)in_u16_at(rp+2);
            uint16_t rw = in_u16_at(rp+4);
            uint16_t rh = in_u16_at(rp+6);
            rp += 8;
            if (dp) {
                x11_pixmap_t *pm = find_pixmap(dp->drawable);
                if (pm && pm->pixels) {
                    uint32_t r2, c2;
                    for (r2 = 0; r2 < rh; r2++) {
                        int py = (int)ry + (int)r2;
                        if (py < 0 || py >= (int)pm->h) continue;
                        for (c2 = 0; c2 < rw; c2++) {
                            int px = (int)rx + (int)c2;
                            if (px < 0 || px >= (int)pm->w) continue;
                            pm->pixels[(uint32_t)py * pm->w + (uint32_t)px] = color;
                        }
                    }
                } else {
                    fb_fill_rect((int)rx, (int)ry, (int)rw, (int)rh, color);
                }
            } else {
                fb_fill_rect((int)rx, (int)ry, (int)rw, (int)rh, color);
            }
        }
        break;
    }
    
    case 17: case 18: case 19: case 20: case 21: case 22: break;
    default: break;
    }
}


static void handle_xfixes_ext(const uint8_t *req, uint32_t len) {
    uint8_t minor = req[1];
    (void)len;
    if (minor == 0) { 
        reply_start(0); out_u32(0); out_u32(5); out_u32(0); out_zero(16); flush_out();
    } else {
        handle_extension_stub();
    }
}


static void handle_randr_ext(const uint8_t *req, uint32_t len) {
    uint8_t minor = req[1];
    (void)len;
    if (minor == 0) { 
        reply_start(0); out_u32(0); out_u32(1); out_u32(5); out_zero(16); flush_out();
    } else {
        handle_extension_stub();
    }
}


static void handle_composite_ext(const uint8_t *req, uint32_t len) {
    uint8_t minor = req[1];
    (void)len;
    if (minor == 0) { 
        reply_start(0); out_u32(0); out_u32(0); out_u32(4); out_zero(16); flush_out();
    }
    
}


static void handle_sync_ext(const uint8_t *req, uint32_t len) {
    uint8_t minor = req[1];
    (void)len;
    if (minor == 0) { 
        reply_start(0); out_u32(0); out_u32(3); out_u32(1); out_zero(16); flush_out();
    }
    
}


static void handle_damage_ext(const uint8_t *req, uint32_t len) {
    uint8_t minor = req[1];
    (void)len;
    if (minor == 0) { 
        reply_start(0); out_u32(0); out_u32(1); out_u32(1); out_zero(16); flush_out();
    }
}


static uint32_t g_focus_win = ROOT_WIN_ID;

void x11_inject_key(uint8_t keycode, bool pressed, uint32_t state) {
    if (g_svc < 0 || !lx64_ksvc_has_client(g_svc)) return;
    
    uint32_t target = g_focus_win;
    out_u8(pressed ? 2 : 3); 
    out_u8(keycode);
    out_u16((uint16_t)++g_seq);
    out_u32(timer_get_ticks()); 
    out_u32(ROOT_WIN_ID);        
    out_u32(target);             
    out_u32(target);             
    out_u16(0); out_u16(0);      
    out_u16(0); out_u16(0);      
    out_u16((uint16_t)state);    
    out_u8(1);                   
    out_u8(0);
    flush_out();
}

void x11_inject_mouse(int abs_x, int abs_y, int dx, int dy, uint8_t buttons) {
    (void)dx; (void)dy;
    if (g_svc < 0 || !lx64_ksvc_has_client(g_svc)) return;
    out_u8(6); 
    out_u8(0); 
    out_u16((uint16_t)++g_seq);
    out_u32(timer_get_ticks());
    out_u32(ROOT_WIN_ID);
    out_u32(ROOT_WIN_ID);
    out_u32(0);  
    out_u16((uint16_t)abs_x); out_u16((uint16_t)abs_y);
    out_u16((uint16_t)abs_x); out_u16((uint16_t)abs_y);
    out_u16(buttons); 
    out_u8(1); out_u8(0);
    flush_out();
}


static bool g_client_connected = false;


static uint32_t dispatch_request(const uint8_t *req, uint32_t avail) {
    if (avail < 4) return 0;
    uint8_t opcode = req[0];
    uint32_t req_len = (uint32_t)in_u16_at(req + 2) * 4;
    if (req_len == 0) req_len = 4;
    if (avail < req_len) return 0;

    g_seq++;

    switch (opcode) {
    case   1: handle_create_window(req, req_len); break;
    case   2: handle_change_window_attrs(req); break;
    case   3: handle_get_window_attrs(req); break;
    case   4:  break;
    case   8: handle_map_window(req); break;
    case  10: handle_unmap_window(req); break;
    case  12: handle_configure_window(req); break;
    case  14: handle_get_geometry(req); break;
    case  16: handle_intern_atom(req, req_len); break;
    case  17: handle_get_atom_name(req); break;
    case  18:  break;
    case  20: handle_get_property(req); break;
    case  21:  handle_extension_stub(); break;
    case  25:  break;
    case  26:  handle_extension_stub(); break;
    case  28:  break;
    case  31:  handle_extension_stub(); break;
    case  33:  break;
    case  39: 
        g_focus_win = in_u32_at(req + 4);
        break;
    case  40: handle_get_input_focus(); break;
    case  43: handle_query_keymap(); break;
    case  45:  break;
    case  47:  handle_extension_stub(); break;
    case  49:  handle_extension_stub(); break;
    case  50: handle_create_gc(req); break;
    case  51: handle_change_gc(req); break;
    case  53: handle_create_pixmap(req); break;
    case  54: handle_free_pixmap(req); break;
    case  55:  {
        uint32_t wid = in_u32_at(req+4);
        x11_win_t *w = find_win(wid);
        if (w) fb_fill_rect(w->x, w->y, (int)w->w, (int)w->h, w->bg_pixel);
        break;
    }
    case  60: handle_free_gc(req); break;
    case  62: handle_copy_area(req); break;
    case  65:  break;
    case  66: handle_fill_rect(req, req_len); break;
    case  72: handle_put_image(req); break;
    case  74: 
    case  76: handle_text8(req); break;
    case  79:  break;
    case  81:  break;
    case  84: handle_alloc_color(req); break;
    case  85:  handle_extension_stub(); break;
    case  91:  handle_extension_stub(); break;
    case  98: handle_query_extension(req); break;
    case  99:  handle_extension_stub(); break;
    case 101: handle_get_keyboard_mapping(req); break;
    case 102:  break;
    case 103:  handle_extension_stub(); break;
    case 107:  handle_extension_stub(); break;
    case 115:  handle_extension_stub(); break;
    case 116:  handle_extension_stub(); break;
    case 117:  break;
    case 118:  break;
    case 119:  handle_get_input_focus(); break;

    
    case 133: handle_big_requests(req); break;

    
    case 138: handle_xfixes_ext(req, req_len); break;
    case 139: handle_render_ext(req, req_len); break;
    case 140: handle_randr_ext(req, req_len); break;
    case 142: handle_composite_ext(req, req_len); break;
    case 143: handle_damage_ext(req, req_len); break;

    default:
        
        break;
    }
    return req_len;
}


void x11_server_process(int svc_idx) {
    if (svc_idx != g_svc || g_svc < 0) return;

    
    uint8_t tmp[4096];
    int nr;
    while ((nr = lx64_ksvc_read(g_svc, tmp, sizeof(tmp))) > 0) {
        uint32_t copy = (uint32_t)nr;
        if (g_inlen + copy > sizeof(g_inbuf)) copy = (uint32_t)sizeof(g_inbuf) - g_inlen;
        memcpy(g_inbuf + g_inlen, tmp, copy);
        g_inlen += copy;
    }

    if (!g_client_connected) {
        
        if (g_inlen < 12) return;
        
        uint16_t auth_name_len = in_u16_at(g_inbuf + 6);
        uint16_t auth_data_len = in_u16_at(g_inbuf + 8);
        uint32_t setup_len = 12
            + ((auth_name_len + 3) & ~3u)
            + ((auth_data_len + 3) & ~3u);
        if (g_inlen < setup_len) return;
        
        g_inlen -= setup_len;
        memmove(g_inbuf, g_inbuf + setup_len, g_inlen);
        g_client_connected = true;
        send_connection_accepted();
        return;
    }

    
    uint32_t pos = 0;
    while (pos < g_inlen) {
        uint32_t consumed = dispatch_request(g_inbuf + pos, g_inlen - pos);
        if (!consumed) break;
        pos += consumed;
    }
    if (pos > 0) {
        g_inlen -= pos;
        if (g_inlen) memmove(g_inbuf, g_inbuf + pos, g_inlen);
    }
}


void x11_server_init(void) {
    
    g_nwins = 0; g_ngcs = 0;
    g_natoms = N_PREDEF_ATOMS;
    g_seq = 0; g_outlen = 0; g_inlen = 0;
    g_client_connected = false;
    g_focus_win = ROOT_WIN_ID;
    g_npixmaps = 0;
    g_npics = 0;

    
    extern framebuffer_t fb;
    x11_win_t *root = alloc_win(ROOT_WIN_ID);
    if (root) {
        root->w = fb.width  ? fb.width  : 1024;
        root->h = fb.height ? fb.height : 768;
        root->mapped = true;
        root->bg_pixel = 0xFF0C2461;
    }

    
    g_svc = lx64_register_kernel_service(X11_SOCKET_PATH);
    if (g_svc < 0) return;

    
    lx64_register_kernel_service("\0" X11_SOCKET_PATH);

    
    vfs_node_t *tmp_dir = vfs_resolve("/tmp");
    if (tmp_dir) {
        extern int vfs_mkdir(vfs_node_t *, const char *, uint32_t);
        vfs_mkdir(tmp_dir, ".X11-unix", 0777);
    }
}

void x11_server_poll(void) {
    
    if (g_svc >= 0 && lx64_ksvc_has_client(g_svc))
        x11_server_process(g_svc);
}
