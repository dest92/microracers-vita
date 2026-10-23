// src/audio/vita_sink.cpp — sink sceAudioOut + hilo dedicado (HW-018).
//
// El hilo llama `spec.step(ctx)` en loop; cada step mezcla un grano y llama a
// write(), que bloquea en sceAudioOutOutput. El hilo del guest NUNCA llama a
// write(): sólo hace push() al DsoundBus (no bloqueante).
#include "audio/vita_sink.h"
#include "diag/thread_cpu.h"

#if defined(__vita__)

#include <psp2/audioout.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#include <pthread.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "audio/output_contract.h"
#include "audio/port_contract.h"
#include "audio/pcm.h"
#include "runtime/heartbeat.h"

namespace hw::audio {
namespace {

constexpr int kOutRate = 48000;
// Granos entre líneas de telemetría: 936 * 512 / 48000 ≈ 10 s.
constexpr uint64_t kTelGrains = 936;

// Log durable (open/write/close por línea): sobrevive a un crash inmediato.
void slog(const char* fmt, ...) {
    char b[200];
    va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    sceClibPrintf("[audio] %s\n", b);  // también al stdout de Vita3K / psp2shell
    if (FILE* f = std::fopen("ux0:data/hotwheels/audio.log", "a")) { std::fputs(b, f); std::fputc('\n', f); std::fclose(f); }
}
// El driver real del puerto: psp2/audioout.h. El contrato (len alineado a 64,
// bloque del tamaño del puerto, pingpong, rc) vive en PortOut y está testeado en
// host (tests/run_audio_port_contract.sh).
PortDriver vita_driver() {
    PortDriver d;
    d.open = [](int type, int len, int rate, int mode) -> int {
        return sceAudioOutOpenPort((SceAudioOutPortType)type, len, rate, (SceAudioOutMode)mode);
    };
    d.output = [](int port, const void* buf) -> int { return sceAudioOutOutput(port, buf); };
    d.rest = [](int port) -> int { return sceAudioOutGetRestSample(port); };
    d.release = [](int port) -> int { return sceAudioOutReleasePort(port); };
    return d;
}

class VitaSink : public Sink {
public:
    explicit VitaSink(const AudioThreadSpec& spec) {
        grain_ = (spec.grain > 0 && spec.grain <= kOutputBlockMaxFrames) ? spec.grain : 512;
        step_ = spec.step;
        ctx_ = spec.ctx;
    }
    ~VitaSink() override { close(); }

    bool open(int rate, int /*ch*/) override {
        (void)rate;  // el MAIN port siempre abre a 48000
        // El driver exige un len múltiplo de 64 y consume SIEMPRE ese len; el
        // bloque que enviamos mide lo mismo (PortOut lo alinea y lo pasa).
        slog("vita_sink: open grain=%d", grain_);
        if (!port_.open(SCE_AUDIO_OUT_PORT_TYPE_MAIN, SCE_AUDIO_OUT_PORT_TYPE_BGM,
                        kOutRate, SCE_AUDIO_OUT_MODE_STEREO, grain_)) {
            slog("vita_sink: no se pudo abrir el puerto (len=%d inválido o driver)", port_.len());
            return false;
        }
        slog("vita_sink: OpenPort rc=%d; SetVolume ...", port_.port());
        int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
        sceAudioOutSetVolume(port_.port(),
            (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
        // CA11: MAIN es el puerto del contrato; si no abre, el fallback BGM
        // queda REGISTRADO (el tipo decide la ruta de mezcla del sistema).
        slog("vita_sink: port=%d tipo=%s len=%d", port_.port(),
             port_.type() == SCE_AUDIO_OUT_PORT_TYPE_MAIN ? "MAIN" : "BGM", port_.len());
        if (step_) {
            // Hilo PTHREAD (pte), no sceKernelCreateThread crudo: el step toma
            // std::mutex (DsoundBus) = pthread_mutex de pte, y usa newlib; en un
            // hilo Sce crudo sin TLS de pte eso desreferencia NULL (crash Vita3K
            // run002952, entre "engine: start ..." y "start ok"). getenv se lee
            // ACÁ, en el hilo llamador, por la misma razón.
            mask_ = audio_thread_cpu_mask();  // USER_2 por defecto; HW_SONCPU=1 -> USER_1
            run_ = true;
            pthread_attr_t at; pthread_attr_init(&at);
            pthread_attr_setstacksize(&at, 64 * 1024);
            const int rc = pthread_create(&th_, &at, &VitaSink::thread_entry, this);
            pthread_attr_destroy(&at);
            slog("vita_sink: pthread_create rc=%d", rc);
            if (rc != 0) {
                run_ = false;
                port_.close();
                return false;
            }
            has_th_ = true;
        }
        return true;
    }

    // Bloquea ~grain/48000 s. Sólo lo llama el hilo `hw_audio`.
    void write(const int16_t* pcm, int frames) override {
        if (port_.port() < 0 || !pcm || frames <= 0) return;
        // sceKernelGetProcessTimeLow() es de 32 bits y hace wrap (~71,6 min a µs):
        // las restas se hacen en uint32 (el wrap es correcto) y recién después se
        // ensanchan a 64.
        const uint32_t t0 = sceKernelGetProcessTimeLow();
        // La copia al buffer alineado a 64 B y el pad de la cola los hace PortOut
        // (el SDK no documenta un requisito de alineación del puntero; 64 B es
        // múltiplo de la línea L1 de 32 B de la plataforma).
        hw::hb::mark(hw::hb::Slot::Audio, 1, (uint32_t)(t0 / 1000u));
        const int rc = port_.send(pcm, frames);
        const uint32_t t1 = sceKernelGetProcessTimeLow();
        hw::hb::mark(hw::hb::Slot::Audio, 2, (uint32_t)(t1 / 1000u));
        // Telemetría (SPEC-048 Fase 2): el período entre granos incluye mezcla +
        // copia + Output. Si supera el esperado, el hilo se atrasó (starvation
        // por guest/D3D o GIL) y el driver repite el ring. rest_min=0 con el
        // puerto tocando significa que se quedó sin datos: underrun. Sólo se
        // muestrea una vez que hubo una salida correcta (wrote_), para no
        // mezclar el estado previo con el del puerto en marcha.
        if (wrote_) {
            const int rest_before = port_.rest_before();
            const int rest_after = port_.rest_after();
            if (rest_before >= 0 && rest_before < rest_min_) rest_min_ = rest_before;
            if (rest_after >= 0 && rest_after < rest_min_) rest_min_ = rest_after;
        }
        if (prev_us_) {
            const uint32_t d = t0 - prev_us_;
            period_sum_us_ += d;
            ++period_n_;
            if (d > period_max_us_) period_max_us_ = d;
        }
        prev_us_ = t0;
        const uint32_t step_us = t1 - t0;
        if (step_us > step_max_us_) step_max_us_ = step_us;
        if (!output_succeeded(rc)) {
            // El bloqueo es el reloj del hilo: si el driver rechaza, no hay
            // reloj -> anti-busy-wait tras 4 fallos consecutivos.
            if (++consec_ >= 4) { sceKernelDelayThread(11000); consec_ = 0; }
        } else {
            consec_ = 0;
            wrote_ = true;
        }
        if (++grains_ >= kTelGrains) log_telemetry();
    }

    void close() override {
        if (has_th_) {
            run_ = false;
            pthread_join(th_, nullptr);  // el step bloquea <= 1 grano (~11 ms)
            has_th_ = false;
        }
        if (port_.port() >= 0) {
            if (wrote_) sceAudioOutOutput(port_.port(), nullptr);  // drain
            port_.close();
        }
    }

    const char* name() const override { return "vita"; }
    unsigned long long errors() const { return port_.errors(); }

private:
    static void* thread_entry(void* arg) {
        hw::diag::ThreadRegistration registration(hw::diag::ThreadRole::Feeder);
        auto* self = static_cast<VitaSink*>(arg);
        const int thid = (int)sceKernelGetThreadId();
        // La prioridad del kernel es ABSOLUTA (64..191, "menos es más alto"); el
        // 0x1000xxxx es una codificación RELATIVA al default del proceso: 0x100000E0
        // = DEFAULT-32. Por eso se lee la de ANTES de cambiarla: el readback
        // después es el valor efectivo (medido: 0x80 = DEFAULT-32) y no el pedido.
        const int prio_before = sceKernelGetThreadCurrentPriority();
        const int rcp = sceKernelChangeThreadCpuAffinityMask(thid, (int)self->mask_);
        const int rcr = sceKernelChangeThreadPriority(thid, kAudioThreadPriority);
        // Readback: el kernel puede devolver rc=0 sin aplicarlo (el sink de
        // winx86 documenta que el mask leído puede dar 0). Si el mask leído no
        // es el pedido, el hilo NO está donde se cree: la corrida no sirve como
        // medición de scheduling y queda marcado como inválida.
        const int got_mask = sceKernelGetThreadCpuAffinityMask(thid);
        const int got_prio = sceKernelGetThreadCurrentPriority();
        const bool mask_ok = (rcp == 0) && (got_mask == (int)self->mask_);
        const bool prio_ok = (rcr == 0) && (got_prio == kAudioThreadPriorityAbs);
        slog("vita_sink: hilo audio rc_pin=%d rc_prio=%d mask=0x%x leido=0x%x prio_antes=0x%x "
             "prio_efectiva=0x%x prio_esperada=0x%x afinidad=%s prioridad=%s",
             rcp, rcr, self->mask_, (unsigned)got_mask, (unsigned)prio_before,
             (unsigned)got_prio, (unsigned)kAudioThreadPriorityAbs,
             mask_ok ? "OK" : "INVALIDA (medición de scheduling no vale)",
             prio_ok ? "OK" : "INVALIDA");
        while (self->run_) {
            // 3 = dentro del step (mutex DsoundBus + mezcla); write() marca 1/2.
            hw::hb::mark(hw::hb::Slot::Audio, 3, (uint32_t)(sceKernelGetProcessTimeLow() / 1000u));
            self->step_(self->ctx_);
        }
        return nullptr;
    }

    // Publica el período observado entre granos contra el esperado (out_len_ a
    // 48000), el mínimo de samples en cola y la duración del paso de envío
    // completo (copia + GetRestSample + Output). per_avg > exp = el hilo llegó
    // tarde (starvation); rest_min = 0 con el puerto tocando = underrun.
    void log_telemetry() {
        const uint64_t expect = (uint64_t)port_.len() * 1000000ull / (uint64_t)kOutRate;
        slog("vita_sink: tel grains=%llu err=%u per_avg=%llu per_max=%llu exp=%llu step_max=%llu rest_min=%d",
             (unsigned long long)grains_, port_.errors(),
             (unsigned long long)(period_n_ ? period_sum_us_ / period_n_ : 0),
             (unsigned long long)period_max_us_, (unsigned long long)expect,
             (unsigned long long)step_max_us_, rest_min_);
        grains_ = 0;
        period_sum_us_ = period_max_us_ = step_max_us_ = 0;
        period_n_ = 0;
        rest_min_ = 0x7fffffff;
    }

    PortOut port_{vita_driver()};
    int grain_ = 512;
    void (*step_)(void*) = nullptr;
    void* ctx_ = nullptr;
    pthread_t th_{};
    bool has_th_ = false;
    unsigned mask_ = kAudioCpuMask;
    // Bandera de parada leída por thread_entry() en otro hilo: tiene que ser
    // atómica (volatile no sincroniza; era carrera de datos, review HW-018).
    std::atomic<bool> run_{false};
    bool wrote_ = false;
    unsigned consec_ = 0;
    // Telemetría por grano (SPEC-048 Fase 2), sólo la toca el hilo de audio.
    // prev_us_ es de 32 bits como el reloj del kernel (para que el wrap sea el
    // correcto al restar); los acumuladores son de 64.
    uint64_t grains_ = 0, period_sum_us_ = 0, period_max_us_ = 0, step_max_us_ = 0;
    uint32_t prev_us_ = 0, period_n_ = 0;
    int rest_min_ = 0x7fffffff;
};

}  // namespace

Sink* make_vita_sink(const AudioThreadSpec& spec) { return new VitaSink(spec); }

}  // namespace hw::audio

#else

namespace hw::audio {
Sink* make_vita_sink(const AudioThreadSpec& /*spec*/) { return nullptr; }
}  // namespace hw::audio

#endif
