/* Binding of the P1 parser (peq.c) to ctl_parse_fn. */
#ifndef PEQ_ADAPTER_H
#define PEQ_ADAPTER_H

/* Optional parse context. rate > 0 additionally designs every band at that rate (peq_compile), which
 * catches e.g. a band at or above Nyquist. This is ADVISORY: the UI does not know the rate the player
 * will run at, so a preset can still be rejected by the player later (see README "Rate gap"). */
typedef struct { int rate; } ctl_peq_options;

/* ctl_parse_fn: ctx is NULL or a ctl_peq_options. */
int ctl_peq_parse(void *ctx, const char *text, unsigned size, unsigned *line, const char **reason);

#endif
