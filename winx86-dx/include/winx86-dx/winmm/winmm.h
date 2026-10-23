// Portable WinMM API subset for PE32/i386 Win32 guests.
//
// Subconjunto de mmiscapi.h / mciapi.h usado por juegos Win32. Las constantes
// reflejan esos headers; las structs C++ son valores de host y NO representan
// directamente el layout PE32 del guest. El bridge decodifica/escribe cada
// campo desde direcciones guest uint32_t y copia explícitamente sus buffers.
//
// Todo el I/O físico entra por interfaces abstractas (FileSource / MusicSink):
// el backend real (host null/mixer, Vita sceAudio) queda fuera de esta unidad.
// Las interfaces se prestan por referencia: deben vivir más que el objeto que
// las consume. Instancias de MMIO/MCI se usan desde un hilo a la vez; no tienen
// locks internos.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winx86::dx::winmm {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a))
        | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

// ---------------------------------------------------------------------------
// MMIO — mmiscapi.h
// ---------------------------------------------------------------------------
constexpr uint32_t MMIO_READ = 0x00000000;
constexpr uint32_t MMIO_WRITE = 0x00000001;
constexpr uint32_t MMIO_READWRITE = 0x00000002;
constexpr uint32_t MMIO_CREATE = 0x00001000;
constexpr uint32_t MMIO_ALLOCBUF = 0x00010000;
constexpr int32_t MMIO_DEFAULTBUFFER = 8192;
constexpr uint32_t MMIO_FINDCHUNK = 0x0010;
constexpr uint32_t MMIO_FINDRIFF = 0x0020;
constexpr uint32_t MMIO_FINDLIST = 0x0040;

constexpr uint32_t MMIOERR_BASE = 256;
constexpr uint32_t MMIOERR_FILENOTFOUND = MMIOERR_BASE + 1;   // 257
constexpr uint32_t MMIOERR_CANNOTOPEN = MMIOERR_BASE + 3;      // 259
constexpr uint32_t MMIOERR_CANNOTCLOSE = MMIOERR_BASE + 4;     // 260
constexpr uint32_t MMIOERR_CANNOTSEEK = MMIOERR_BASE + 7;      // 263
constexpr uint32_t MMIOERR_CHUNKNOTFOUND = MMIOERR_BASE + 9;   // 265

constexpr int MMSEEK_SET = 0;
constexpr int MMSEEK_CUR = 1;
constexpr int MMSEEK_END = 2;

// ---------------------------------------------------------------------------
// MCI — mciapi.h
// ---------------------------------------------------------------------------
constexpr uint32_t MCI_OPEN = 0x0803;
constexpr uint32_t MCI_CLOSE = 0x0804;
constexpr uint32_t MCI_PLAY = 0x0806;
constexpr uint32_t MCI_STOP = 0x0808;
constexpr uint32_t MCI_PAUSE = 0x0809;
constexpr uint32_t MCI_SET = 0x080D;
constexpr uint32_t MCI_STATUS = 0x0814;

constexpr uint32_t MCI_NOTIFY = 0x00000001;
constexpr uint32_t MCI_WAIT = 0x00000002;
constexpr uint32_t MCI_FROM = 0x00000004;
constexpr uint32_t MCI_TO = 0x00000008;
constexpr uint32_t MCI_TRACK = 0x00000010;

constexpr uint32_t MCI_OPEN_SHAREABLE = 0x00000100;
constexpr uint32_t MCI_OPEN_ELEMENT = 0x00000200;
constexpr uint32_t MCI_OPEN_ALIAS = 0x00000400;
constexpr uint32_t MCI_OPEN_TYPE_ID = 0x00001000;
constexpr uint32_t MCI_OPEN_TYPE = 0x00002000;

constexpr uint32_t MCI_STATUS_ITEM = 0x00000100;
constexpr uint32_t MCI_STATUS_START = 0x00000200;
constexpr uint32_t MCI_STATUS_LENGTH = 0x00000001;
constexpr uint32_t MCI_STATUS_POSITION = 0x00000002;
constexpr uint32_t MCI_STATUS_NUMBER_OF_TRACKS = 0x00000003;
constexpr uint32_t MCI_STATUS_MODE = 0x00000004;
constexpr uint32_t MCI_STATUS_MEDIA_PRESENT = 0x00000005;
constexpr uint32_t MCI_STATUS_TIME_FORMAT = 0x00000006;
constexpr uint32_t MCI_STATUS_READY = 0x00000007;
constexpr uint32_t MCI_STATUS_CURRENT_TRACK = 0x00000008;

constexpr uint32_t MCI_SET_DOOR_OPEN = 0x00000100;
constexpr uint32_t MCI_SET_DOOR_CLOSED = 0x00000200;
constexpr uint32_t MCI_SET_TIME_FORMAT = 0x00000400;
constexpr uint32_t MCI_SET_AUDIO = 0x00000800;
constexpr uint32_t MCI_SET_VIDEO = 0x00001000;
constexpr uint32_t MCI_SET_ON = 0x00002000;
constexpr uint32_t MCI_SET_OFF = 0x00004000;

constexpr uint32_t MCI_FORMAT_MSF = 2;
constexpr uint32_t MCI_FORMAT_TMSF = 10;

constexpr uint32_t MCI_STRING_OFFSET = 512;
constexpr uint32_t MCI_MODE_NOT_READY = MCI_STRING_OFFSET + 12;  // 524
constexpr uint32_t MCI_MODE_STOP = MCI_STRING_OFFSET + 13;       // 525
constexpr uint32_t MCI_MODE_PLAY = MCI_STRING_OFFSET + 14;       // 526
constexpr uint32_t MCI_MODE_RECORD = MCI_STRING_OFFSET + 15;     // 527
constexpr uint32_t MCI_MODE_SEEK = MCI_STRING_OFFSET + 16;       // 528
constexpr uint32_t MCI_MODE_PAUSE = MCI_STRING_OFFSET + 17;      // 529
constexpr uint32_t MCI_MODE_OPEN = MCI_STRING_OFFSET + 18;       // 530

constexpr uint32_t MCIERR_BASE = 256;
constexpr uint32_t MCIERR_INVALID_DEVICE_ID = MCIERR_BASE + 1;       // 257
constexpr uint32_t MCIERR_UNRECOGNIZED_COMMAND = MCIERR_BASE + 5;    // 261
constexpr uint32_t MCIERR_DEVICE_OPEN = MCIERR_BASE + 9;             // 265
constexpr uint32_t MCIERR_UNSUPPORTED_FUNCTION = MCIERR_BASE + 18;   // 274
constexpr uint32_t MCIERR_DEVICE_TYPE_REQUIRED = MCIERR_BASE + 31;   // 287

// TMSF: track = byte bajo, frame = byte alto (formula de mciapi.h).
constexpr uint32_t MCI_MAKE_TMSF(int t, int m, int s, int f) {
    return static_cast<uint32_t>(static_cast<uint8_t>(t))
        | (static_cast<uint32_t>(static_cast<uint8_t>(m)) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(s)) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(f)) << 24);
}
constexpr int MCI_TMSF_TRACK(uint32_t v) { return static_cast<uint8_t>(v); }
constexpr int MCI_TMSF_MINUTE(uint32_t v) { return static_cast<uint8_t>(v >> 8); }
constexpr int MCI_TMSF_SECOND(uint32_t v) { return static_cast<uint8_t>(v >> 16); }
constexpr int MCI_TMSF_FRAME(uint32_t v) { return static_cast<uint8_t>(v >> 24); }

// ---------------------------------------------------------------------------
// Interfaces inyectadas
// ---------------------------------------------------------------------------
struct FileSource {
    // Prestado por Mmio; debe sobrevivir a la instancia y serializar llamadas
    // si su backend no es thread-safe.
    virtual ~FileSource() = default;
    // true y `out` con el contenido si `win_path` existe; false si no.
    virtual bool read(const std::string& win_path, std::vector<uint8_t>& out) = 0;
};

struct MusicSink {
    // Prestado por Cdaudio; debe sobrevivir a la instancia y serializar
    // llamadas si su backend no es thread-safe.
    virtual ~MusicSink() = default;
    // path puede no existir: la implementación reproduce silencio en ese caso.
    virtual void play(int track, const std::string& path, uint32_t from_tmsf, uint32_t to_tmsf) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
};

}  // namespace winx86::dx::winmm
