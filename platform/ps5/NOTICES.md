# Native platform source provenance

The PE2 path/build/stage wrappers are CC0-1.0 under the project license.
The separately marked ps5_app_discovery.c/header retain MIT, adapting the
selected PE1 Build18 pe-portable-root.c identity-matching policy. No PE1
game structures, app IDs, discs or save serialization are imported.
This does not relicense linked runtime/dependency components or original
game content. Preserve root THIRD_PARTY_NOTICES.md and every input notice.

- The compatibility patch is relative to PsyZ
  `ff5457d8bfcf636c6e12941a71ee66c065a3e01f`. It contains only imported-SDL
  target acceptance, the native allocator-header branch and selected
  PS5-only GPU initialization/cleanup/device-property/display handoff changes.
  PsyZ platform code retains MPL2.0 and its other existing source notices.
- Public application CRT/base linker layout: BlackBearReloaded,
  GPL-3.0-or-later; PSXS5 reference
  `5c17ea31aec5791c12451e915386fa98d670adb4`.
- Native unwind/layout/platform support: Mihawk/Mihawk-99,
  GPL-3.0-or-later; PS5_PayloadSDK reference
  `b83202be73e930050e00de0f1b4d9ec46c0391bf`. The native provider supplies
  FP, heap/thread wrappers and directory services.
- The caller supplies the reviewed PE1 public native support recipe from
  GUMMIBRI/parasite-eve-ps5, `platform/ps5/native-gpu`. Preparation records
  SHA256 of the exact source inputs and link template. The small directory
  adapters' provenance is documented separately in directory/PROVENANCE.md.
  This is source reuse, not a clean-room claim.
- Compiler runtime: LLVM Project, Apache2.0 with LLVM exception. SDL and
  its altered native drivers retain their zlib/upstream contributor notices.
  Mesa/RADV and runtime libraries retain their own notices and corresponding
  source obligations. The recipe does not assert a complete Mesa rebuild.

The combined native binary must not be described as wholly CC0. Native
SDK ELF linking does not establish runtime ABI compatibility or permission
to redistribute console modules or original game assets. No proprietary
SDK, system module, retail image, extracted game data or personal save is
part of this source contribution. This contribution uses no Snaity code.
