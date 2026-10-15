#include "zipc.h"
#include <stdlib.h>

Node *build_tree(const uint64_t freq[ALPHABET_SIZE]) {
    MinHeap *h = heap_create(256);
    if (!h) return NULL;

    int distinct = 0;
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        if (freq[i] > 0) {
            Node *leaf = malloc(sizeof(Node));
            if (!leaf) {
                heap_free(h);
                return NULL;
            }
            leaf->byte = (uint8_t)i;
            leaf->freq = freq[i];
            leaf->is_leaf = 1;
            leaf->left = NULL;
            leaf->right = NULL;
            heap_push(h, leaf);
            distinct++;
        }
    }

    if (distinct == 0) {
        heap_free(h);
        return NULL;
    }

    if (distinct == 1) {
        Node *leaf = heap_pop(h);
        Node *root = malloc(sizeof(Node));
        if (!root) {
            free(leaf);
            heap_free(h);
            return NULL;
        }
        root->byte = 0;
        root->freq = leaf->freq;
        root->is_leaf = 0;
        root->left = leaf;
        root->right = NULL;
        heap_free(h);
        return root;
    }

    while (heap_size(h) > 1) {
        Node *left = heap_pop(h);
        Node *right = heap_pop(h);

        Node *parent = malloc(sizeof(Node));
        if (!parent) {
            free_tree(left);
            free_tree(right);
            heap_free(h);
            return NULL;
        }
        parent->byte = 0;
        parent->freq = left->freq + right->freq;
        parent->is_leaf = 0;
        parent->left = left;
        parent->right = right;

        heap_push(h, parent);
    }

    Node *root = heap_pop(h);
    heap_free(h);
    return root;
}

void free_tree(Node *root) {
    if (!root) return;
    free_tree(root->left);
    free_tree(root->right);
    free(root);
}

static void generate_codes_rec(const Node *node, uint32_t bits, uint8_t len, Code table[ALPHABET_SIZE]) {
    if (!node) return;
    if (node->is_leaf) {
        table[node->byte].bits = (len == 0) ? 0 : bits;
        table[node->byte].len = (len == 0) ? 1 : len;
        return;
    }
    generate_codes_rec(node->left, (bits << 1) | 0, len + 1, table);
    generate_codes_rec(node->right, (bits << 1) | 1, len + 1, table);
}

void generate_codes(const Node *root, Code table[ALPHABET_SIZE]) {
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        table[i].bits = 0;
        table[i].len = 0;
    }
    if (!root) return;
    if (root->is_leaf) {
        table[root->byte].bits = 0;
        table[root->byte].len = 1;
        return;
    }
    generate_codes_rec(root, 0, 0, table);
}