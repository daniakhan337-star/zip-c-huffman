#include "zipc.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *zipc_strerror(ZipcStatus s) {
    switch (s) {
        case ZIPC_OK: return "Success";
        case ZIPC_ERR_IO: return "I/O error (cannot open/read/write file)";
        case ZIPC_ERR_BAD_MAGIC: return "Bad magic bytes (not a .huff file)";
        case ZIPC_ERR_CORRUPT: return "Corrupt payload or frequency table";
        case ZIPC_ERR_VERIFY: return "Round trip verification failed";
        default: return "Unknown error";
    }
}

int files_identical(const char *path_a, const char *path_b) {
    FILE *fa = fopen(path_a, "rb");
    FILE *fb = fopen(path_b, "rb");
    if (!fa || !fb) {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 0;
    }
    int identical = 1;
    while (1) {
        int ca = fgetc(fa);
        int cb = fgetc(fb);
        if (ca != cb) {
            identical = 0;
            break;
        }
        if (ca == EOF) break;
    }
    fclose(fa);
    fclose(fb);
    return identical;
}

void print_metrics(const Metrics *m) {
    printf("Original size:   %llu bytes\n", (unsigned long long)m->original_size);
    printf("Compressed size: %llu bytes\n", (unsigned long long)m->compressed_size);
    printf("Compression ratio: %.2f%%\n", m->ratio_percent);
    printf("Time elapsed:    %.4f seconds\n", m->seconds);
}

ZipcStatus compress_file(const char *in_path, const char *out_path, Metrics *m) {
    clock_t start = clock();
    FILE *in = fopen(in_path, "rb");
    if (!in) return ZIPC_ERR_IO;

    uint64_t freq[ALPHABET_SIZE];
    uint64_t orig_size = count_frequencies(in, freq);
    rewind(in);

    Node *root = build_tree(freq);
    if (!root) {
        fclose(in);
        return ZIPC_ERR_CORRUPT;
    }

    Code table[ALPHABET_SIZE];
    generate_codes(root, table);
    free_tree(root);

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fclose(in);
        return ZIPC_ERR_IO;
    }

    // Write header components
    fwrite(HUFF_MAGIC, 1, 4, out);
    fwrite(&orig_size, sizeof(uint64_t), 1, out);

    uint16_t distinct = 0;
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        if (freq[i] > 0) distinct++;
    }
    fwrite(&distinct, sizeof(uint16_t), 1, out);

    for (int i = 0; i < ALPHABET_SIZE; i++) {
        if (freq[i] > 0) {
            uint8_t byte_val = (uint8_t)i;
            fwrite(&byte_val, 1, 1, out);
            fwrite(&freq[i], sizeof(uint64_t), 1, out);
        }
    }

    // Placeholder for padding byte
    long padding_pos = ftell(out);
    uint8_t dummy_pad = 0;
    fputc(dummy_pad, out);

    // Write payload bits
    BitWriter bw;
    bw_init(&bw, out);

    uint8_t file_buf[4096];
    size_t n;
    while ((n = fread(file_buf, 1, sizeof(file_buf), in)) > 0) {
        for (size_t i = 0; i < n; i++) {
            uint8_t b = file_buf[i];
            bw_write_bits(&bw, table[b].bits, table[b].len);
        }
    }
    int pad_bits = bw_flush(&bw);
    fclose(in);

    // Update padding byte in header
    fseek(out, padding_pos, SEEK_SET);
    uint8_t final_pad = (uint8_t)pad_bits;
    fputc(final_pad, out);

    fclose(out);

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    FILE *check_out = fopen(out_path, "rb");
    fseek(check_out, 0, SEEK_END);
    uint64_t comp_size = ftell(check_out);
    fclose(check_out);

    m->original_size = orig_size;
    m->compressed_size = comp_size;
    m->ratio_percent = orig_size > 0 ? (1.0 - ((double)comp_size / orig_size)) * 100.0 : 0.0;
    m->seconds = elapsed;

    return ZIPC_OK;
}

ZipcStatus decompress_file(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "rb");
    if (!in) return ZIPC_ERR_IO;

    char magic[4];
    if (fread(magic, 1, 4, in) != 4 || memcmp(magic, HUFF_MAGIC, 4) != 0) {
        fclose(in);
        return ZIPC_ERR_BAD_MAGIC;
    }

    uint64_t orig_size;
    if (fread(&orig_size, sizeof(uint64_t), 1, in) != 1) {
        fclose(in);
        return ZIPC_ERR_CORRUPT;
    }

    uint16_t distinct;
    if (fread(&distinct, sizeof(uint16_t), 1, in) != 1) {
        fclose(in);
        return ZIPC_ERR_CORRUPT;
    }

    uint64_t freq[ALPHABET_SIZE] = {0};
    for (int i = 0; i < distinct; i++) {
        uint8_t byte_val;
        uint64_t f;
        if (fread(&byte_val, 1, 1, in) != 1 || fread(&f, sizeof(uint64_t), 1, in) != 1) {
            fclose(in);
            return ZIPC_ERR_CORRUPT;
        }
        freq[byte_val] = f;
    }

    uint8_t pad_bits;
    if (fread(&pad_bits, 1, 1, in) != 1) {
        fclose(in);
        return ZIPC_ERR_CORRUPT;
    }

    Node *root = build_tree(freq);
    if (!root) {
        fclose(in);
        return ZIPC_ERR_CORRUPT;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        free_tree(root);
        fclose(in);
        return ZIPC_ERR_IO;
    }

    BitReader br;
    br_init(&br, in);

    uint64_t decoded_count = 0;
    Node *curr = root;

    while (decoded_count < orig_size) {
        int bit = br_read_bit(&br);
        if (bit == -1) break;

        curr = (bit == 0) ? curr->left : curr->right;
        if (!curr) {
            free_tree(root);
            fclose(out);
            fclose(in);
            return ZIPC_ERR_CORRUPT;
        }

        if (curr->is_leaf) {
            fputc(curr->byte, out);
            decoded_count++;
            curr = root;
        }
    }

    free_tree(root);
    fclose(out);
    fclose(in);

    return ZIPC_OK;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n");
        printf("  zipc compress   <input> <output.huff>\n");
        printf("  zipc decompress <input.huff> <output>\n");
        printf("  zipc compress   <input> <output.huff> --verify\n");
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "compress") == 0) {
        if (argc < 4) {
            printf("Error: missing arguments for compression.\n");
            return 1;
        }
        Metrics m;
        ZipcStatus s = compress_file(argv[2], argv[3], &m);
        if (s != ZIPC_OK) {
            printf("Compression failed: %s\n", zipc_strerror(s));
            return 1;
        }
        print_metrics(&m);

        if (argc > 4 && strcmp(argv[4], "--verify") == 0) {
            char temp_out[256];
            snprintf(temp_out, sizeof(temp_out), "%s.verify", argv[2]);
            ZipcStatus ds = decompress_file(argv[3], temp_out);
            if (ds != ZIPC_OK || !files_identical(argv[2], temp_out)) {
                printf("Verification FAILED!\n");
                remove(temp_out);
                return 1;
            }
            remove(temp_out);
            printf("Verification PASSED: Round-trip matches perfectly.\n");
        }
    } else if (strcmp(cmd, "decompress") == 0) {
        if (argc < 4) {
            printf("Error: missing arguments for decompression.\n");
            return 1;
        }
        ZipcStatus s = decompress_file(argv[2], argv[3]);
        if (s != ZIPC_OK) {
            printf("Decompression failed: %s\n", zipc_strerror(s));
            return 1;
        }
        printf("Decompression successful.\n");
    } else {
        printf("Unknown command: %s\n", cmd);
        return 1;
    }

    return 0;
}