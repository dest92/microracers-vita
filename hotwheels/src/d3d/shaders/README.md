# D3D6 GXP shaders

`d3d6_v.gxp`, `d3d6_f_pal.gxp`, and `d3d6_f_rgba.gxp` are checked-in program
binaries for the renderer. They are the workspace's previously generated
GXP outputs for the matching Cg sources, reused here with the reduced D3D6
attribute/state contract. The PAL fragment samples an index texture and a
256×1 palette, modulates by the white diffuse attribute, and discards the
configured colorkey. The RGBA fragment returns the texture; colorkey alpha is
discarded by the GL alpha test. With this D3D path, diffuse color is white, so
RGBA texture×diffuse is the sampled texture.

The GXP files and Cg source are GPL-3.0, from the D2Vita renderer in this
workspace. `COPYING-GPL-3.0` is included. No game assets or shader code from the
Hot Wheels executable are present.

The host VITASDK installation has no `psp2cgc` or ShaccCg compiler. The original
GXP binaries were generated from these sources through `sceShaccCgCompileProgram`
using `SCE_SHACCCG_PROFILE_VP` for the vertex shader and
`SCE_SHACCCG_PROFILE_FP` for the fragments, then saved as `.gxp`. To regenerate,
use a Vita homebrew build with the `SceShaccCg` stubs and a real
`libshacccg.suprx`, call the same compiler API with entry point `main`, and
replace the matching binary only after validating it on Vita3K and console.

`d3d6_gxp_blobs.h` is the aligned C embedding used by the VPK build. Regenerate
it after replacing a `.gxp` with:

```sh
python3 hotwheels/tools/embed_d3d_gxp.py
python3 hotwheels/tools/embed_d3d_gxp.py --check
```
