#include "fuzz_common.h"
#include "regex.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size > 4096) return 0;

    /* Split input on first NUL or midpoint into pattern + haystack */
    const uint8_t *nul = memchr(data, '\0', size);
    size_t pat_len = nul ? (size_t)(nul - data) : (size / 2);
    if (pat_len > 256) pat_len = 256;

    char pat[257];
    memcpy(pat, data, pat_len);
    pat[pat_len] = '\0';

    const char *hay = (const char *)(data + pat_len + (nul ? 1 : 0));
    size_t hay_len = (size > pat_len + (nul ? 1 : 0))
                         ? (size - pat_len - (nul ? 1 : 0))
                         : 0;

    char err[128] = "";
    tam_regex_t *re = tam_regex_compile(pat, 0, err, sizeof(err));
    if (re) {
        tam_regex_match_t m = {0};
        (void)tam_regex_find(re, hay, hay_len, 0, &m);
        tam_regex_free(re);
    }
    return 0;
}
