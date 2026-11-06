# Third-Party Notices

The optional native PS5 build also consumes public native runtime components
under their own licenses, including GPL-3.0-or-later startup/layout/platform
code. See [platform/ps5/NOTICES.md](platform/ps5/NOTICES.md); the combined
native binary is not wholly CC0. Desktop dependency selection is unchanged.

This repository's own code is released under CC0 1.0 ([LICENSE](LICENSE)). It contains and links the following work
by others. These notices do not relicense the original game, which is not part of this repository.

## parasite-eve-2-decomp

[GabeRealB/parasite-eve-2-decomp](https://github.com/GabeRealB/parasite-eve-2-decomp), the matching decompilation this
repository started from (source in `src/`, `include/`, `configs/`, asset tooling in `tools/peassets`). CC0 1.0.

## PsyZ and SDL

The application links our source fork of [PsyZ](https://github.com/khasinski/parasite-eve-psyz) (upstream:
[Xeeynamo/psyz](https://github.com/Xeeynamo/psyz)) and its pinned SDL submodule through `third_party/psyz`. PsyZ's
platform and runtime source is under MPL 2.0; its `decomp/` directory retains its MIT notice, and its top-level LICENSE
identifies headers and PSY-Q source that carry no licence. SDL uses the zlib licence, with additional notices for the
libraries it bundles. The matching sources are the pinned submodules of this repository.

## sotn-decomp and psy-q-decomp

PsyZ builds on PSY-Q reimplementation work from [Xeeynamo/sotn-decomp](https://github.com/Xeeynamo/sotn-decomp)
(MIT) and [sozud/psy-q-decomp](https://github.com/sozud/psy-q-decomp) (MIT); see PsyZ for details.

## PSY-Q

*Parasite Eve II* was built with Sony's PSY-Q SDK. No PSY-Q library or header from the SDK is distributed here; the
port uses PsyZ instead.

## stb_truetype and IBM Plex Sans (host menu)

`src/port/ui/stb_truetype.h` is Sean Barrett's stb_truetype (public domain). `src/port/ui/fonts/ibm_plex_sans.ttf.inc`
embeds IBM Plex Sans under the SIL Open Font License 1.1; the licence text is `src/port/ui/fonts/OFL-IBMPlex.txt`. Both
are used by the settings overlay, as in the first game's port.
