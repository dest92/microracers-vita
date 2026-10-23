# Initial source publication validation

This is an experimental source snapshot, not a hardware-tested binary release. No VPK or commercial game data are published. The README includes a separately labeled screenshot from an earlier Vita3K development run, with provenance in `docs/images/README.md`.

## Checks run on the exported tree

| Check | Result |
|---|---|
| Fresh portable CMake Debug build and CTest | **25 passed, 1 skipped** (retail executable absent), no failed tests |
| Thread CPU snapshot runner | Host and ThreadSanitizer PASS |
| Clock calibration fixture runner | Host and ASan/UBSan PASS; no physical unit calibration |
| Packaging image tests on clean export | 3 passed, 2 SDK-layout-dependent tests skipped |
| Standalone generic VPK packaging test with explicit VitaSDK | PASS, six entries, expected TITLEID |
| Fresh Vita build from exported sources | PASS, generic LiveArea, 1,957,362-byte private verification artifact |
| README Mermaid diagrams | All four parse with Mermaid 11.12.0; original sequence-diagram error reproduced before correction |
| Source scan | No executable/game/audio payloads, private keys or token patterns in selected sources |
| Documentation and publication review | [Acotado / limited review](reviews/publication-review-claude.md); not an exhaustive runtime audit |

The private verification VPK hash was `726568945a9e2b83511c5c51d557c05a72aad07e244945c2d9580b8ae9c550e0`; embedded eboot hash `5d30531a90e848f9f4765700ea0ddacaf2e90fb16500dc8abc75c3be37ddc02d`. These hashes identify a build check only; the binary was **not** deployed or run on Vita or Vita3K. VitaSDK and graphics archives were available in the build environment; the external graphics bootstrap was not rerun. Binary reproducibility across toolchain/Pillow versions has not been established.

The initial Release-mode host fixture attempt exposed setup inside `assert` (removed by NDEBUG), so the supported host fixture configuration is Debug. The D3D golden fixture writes under `hotwheels/build/`; that output directory must exist when selecting an alternative CMake build directory. An incomplete local capture-policy experiment referencing a missing header was excluded from this export and preserved in the private workspace. See [PUBLICATION.json](PUBLICATION.json).

## Earlier physical observations

The user completed OFFICE runs on the same installed e6749fb binary with music disabled and restored, reporting equal perceived fluency with music restored. Candidate-window medians were 22.8 guest Hz without music and 26.4 with music; ranges overlapped and windows included transitions. These are descriptive observations, **not** a causal A/B benchmark, stable frame-rate guarantee, screen refresh measurement, or validation of the newly compiled publication snapshot. Black beach textures with batching remain unresolved.

HW-075 has reviewed diagnostic instrumentation and a prepared isolated calibration sample. Physical calibration and measured serial-work offload remain pending. Requested affinity and last CPU are not measurements of total core utilization. The snapshot does not demonstrate multicore performance improvement.

[Build instructions](BUILDING.md) distinguish portable tests, ARM/QEMU integration and Vita builds. Use the pinned source and identify build/configuration/scene/evidence level when reporting new results.
