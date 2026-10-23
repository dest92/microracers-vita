// tests/test_ds_cursor_contract.cpp — SPEC-048 (frente 2): contrato de cursores
// DirectSound.
//
// El guest escribe por el puntero de Lock SIN tomar g_mx, así que ningún lock del
// lado host lo puede excluir: la única garantía posible es geométrica. El write
// cursor que ve el guest tiene que estar a o más allá del final de la ventana
// que el mixer lee en un grano, en bytes de la fuente y escalada por la
// frecuencia actual (a 32342 Hz una voz consume ceil(512*32342/22050) frames de
// fuente por grano, no 512).
//
// Ejercita la definición REAL de winx86 `src/runtime/ds_cursor.h`, materializada
// desde el parche versionado (ver run_ds_cursor_contract.sh), y además comprueba
// que ds_emul.cpp conecte esa definición (siwrite_cursor vuelve a la fórmula
// vieja, el test se cae).
#include <ds_cursor.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

using namespace d2rt::dsound;

namespace {
constexpr uint32_t kGrain = 512;   // frames de salida por grano (ds_emul kGrain)
constexpr uint32_t kRate = 22050;  // kRate del guest
constexpr uint32_t ba = 4;         // 16-bit estéreo

uint32_t ceil_div(uint64_t a, uint64_t b) { return (uint32_t)((a + b - 1) / b); }

// La ventana NO se hardcodea: se deriva de la fórmula que dice qué lee el mixer.
uint32_t expect_window(uint32_t rate) {
    return ceil_div((uint64_t)kGrain * rate, kRate) + kResamplerLookahead;
}
uint32_t expect_advance(uint32_t rate) { return (uint32_t)((uint64_t)kGrain * rate / kRate); }

// Fórmula vieja de ds_emul.cpp (2 granos a tasa nominal, tope len/4): la que
// dejaba al guest escribir sobre lo que el mixer no había leído.
uint32_t legacy_lead_bytes(uint32_t len, uint32_t ba_) {
    uint32_t l = (uint32_t)(2 * kGrain) * ba_;
    if (l > len / 4) l = len / 4;
    if (l < ba_) l = ba_;
    return l;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

int main(int argc, char** argv) {
    int bad = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL %s\n", what); ++bad; }
    };

    // 0. El look-ahead es un dato del resampler (mix_voice lee `pos` y
    //    `np` = pos + blockAlign: un frame), no un margen inventado.
    check(kResamplerLookahead == 1, "el resampler lee un frame adelante");

    // 1. La ventana se deriva de la fórmula (mata look-ahead=0 y la falta de
    //    escala por frecuencia).
    for (uint32_t rate : {22050u, 24000u, 32342u, 44100u}) {
        check(read_window_frames(kGrain, rate, kRate) == expect_window(rate), "ventana derivada");
        check(cursor_advance_frames(kGrain, rate, kRate) == expect_advance(rate), "avance derivado");
    }

    // 1b. Sin tasa conocida el lead sigue siendo de un frame: el write cursor
    //     no puede coincidir con el play cursor.
    check(cursor_lead_bytes(17640, ba, 0, kRate, kGrain) >= ba, "sin tasa conocida el lead es >= 1 frame");
    check(cursor_lead_bytes(17640, ba, 0, 0, kGrain) >= ba, "sin kRate el lead es >= 1 frame");

    // 2. El adelanto cubre la ventana Y deja un grano de holgura para el
    //    relleno del guest (si se saca la holgura, el test se cae).
    for (uint32_t rate : {22050u, 32342u, 44100u}) {
        for (uint32_t len : {17640u, 0x8000u}) {
            const uint32_t lead = cursor_lead_bytes(len, ba, rate, kRate, kGrain);
            check(lead >= read_window_frames(kGrain, rate, kRate) * ba, "lead >= ventana");
            check(lead >= (read_window_frames(kGrain, rate, kRate)
                           + cursor_advance_frames(kGrain, rate, kRate)) * ba, "lead con holgura de un grano");
            check(lead <= len - ba, "lead <= len - blockAlign");
            check(lead >= ba, "lead >= blockAlign");
            check(write_cursor_bytes(0, len, ba, rate, kRate, kGrain) != 0, "write cursor != play cursor");
        }
    }

    // 3. Peor caso de la captura: motor con pitch 32342 Hz + buffer de 0.1 s.
    //    La fórmula vieja no cubría la ventana (2205 B < 3008 B): el guest
    //    escribía sobre lo que el mixer no había leído.
    check(expect_window(32342) == 752, "ventana con pitch 32342 (derivada)");
    check(legacy_lead_bytes(8820, ba) < expect_window(32342) * ba, "la fórmula vieja viola el contrato");
    check(cursor_lead_bytes(8820, ba, 32342, kRate, kGrain) >= expect_window(32342) * ba,
          "el fix cubre la ventana con pitch");

    // 4. Buffer chico: el tope del lead bites (lead crudo > len - blockAlign) y
    //    el write cursor nunca puede coincidir con el play cursor.
    {
        const uint32_t len = 4096;
        const uint32_t lead = cursor_lead_bytes(len, ba, kRate, kRate, kGrain);
        check(lead == len - ba, "lead capado a len - blockAlign");
        check(write_cursor_bytes(1000, len, ba, kRate, kRate, kGrain) != 1000, "write cursor != play cursor");
    }

    // 5. Cableado: ds_emul.cpp tiene que usar esta definición, no la fórmula
    //    vieja. Se lee el parche versionado (el winx86 no está en este repo):
    //    lo que el parche AGREGA debe llamar a write_cursor_bytes, y lo que
    //    QUITA debe ser la fórmula vieja.
    {
        const std::string patch = read_file(argc > 1 ? argv[1]
                                                      : "patches/winx86-0014-dsound-cursor-contract.patch");
        const size_t at = patch.find("+++ b/src/runtime/ds_emul.cpp");
        check(at != std::string::npos, "el parche toca ds_emul.cpp");
        if (at != std::string::npos) {
            const size_t end = patch.find("\ndiff --git", at);
            const std::string hunk = patch.substr(at, (end == std::string::npos ? patch.size() : end) - at);
            std::string added, removed;
            std::istringstream lines(hunk);
            std::string line;
            while (std::getline(lines, line)) {
                if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) continue;
                if (!line.empty() && line[0] == '+') added += line + '\n';
                if (!line.empty() && line[0] == '-') removed += line + '\n';
            }
            check(added.find("write_cursor_bytes(") != std::string::npos,
                  "write_cursor llama a la definición del header");
            check(added.find("2 * kGrain") == std::string::npos, "lo agregado no trae la fórmula vieja");
            check(added.find("len / 4") == std::string::npos, "lo agregado no trae el tope len/4");
            check(removed.find("2 * kGrain") != std::string::npos, "el parche quita la fórmula vieja");
        }
    }

    if (bad) { std::printf("DS-CURSOR-CONTRACT: %d FAIL\n", bad); return 1; }
    std::printf("DS-CURSOR-CONTRACT: PASS (ventana@32342=%uB, lead@8820=%uB, legacy=%uB)\n",
                expect_window(32342) * ba, cursor_lead_bytes(8820, ba, 32342, kRate, kGrain),
                legacy_lead_bytes(8820, ba));
}
