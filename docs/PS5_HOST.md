# Draft native PS5 host integration

This is an opt-in source contribution using the public native PS5 runtime.
Desktop builds retain their normal defaults. It adds the native entry/build
graph, PE2 storage routing, directory ABI bridge and a compatibility patch
for this project's exact PsyZ pin. It does not change PE2 combat, story,
inventory, save-file bytes or disc asset formats.

Status: a local predecessor linked and converted, but the first human console
attempt reported a black screen and choppy audio. Its log identified an SDL
GPU backend rejection. The revised device-property/header/library pairing
and startup guards subsequently reached gameplay in the owner's revised
console candidate. The owner reports character wobble and FMV audio stutter;
font/HD presentation work is incomplete. This is not full-game stability or
general firmware qualification. The public recipe below has separately
completed an actual native SDK compile/link with the unchanged dependency
pin and its build-local compatibility patch; no public-recipe game run was
performed.

## Build prerequisites

Initialize the existing pinned dependencies using the normal project setup.
The PsyZ submodule remains at `ff5457d8bfcf636c6e12941a71ee66c065a3e01f`;
do not update it to PsyZ main to use this contribution. A small compatibility
patch in `platform/ps5/patches` is applied to an exported build-local copy.
The checked-out submodule is never patched by the recipe.

The native recipe currently uses Windows host tools, Node.js, Git, tar,
CMake, Ninja and LLVM18 with the public PS5 payload SDK. It requires explicit
local paths to a reviewed public native prefix and source dependencies:

- `prefix`: public LLVM, SDK, RADV/Mesa, compiler-rt, native runtime source
  and libraries in the layout used by the PE1 public native build recipe.
- `recipeSupport` and `linkTemplate`: reviewed public native application
  CRT, filesystem/dynamic-loader adapters and link-response template.
- `sdlSource`, `sdlBuild`, **`sdlInstall`**: matching altered native SDL
  source, generated configuration and installed static library/headers.
- `webpSource`, `cmake`, `ninja`: local source/tools; dependency fetching is
  disabled. No game images, extracted data or saves are build inputs.

Copy `platform/ps5/tools.example.json` to an ignored local configuration and
replace every placeholder with the matching absolute local input. Use a
fresh output directory beside the checkout, not the checkout or a shared
dependency directory:

```text
node platform/ps5/build-native.cjs <local-inputs.json> ../pe2-native-build
```

The recipe exports only committed PsyZ/decomp sources from the exact pin,
checks/applies `psyz-pe2-native-host.patch` in its own staging directory and
passes that staged `PSYZ_DIR` to CMake. It rebuilds native CRT/compiler-runtime
support, derives local archive/layout variants and compiles the PE2 game,
overlays and variants into `game/parasite-eve-ii-ps5.elf`. It emits input
hashes, exact commands and build/layout/import logs. Use a fresh output if
the source compatibility patch changes; existing staging is not overwritten.
Host test executables are omitted from this cross-build graph.

Pinned source export and patch application both override Git's `core.autocrlf`
to false and `core.eol` to LF for those commands only. This avoids host-wide
Git for Windows settings changing archive or patched bytes. The compatibility
manifest binds exact LF before/after file hashes and the exact patch digest;
verification never silently normalizes a mismatching result. Failed output
directories remain available for diagnosis and are not overwritten.

The retained Mesa/RADV and SDL archives are not rebuilt by this driver.
This is explicit local integration, not a complete public dependency/source
build closure. Conversion, application metadata, installation, deployment,
branding and retail data are intentionally outside this source contribution.

When preparing the external native SDL build with a compile-only CMake
try-compile toolchain, set `LIBC_HAS_WCSNLEN=OFF`, `LIBC_HAS_WCSLCPY=OFF` and
`LIBC_HAS_WCSLCAT=OFF` for this public runtime. The SDK headers declare these
functions but the target runtime does not export them; a compile-only probe
can otherwise produce a false positive. These are the `LIBC_HAS_*` cache
variables, not `HAVE_*`. This selects SDL's real fallback implementations;
do not substitute fake-success runtime functions. Native linking still must
resolve every remaining referenced symbol normally.

## Required native SDL pairing

Stock desktop SDL is insufficient. The native SDL tree must include the
public PS5 video/audio/pad and static Vulkan/RADV integration. For the
reviewed native RADV device, its header exposes
`CPE_PROP_GPU_DEVICE_CREATE_PS5_RADV_NONCONFORMANT_BOOLEAN` with property
string `CPE.gpu.device.create.ps5.radv.nonconformant`; its Vulkan backend
consumes that explicitly opted-in property to admit the device's version
report while retaining ordinary capability checks. The PsyZ patch requests
SPIR-V and this property only under `PE2_PS5_HOST`.

The driver checks source and installed headers, the Vulkan property's source
consumer, the generated configuration and the property's presence in the
installed static archive. It records their hashes. These checks reject the
known stale-library/header mismatch, but do not replace an actual SDK build
or runtime ABI validation. `sdlInstall` is required: there is no fallback to
an older `prefix/install-sdl` archive. Existing generic imported-SDL support
in newer PsyZ main is not proposed again; this local compatibility patch is
specifically for PE2's older unchanged pin.

The small allocator-header companion is proposed independently in
[PsyZ PR7](https://github.com/khasinski/parasite-eve-psyz/pull/7). This recipe
does not change PE2's pin or assume that proposal has merged.

## Native startup and storage

The reviewed application CRT calls `pe_application_stage` before environment
initialization and constructors. Its first invocation establishes the native
platform's documented IEEE floating-point state through `ps5_fp_ieee`;
wrapped child threads inherit that MXCSR. Early output uses the public
kernel open/write/fsync/close functions without libc/errno/TLS. It appends
to `/mnt/usb0/parasite-eve-ii-startup.log` only after no-follow directory
checks for the existing `/mnt` and `/mnt/usb0`; failure leaves inherited
output only. It creates no mount or alternate log root.
The native runtime must provide that FP function and its thread/heap wrappers.

GPU failure is returned to main before original game/audio playback; partial
renderer resources are released, and a failed attempt is not silently retried
per draw. A normal display submission performs the bounded native shell
splash handoff. No fake frame or game-event completion is introduced.

Settings default to `/mnt/usb0/parasite-eve-ii/settings.ini`; card files use
`/mnt/usb0/parasite-eve-ii/memory-cards/card0` and `card1`.
`PE2_CONFIG_DIR` and `PE2_MEMORY_CARD_DIR` retain priority only within the
checked existing `/mnt/usb0`..`/mnt/usb7` mounts. `/data`, `/mnt/homebrew`,
relative paths and traversal are rejected for writes in this USB-only policy.
Each component is opened with the actual SDK no-follow/directory flags;
no libc `stat` layout or `realpath` ABI is assumed. Missing descendants may
be created only below an already opened existing mount; missing mounts,
symlinks, IO/close failures and unsupported paths fail closed with a reason.
There is no silent migration, cwd fallback or copying of disc/save bytes.
Settings/temp-CUE publication, card-temp rename and native menu dumps are
checked at their actual write boundaries. Desktop defaults are unchanged.

Disc arguments, environment and persisted choices retain priority. Without
those, a readable nonempty same-folder pair in a uniquely identity-matched
application's `parasite-eve-ii/discs` wins; matching compares the candidate
eboot with active `/app0/eboot.bin` using bounded raw reads. The current
package namespace is a candidate location, never proof of executable identity.
Application IO/ambiguity stops default selection; missing/incomplete app data
permits exactly one complete external pair under USB/homebrew read roots.
Read roots do not grant write permission. A configured disc searches only
its own original directory for a missing companion; partial roots are never
joined. Default pair paths are captured before BIN-to-CUE normalization.
USA CUE aliases and canonical `disc1.bin`/`disc2.bin` remain subject to the
normal retail identity/actual image loader. Generated CUEs go into checked
preferences storage, never the app or image folder. A nonempty CUE alone
is not proof its referenced BIN is present or valid.

The directory bridge crosses from the game SDK's DIR/dirent layout to the
exact native provider SDK through opaque handles and copied name/type/inode
values. PE2 retains its save format, block limits, temp-write/fsync/rename
logic and real error handling. No PE1 cards or saves are imported.

See `platform/ps5/NOTICES.md` for component provenance and license limits.
