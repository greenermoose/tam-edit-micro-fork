#include "fuzz_common.h"
#include "actions.h"
#include "editor.h"

#include <tam_key.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 1024) return 0;

    editor_t ed;
    editor_init(&ed);

    tam_key_parser_t dec;
    tam_key_init(&dec);
    tam_key_feed(&dec, data, size);

    tam_key_event_t ev;
    int guard = 0;
    while (tam_key_next(&dec, &ev) > 0 && guard++ < 64) {
        actions_handle_key(&ed, &ev, 24);
    }

    editor_free(&ed);
    return 0;
}
