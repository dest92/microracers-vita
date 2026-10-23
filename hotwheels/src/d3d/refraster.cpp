// refraster.cpp — HW-010: rasterizador de referencia (CPU, ARGB8888) + PNG. Spec 003.
// Fiel a D3D6: centros de píxel enteros, regla top-left, uv afín (rhw=1), WRAP,
// NEAREST/LINEAR, blend D3DBLEND por canal, alpha test sólo para colorkey.
#include "d3d/d3d.h"
#include "host/frame_retention.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hw::d3d {

void RefRaster::begin_frame(uint32_t w, uint32_t h) {
    if (w != w_ || h != h_) { w_ = w; h_ = h; fb_.assign(size_t(w) * h, 0xFF000000u); }
}
void RefRaster::clear(const Rect& r, uint32_t c) {
    int32_t x0 = std::max(0, r.x), y0 = std::max(0, r.y);
    int32_t x1 = std::min(int32_t(w_), r.x + r.w), y1 = std::min(int32_t(h_), r.y + r.h);
    for (int32_t y = y0; y < y1; ++y) std::fill(&fb_[size_t(y) * w_ + x0], &fb_[size_t(y) * w_ + x1], c | 0xFF000000u);
}

namespace {
int wrap(int v, int n) { v %= n; return v < 0 ? v + n : v; }
uint32_t lerp(uint32_t a, uint32_t b, float t) {
    uint32_t o = 0;
    for (int s = 0; s < 32; s += 8) {
        float v = float(a >> s & 255) * (1 - t) + float(b >> s & 255) * t;
        o |= uint32_t(std::lround(v)) << s;
    }
    return o;
}
// Factor D3DBLEND para un canal (0..255). sa = alfa del src; el target no tiene alfa (=255).
int factor(uint32_t f, int s, int d, int sa) {
    switch (f) {
    case 1: return 0; case 2: return 255; case 3: return s; case 4: return 255 - s;
    case 5: return sa; case 6: return 255 - sa; case 7: return 255; case 8: return 0;
    case 9: return d; case 10: return 255 - d; default: return 255;
    }
}
} // namespace

uint32_t RefRaster::sample(const TextureData& t, float u, float v, bool linear) const {
    int W = int(t.width), H = int(t.height);
    auto at = [&](int x, int y) { return t.argb[size_t(wrap(y, H)) * W + wrap(x, W)]; };
    if (!linear) return at(int(std::floor(u * W)), int(std::floor(v * H)));
    float fu = u * W - 0.5f, fv = v * H - 0.5f;
    int x0 = int(std::floor(fu)), y0 = int(std::floor(fv));
    float ax = fu - x0, ay = fv - y0;
    return lerp(lerp(at(x0, y0), at(x0 + 1, y0), ax), lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), ax), ay);
}

void RefRaster::triangle(const Vertex& A, const Vertex& B, const Vertex& C, const DrawCall& d) {
    struct P { float x, y, u, v; };
    auto px = [&](const Vertex& q) {
        return P{(q.x + 1.f) * float(w_) / 2.f - 0.5f, (1.f - q.y) * float(h_) / 2.f - 0.5f, q.u, q.v}; };
    P a = px(A), b = px(B), c = px(C);
    int32_t sx0 = std::max(0, d.scissor.x), sy0 = std::max(0, d.scissor.y);
    int32_t sx1 = std::min(int32_t(w_), d.scissor.x + d.scissor.w), sy1 = std::min(int32_t(h_), d.scissor.y + d.scissor.h);
    int32_t x0 = std::max(sx0, int32_t(std::floor(std::min({a.x, b.x, c.x})))),
            x1 = std::min(sx1 - 1, int32_t(std::ceil(std::max({a.x, b.x, c.x})))),
            y0 = std::max(sy0, int32_t(std::floor(std::min({a.y, b.y, c.y})))),
            y1 = std::min(sy1 - 1, int32_t(std::ceil(std::max({a.y, b.y, c.y}))));
    // Subpíxel 28.4 en enteros: las funciones de borde son exactas y antisimétricas,
    // así dos triángulos que comparten arista no dejan grietas ni se pisan.
    struct I { int64_t x, y; };
    auto fx16 = [](const P& p) { return I{std::llround(p.x * 16.f), std::llround(p.y * 16.f)}; };
    I ia = fx16(a), ib = fx16(b), ic = fx16(c);
    int64_t iarea = (ib.x - ia.x) * (ic.y - ia.y) - (ib.y - ia.y) * (ic.x - ia.x);
    if (iarea == 0) return;
    if (iarea < 0) { std::swap(ib, ic); std::swap(b, c); iarea = -iarea; }
    // Regla top-left (y hacia abajo, área > 0).
    auto topleft = [](const I& p, const I& q) { int64_t dx = q.x - p.x, dy = q.y - p.y; return (dy == 0 && dx > 0) || dy < 0; };
    auto edge = [](const I& p, const I& q, int64_t x, int64_t y) { return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x); };
    bool tl0 = topleft(ib, ic), tl1 = topleft(ic, ia), tl2 = topleft(ia, ib);
    const TextureData* tex = nullptr;
    if (d.tex) { auto it = tex_.find(d.tex); if (it != tex_.end()) tex = &it->second; }
    for (int32_t y = y0; y <= y1; ++y)
        for (int32_t x = x0; x <= x1; ++x) {
            int64_t X = int64_t(x) * 16, Y = int64_t(y) * 16;
            int64_t e0 = edge(ib, ic, X, Y), e1 = edge(ic, ia, X, Y), e2 = edge(ia, ib, X, Y);
            if (e0 < 0 || e1 < 0 || e2 < 0) continue;
            if ((e0 == 0 && !tl0) || (e1 == 0 && !tl1) || (e2 == 0 && !tl2)) continue;
            float w0 = float(e0), w1 = float(e1), w2 = float(e2), area = float(iarea);
            float l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;
            float u = l0 * a.u + l1 * b.u + l2 * c.u, v = l0 * a.v + l1 * b.v + l2 * c.v;
            uint32_t s = tex ? sample(*tex, u, v, d.linear) : 0xFFFFFFFFu;
            int sa = int(s >> 24);
            if (d.alpha_test && sa == 0) continue;
            uint32_t& dst = fb_[size_t(y) * w_ + x];
            if (!d.blend) { dst = s | 0xFF000000u; continue; }
            uint32_t o = 0xFF000000u;
            for (int sh = 0; sh < 24; sh += 8) {
                int sc = int(s >> sh & 255), dc = int(dst >> sh & 255);
                int r = (sc * factor(d.src, sc, dc, sa) + dc * factor(d.dst, sc, dc, sa) + 127) / 255;
                o |= uint32_t(std::min(255, r)) << sh;
            }
            dst = o;
        }
}

void RefRaster::draw(const DrawCall& d) {
    const auto& v = d.verts;
    if (v.size() < 3) return;
    if (d.prim == 4) for (size_t i = 0; i + 2 < v.size(); i += 3) triangle(v[i], v[i + 1], v[i + 2], d);
    else if (d.prim == 5) for (size_t i = 0; i + 2 < v.size(); ++i) triangle(v[i], v[i + 1], v[i + 2], d);
    else if (d.prim == 6) for (size_t i = 1; i + 1 < v.size(); ++i) triangle(v[0], v[i], v[i + 1], d);
}

uint64_t RefRaster::hash() const {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t px : fb_) for (int s = 0; s < 32; s += 8) { h ^= (px >> s) & 255; h *= 1099511628211ull; }
    return h;
}

namespace {
uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    c = ~c;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
void be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()));
}
} // namespace

bool RefRaster::write_png(const std::string& path) const {
    if (!w_ || !h_) return false;
    std::vector<uint8_t> raw;  // filtro 0 + RGBA por fila
    for (uint32_t y = 0; y < h_; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < w_; ++x) {
            uint32_t c = fb_[size_t(y) * w_ + x];
            raw.insert(raw.end(), {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), uint8_t(c >> 24)});
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};  // zlib con bloques "stored"
    for (size_t i = 0; i < raw.size(); i += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n == raw.size() ? 1 : 0);
        z.insert(z.end(), {uint8_t(n), uint8_t(n >> 8), uint8_t(~n), uint8_t(~n >> 8)});
        z.insert(z.end(), raw.begin() + long(i), raw.begin() + long(i + n));
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, b << 16 | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 13, 10, 26, 10}, ihdr;
    be32(ihdr, w_); be32(ihdr, h_); ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr); chunk(out, "IDAT", z); chunk(out, "IEND", {});
    return hw::host::FrameRetention::save(path, [&](const std::string& target) {
        FILE* f = std::fopen(target.c_str(), "wb");
        if (!f) return false;
        bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
        return std::fclose(f) == 0 && ok;
    });
}

} // namespace hw::d3d
