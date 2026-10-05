#include "Render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

#ifdef TOPOWEAVE_HAVE_ZLIB
#include <zlib.h>
#endif

namespace rtp {
namespace kit {

namespace {
constexpr int kSS = 2;  // supersampling factor

struct ScreenPt {
    double x, y, invZ;
    bool ok;
};
}  // namespace

Canvas::Canvas(const Camera& cam) : cam_(cam) {
    cam_.width = cam.width * kSS;
    cam_.height = cam.height * kSS;
    w_ = cam_.width;
    h_ = cam_.height;
    color_.assign(static_cast<size_t>(w_) * h_, Rgb());
    depth_.assign(static_cast<size_t>(w_) * h_, kInf);
    depthBias_ = distance(cam.eye, cam.target) * 0.006;
}

void Canvas::clearGradient(const Rgb& top, const Rgb& bottom) {
    for (int y = 0; y < h_; ++y) {
        const double t = static_cast<double>(y) / (h_ - 1);
        const Rgb c{lerp(top.r, bottom.r, t), lerp(top.g, bottom.g, t), lerp(top.b, bottom.b, t)};
        for (int x = 0; x < w_; ++x) color_[static_cast<size_t>(y) * w_ + x] = c;
    }
    std::fill(depth_.begin(), depth_.end(), kInf);
}

void Canvas::blend(int x, int y, const Rgb& c, double a) {
    Rgb& d = color_[static_cast<size_t>(y) * w_ + x];
    d.r = lerp(d.r, c.r, a);
    d.g = lerp(d.g, c.g, a);
    d.b = lerp(d.b, c.b, a);
}

namespace {
template <class F>
void rasterTriangle(const ScreenPt& a, const ScreenPt& b, const ScreenPt& c, int w, int h, F&& shade) {
    if (!a.ok || !b.ok || !c.ok) return;
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-12) return;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            double w0 = (b.x - px) * (c.y - py) - (b.y - py) * (c.x - px);
            double w1 = (c.x - px) * (a.y - py) - (c.y - py) * (a.x - px);
            double w2 = (a.x - px) * (b.y - py) - (a.y - py) * (b.x - px);
            w0 /= area; w1 /= area; w2 /= area;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const double invZ = w0 * a.invZ + w1 * b.invZ + w2 * c.invZ;
            // Perspective-correct barycentrics.
            const double p0 = w0 * a.invZ / invZ, p1 = w1 * b.invZ / invZ, p2 = w2 * c.invZ / invZ;
            shade(x, y, 1.0 / invZ, p0, p1, p2);
        }
}
}  // namespace

void Canvas::drawSurface(const TriMesh& mesh, const Rgb& base) {
    std::vector<Vec3> vn(mesh.verts.size());
    for (const auto& t : mesh.tris) {
        const Vec3 n = cross(mesh.verts[t[1]] - mesh.verts[t[0]], mesh.verts[t[2]] - mesh.verts[t[0]]);
        for (int k = 0; k < 3; ++k) vn[t[k]] += n;
    }
    for (auto& n : vn) n = normalize(n);
    std::vector<ScreenPt> sp(mesh.verts.size());
    for (size_t i = 0; i < mesh.verts.size(); ++i) {
        double x = 0, y = 0, z = 1;
        const bool ok = cam_.project(mesh.verts[i], x, y, z);
        sp[i] = {x, y, ok ? 1.0 / z : 0.0, ok};
    }
    const Vec3 viewDir = cam_.forward();
    const Vec3 key = normalize(Vec3(-0.5, 0.6, -0.6) * -1.0 + viewDir * -0.3);
    for (const auto& t : mesh.tris) {
        rasterTriangle(sp[t[0]], sp[t[1]], sp[t[2]], w_, h_,
                       [&](int x, int y, double z, double b0, double b1, double b2) {
                           double& zb = depth_[static_cast<size_t>(y) * w_ + x];
                           if (z >= zb) return;
                           zb = z;
                           Vec3 n = normalize(vn[t[0]] * b0 + vn[t[1]] * b1 + vn[t[2]] * b2);
                           if (dot(n, viewDir) > 0) n = -n;
                           const double diff = std::max(0.0, dot(n, key));
                           const double fill = 0.5 + 0.5 * std::max(0.0, -dot(n, viewDir));
                           const double rim = std::pow(1.0 - std::fabs(dot(n, viewDir)), 3.0) * 0.25;
                           const double l = 0.18 + 0.55 * diff + 0.30 * fill;
                           color_[static_cast<size_t>(y) * w_ + x] = {
                               std::min(1.0, base.r * l + rim), std::min(1.0, base.g * l + rim),
                               std::min(1.0, base.b * l + rim)};
                       });
    }
}

void Canvas::drawPolyFill(const PolyMesh& mesh, const Rgb& color, double alpha) {
    std::vector<ScreenPt> sp(mesh.verts.size());
    for (size_t i = 0; i < mesh.verts.size(); ++i) {
        double x = 0, y = 0, z = 1;
        const bool ok = cam_.project(mesh.verts[i], x, y, z);
        sp[i] = {x, y, ok ? 1.0 / z : 0.0, ok};
    }
    std::vector<char> touched(static_cast<size_t>(w_) * h_, 0);
    for (const auto& f : mesh.faces) {
        for (size_t k = 1; k + 1 < f.size(); ++k) {
            rasterTriangle(sp[f[0]], sp[f[k]], sp[f[k + 1]], w_, h_,
                           [&](int x, int y, double z, double, double, double) {
                               const size_t i = static_cast<size_t>(y) * w_ + x;
                               if (z > depth_[i] + depthBias_ || touched[i]) return;
                               touched[i] = 1;
                               blend(x, y, color, alpha);
                           });
        }
    }
}

void Canvas::disk(double cx, double cy, double z, const Rgb& c, double radius, bool depthTest) {
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius - 1)));
    const int x1 = std::min(w_ - 1, static_cast<int>(std::ceil(cx + radius + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius - 1)));
    const int y1 = std::min(h_ - 1, static_cast<int>(std::ceil(cy + radius + 1)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            const double a = clamp(radius + 0.5 - d, 0.0, 1.0);
            if (a <= 0) continue;
            if (depthTest && z > depth_[static_cast<size_t>(y) * w_ + x] + depthBias_) continue;
            blend(x, y, c, a);
        }
}

void Canvas::drawLine(const Vec3& a, const Vec3& b, const Rgb& c, double width, bool depthTest) {
    double ax, ay, az, bx, by, bz;
    if (!cam_.project(a, ax, ay, az) || !cam_.project(b, bx, by, bz)) return;
    const double len = std::hypot(bx - ax, by - ay);
    const int n = std::max(1, static_cast<int>(len / 0.75));
    const double r = width * kSS * 0.5;
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double invZ = lerp(1.0 / az, 1.0 / bz, t);
        disk(lerp(ax, bx, t), lerp(ay, by, t), 1.0 / invZ, c, r, depthTest);
    }
}

void Canvas::drawPolyWire(const PolyMesh& mesh, const Rgb& color, double width) {
    for (const auto& e : mesh.edges()) drawLine(mesh.verts[e.a], mesh.verts[e.b], color, width, true);
}

void Canvas::drawPolyline(const Polyline& line, const Rgb& color, double width, bool depthTest) {
    for (int i = 0; i < line.segmentCount(); ++i)
        drawLine(line.pts[i], line.pts[(i + 1) % line.size()], color, width, depthTest);
}

void Canvas::drawPoint(const Vec3& p, const Rgb& color, double radius, bool depthTest) {
    double x, y, z;
    if (cam_.project(p, x, y, z)) disk(x, y, z, color, radius * kSS, depthTest);
}

bool Canvas::writePng(const std::string& path) const {
    const int w = w_ / kSS, h = h_ / kSS;
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Rgb acc;
            for (int sy = 0; sy < kSS; ++sy)
                for (int sx = 0; sx < kSS; ++sx) {
                    const Rgb& c = color_[static_cast<size_t>(y * kSS + sy) * w_ + x * kSS + sx];
                    acc.r += c.r; acc.g += c.g; acc.b += c.b;
                }
            const double k = 1.0 / (kSS * kSS);
            unsigned char* o = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            o[0] = static_cast<unsigned char>(clamp(acc.r * k, 0.0, 1.0) * 255.0 + 0.5);
            o[1] = static_cast<unsigned char>(clamp(acc.g * k, 0.0, 1.0) * 255.0 + 0.5);
            o[2] = static_cast<unsigned char>(clamp(acc.b * k, 0.0, 1.0) * 255.0 + 0.5);
        }
    return kit::writePng(path, w, h, rgb);
}

// ---------------------------------------------------------------------------
// Minimal PNG encoder. Uses zlib when available, otherwise "stored" blocks.
// ---------------------------------------------------------------------------
namespace {
uint32_t crcTable[256];
bool crcInit = false;
uint32_t pngCrc(const unsigned char* data, size_t n, uint32_t crc = 0xffffffffu) {
    if (!crcInit) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            crcTable[i] = c;
        }
        crcInit = true;
    }
    for (size_t i = 0; i < n; ++i) crc = crcTable[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc;
}
void put32(std::vector<unsigned char>& v, uint32_t x) {
    v.push_back(static_cast<unsigned char>(x >> 24));
    v.push_back(static_cast<unsigned char>(x >> 16));
    v.push_back(static_cast<unsigned char>(x >> 8));
    v.push_back(static_cast<unsigned char>(x));
}
void chunk(std::FILE* f, const char* type, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> buf;
    put32(buf, static_cast<uint32_t>(data.size()));
    buf.insert(buf.end(), type, type + 4);
    buf.insert(buf.end(), data.begin(), data.end());
    const uint32_t crc = pngCrc(buf.data() + 4, buf.size() - 4) ^ 0xffffffffu;
    put32(buf, crc);
    std::fwrite(buf.data(), 1, buf.size(), f);
}
}  // namespace

bool writePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(sig, 1, 8, f);

    std::vector<unsigned char> ihdr;
    put32(ihdr, static_cast<uint32_t>(w));
    put32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bit, RGB
    chunk(f, "IHDR", ihdr);

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * w * 3,
                   rgb.begin() + static_cast<size_t>(y + 1) * w * 3);
    }
#ifdef TOPOWEAVE_HAVE_ZLIB
    uLongf zlen = compressBound(static_cast<uLong>(raw.size()));
    std::vector<unsigned char> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), static_cast<uLong>(raw.size()), 9) != Z_OK) {
        std::fclose(f);
        return false;
    }
    z.resize(zlen);
#else
    // zlib stream made of uncompressed "stored" blocks.
    std::vector<unsigned char> z = {0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<unsigned char>(n & 0xff));
        z.push_back(static_cast<unsigned char>(n >> 8));
        z.push_back(static_cast<unsigned char>(~n & 0xff));
        z.push_back(static_cast<unsigned char>((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);
#endif
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    std::fclose(f);
    return true;
}

}  // namespace kit
}  // namespace rtp
