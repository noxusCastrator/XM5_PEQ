# P1 core: accepted Equalizer APO syntax and runtime behaviour

The behaviour here is the contract for `src/peq.c` and `src/peq.h`. APO semantics come from the R1 source reading (APO `bbfcc3e5`, which matches release 1.4.2). APO itself was never run. Each "R1" ID refers to a case in `tasks/R1-apo-semantics/scripts/cases.py`, and each one is tested.

## 1. File and lines

| Input | Result |
|---|---|
| UTF-8, with or without a BOM (the BOM is stripped) | accepted. This deliberately differs from APO, which probably loses the first line (R1 U1/X13) |
| file > 16384 B, line > 512 B (CR counts toward the 512) | reject |
| NUL byte (so UTF-16 is rejected), invalid UTF-8 (overlong, surrogate, > U+10FFFF) | reject |
| LF or CRLF line ends | accepted |
| a CR anywhere except one CR just before LF (this covers CR-only files) | reject |
| blank line; line whose first non-blank character is `#` | ignored |
| backtick anywhere outside a comment line (APO evaluates inline expressions on every line) | reject |
| line without `:` (exporter headers: `Filter Settings file`, `Room EQ V5.20`, `Averages`) | ignored, as in APO |
| line without `:` whose first word is a command word (any case: Preamp, Channel, FilterN, or one of the unsupported commands below) followed by a word that contains a digit or is `ON`/`OFF` | reject ("command without ':'"). APO would ignore it silently |
| key not an APO command (`Dated:`, `Notes:`, `Equaliser:`, `Foo:`) | ignored, as in APO. Non-ASCII text is allowed on these lines |
| Device, If, ElseIf, Else, EndIf, Eval, Include, Stage, IIR, BiQuad, Delay, Copy, Convolution, GraphicEQ, VSTPlugin, LoudnessCorrection (any case) | reject "unsupported command" |
| `preamp:`, `CHANNEL:`, `filter 1:` (case variants of the supported commands; APO would ignore them) | reject |
| non-ASCII byte on a Preamp, Channel or Filter line (for example NBSP) | reject |

## 2. Commands

**Numbers** use `[+-]digits[(.|,)digits][(e|E)[+-]digits]`, with at least one mantissa digit and at most 63 characters. They are converted with `strtod` after `,` is replaced by `.`, so results are correctly rounded (this fixes F3). Hex, `nan`, `inf`, overflow, `1.5.3` and `20junk` are all rejected.

**`Preamp: <n> dB`** takes `dB` in any case, separated or attached (`-6 db`, `-6dB`, `-6 DB`). A trailing `# comment` is allowed. A missing unit, or any other trailing text, rejects the line. Each line must be within −60..+24 dB. Repeated lines add up (inside the current Channel scope), and the sum must also be within −60..+24.

**`Channel: <names>`** splits on U+0020 only, as APO does. The names `L`, `R` and `ALL` are accepted in any case, so `L R` means both. Tabs, `#`, numbers, other names and an empty list are rejected. The existing Q2 L/R support is kept: bands go to one side, and per-side preamps become a common preamp plus a balance of at most ±12 dB. This matches the R1 model in C10, C12 and C17.

**`Filter[ N]: …`** The key is `Filter`, optionally followed by blanks and digits (`Filter1:` is accepted). Any other label is rejected.

- **`OFF …`** (any case): the line is ignored without validation (APO behaviour) and uses no band slot (this fixes F2). The exception is a backtick, which is still rejected.
- **`ON None`**: ignored. Any text after `None` is rejected.
- **`on`/`On`** (APO would ignore the filter) and any other first word are rejected.
- **`ON <T> Fc <f> Hz Gain <g> dB [Q <q>]`** is the canonical order. `Hz` and `dB` are case-sensitive, as in APO's regexes, and can be attached (`1000Hz`, `6dB`). Other orders, `BW Oct`, slope forms (`LS 6dB`), `Modal`, LP/HP/BP/NO/AP and repeated parameters are rejected.
  - **PK, PEQ**: Q is required.
  - **LSC, HSC**: Fc is the centre frequency. If Q is missing, S = 0.9 (stored as the equivalent Q, 1/√((A+1/A)(1/S−1)+2)).
  - **LS, HS**: if Q is missing, S = 0.9 at Fc with no shift. If Q is given, APO's corner shift is applied: centre = Fc·10^(±|G|/80/S), where 1/S = (1/Q²−2)/(A+1/A)+1. **If the shifted centre falls outside 20..20000 Hz, the file is rejected.** Upstream clamped it silently (F5).
- **Ranges** (unchanged upstream product bounds): Fc 20..20000 Hz, gain ±24 dB and Q 0.1..10, all checked before the shift. Q ≤ 0 is rejected (APO would treat a shelf with Q 0 as S = 0.9).
- **Thousands ambiguity**: an Fc token with 5 or more characters, no exponent, and `.` or `,` four characters from the end (`31.250`, `1,000`, `20.000`, `+31.250`) is rejected. APO multiplies such values by 1000.
- **`#` after a complete filter**: accepted as a comment only if the comment text contains none of `Q`, `BW`, `Fc` or `Gain`, because APO's regexes would read those (R1 X26). Otherwise the file is rejected.
- **Limit**: at most 30 ON bands, counting gain-0 bands.

Parsing is transactional. On any error, `*out` is left untouched and `error` holds the 1-based line number and a reason.

## 3. Runtime (compile/process)

- **`peq_compile`**:
  - Rejects a preset that is invalid or has a rate outside 8000..384000 Hz.
  - Rejects **any enabled band with a non-zero gain whose frequency is ≥ fs/2**. Upstream skipped that band and ran the rest (F6).
  - Rejects any biquad with non-finite coefficients or poles not strictly inside the unit circle (Jury test, `peq_biquad_stable`).
  - On failure, the error's `line` field holds the band number (0 means preset-level).
  - Coefficients and state are `double`, processed as TDF2, with RBJ Cookbook design per rate.
- **Bypass is selected and reported, never stale**:
  - `peq_reset` or `peq_update` with a preset that fails to compile selects bypass, sets `d->failed` and returns 0. Upstream `peq_update` returned 0 and left the previous filter running.
  - `peq_apply_text` is parse plus update. A rejected or missing (NULL) text selects bypass and returns 0, and the caller's preset is left untouched.
  - Passing `NULL` as the preset to `peq_reset` or `peq_update` is an explicit bypass request and returns 1.
- **Neutral is bypass**: if no band is effective and preamp = balance = 0 (for example an empty file, or only OFF, None or gain-0 lines), the engine is compiled as bypass.
- **Bypass output is bit-exact**: in steady bypass (user `bypass` flag, neutral preset, NULL, or failure), samples are not touched. This includes NaN payloads, ±Inf, finite values beyond ±1 and denormals. A transition from bypass to bypass is adopted immediately with no fade, so it is also bit-exact.
- **Active output policy (explicit)**: non-finite input samples become 0 before filtering, and the output is hard-clipped to [−1, 1] and converted to float. There is no automatic headroom or headphone curve. The preamp is the only gain stage.
- **Smooth updates are kept from upstream**:
  - Active ↔ active, active → bypass and bypass → active all crossfade linearly over fs/50 samples (20 ms).
  - Filter memory is carried over, and bands removed from the end drain their memory.
  - An update that arrives during a fade is queued and starts when the current fade ends, so the worst case is 40 ms before bypass is fully in effect.
  - During any fade, the active policy (NaN → 0, clip) applies. After the fade, bypass output is bit-exact.
- **Threading**: `peq_process` does no allocation or file access. `peq_update`, `peq_reset` and `peq_apply_text` write `d->pending` without locking (as upstream did), so the adapter must serialize them with `peq_process`.
- **Build**: the core builds as strict C99 (`-std=c99 -pedantic -Werror`). `peq_platform.h` maps libc and libm functions (`PEQ_PLATFORM_HEADER`, `PEQ_STRTOD`, `PEQ_ISFINITE`). `strtod` must use the "C" `LC_NUMERIC` locale. Building with `-ffast-math` is a compile error, because the code depends on `isfinite` and on the `+1e-18-1e-18` denormal flush.

## 4. API changes from upstream q2-ringnav `d127c6e`

- `peq_compile`, `peq_reset` and `peq_update` gained a `peq_error *` parameter. `peq_reset` now returns `int`.
- `peq_dsp` gained `failed`.
- New functions: `peq_apply_text`, `peq_biquad_stable`.
- `peq_import_file`, `peq_load`, `peq_load_active`, `peq_save` and the `/mnt/...` paths moved to `src/peq_store.{c,h}` (POSIX, optional, behaviour unchanged). `peq_preset` keeps the `Q2PEQ02` layout.
- The parser no longer stores OFF lines as `enabled = 0` bands. A UI that expects disabled bands to round-trip through import must keep them itself.
- `peq.h` no longer includes a generated `peq_platform.h` or uses `PEQ_HOST`.

## 5. Known differences from the APO model that remain

These are the deliberate, fail-safe rejections in R1 §3 (slope/BW forms, attached `Fc`/`Q` prefixes such as `Fc1000`, free parameter order, `Modal`, product ranges, PK without Q, channel numbers). BOM stripping is the one case where the core accepts a file APO probably mis-reads. Inside the accepted subset, the core computes in double TDF2 while APO computes in float32 per stage with DF1. The measured difference is in REPORT.md.
