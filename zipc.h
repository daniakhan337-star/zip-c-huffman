/*
 * zipc.h - shared interfaces for Zip-C (Huffman file compressor), C99.
 * Agree on this file on day one. Each member implements their own section
 * in their own .c file and can stub the others until they are ready.
 */
#ifndef ZIPC_H
#define ZIPC_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

/* ---------- Shared constants ---------- */
#define ALPHABET_SIZE 256
#define MAX_CODE_LEN  255            /* worst case depth of a 256-leaf tree */
#define HUFF_MAGIC    "HUFF"         /* 4 bytes at the start of every .huff */

/*
 * .huff layout (all integers little-endian):
 *   [4]  magic "HUFF"
 *   [8]  original size in bytes (uint64_t)
 *   [2]  number of distinct symbols (uint16_t, 0..256)
 *   [n * (1 + 8)] table: byte value (uint8_t), frequency (uint64_t)
 *   [1]  padding bits in the final payload byte (0..7)
 *   [..] payload: Huffman codes packed MSB-first
 */

/* ---------- Node shared by heap and tree (Members 1 and 2) ---------- */
typedef struct Node {
    uint8_t      byte;               /* valid for leaves only */
    uint64_t     freq;
    int          is_leaf;
    struct Node *left, *right;
} Node;

/* ======================================================================
 * MEMBER 1: frequency counter + min-heap  (heap.c)
 * ====================================================================== */
typedef struct {
    Node  **items;                   /* array-based binary heap */
    size_t  size, capacity;
} MinHeap;

/* Count byte frequencies of an open binary file. Returns total bytes read. */
uint64_t count_frequencies(FILE *in, uint64_t freq[ALPHABET_SIZE]);

MinHeap *heap_create(size_t capacity);
void     heap_free(MinHeap *h);               /* frees the heap only, not nodes */
int      heap_push(MinHeap *h, Node *n);      /* 0 ok, -1 full/alloc failure */
Node    *heap_pop(MinHeap *h);                /* smallest freq, NULL if empty */
size_t   heap_size(const MinHeap *h);
/* Tie-break rule (required for determinism): lower freq first; on equal
 * freq, the node with the smaller smallest-byte-in-subtree goes first. */
int      node_less(const Node *a, const Node *b);

/* ======================================================================
 * MEMBER 2: Huffman tree + code generator  (tree.c)
 * ====================================================================== */
typedef struct {
    uint32_t bits;                   /* code stored in the low `len` bits */
    uint8_t  len;                    /* 0 means symbol unused */
} Code;

/* Builds the tree from the table. Returns NULL when all freqs are 0.
 * A single used symbol must yield a root with one leaf (code "0"). */
Node *build_tree(const uint64_t freq[ALPHABET_SIZE]);
void  free_tree(Node *root);

/* Fills table[256] by walking the tree (left = 0, right = 1). */
void  generate_codes(const Node *root, Code table[ALPHABET_SIZE]);

/* ======================================================================
 * MEMBER 3: bit writer / reader  (bitio.c)
 * ====================================================================== */
typedef struct {
    FILE   *fp;
    uint8_t buf;                     /* partial byte being filled */
    int     nbits;                   /* bits currently in buf (0..7) */
} BitWriter;

typedef struct {
    FILE   *fp;
    uint8_t buf;
    int     nbits_left;              /* unread bits in buf */
} BitReader;

void bw_init(BitWriter *w, FILE *fp);
int  bw_write_bit(BitWriter *w, int bit);
int  bw_write_bits(BitWriter *w, uint32_t code, int len);  /* MSB first */
/* Pads the last byte with zeros and writes it. Returns the number of
 * padding bits (0..7) so it can be stored in the header, or -1 on error. */
int  bw_flush(BitWriter *w);

void br_init(BitReader *r, FILE *fp);
/* Returns 0 or 1, or -1 on EOF/error. */
int  br_read_bit(BitReader *r);

/* ======================================================================
 * MEMBER 4: compress/decompress drivers, verification, CLI  (main.c)
 * ====================================================================== */
typedef enum {
    ZIPC_OK = 0,
    ZIPC_ERR_IO,                     /* cannot open/read/write file */
    ZIPC_ERR_BAD_MAGIC,              /* not a .huff file */
    ZIPC_ERR_CORRUPT,                /* truncated payload or bad table */
    ZIPC_ERR_VERIFY                  /* round trip did not match */
} ZipcStatus;

typedef struct {
    uint64_t original_size;
    uint64_t compressed_size;
    double   ratio_percent;          /* (1 - compressed/original) * 100 */
    double   seconds;
} Metrics;

ZipcStatus compress_file(const char *in_path, const char *out_path, Metrics *m);
ZipcStatus decompress_file(const char *in_path, const char *out_path);
/* Compares two files byte for byte. 1 = identical, 0 = different. */
int        files_identical(const char *path_a, const char *path_b);
void       print_metrics(const Metrics *m);
const char *zipc_strerror(ZipcStatus s);

/*
 * CLI (main.c):
 *   zipc compress   <input> <output.huff>
 *   zipc decompress <input.huff> <output>
 *   zipc compress   <input> <output.huff> --verify
 */

#endif /* ZIPC_H */
