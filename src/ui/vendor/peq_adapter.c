/* Binding of the P1 parser to ctl_parse_fn. Integration-tested by tests/test_integration.c with
 * the real P1 peq.c (P1 sources unmodified). */
#include "peq_adapter.h"

#include "peq.h"

int ctl_peq_parse(void *ctx, const char *text, unsigned size, unsigned *line, const char **reason) {
    const ctl_peq_options *opt = ctx;
    peq_preset preset;
    peq_error error = { 0, 0 };
    if (!peq_parse(text, size, &preset, &error)) {
        *line = error.line;
        *reason = error.reason;
        return 0;
    }
    if (opt && opt->rate > 0) {
        static peq_engine engine; /* ~2 KiB; UI thread only, keep it off the stack */
        if (!peq_compile(&preset, opt->rate, &engine, &error)) {
            *line = 0; /* compile errors number bands, not text lines */
            *reason = error.reason;
            return 0;
        }
    }
    return 1;
}
