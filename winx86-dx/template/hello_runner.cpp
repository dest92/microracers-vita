// qemu-arm-side consumer: load a PE32 guest with winx86 and map three of its
// Win32 imports to the portable DirectDraw, DirectSound and DirectInput APIs.
#include <winx86-dx/audio/dsound_bus.h>
#include <winx86-dx/audio/mixer.h>
#include <winx86-dx/ddraw/software.h>
#include <winx86-dx/dinput/dinput.h>

#include "runtime/bridge.h"
#include "runtime/cpu.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace dx = winx86::dx;

namespace {
struct GuestMemory final : dx::ddraw::Memory {
    uint32_t alloc(size_t bytes) override {
        const uint32_t address = next;
        next += static_cast<uint32_t>((bytes + 0xfff) & ~size_t{0xfff});
        blocks.push_back({address, std::vector<uint8_t>(bytes)});
        return address;
    }
    uint8_t* ptr(uint32_t address, size_t bytes) override {
        for (auto& block : blocks) {
            const size_t offset = address - block.first;
            if (address >= block.first && offset <= block.second.size() &&
                bytes <= block.second.size() - offset) return block.second.data() + offset;
        }
        return nullptr;
    }
    uint32_t next = 0x10000000;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> blocks;
};

struct Presenter final : dx::ddraw::Presenter {
    void present(const uint8_t*, uint32_t, const uint32_t*) override { ++frames; }
    uint32_t frames = 0;
};

struct TestInput final : dx::dinput::InputSource {
    dx::dinput::InputSnapshot poll() override {
        dx::dinput::InputSnapshot s{};
        s.pad_count = 1;
        s.pads[0].buttons = dx::dinput::kPadCross;
        return s;
    }
};

struct Sample {
    static dx::audio::DsoundBusConfig bus_config() {
        dx::audio::DsoundBusConfig config;
        config.prime_ms = 0;
        config.prime_max_ms = 0;
        return config;
    }
    GuestMemory memory;
    Presenter presenter;
    dx::ddraw::Device ddraw{memory, presenter};
    dx::audio::DsoundBus dsound{bus_config()};
    TestInput input;
    dx::dinput::MapperConfig mapper;
    bool ddraw_ok = false, dsound_ok = false, dinput_ok = false;

    uint32_t run_ddraw() {
        using namespace dx::ddraw;
        Surface* primary = ddraw.create_surface(kPrimary | kComplex | kFlip, 2, 1, 1);
        if (!primary || !primary->attached) return 0x80004005u;
        LockInfo lock{};
        if (ddraw.lock(*primary->attached, lock) != kOk) return 0x80004005u;
        uint8_t* pixels = memory.ptr(lock.va, 2);
        if (!pixels) return 0x80004005u;
        pixels[0] = 1;
        pixels[1] = 2;
        if (ddraw.unlock(*primary->attached) != kOk || ddraw.flip(*primary) != kOk)
            return 0x80004005u;
        ddraw_ok = presenter.frames == 1;
        return ddraw_ok ? 0u : 0x80004005u;
    }

    uint32_t run_dsound() {
        const int16_t pcm[] = {1200, -1200, 900, -900, 600, -600, 300, -300};
        dsound.push(pcm, 4, dx::audio::kSampleRate);
        dx::audio::Mixer mixer;
        mixer.add(&dsound);
        int16_t out[8]{};
        if (mixer.mix(out, 4) != 4) return 0x80004005u;
        for (int16_t sample : out) if (sample != 0) {
            dsound_ok = true;
            break;
        }
        return dsound_ok ? 0u : 0x80004005u;
    }

    uint32_t run_dinput() {
        using namespace dx::dinput;
        mapper.pad[0].set(Button::Cross, dik::X);
        DirectInput direct_input(input, mapper);
        Device* keyboard = direct_input.create_device(kGuidSysKeyboard);
        if (!keyboard || keyboard->set_data_format(DataFormat::Keyboard) != DI_OK ||
            keyboard->acquire() != DI_OK) return 0x80004005u;
        uint8_t keys[kKeyboardStateBytes]{};
        if (keyboard->get_device_state(sizeof keys, keys) != DI_OK) return 0x80004005u;
        dinput_ok = (direct_input.snapshot().pads[0].buttons & kPadCross) != 0 &&
                    mapper.pad[0].get(Button::Cross) == dik::X && (keys[dik::X] & 0x80) != 0;
        return dinput_ok ? 0u : 0x80004005u;
    }
};

bool read_file(const char* path, std::vector<uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !bytes.empty();
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "uso: hello_runner <hello.exe>\n");
        return 2;
    }
    std::vector<uint8_t> pe;
    if (!read_file(argv[1], pe)) {
        std::fprintf(stderr, "no se pudo leer %s\n", argv[1]);
        return 2;
    }
    std::unique_ptr<d2rt::Cpu> cpu(d2rt::make_cpu_box86());
    if (!cpu) return 3;
    d2rt::Bridge bridge(cpu.get());
    Sample sample;
    auto register_import = [&](const char* dll, const char* name, uint32_t argc,
                               uint32_t (Sample::*call)()) {
        d2rt::Shim shim;
        shim.tag = std::string(dll) + "!" + name;
        shim.argc = argc;
        shim.stdcall_cleanup = true;
        shim.fn = [&sample, call](d2rt::Cpu&) { return (sample.*call)(); };
        bridge.register_shim(dll, name, std::move(shim));
    };
    register_import("ddraw.dll", "DirectDrawCreate", 3, &Sample::run_ddraw);
    register_import("dsound.dll", "DirectSoundCreate", 3, &Sample::run_dsound);
    register_import("dinput.dll", "DirectInputCreateA", 4, &Sample::run_dinput);

    std::string error;
    d2rt::PeImage* image = bridge.add_module("hello.exe", pe, error);
    if (!image || !bridge.commit(error) || !bridge.link(error)) {
        std::fprintf(stderr, "carga PE: %s\n", error.c_str());
        return 4;
    }
    uint32_t result = 0xffffffffu;
    const char* fault = nullptr;
    if (!bridge.call_va(image->entry_va(), {}, result, &fault)) {
        std::fprintf(stderr, "ejecución PE fault=%s addr=0x%08x code=0x%08x\n",
                     fault ? fault : "desconocido", cpu->fault_addr(), cpu->fault_code());
        return 5;
    }
    if (result || !sample.ddraw_ok || !sample.dsound_ok || !sample.dinput_ok ||
        bridge.unresolved_count() != 0) {
        std::fprintf(stderr, "resultado=%u ddraw=%u dsound=%u dinput=%u imports_sin_shim=%u\n",
                     result, sample.ddraw_ok, sample.dsound_ok, sample.dinput_ok,
                     bridge.unresolved_count());
        return 6;
    }
    std::puts("HELLO_PORT|loaded=hello.exe|ddraw=ok|dsound=ok|dinput=ok|unresolved=0");
    return 0;
}
