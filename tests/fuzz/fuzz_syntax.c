#include "fuzz_common.h"
#include "syntax.h"

#include <stdio.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;

    char path[128];
    snprintf(path, sizeof(path), "/tmp/tam-fuzz-syn-%ld.yaml", (long)getpid());
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }

    char err[128] = "";
    syntax_registry_load_yaml_file(path, err, sizeof(err));
    syntax_def_t *def = syntax_find_by_filetype("custom");
    if (def) {
        uint16_t groups[256];
        size_t rlen = 0;
        (void)syntax_highlight_line(def, NULL, (const char *)data, size,
                                    false, groups, 256, &rlen);
    }
    syntax_registry_free();

    unlink(path);
    return 0;
}
