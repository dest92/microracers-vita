# Building the public source snapshot

Run commands from the repository root on Linux (or a Linux filesystem in WSL). Native Windows checkouts must preserve symlinks; a checkout that converts them to plain text cannot use the compatibility paths. This checkout contains the modified runtime source needed by the port, including the Box86-derived translator; no D2Vita game installation or private monorepo is needed. The directories under `d2vita/D2Vita/third_party/` are compatibility symlinks to the source-only `third_party/` tree, retained because historical test/build scripts use those paths.

## Portable host tests (no retail files)

Requirements: CMake 3.24+, C/C++17 toolchain, pthread, Python 3; Pillow for packaging image tests. Build in **Debug**: several legacy test programs use `assert` for both setup and checks, and `NDEBUG` removes those operations. A Release-mode test run is not a valid validation of those fixtures. This does not control Vita's separate `-O2` production build.

```sh
cmake -S hotwheels -B hotwheels/build -DCMAKE_BUILD_TYPE=Debug
cmake --build hotwheels/build -j4
ctest --test-dir hotwheels/build --output-on-failure
```

The retail-image skeleton check may skip when the executable is absent; standalone synthetic tests still exercise the PE loader. Additional suites:

```sh
bash hotwheels/tests/run_thread_cpu.sh
bash hotwheels/tests/run_clock_calibration.sh
python3 hotwheels/tests/test_sce_sys.py
```

Sanitizer runs require a supported host kernel; TSan runners explain their prerequisites. The broader `hotwheels/tests/run_all.sh` discovers standalone C++, Python and shell suites. Some are development-environment/integration checks and require Docker, Wine, ARM Linux tools, VitaSDK or the user's retail files; skips are distinct from passes. It is not a claim that a stock Linux host can perform every integration test.

## Vita build

Install VitaSDK and the packages required by the port (zlib, pthread/taiHEN and the Vita system import stubs). Select the SDK explicitly:

```sh
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

Build the pinned graphics dependencies if your SDK does not already contain compatible versions:

```sh
bash hotwheels/tools/build_vitagl.sh
```

The reference vitaGL configuration is commit `c707ff00d9a372f0ba7056b4a4b723f5a00ba880`, `HAVE_VITA3K_SUPPORT=1 HAVE_FFP_SHADER_SUPPORT=1`. The default user build does not require the optional diagnostics patch. The bootstrap script currently warns rather than fails if a pinned checkout fails: verify the resulting revisions before calling a build reproducible. Do not use a different default branch as if it were the pinned build.

Generate the two weak import archives from their published source definitions. They are link interfaces, not kernel plugins:

```sh
bash third_party/kubridge-stub/regen.sh
mkdir -p hotwheels/out/vpmu-stubs
vita-libs-gen third_party/vpmu-stub/stubs.yml hotwheels/out/vpmu-stubs
make -C hotwheels/out/vpmu-stubs
cp hotwheels/out/vpmu-stubs/libvpmu_stub_weak.a third_party/vpmu-stub/
```

Build with **generic public LiveArea artwork** and no retail payload:

```sh
HW_ORIGINAL_ART_DIR='' bash hotwheels/tools/build_vita.sh
```

Output: `hotwheels/build-vita/hotwheels-vita.vpk`. The script builds `libwinx86_vita.a` from the included runtime and then compiles the port with Cortex-A9/NEON hard-float flags. Use `HW_VITA_BUILD_DIR=/absolute/output/directory` for an isolated output directory. The compatibility symlinks let the default `WINX86_DIR` and sibling stub lookup work together. An arbitrary external `WINX86_DIR` must provide a matching runtime revision, applied changes and sibling import-stub layout.

Historical runtime patches in `hotwheels/patches/` are retained as development records. The vendored runtime already includes local changes: **do not apply the entire patch directory on top of it**. The initial publication source hash and dependency provenance are recorded in [PUBLICATION.json](PUBLICATION.json).

## ARM Linux / QEMU integration

A desktop CMake build checks portable C++ behavior; it does not execute the ARM dynarec. To build the ARM Linux target, install `arm-linux-gnueabihf-gcc/g++`, qemu-arm and zlib development files, or prepare the `hw-host-arm:24.04` Docker image described by the existing host tooling. Inspect `hotwheels/tools/build_host.sh` and `run_host.sh` before running them: the latter is a development harness with monorepo-era container/mount assumptions, not an installer.

A full retail integration run additionally requires locally extracted files under `hotwheels/extracted/original/Program_Files`. No download of the commercial title is provided. Use synthetic tests when you do not own the data.

## Local original LiveArea assets

The packager supports an owner's `HWICON.BMP` and `autostart/background.bmp` under a local extracted source directory. `HW_ORIGINAL_ART_DIR=/path/to/owned/files` selects it explicitly; missing/incomplete selected assets fail rather than silently switching designs. An empty value forces generic artwork. Originals and generated results must not be added to Git or public release assets.

## Validation scope

Consult [VALIDATION.md](VALIDATION.md) for checks actually run against the publication snapshot. Compiling a fresh VPK does not validate it on hardware, and earlier console runs used the binary identified in their own run record. Do not transfer a hardware performance claim to a newly rebuilt binary without testing it.

## Developer network utilities

Network helpers have no personal-machine defaults. Set `VITA_IP` (or `vita_deploy.py --ip`), `REMOTE_RUN_HOST` and `REMOTE_RUN_ROOT` explicitly for your own hardware/worker. The isolated `diag_minvgl` experiment uses `HW_DIAG_LOG_IP` as a compile-time override; its default address is a non-routable documentation example. The historical x87 oracle additionally requires explicit `MINGW_BIN` and `WINE_BIN`. These developer utilities are not required for a portable build/test and should not be used to manage unrelated applications.
