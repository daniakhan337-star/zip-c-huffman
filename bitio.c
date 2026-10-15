#include "zipc.h"

void bw_init(BitWriter *w, FILE *fp) {
    w->fp = fp;
    w->buf = 0;
    w->nbits = 0;
}

int bw_write_bit(BitWriter *w, int bit) {
    w->buf = (w->buf << 1) | (bit & 1);
    w->nbits++;
    if (w->nbits == 8) {
        if (fputc(w->buf, w->fp) == EOF) return -1;
        w->buf = 0;
        w->nbits = 0;
    }
    return 0;
}
 
int bw_write_bits(BitWriter *w, uint32_t code, int len) {
    for (int i = len - 1; i >= 0; i--) {
        int bit = (code >> i) & 1;
        if (bw_write_bit(w, bit) == -1) return -1;
    }
    return 0;
}

int bw_flush(BitWriter *w) {
    if (w->nbits == 0) return 0;
    int padding = 8 - w->nbits;
    w->buf <<= padding;
    if (fputc(w->buf, w->fp) == EOF) return -1;
    int pad_count = padding;
    w->nbits = 0;
    w->buf = 0;
    return pad_count;
}

void br_init(BitReader *r, FILE *fp) {
    r->fp = fp;
    r->buf = 0;
    r->nbits_left = 0;
}

int br_read_bit(BitReader *r) {
    if (r->nbits_left == 0) {
        int c = fgetc(r->fp);
        if (c == EOF) return -1;
        r->buf = (uint8_t)c;
        r->nbits_left = 8;
    }
    r->nbits_left--;
    return (r->buf >> r->nbits_left) & 1;
}