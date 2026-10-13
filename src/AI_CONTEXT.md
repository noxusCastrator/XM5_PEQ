# XM5 PEQ: source context for AI assistants

Read this before changing code. For converting a user's EQ settings, use [AI_EQ_CONVERSION.md](../docs/AI_EQ_CONVERSION.md) instead.

## Release and scope

The current snapshot is 0.1.0 a (alpha), based on ONIX XM5 V2.43, with internal firmware tag V2.43-PEQ08. It adds preset import and native PEQ controls to the stock system. The original ten-band EQ remains available. This is neither a complete Equalizer APO port nor a full-system emulator.

The owner confirmed installation and usability on one device. A separate build from this publication tree reproduced all four overlays and the complete image byte for byte. These results do not establish analog-output equivalence, long-term stability, compatibility across devices or recovery after a failed update. The last small layout changes did not receive another functional suite or an independent full-package verifier.

Shanling provided technical support and an official recovery package. No approval of this modification or redistribution permission has been established. License boundaries are in [THIRD_PARTY.md](../THIRD_PARTY.md).

## Where to look

- `audio/xm5_peq_player.c`: player adapter, stream guards, native PCM processing and stock EQ sequencing.
- `audio/vendor/peq.c` and `peq.h`: parser, coefficient compilation and DSP core; upstream provenance and MIT notice are alongside them.
- `ui/layer_owner.c`: independent switch state, persistence and stock EQ communication.
- `ui/vendor/controller.c` and `native_files.c`: preset listing, validation, internal copy and reload requests.
- `ui/vendor/peq_awtk_view.c`: AWTK PEQ view and interactions; four visible preset rows.
- `ui/binder.c`: bindings to native UI, including the animated current-playback shortcut.
- `startup/`: rescue hook, its invocation and guarded insertion metadata.
- `../tools/build.py`: build orchestration; `../release.json`: pinned release and output hashes.

## Audio contract

For supported local PCM at or below 48 kHz, the independent switches select bypass, PEQ only, stock only, or PEQ followed by stock. Above 48 kHz through 192 kHz, PEQ processes at the actual sample rate and stock EQ is skipped while its saved switch state is retained. There is no resampling to 48 kHz. Supported native high-rate representations are little-endian S16, S24, S32 and float32.

DSD/DoP, unknown or unsafe stream formats bypass PCM DSP. USB DAC mode is outside this local-playback path. Do not broaden processing coverage by removing these guards.

The PEQ core uses double-precision RBJ biquads with transposed direct form II. Active processing sanitizes non-finite input and hard-clips output to [-1, 1]. Steady bypass leaves samples untouched. Updates use a 20 ms crossfade; a pending update during a fade can extend the transition to 40 ms. No automatic preamp adjustment or limiter guarantees freedom from clipping. Stock EQ can add gain after PEQ. Update/reset and processing must be serialized by the adapter.

## Preset and UI contract

Presets come from `/mnt/mmc/EQ`, without recursive scanning. Up to 64 files are listed; filenames must be valid UTF-8, fit within 63 bytes including the extension, and satisfy the controller's path restrictions. Files are limited to 16384 bytes including the saved source header, and lines to 512 bytes. Use the [format reference](../docs/CONFIG_FORMAT.md) and [AI conversion guide](../docs/AI_EQ_CONVERSION.md) for supported syntax, shelving semantics and rejection rules. Do not silently drop unsupported effects.

Selecting a valid preset saves an internal copy at `/mnt/data/peq/active.txt`; playback does not continuously read the TF-card file. Selecting while PEQ is off leaves it off. Rescan refreshes the list; selecting again imports an edited file. Layer state is stored in `/mnt/data/peq/layers.conf`. Missing or invalid configuration must fail to PEQ bypass.

Preserve the original EQ resources, native swipe navigation, independent switches, shared animated playback shortcut, English/Simplified Chinese strings, four-row layout and stock-aligned OFF artwork. Hidden widgets must not enlarge the scroll region or intercept touches.

## Build and change boundaries

Follow [BUILD.md](../docs/BUILD.md). Supply the exact official firmware and GCC 12 MIPS toolchain locally. The build stages its own Linux tree and changes only:

```text
release/bin/demo
usr/bin/hciplayer
etc/init.d/S90play
release/assets/default/raw/ui/playset/equalizer_page.bin
```

The kernel is unchanged. Fixed-address injection depends on exact baseline bytes, ABI settings and Linux metadata. Keep hash guards, patch-byte checks, resource subtree checks and rootfs size constraints. Never copy a whole extracted rootfs through an ordinary Windows copy or ZIP workflow.

The current build requires the frozen output hashes in `release.json`. A deliberate code change needs new reviewed expectations; do not merely replace hashes to conceal an unexplained mismatch. `SOURCE-MANIFEST.json` records source provenance and hashes.

Firmware, stock assets, recovery packages, toolchains, personal presets and device dumps stay outside Git. The publication tree is separate from the development workspace. Do not publish, send mail, connect to hardware or flash a device as a side effect of documentation or source work. Startup hooks depend on reaching startup and are not a proven universal recovery path. USB Boot communication alone does not establish restoration.
