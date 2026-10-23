#include "vita_present.h"
#include "present_math.h"
#include "vgl_pools.h"
#include "diag/hw_diag.h"
#ifdef __vita__
#include <vitaGL.h>
#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <cstring>
#include <cstdlib>
#include "shaders/precompiled_blit_v.h"
#include "shaders/precompiled_blit_f.h"
namespace hw::present {
namespace {
constexpr uint32_t kScreenW=960,kScreenH=544;
constexpr int kLegacyPool=0; // No usamos glBegin; evita reservar memoria por frame.
}
bool VitaPresenter::init(uint32_t w,uint32_t h) {
    if(init_state_.ready()) return resize(w,h);
    if(!w||!h||w>2048||h>2048) { error_="invalid dimensions";return false; }
    // vitaGL c707ff00 devuelve `res_fallback`: GL_FALSE en el caso normal
    // 960x544, GL_TRUE si tuvo que reducir resolución. No es un bool éxito.
    // El default de vitaGL es 32 MiB: no cabe en nuestro pool RAM de 8 MiB.
    if(!init_state_.context_started()) {
        // SPEC-052: el tamaño del pool circular es un toggle de diagnóstico
        // (HW_DIAG_CIRC_MB=2..32 MiB; default 2 MiB = el del VPK de usuario).
        const uint32_t circ_mb = uint32_t(diag::parse_toggles_env().circ_mb);
        vglSetCircularPoolSize(circ_mb * 1024 * 1024);
        // BUG-035: pools más grandes (ver vgl_pools.h); knobs HW_VGL_*_MB.
        const VglPools pools=vgl_pools([](const char* k){ return std::getenv(k); });
        (void)vglInitWithCustomSizes(kLegacyPool,kScreenW,kScreenH,int(pools.ram),int(pools.cdram),
                                     0,0,SCE_GXM_MULTISAMPLE_NONE);
        sceClibPrintf("[hw-present] vitaGL pools ram=%uK cdram=%uK\n",pools.ram>>10,pools.cdram>>10);
        init_state_.mark_context_started();
    }
    vglWaitVblankStart(GL_TRUE);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,kScreenW,kScreenH,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glClearColor(0,0,0,1);
    // Shaders de blit de vitaGL: GXP existente, sin compilación SceShaccCg.
    GLuint vs=glCreateShader(GL_CG_VERTEX_SHADER_EXT),fs=glCreateShader(GL_CG_FRAGMENT_SHADER_EXT);
    if(!vs||!fs) {
        if(vs)glDeleteShader(vs);
        if(fs)glDeleteShader(fs);
        error_="blit shader allocation failed";release_resources();return false;
    }
    vglShaderGxpBinary(1,&vs,blit_v,size_blit_v);
    vglShaderGxpBinary(1,&fs,blit_f,size_blit_f);
    program_=glCreateProgram();
    if(!program_) {
        glDeleteShader(vs);glDeleteShader(fs);
        error_="blit program allocation failed";release_resources();return false;
    }
    glAttachShader(program_,vs);glAttachShader(program_,fs);
    glBindAttribLocation(program_,0,"position");
    glBindAttribLocation(program_,1,"texcoord");
    glLinkProgram(program_);
    GLint linked=0;glGetProgramiv(program_,GL_LINK_STATUS,&linked);
    glDeleteShader(vs);glDeleteShader(fs);
    if(!linked) { error_="blit program link failed";release_resources();return false; }
    if(!make_texture(w,h)) { const char* e=error_;release_resources();error_=e;return false; }
    init_state_.mark_ready();
    return true;
}
bool VitaPresenter::make_texture(uint32_t w,uint32_t h) {
    if(texture_) { GLuint old=texture_;glDeleteTextures(1,&old);texture_=0; }
    uint32_t black[256];for(auto& c:black)c=0xFF000000u;
    glColorTable(GL_COLOR_TABLE,GL_RGBA,256,GL_RGBA,GL_UNSIGNED_BYTE,black);
    GLuint tex=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_COLOR_INDEX8_EXT,w,h,0,GL_RED,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    if(glGetError()!=GL_NO_ERROR) { glDeleteTextures(1,&tex);error_="P8 texture GL error";return false; }
    texture_=tex;
    texture_data_=static_cast<uint8_t*>(vglGetTexDataPointer(GL_TEXTURE_2D));
    SceGxmTexture* gxm=vglGetGxmTexture(GL_TEXTURE_2D);
    palette_data_=gxm?static_cast<uint32_t*>(sceGxmTextureGetPalette(gxm)):nullptr;
    sceClibPrintf("[hw-present] P8 data=%p palette=%p descriptor=%08x,%08x,%08x,%08x\n",
        texture_data_,palette_data_,gxm?reinterpret_cast<uint32_t*>(gxm)[0]:0,
        gxm?reinterpret_cast<uint32_t*>(gxm)[1]:0,gxm?reinterpret_cast<uint32_t*>(gxm)[2]:0,
        gxm?reinterpret_cast<uint32_t*>(gxm)[3]:0);
    if(!texture_data_||!palette_data_) { error_="P8 texture/palette mapping failed";return false; }
    width_=w;height_=h;error_="ok";return true;
}
bool VitaPresenter::resize(uint32_t w,uint32_t h) {
    if(!init_state_.ready()) return init(w,h);
    if(w==width_&&h==height_) return true;
    if(!w||!h||w>2048||h>2048) { error_="invalid dimensions";return false; }
    if(make_texture(w,h)) return true;
    const char* e=error_;release_resources();error_=e;return false;
}
void VitaPresenter::present(const uint8_t* idx,uint32_t pitch,const uint32_t pal[256]) {
    if(!init_state_.ready()||!texture_data_||!palette_data_||!idx||!pal||pitch<width_) {
        error_="invalid present input";return;
    }
    const uint32_t stride=(width_+7u)&~7u;
    if(!copy_index_rows(texture_data_,stride,idx,pitch,width_,height_)) {
        error_="P8 row copy failed";return;
    }
    for(unsigned i=0;i<256;++i) palette_data_[i]=argb_to_abgr(pal[i]);
    // D3D leaves fixed-function state enabled on the shared vitaGL context.
    // Reset it before the P8 clear/blit so its scissor/blend/alpha test cannot
    // clip the frame or discard the indexed overlay after a D3D scene.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0,0,kScreenW,kScreenH);
    glBindTexture(GL_TEXTURE_2D,texture_);
    const Rect dst=fit_rect(int32_t(width_),int32_t(height_),kScreenW,kScreenH);
    const float x0=2.0f*dst.x/kScreenW-1.0f,x1=2.0f*(dst.x+dst.w)/kScreenW-1.0f;
    const float y0=1.0f-2.0f*dst.y/kScreenH,y1=1.0f-2.0f*(dst.y+dst.h)/kScreenH;
    const float positions[]={x0,y0,x1,y0,x0,y1,x1,y1};
    const float uv[]={0,0,1,0,0,1,1,1};
    glUseProgram(program_);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,positions);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,uv);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    vglSwapBuffers(GL_FALSE);
    ++frames_;
}
void VitaPresenter::shutdown() {
    release_resources();
    // La versión fijada de vitaGL no expone vglEnd: su contexto vive hasta ExitProcess.
}
void VitaPresenter::release_resources() {
    if(program_) { glUseProgram(0);glDeleteProgram(program_);program_=0; }
    if(texture_) { GLuint old=texture_;glDeleteTextures(1,&old);texture_=0; }
    texture_data_=nullptr;palette_data_=nullptr;width_=height_=0;
    init_state_.mark_failed();
}
}
#endif
