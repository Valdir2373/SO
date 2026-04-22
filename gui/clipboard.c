


#include <gui/clipboard.h>
#include <lib/string.h>
#include <types.h>

#define CLIP_MAX 8192

static char clip_buf[CLIP_MAX];
static int  clip_len = 0;

void clipboard_set(const char *data, int len) {
    if (!data || len <= 0) return;
    if (len >= CLIP_MAX) len = CLIP_MAX - 1;
    memcpy(clip_buf, data, (uint32_t)len);
    clip_buf[len] = '\0';
    clip_len = len;
}

const char *clipboard_get(void) { return clip_buf; }
int         clipboard_size(void) { return clip_len; }
void        clipboard_clear(void) { clip_buf[0] = '\0'; clip_len = 0; }
