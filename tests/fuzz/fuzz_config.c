#include "fuzz_common.h"
#include "config.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;

    char dir[128];
    snprintf(dir, sizeof(dir), "/tmp/tam-fuzz-cfg-%ld", (long)getpid());
    mkdir(dir, 0755);

    char ini_path[192];
    snprintf(ini_path, sizeof(ini_path), "%s/settings.ini", dir);
    FILE *f = fopen(ini_path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }

    config_t cfg;
    config_init_defaults(&cfg);
    custom_keybind_t *kb = NULL;
    (void)config_load_file(ini_path, &cfg, &kb);
    keybinds_free(kb);

    /* Also exercise JSON5 importer */
    char sjson[192], bjson[192], out_ini[192];
    snprintf(sjson, sizeof(sjson), "%s/settings.json", dir);
    snprintf(bjson, sizeof(bjson), "%s/bindings.json", dir);
    snprintf(out_ini, sizeof(out_ini), "%s/out.ini", dir);
    f = fopen(sjson, "wb");
    if (f) { fwrite(data, 1, size, f); fclose(f); }
    f = fopen(bjson, "wb");
    if (f) { fwrite(data, 1, size, f); fclose(f); }

    FILE *devnull = fopen("/dev/null", "w");
    (void)config_import_micro(dir, out_ini, devnull ? devnull : stderr);
    if (devnull) fclose(devnull);

    unlink(ini_path);
    unlink(sjson);
    unlink(bjson);
    unlink(out_ini);
    rmdir(dir);
    return 0;
}
