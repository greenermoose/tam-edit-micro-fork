#ifndef TAM_FUZZ_COMMON_H
#define TAM_FUZZ_COMMON_H

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#ifndef FUZZ_LIBFUZZER
static uint64_t fuzz_rng_state = 0x9e3779b97f4a7c15ULL;

static uint32_t fuzz_rand32(void)
{
    uint64_t x = fuzz_rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    fuzz_rng_state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

static void run_seed_file(const char *path, uint8_t **last_buf, size_t *last_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 65536) {
        fclose(f);
        return;
    }
    uint8_t *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    LLVMFuzzerTestOneInput(buf, n);
    free(*last_buf);
    *last_buf = buf;
    *last_len = n;
}

int main(int argc, char **argv)
{
    uint8_t *seed_buf = NULL;
    size_t seed_len = 0;

    for (int i = 1; i < argc; i++) {
        DIR *d = opendir(argv[i]);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char p[512];
                snprintf(p, sizeof(p), "%s/%s", argv[i], de->d_name);
                run_seed_file(p, &seed_buf, &seed_len);
            }
            closedir(d);
        } else {
            run_seed_file(argv[i], &seed_buf, &seed_len);
        }
    }

    if (!seed_buf) {
        seed_buf = malloc(16);
        memcpy(seed_buf, "hello world\n", 12);
        seed_len = 12;
    }

    uint8_t work[4096];
    for (int iter = 0; iter < 2000; iter++) {
        size_t len = seed_len;
        if (len > sizeof(work) - 1) len = sizeof(work) - 1;
        if (len > 0) memcpy(work, seed_buf, len);

        int mode = (int)(fuzz_rand32() % 4);
        if (mode == 0 && len > 0) {
            /* Bit flip */
            work[fuzz_rand32() % len] ^= (uint8_t)(1u << (fuzz_rand32() % 8));
        } else if (mode == 1 && len > 1) {
            /* Truncate */
            len = fuzz_rand32() % len;
        } else if (mode == 2 && len < sizeof(work) - 1) {
            /* Insert random byte */
            size_t pos = len ? (fuzz_rand32() % len) : 0;
            memmove(work + pos + 1, work + pos, len - pos);
            work[pos] = (uint8_t)(fuzz_rand32() & 0xff);
            len++;
        } else {
            /* Fresh random bytes */
            len = fuzz_rand32() % 128;
            for (size_t j = 0; j < len; j++) {
                work[j] = (uint8_t)(fuzz_rand32() & 0xff);
            }
        }
        LLVMFuzzerTestOneInput(work, len);
    }

    free(seed_buf);
    return 0;
}
#endif

#endif
