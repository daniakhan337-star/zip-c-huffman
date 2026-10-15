#include "zipc.h"
#include <stdlib.h>
#include <string.h>

uint64_t count_frequencies(FILE *in, uint64_t freq[ALPHABET_SIZE]) {
    memset(freq, 0, ALPHABET_SIZE * sizeof(uint64_t));
    uint8_t buf[4096];
    uint64_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        for (size_t i = 0; i < n; i++) {
            freq[buf[i]]++;
        }
        total += n;
    }
    return total;
}

MinHeap *heap_create(size_t capacity) {
    MinHeap *h = malloc(sizeof(MinHeap));
    if (!h) return NULL;
    h->items = malloc(capacity * sizeof(Node *));
    if (!h->items) {
        free(h);
        return NULL;
    }
    h->size = 0;
    h->capacity = capacity;
    return h;
}

void heap_free(MinHeap *h) {
    if (h) {
        free(h->items);
        free(h);
    }
}

size_t heap_size(const MinHeap *h) {
    return h ? h->size : 0;
}

static uint8_t min_byte(const Node *n) {
    if (!n) return 0xFF;
    if (n->is_leaf) return n->byte;
    uint8_t l = min_byte(n->left);
    uint8_t r = min_byte(n->right);
    return (l < r) ? l : r;
}

int node_less(const Node *a, const Node *b) {
    if (a->freq != b->freq) {
        return a->freq < b->freq;
    }
    return min_byte(a) < min_byte(b);
}

static void swap(Node **a, Node **b) {
    Node *tmp = *a;
    *a = *b;
    *b = tmp;
}

int heap_push(MinHeap *h, Node *n) {
    if (!h || !n) return -1;
    if (h->size >= h->capacity) {
        size_t new_cap = h->capacity * 2;
        Node **new_items = realloc(h->items, new_cap * sizeof(Node *));
        if (!new_items) return -1;
        h->items = new_items;
        h->capacity = new_cap;
    }
    size_t i = h->size++;
    h->items[i] = n;
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (node_less(h->items[i], h->items[parent])) {
            swap(&h->items[i], &h->items[parent]);
            i = parent;
        } else {
            break;
        }
    }
    return 0;
}

Node *heap_pop(MinHeap *h) {
    if (!h || h->size == 0) return NULL;
    Node *root = h->items[0];
    h->items[0] = h->items[--h->size];
    size_t i = 0;
    while (1) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t smallest = i;

        if (left < h->size && node_less(h->items[left], h->items[smallest])) {
            smallest = left;
        }
        if (right < h->size && node_less(h->items[right], h->items[smallest])) {
            smallest = right;
        }
        if (smallest != i) {
            swap(&h->items[i], &h->items[smallest]);
            i = smallest;
        } else {
            break;
        }
    }
    return root;
}