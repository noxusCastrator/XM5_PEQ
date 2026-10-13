# Provenance

`peq.c`, `peq.h` and `peq_store.c` are derived from q2-ringnav (MIT, (c) 2026 Diamond Bond):

- repo: https://github.com/DiamondBond/q2-ringnav
- commit: `d127c6ea07af20bde73dd842df255ce58d52e5be`
- `patch/peq.c` sha256 `0027ae8706392964ac1a7767f9e7bc0706152dbde453ff4b456b1eabb4c8da13`
- `patch/peq.h` sha256 `ec552001dc58542de994540faa93f9ddc38de3c0ea417dce90665e890ebd6d2e`

Unmodified copies are kept as `peq.upstream.c.orig` / `peq.upstream.h.orig`, and the upstream
`LICENSE` is in this directory. The modifications are listed in `../COMPATIBILITY.md`, and they are
under the same MIT licence.

No Equalizer APO (GPL-2.0-or-later) code was copied. The APO-compatible behaviour was written from
the public RBJ Audio EQ Cookbook formulas and from the semantics described in
`tasks/R1-apo-semantics/REPORT.md`.
