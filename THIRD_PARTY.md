# Provenance and license boundaries

The PEQ core used by the audio and UI components derives from [DiamondBond/q2-ringnav](https://github.com/DiamondBond/q2-ringnav), revision `d127c6ea07af20bde73dd842df255ce58d52e5be`, under MIT. LICENSE and UPSTREAM.md files are retained beside the relevant sources. That license applies to the components it covers.

The project contains custom player adaptation, text compatibility handling, and native UI bindings. Minimal AWTK interface declarations and related adaptation still require a file-level provenance and license review. Equalizer APO application source is not included; full application compatibility is not claimed.

Builders obtain stock V2.43 executables, artwork, fonts, and system files from their own official input. The source tree includes only the added startup hook and insertion metadata, not the complete stock S90play script. The local modified image in `dist/` contains stock material; its presence does not establish redistribution permission.

There is no repository-wide LICENSE yet. Permission to publish source, distribute modified firmware, and reuse stock material remains to be confirmed with Shanling. Any vendor response must also be considered alongside the licenses of other upstream components.
