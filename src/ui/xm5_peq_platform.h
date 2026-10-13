/* U5 platform header for the vendored P1 core (selected with
 * -DPEQ_PLATFORM_HEADER='"xm5_peq_platform.h"'). P1's peq_platform.h includes this instead of the
 * hosted <math.h>/<stdlib.h>/<string.h> defaults, which lets U5 point PEQ_STRTOD at a narrow adapter
 * without editing any P1 source. The adapter (src/runtime.c) converts ONLY strings P1 has already
 * validated as clean decimal tokens, delegating the numeric conversion to libc atof (the demo's own
 * atof@GLIBC_2.0 on target, the host libc under -DU5_HOST) and setting endptr to the token end.
 * isfinite comes from <math.h> as before, so P1's overflow/inf rejection is unchanged. */
#ifndef XM5_PEQ_PLATFORM_H
#define XM5_PEQ_PLATFORM_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

double u5_peq_strtod(const char *s, char **endptr);
#define PEQ_STRTOD u5_peq_strtod

#endif
