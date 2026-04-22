

#include <drivers/keyboard.h>
#include <kernel/idt.h>
#include <types.h>
#include <io.h>


#define KB_BUFFER_SIZE 256


static char     kb_buf[KB_BUFFER_SIZE];
static uint8_t  kb_read_pos  = 0;
static uint8_t  kb_write_pos = 0;


static bool shift_pressed  = false;
static bool caps_lock      = false;
static bool ctrl_pressed   = false;
static bool alt_pressed    = false;
static bool abnt2_mode     = true;   
static uint8_t dead_key    = 0;      


static const char sc_to_ascii[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0,                            
    0, 0, 0, '-',                    
    0, 0, 0, '+',                    
    0, 0, 0, 0, 0,                   
    0, 0, 0,                         
    0, 0,                            
};


static const char sc_to_ascii_shift[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    0, 0, 0, '-',
    0, 0, 0, '+',
    0, 0, 0, 0, 0,
    0, 0, 0,
    0, 0,
};


#define SC_LSHIFT      0x2A
#define SC_RSHIFT      0x36
#define SC_LCTRL       0x1D
#define SC_LALT        0x38
#define SC_CAPS_LOCK   0x3A
#define SC_RELEASE     0x80  

static void kb_buf_push(char c) {
    uint8_t next = (kb_write_pos + 1) % KB_BUFFER_SIZE;
    if (next != kb_read_pos) {  
        kb_buf[kb_write_pos] = c;
        kb_write_pos = next;
    }
}


static char abnt2_apply_dead(uint8_t dk, char base) {
    
    static const char agudo[]  = "aeiouAEIOU";
    static const char agudo_r[]= "\xe1\xe9\xed\xf3\xfa\xc1\xc9\xcd\xd3\xda";
    static const char til[]    = "aoAO";
    static const char til_r[]  = "\xe3\xf5\xc3\xd5";
    static const char circ[]   = "aeiouAEIOU";
    static const char circ_r[] = "\xe2\xea\xee\xf4\xfb\xc2\xca\xce\xd4\xdb";
    int i;
    if (dk == 1) { for (i=0;agudo[i];i++) if (base==agudo[i]) return agudo_r[i]; }
    if (dk == 2) { for (i=0;til[i];i++)   if (base==til[i])   return til_r[i]; }
    if (dk == 3) { for (i=0;circ[i];i++)  if (base==circ[i])  return circ_r[i]; }
    return base;
}

static void keyboard_handler(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(KB_DATA_PORT);

    if (sc & SC_RELEASE) {
        uint8_t r = sc & ~SC_RELEASE;
        if (r == SC_LSHIFT || r == SC_RSHIFT) shift_pressed = false;
        if (r == SC_LCTRL)  ctrl_pressed = false;
        if (r == SC_LALT)   alt_pressed  = false;
        pic_send_eoi(33);
        return;
    }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT) { shift_pressed = true;  goto eoi; }
    if (sc == SC_LCTRL)                     { ctrl_pressed  = true;  goto eoi; }
    if (sc == SC_LALT)                      { alt_pressed   = true;  goto eoi; }
    if (sc == SC_CAPS_LOCK)                 { caps_lock = !caps_lock; goto eoi; }

    if (sc < 128) {
        char c;
        bool use_shift = shift_pressed;

        if (caps_lock && sc >= 0x10 && sc <= 0x32) use_shift = !use_shift;

        c = use_shift ? sc_to_ascii_shift[sc] : sc_to_ascii[sc];

        
        if (abnt2_mode) {
            
            if (sc == 0x27) c = use_shift ? 'C' : '\xe7';  
            
            if (sc == 0x29) c = use_shift ? '"' : '\'';
            
            if (sc == 0x28) { 
                dead_key = use_shift ? 2 : 1;
                goto eoi;
            }
            if (sc == 0x1A) { 
                dead_key = 3;
                goto eoi;
            }
        }

        if (c) {
            if (dead_key) {
                char acc = abnt2_apply_dead(dead_key, c);
                dead_key = 0;
                kb_buf_push(acc != c ? acc : c);
                goto eoi;
            }
            if (ctrl_pressed && c >= 'a' && c <= 'z') c = c - 'a' + 1;
            kb_buf_push(c);
        } else if (dead_key) {
            dead_key = 0;
        }
    }

eoi:
    pic_send_eoi(33);
}

void keyboard_init(void) {
    kb_read_pos  = 0;
    kb_write_pos = 0;
    idt_register_handler(IRQ_KEYBOARD, keyboard_handler);
    pic_unmask_irq(1);
}

char keyboard_getchar(void) {
    if (kb_read_pos == kb_write_pos) return 0;
    char c = kb_buf[kb_read_pos];
    kb_read_pos = (kb_read_pos + 1) % KB_BUFFER_SIZE;
    return c;
}

char keyboard_read(void) {
    char c;
    while ((c = keyboard_getchar()) == 0) {
        __asm__ volatile ("hlt");
    }
    return c;
}

bool keyboard_available(void) {
    return kb_read_pos != kb_write_pos;
}

void keyboard_inject(char c) {
    if (c) kb_buf_push(c);
}
