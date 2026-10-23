// tests/test_winmm.cpp — criterios CA1..CA12 de specs/005-winmm.md (host).
// WAVs sintéticos generados acá; nada con copyright.
#include "winmm/mci.h"
#include "winmm/mmio.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace hw::winmm;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static TrackCatalog hotwheels_test_tracks() {
    return {{{"MUSIC\\Track1.wav", 312.37}, {"MUSIC\\Track2.wav", 313.37},
             {"MUSIC\\Track3.wav", 264.64}, {"MUSIC\\Track4.wav", 282.61}}};
}

// ---- CA12: constantes contra mmiscapi.h/mciapi.h + helpers TMSF ----
static_assert(MMIO_FINDCHUNK == 0x0010 && MMIO_FINDRIFF == 0x0020 && MMIO_FINDLIST == 0x0040, "MMIO flags");
static_assert(MMIOERR_FILENOTFOUND == 257 && MMIOERR_CANNOTOPEN == 259 && MMIOERR_CHUNKNOTFOUND == 265, "MMIOERR");
static_assert(MCI_OPEN == 0x0803 && MCI_CLOSE == 0x0804 && MCI_PLAY == 0x0806 && MCI_STOP == 0x0808
    && MCI_PAUSE == 0x0809 && MCI_SET == 0x080D && MCI_STATUS == 0x0814, "MCI msgs");
static_assert(MCI_NOTIFY == 0x1 && MCI_WAIT == 0x2 && MCI_FROM == 0x4 && MCI_TO == 0x8 && MCI_TRACK == 0x10, "MCI play flags");
static_assert(MCI_OPEN_SHAREABLE == 0x100 && MCI_OPEN_ELEMENT == 0x200 && MCI_OPEN_ALIAS == 0x400
    && MCI_OPEN_TYPE_ID == 0x1000 && MCI_OPEN_TYPE == 0x2000, "MCI open flags");
static_assert(MCI_STATUS_ITEM == 0x100 && MCI_STATUS_START == 0x200, "MCI status flags");
static_assert(MCI_STATUS_LENGTH == 1 && MCI_STATUS_POSITION == 2 && MCI_STATUS_NUMBER_OF_TRACKS == 3
    && MCI_STATUS_MODE == 4 && MCI_STATUS_MEDIA_PRESENT == 5 && MCI_STATUS_TIME_FORMAT == 6
    && MCI_STATUS_READY == 7 && MCI_STATUS_CURRENT_TRACK == 8, "MCI status items");
static_assert(MCI_SET_TIME_FORMAT == 0x400 && MCI_SET_DOOR_OPEN == 0x100 && MCI_SET_DOOR_CLOSED == 0x200, "MCI set flags");
static_assert(MCI_FORMAT_MSF == 2 && MCI_FORMAT_TMSF == 10, "MCI formats");
static_assert(MCI_MODE_STOP == 525 && MCI_MODE_PLAY == 526 && MCI_MODE_PAUSE == 529 && MCI_MODE_OPEN == 530, "MCI modes");
static_assert(MCIERR_INVALID_DEVICE_ID == 257 && MCIERR_UNRECOGNIZED_COMMAND == 261
    && MCIERR_DEVICE_OPEN == 265 && MCIERR_UNSUPPORTED_FUNCTION == 274 && MCIERR_DEVICE_TYPE_REQUIRED == 287, "MCIERR");
static_assert(MCI_TMSF_TRACK(MCI_MAKE_TMSF(2, 3, 4, 5)) == 2, "TMSF track");
static_assert(MCI_TMSF_MINUTE(MCI_MAKE_TMSF(2, 3, 4, 5)) == 3, "TMSF minute");
static_assert(MCI_TMSF_SECOND(MCI_MAKE_TMSF(2, 3, 4, 5)) == 4, "TMSF second");
static_assert(MCI_TMSF_FRAME(MCI_MAKE_TMSF(2, 3, 4, 5)) == 5, "TMSF frame");

// ---- helpers ----
static void put_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xff));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
static void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xff));
}
static void put_tag(std::vector<uint8_t>& v, const char* s) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(s[i]));
}
static uint16_t rd_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

static std::vector<uint8_t> make_wav(const std::vector<uint8_t>& pcm, uint16_t channels = 1,
    uint32_t rate = 22050, uint16_t bits = 16) {
    std::vector<uint8_t> d;
    put_tag(d, "RIFF");
    put_u32(d, 4 + (8 + 16) + (8 + static_cast<uint32_t>(pcm.size())));
    put_tag(d, "WAVE");
    put_tag(d, "fmt ");
    put_u32(d, 16);
    put_u16(d, 1);  // PCM
    put_u16(d, channels);
    put_u32(d, rate);
    put_u32(d, rate * channels * bits / 8);
    put_u16(d, static_cast<uint16_t>(channels * bits / 8));
    put_u16(d, bits);
    put_tag(d, "data");
    put_u32(d, static_cast<uint32_t>(pcm.size()));
    d.insert(d.end(), pcm.begin(), pcm.end());
    return d;
}

static std::string norm(std::string p) {
    for (char& c : p) c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p;
}

struct MemFs : FileSource {
    std::map<std::string, std::vector<uint8_t>> files;
    void add(const std::string& name, std::vector<uint8_t> data) { files[norm(name)] = std::move(data); }
    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        auto it = files.find(norm(path));
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
};

struct CaptureSink : MusicSink {
    int calls = 0;
    int last_track = 0;
    std::string last_path;
    uint32_t from = 0, to = 0;
    int stops = 0, pauses = 0;
    void play(int track, const std::string& path, uint32_t f, uint32_t t) override {
        ++calls; last_track = track; last_path = path; from = f; to = t;
    }
    void stop() override { ++stops; }
    void pause() override { ++pauses; }
};

int main() {
    std::vector<uint8_t> pcm(20);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<uint8_t>(i);

    MemFs fs;
    fs.add("MUSIC\\Track1.wav", make_wav(pcm));
    const std::vector<uint8_t>& wav = fs.files[norm("MUSIC\\Track1.wav")];
    const size_t wav_size = wav.size();

    // ---- CA1 ----
    Mmio mmio(fs);
    MmioInfo info{};
    HMMIO h = mmio.open("MUSIC\\Track1.wav", &info, MMIO_READ);
    CHECK(h != 0);
    CHECK(info.wErrorRet == 0);
    CHECK(info.lDiskOffset == 0);
    MmioInfo bad{};
    CHECK(mmio.open("MUSIC\\Nope.wav", &bad, MMIO_READ) == 0);
    CHECK(bad.wErrorRet == MMIOERR_FILENOTFOUND);

    // ---- CA2 ----
    MmckInfo riff{};
    CHECK(mmio.descend(h, riff, nullptr, MMIO_FINDRIFF) == 0);
    CHECK(riff.ckid == fourcc('R', 'I', 'F', 'F'));
    CHECK(riff.fccType == fourcc('W', 'A', 'V', 'E'));
    CHECK(riff.cksize == wav_size - 8);
    MmckInfo miss{};
    miss.ckid = fourcc('n', 'o', 'p', 'e');
    CHECK(mmio.descend(h, miss, nullptr, MMIO_FINDCHUNK) == MMIOERR_CHUNKNOTFOUND);

    // Un hijo RIFF no puede cruzar el límite del padre aunque el archivo tenga
    // bytes posteriores que cubran el tamaño declarado maliciosamente.
    std::vector<uint8_t> malformed;
    put_tag(malformed, "RIFF"); put_u32(malformed, 12); put_tag(malformed, "WAVE");
    put_tag(malformed, "JUNK"); put_u32(malformed, 16);
    malformed.insert(malformed.end(), 16, 0x5a);
    fs.add("MUSIC\\Bad.wav", malformed);
    HMMIO h_bad = mmio.open("MUSIC\\Bad.wav", nullptr, MMIO_READ);
    CHECK(h_bad != 0);
    MmckInfo bad_riff{};
    CHECK(mmio.descend(h_bad, bad_riff, nullptr, MMIO_FINDRIFF) == 0);
    MmckInfo bad_child{}; bad_child.ckid = fourcc('J', 'U', 'N', 'K');
    CHECK(mmio.descend(h_bad, bad_child, &bad_riff, MMIO_FINDCHUNK) == MMIOERR_CHUNKNOTFOUND);
    CHECK(mmio.close(h_bad) == 0);

    // Un MMCKINFO de 32 bits no puede envolver el inicio/final del padre y
    // hacer que bytes del inicio del archivo aparezcan como un hijo suyo.
    fs.add("MUSIC\\Wrapped.wav", { 'J', 'U', 'N', 'K', 4, 0, 0, 0, 1, 2, 3, 4 });
    HMMIO h_wrapped = mmio.open("MUSIC\\Wrapped.wav", nullptr, MMIO_READ);
    CHECK(h_wrapped != 0);
    MmckInfo wrapped_parent{};
    wrapped_parent.ckid = fourcc('R', 'I', 'F', 'F');
    wrapped_parent.dwDataOffset = UINT32_MAX - 3;
    wrapped_parent.cksize = 12;
    MmckInfo false_child{}; false_child.ckid = fourcc('J', 'U', 'N', 'K');
    CHECK(mmio.descend(h_wrapped, false_child, &wrapped_parent, MMIO_FINDCHUNK) == MMIOERR_CHUNKNOTFOUND);
    CHECK(mmio.close(h_wrapped) == 0);

    // ---- CA3 ----
    MmckInfo fmt{};
    fmt.ckid = fourcc('f', 'm', 't', ' ');
    CHECK(mmio.descend(h, fmt, &riff, MMIO_FINDCHUNK) == 0);
    CHECK(fmt.cksize == 16);
    uint8_t fh[16] = {};
    CHECK(mmio.read(h, fh, 16) == 16);
    CHECK(rd_u16(fh) == 1 && rd_u16(fh + 2) == 1 && rd_u32(fh + 4) == 22050 && rd_u16(fh + 14) == 16);
    CHECK(mmio.read(h, fh, 16) == 0);  // no sale del chunk fmt
    CHECK(mmio.ascend(h, fmt, 0) == 0);

    // ---- CA4 ----
    MmckInfo data{};
    data.ckid = fourcc('d', 'a', 't', 'a');
    CHECK(mmio.descend(h, data, &riff, MMIO_FINDCHUNK) == 0);
    CHECK(data.cksize == pcm.size());
    uint8_t buf[64] = {};
    CHECK(mmio.read(h, buf, static_cast<int32_t>(sizeof(buf))) == static_cast<int32_t>(pcm.size()));
    CHECK(std::memcmp(buf, pcm.data(), pcm.size()) == 0);
    CHECK(mmio.read(h, buf, 1) == 0);  // borde del chunk data
    CHECK(mmio.seek(h, 0, MMSEEK_SET) == 0);
    CHECK(mmio.read(h, buf, 4) == 4);

    // ---- CA5 ----
    MmioInfo gi{};
    CHECK(mmio.get_info(h, gi) == 0);
    CHECK(gi.lDiskOffset == 4);
    CHECK(mmio.seek(h, 2, MMSEEK_CUR) == 6);
    CHECK(mmio.seek(h, 0, MMSEEK_END) == static_cast<int32_t>(wav_size));
    CHECK(mmio.seek(h, 2, MMSEEK_SET) == 2);
    MmioInfo si{};
    si.lDiskOffset = 0;
    CHECK(mmio.set_info(h, si) == 0);
    CHECK(mmio.seek(h, 0, MMSEEK_CUR) == 0);
    MmioInfo ai{};
    ai.cchBuffer = 8;
    CHECK(mmio.advance(h, ai, 0) == 0);
    CHECK(ai.pchBuffer != nullptr && ai.pchNext == ai.pchBuffer && ai.pchEndRead == ai.pchBuffer + 8);
    CHECK(ai.lBufOffset == 0 && std::memcmp(ai.pchBuffer, wav.data(), 8) == 0);

    // ---- CA6 ----
    CHECK(mmio.close(h) == 0);

    // ---- BUG-006: descend con flags=0 (chunk en la posición actual) y
    // FINDRIFF filtrando por fccType, como lo usa HotWheels.exe al cargar SFX.
    {
        HMMIO h2 = mmio.open("MUSIC\\Track1.wav", nullptr, MMIO_READ);
        MmckInfo riff{}; riff.fccType = fourcc('A', 'V', 'I', ' ');
        CHECK(mmio.descend(h2, riff, nullptr, MMIO_FINDRIFF) == MMIOERR_CHUNKNOTFOUND);
        MmckInfo any{};
        CHECK(mmio.descend(h2, any, nullptr, 0) == 0);
        CHECK(any.ckid == fourcc('R', 'I', 'F', 'F') && any.fccType == fourcc('W', 'A', 'V', 'E'));
        CHECK(any.dwDataOffset == 8);
        MmckInfo fmt{};
        CHECK(mmio.descend(h2, fmt, &any, 0) == 0);
        CHECK(fmt.ckid == fourcc('f', 'm', 't', ' ') && fmt.dwDataOffset == 20);
        CHECK(mmio.close(h2) == 0);
    }
    uint8_t x = 0;
    CHECK(mmio.read(h, &x, 1) == -1);
    CHECK(mmio.seek(h, 0, MMSEEK_SET) == -1);

    // ---- CA7 ----
    CaptureSink sink;
    Cdaudio cd(sink, hotwheels_test_tracks());
    MciOpenParms op{};
    op.lpstrDeviceType = "cdaudio";
    op.lpstrElementName = "D:";
    CHECK(cd.send_command(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT, &op) == 0);
    CHECK(op.wDeviceID > 0);
    const MCIDEVICEID dev = op.wDeviceID;
    CHECK(cd.has_device(dev));
    MciOpenParms bad_open{};
    bad_open.lpstrDeviceType = "waveaudio";
    CHECK(cd.send_command(0, MCI_OPEN, MCI_OPEN_TYPE, &bad_open) == MCIERR_UNSUPPORTED_FUNCTION);
    MciGenericParms g{};
    CHECK(cd.send_command(9999, MCI_SET, 0, &g) == MCIERR_INVALID_DEVICE_ID);
    CHECK(cd.send_command(dev, 0x9999, 0, &g) == MCIERR_UNRECOGNIZED_COMMAND);

    // ---- CA8 ----
    MciSetParms sp{};
    sp.dwTimeFormat = MCI_FORMAT_TMSF;
    CHECK(cd.send_command(dev, MCI_SET, MCI_SET_TIME_FORMAT, &sp) == 0);
    CHECK(cd.time_format(dev) == MCI_FORMAT_TMSF);
    MciStatusParms st{};
    st.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
    CHECK(cd.send_command(dev, MCI_STATUS, MCI_STATUS_ITEM, &st) == 0);
    CHECK(st.dwReturn == 4);

    // ---- CA9 ----
    MciPlayParms pl{};
    pl.dwFrom = MCI_MAKE_TMSF(2, 0, 0, 0);
    CHECK(cd.send_command(dev, MCI_PLAY, MCI_FROM, &pl) == 0);
    CHECK(sink.calls == 1 && sink.last_track == 2 && sink.last_path == "MUSIC\\Track2.wav");
    CHECK(cd.current_track(dev) == 2);
    MciStatusParms cur{};
    cur.dwItem = MCI_STATUS_CURRENT_TRACK;
    CHECK(cd.send_command(dev, MCI_STATUS, MCI_STATUS_ITEM, &cur) == 0 && cur.dwReturn == 2);
    MciStatusParms mode{};
    mode.dwItem = MCI_STATUS_MODE;
    CHECK(cd.send_command(dev, MCI_STATUS, MCI_STATUS_ITEM, &mode) == 0 && mode.dwReturn == MCI_MODE_PLAY);
    MciOpenParms op_def{};
    op_def.lpstrDeviceType = "cdaudio";
    CHECK(cd.send_command(0, MCI_OPEN, MCI_OPEN_TYPE, &op_def) == 0);
    MciPlayParms pl2{};  // sin FROM -> default pista 1 (dispositivo sin historial)
    CHECK(cd.send_command(op_def.wDeviceID, MCI_PLAY, 0, &pl2) == 0 && sink.last_track == 1);
    MciPlayParms pl3{};
    pl3.dwFrom = MCI_MAKE_TMSF(3, 0, 0, 0);
    pl3.dwTo = MCI_MAKE_TMSF(3, 0, 5, 0);
    CHECK(cd.send_command(dev, MCI_PLAY, MCI_FROM | MCI_TO, &pl3) == 0);
    CHECK(sink.last_track == 3 && sink.from == pl3.dwFrom && sink.to == pl3.dwTo);

    // ---- CA10 ----
    CHECK(cd.send_command(dev, MCI_PAUSE, 0, &g) == 0);
    CHECK(sink.pauses == 1);
    CHECK(cd.send_command(dev, MCI_STOP, 0, &g) == 0);
    CHECK(sink.stops == 1);
    CHECK(cd.send_command(dev, MCI_CLOSE, 0, &g) == 0);
    CHECK(!cd.has_device(dev));
    CHECK(cd.send_command(dev, MCI_STOP, 0, &g) == MCIERR_INVALID_DEVICE_ID);

    // ---- CA11: sin FileSource, reproducir una pista inexistente no bloquea ----
    CaptureSink sink2;
    Cdaudio cd2(sink2, hotwheels_test_tracks());
    MciOpenParms op2{};
    op2.lpstrDeviceType = "CDAUDIO";  // case-insensitive
    CHECK(cd2.send_command(0, MCI_OPEN, MCI_OPEN_TYPE, &op2) == 0);
    MciPlayParms pl4{};
    pl4.dwFrom = MCI_MAKE_TMSF(4, 0, 0, 0);
    CHECK(cd2.send_command(op2.wDeviceID, MCI_PLAY, MCI_FROM, &pl4) == 0);
    CHECK(sink2.last_track == 4 && sink2.last_path == "MUSIC\\Track4.wav");
    CHECK(cd2.track_path(1) == "MUSIC\\Track1.wav");
    CHECK(cd2.track_path(4) == "MUSIC\\Track4.wav");

    // ---- BUG-006: MCI_FORMAT_MSF = posiciones ABSOLUTAS del disco (pregap
    // 150 frames), como lo usa HotWheels.exe: STATUS POSITION/LENGTH por pista
    // y PLAY FROM/TO en MSF -> pista + offset TMSF para el sink.
    {
        CaptureSink s2; Cdaudio c2(s2, hotwheels_test_tracks());
        MciOpenParms op{}; op.lpstrDeviceType = "cdaudio";
        CHECK(c2.send_command(0, MCI_OPEN, MCI_OPEN_TYPE, &op) == 0);
        const MCIDEVICEID d = op.wDeviceID;
        MciSetParms sp{}; sp.dwTimeFormat = MCI_FORMAT_MSF;
        CHECK(c2.send_command(d, MCI_SET, MCI_SET_TIME_FORMAT, &sp) == 0);
        auto msf = [](uint32_t fr) { return (fr / 4500) | (((fr / 75) % 60) << 8) | ((fr % 75) << 16); };
        const uint32_t t2 = 150 + (uint32_t)(312.37 * 75 + 0.5);   // inicio pista 2 en frames
        MciStatusParms st{}; st.dwItem = MCI_STATUS_POSITION; st.dwTrack = 2;
        CHECK(c2.send_command(d, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, &st) == 0);
        CHECK(st.dwReturn == msf(t2));
        st = {}; st.dwItem = MCI_STATUS_LENGTH; st.dwTrack = 2;
        CHECK(c2.send_command(d, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, &st) == 0);
        CHECK(st.dwReturn == msf((uint32_t)(313.37 * 75 + 0.5)));
        MciPlayParms pp{}; pp.dwFrom = msf(t2 + 75 * 3); pp.dwTo = msf(t2 + 75 * 60);
        CHECK(c2.send_command(d, MCI_PLAY, MCI_FROM | MCI_TO, &pp) == 0);
        CHECK(s2.last_track == 2 && s2.last_path == "MUSIC\\Track2.wav");
        CHECK(s2.from == MCI_MAKE_TMSF(2, 0, 3, 0) && s2.to == MCI_MAKE_TMSF(2, 1, 0, 0));
    }
    // ---- BUG-009: MMIO_ALLOCBUF (wavread.c del DirectX SDK): GetInfo da un
    // buffer de 8 KiB vacío (pchNext==pchEndRead) y Advance lo llena con el chunk.
    {
        MmioInfo oi{};
        HMMIO h3 = mmio.open("MUSIC\\Track1.wav", &oi, MMIO_READ | MMIO_ALLOCBUF);
        CHECK(h3 != 0);
        MmckInfo rf{}; rf.fccType = fourcc('W', 'A', 'V', 'E');
        CHECK(mmio.descend(h3, rf, nullptr, MMIO_FINDRIFF) == 0);
        MmckInfo dt{}; dt.ckid = fourcc('d', 'a', 't', 'a');
        CHECK(mmio.descend(h3, dt, &rf, MMIO_FINDCHUNK) == 0);
        MmioInfo gi{};
        CHECK(mmio.get_info(h3, gi) == 0);
        CHECK(gi.cchBuffer == 8192 && gi.pchBuffer != nullptr && gi.pchNext == gi.pchEndRead);
        CHECK(mmio.advance(h3, gi, MMIO_READ) == 0);
        CHECK(gi.pchEndRead - gi.pchNext == (long)pcm.size());
        CHECK(gi.pchNext && std::memcmp(gi.pchNext, pcm.data(), pcm.size()) == 0);
        CHECK(mmio.close(h3) == 0);
    }
    if (g_fail) {
        std::printf("WINMM: %d fallas\n", g_fail);
        return 1;
    }
    std::printf("WINMM: OK (CA1..CA12)\n");
    return 0;
}
