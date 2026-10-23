# Licenses, provenance, and acknowledgements

The Hot Wheels port and its reusable winx86-dx integration are published under **GPL-3.0**; see [LICENSE](LICENSE). Third-party components keep their original licenses and notices. This is an independent homebrew project, not affiliated with or endorsed by Mattel or the original game publisher/developer. Hot Wheels and related names belong to their respective owners. No game license is granted by this source distribution.

## Included source

| Component | Origin / attribution | License / notice |
|---|---|---|
| winx86 / WinVita runtime | Franck Roustan; https://github.com/Franckrst/WinVita; base commit `3035e1ed3ba9eced7be0ea3fea2210aadbb5b52d` plus local port modifications | [MIT](third_party/winx86/LICENSE), [upstream notices](third_party/winx86/THIRD-PARTY-NOTICES.md) |
| Box86-derived ARMv7 translator | Sebastien Chevalier (ptitSeb) and Box86 contributors; included by winx86 | [MIT](third_party/winx86/third_party/box86-dynarec/LICENSE); retain individual source headers |
| khash | Attractive Chaos | MIT text in the vendored header |
| x86primop | SciTech Software, David Mosberger-Tang, Egbert Eich, SUSE / Steffen Winterfeldt | Distinct permissive license reproduced in [winx86 notices](third_party/winx86/THIRD-PARTY-NOTICES.md) |
| Runtime bitmap font | Rendered DejaVu Sans Mono, via winx86 | [DejaVu / Bitstream terms](third_party/licenses/DejaVu-copyright.txt), upstream generation notice |
| D3D6 Cg/GXP shaders | D2Vita renderer, adapted to this port; no game shaders | [GPL-3.0](hotwheels/src/d3d/shaders/COPYING-GPL-3.0), [provenance](hotwheels/src/d3d/shaders/README.md) |
| P8 blit shaders | Rinnegatamante and vitaGL contributors, vitaGL `c707ff00d9a372f0ba7056b4a4b723f5a00ba880` | LGPL-3.0; [provenance](hotwheels/src/present/shaders/README.md), [GPL text](hotwheels/src/present/shaders/COPYING) and [LGPL additional terms](third_party/licenses/LGPL-3.0.txt) |
| kubridge/vpmu weak import definitions | API names and NIDs only, with local regeneration sources | No kernel-plugin implementation or proprietary SDK binary is included |

The runtime is a **source snapshot with local modifications**, not an unmodified upstream release. The published commit is the source identity for this distribution; historical `winx86-*.patch` files document prior development and must not be blindly reapplied to this already-modified snapshot. [PUBLICATION.json](docs/PUBLICATION.json) records the upstream base and scope.

## Build-time external dependencies

`hotwheels/tools/build_vitagl.sh` identifies the sources and pinned revisions used for the rendering libraries:

- **vitaGL** — Rinnegatamante and contributors, LGPL-3.0, https://github.com/Rinnegatamante/vitaGL.
- **math-neon** — MIT, https://github.com/Rinnegatamante/math-neon.
- **vitaShaRK** — MIT, https://github.com/Rinnegatamante/vitaShaRK.
- **SceShaccCgExt** — GPL-3.0, https://github.com/bythos14/SceShaccCgExt.
- **VitaSDK**, its toolchain and libraries — respective upstream licenses, https://github.com/vitasdk.

These build dependencies are fetched separately; preserve their notices when redistributing linked binaries. This initial repository publication distributes source, not a binary release or an offer to redistribute Sony system modules. `libshacccg.suprx`, firmware, commercial game data, and third-party kernel plugins are not included.

## Local-only inputs

The retail executable, data, music, original icon/background BMPs, memory dumps, private run captures, and VPKs containing retail artwork stay local. The packaging tool can derive original LiveArea images from the owner's local files; the public build uses generated generic artwork when `HW_ORIGINAL_ART_DIR` is empty. This does not grant permission to redistribute those original assets.
