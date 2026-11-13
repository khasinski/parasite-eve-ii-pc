# Tester follow-up: 0.3.4-alpha

The tester's “PE2 v3.4a” report is tracked here as 0.3.4-alpha. The owner
confirmed Windows. Renderer, exact location and reproduction steps remain
unknown.

| Report | Status | Follow-up |
| --- | --- | --- |
| Gun Range dialogue-box graphical glitch | Resolved according to tester | Keep dialogue regression coverage. |
| Starting Gun Range level 3 | Tester identified skipped Pierce instructions | No gameplay change requested for this observation. |
| Incorrect weapon-loading SFX | Appears resolved according to tester; observation continues | Do not mark definitively closed or change audio without a new reproduction. |
| 3D actor/model and background inconsistency | Open | Need the affected scene and settings; no reproduction established yet. |
| Green/yellow combat graphics, possibly reticle or enemy HP | Open | Need the affected scene and settings; do not infer the cause from color alone. |
| Save button blends into editor UI | Implemented locally | Prominent blue button, white bold label, contrasting border, larger hit area. Always visible; disabled until a slot is available. Label: “Save changes to card”. Persistence behavior unchanged. |

## Local verification

The editor passes 16 unit tests and one CLI persistence integration test,
including saving and reopening a standalone save without native windows.
`cargo fmt --check`, `cargo check --locked` and a release build pass.
The button's appearance has not been reviewed in a native desktop window.

Four focused native tests pass: HP packet geometry, GPU state, texture-page
inheritance and player targeting. These are scoped regression checks, not
proof that the two reported graphics bugs are fixed.

Hidden-Metal captures use private configuration/card directories, original
backgrounds, and no widescreen art. Native geometry, corrected geometry and
corrected geometry with AA were compared at identical simulation states:

- Gallery course 1, frame 2924: no active lock-on reticle packet, so this
  capture does not cover the reported reticle symptom.
- Fire Escape combat, frame 2786: one active lock-on reticle packet in each
  mode, with the same native VRAM and normalized GPU command stream. Rendered
  frames were inspected, including the green/yellow reticle and yellow damage
  readout. The tester's unspecified corruption has not been reproduced here.

Evidence and replay scripts are retained privately in
`build-hd-study/feedback-034a-checkpoint/combat-probe/`. These checks cover
specific frames on macOS Metal, not every animation, transition or platform.

## Follow-up investigation

An additional graphics-preference bug was reproduced and fixed locally. A
configured scale of 4x was used by the running renderer but rewritten as 1x
by startup's settings save before the pending render resolution was applied.
Startup now preserves the loaded scale (including the widescreen minimum);
saving from the graphics menu still records the applied live value.
This is not established as the cause of either tester graphics report.

Six hidden-Metal launches exercise two consecutive starts for each of scale4,
scale8 and scale4 with AA/model filtering/smooth lighting. They retain their
selected settings and reach the same combat simulation and native VRAM, with
an active reticle in every capture. The all-enhancements frame was visually
inspected. Scripts and evidence are in
`build-hd-study/feedback-034a-checkpoint/settings-regression/`.
The same scale4 restart check also passes twice in the normal build with HD backgrounds disabled. All60 native CTest checks pass. The settings menu painter/aspect probe passes in both builds without a desktop window.

Browser access is unavailable in this session, so Discord attachments could
not be retrieved. Both original graphics reports remain open pending a
specific affected scene, screenshot/video, platform and renderer. The
green/yellow reticle and yellow damage number in the reached reference frame
are not, by themselves, a reproduction of the unspecified corruption.

## Additional graphics search (2026-11-13)

Twelve paired checkpoints (24 completed hidden-Metal captures) now cover
Fire Escape combat at frames 2700/2750/2800/2860/3000, No.9 on the roof at
3800/4300/4800/5200/5600, gallery course 3 at 15320, and another Fire Escape
2860 comparison in widescreen at scale 3. The 4:3 pairs use scale 4 with
geometry correction, filtering and lighting off versus on. The widescreen
pair also enables AA in the enhanced mode; logs confirm internal scale 3
versus 6 with AA.

Every completed pair has equal simulation state, native VRAM and normalized
GPU commands, with complete GPU audits. Rendered samples were inspected,
including active reticle animation, damage, No.9 attack poses and the gallery
camera. No reproduction was established. These are sparse samples; command
equality does not compare PGXP metadata or prove intermediate frames correct.

Private neutral input tails extend the retained recordings without changing
their original prefixes. Gallery frame 16000 was not captured: both extended
replays reached input EOF at 26217. Earlier short-input EOFs and gallery
65-second time limits are also excluded from completed capture counts.
Scripts, provenance, images and machine-readable assessment are retained in
`build-hd-study/feedback-034a-search/`. Both tester reports remain open.

## Windows-specific follow-up

Windows x64 uses LLP64, unlike the local macOS LP64 environment. The game's
`uintptr` alias incorrectly used `unsigned long`, truncating pointer casts to
32 bits on Windows. It is now `uintptr_t`, with a host pointer-width assertion.
Uses include sound-heap address validation and memory-copy/fill alignment.
The alignment masks only retain low bits; the sound-heap comparisons need the
full address. This is a verified portability defect, not an established cause
of either reported graphics symptom.

A Windows-target GCC probe fails both width and high-address assertions before
the change and passes afterward. A syntax-only Windows x64 pass over 829 game
and port translation units reports no pointer/integer cast-size warnings after
the fix. Legacy incompatible-pointer diagnostics are downgraded for this GCC
audit, matching the existing permissive decompiled-source build policy; this
is not a complete Windows build or runtime test. The native game rebuild and
all 60 native CTest checks pass. Evidence is retained privately under
`build-hd-study/windows-feedback-search/`.

The default SDL_GPU implementation selects DXIL/D3D12 on Windows and MSL/Metal
on macOS. Current Windows CI runs the portable suite with `PSYZ_HEADLESS=1`
(software), so it does not validate D3D12 rendering. No Wine/Windows runtime
was available on the Mac; the subsequently located remote VM is covered below.
Read-only review of GPU queue handling, shader bindings
and packet layouts has not established another defect. The D3D12 descriptor
rollover fix is already present in the SDK revision shipped with 0.3.4-alpha.
Both graphics reports remain open; Metal captures do not rule out a Windows
renderer-specific fault. SDK sources were not modified.

## Windows VM validation (2026-11-13)

At the owner's suggestion, located and started the existing `aya-win11` VM
on `darwine`. SSH access works through the host. An isolated workspace is
`C:\Users\hasik\pe2-feedback-20261113`. The guest reports Microsoft Basic
Display Adapter, but an actual DXGI/D3D12 probe successfully creates a device
on Microsoft Basic Render Driver. The game also explicitly logs
`SDL_GPU with driver direct3d12`; this supersedes older Rage-project notes
about the VM lacking a supported backend. Rendering is CPU-backed, not a
test of the tester's physical GPU/driver. No driver or SDK source was changed.

Installed the CLANG64 build prerequisites, built the current local source
snapshot, and passed all 24 portable Windows CTests. Five additional direct
invocations (`gpu_tpage_test`, `hud_hp_packet_test`, `tmd_prim_test`,
`tmd_model_test`, `gte_compat_test`) pass with `PSYZ_HEADLESS=gpu` and dummy
audio. GPU-initializing checks explicitly log D3D12; pure GTE checks do not
become renderer tests merely by setting that environment variable.

Downloaded the published 0.3.4 Windows experimental archive and verified its
SHA256 against release `SHA256SUMS`:
`53c69afc47dae7df11ce32e03affc0ae0055020db4ceb9d644d7defdc327bf43`.
Eight completed captures cover:

- Current Fire Escape at frame 3000, software versus D3D12, scale 1. This
  initial firing input has no active reticle; native VRAM differs between
  rasterizers, so no bit-equality claim is made for this pair.
- Current versus published Fire Escape, frame 3000, held Square for active
  targeting: D3D12 scale 1, and scale 2 with AA, geometry correction, model
  filtering and lighting enabled (physical raster scale 4).
- Current versus published No.9 rooftop scene, frame 4868, using a fresh
  ordinary-controller route. No active reticle in these endpoint images.

Each current/published pair has identical simulation state, native VRAM,
normalized GPU commands and rendered image, with complete packet audits.
All four held-Square images contain one active reticle packet. The images
were visually inspected; neither reported corruption was reproduced. These
are sparse checkpoints, not continuous camera/animation validation.

Prior private replay artifacts were unavailable in the current local tree;
fresh inputs were recorded in the VM and retained. The No.9 trace preserves
the first 1000 boot samples and then uses documented Cross/Square/R1 button
actions. No game-memory edits were used. The initial capture harness failed
to retain exit codes (null), although reports completed without timeouts;
the corrected No.9 harness records exit 0. An initial optional-test invocation
before its binaries were built is explicitly excluded; rebuilt final direct
invocations all record exit 0.

Logs, inputs, source archive, image comparisons, hashes and assessment are
retained privately in `build-hd-study/windows-vm-feedback/` and in the VM's
isolated workspace. No game process remains running. The VM remains on for
further investigation. Both tester graphics reports remain open.

## Paused artwork handoff

The owner requested that background work be paused in favor of gameplay fixes.
The private checkpoint is
`build-hd-study/widescreen-art-20261113/checkpoint.json`. A patch, archive of
changed source files and manifest were saved before the feedback changes under
`build-hd-study/feedback-034a-checkpoint/`. The owner's `hd_handoff.md` was not
included or modified. No artwork, card data or game assets were committed.

The selected private pack remains v5 with 13 unapproved candidates. Under the
new near-cyan interpolation-footprint gate, 12 reproduce byte-identically;
view 13 still needs overscan repair. The attempted v8 repair failed native
registration, and the subsequent border-only alignment failed coverage. The
original v7 candidate remains retained with the open validation limitation.
The complete 1759-background objective remains unfinished and paused.
