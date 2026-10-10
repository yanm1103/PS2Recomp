// wotm_scene.inc (incluido por ps2_runtime.cpp; raylib e ps2_runtime.h ja estao incluidos)
#include <cctype>
#include "wotm_live.inc"
namespace wotm_scene_ns
{
const uint8_t *g_rd = nullptr;

inline bool okAddr(uint32_t a) { return a >= 0x00100000u && a < 0x01F00000u; }
inline uint32_t u32(uint32_t a) { uint32_t v; std::memcpy(&v, g_rd + a, 4); return v; }
inline uint16_t u16(uint32_t a) { uint16_t v; std::memcpy(&v, g_rd + a, 2); return v; }
inline uint8_t u8(uint32_t a) { return g_rd[a]; }
inline float f32(uint32_t a) { float v; std::memcpy(&v, g_rd + a, 4); return v; }

enum Op { OBJECT = 0, GROUP = 1, LOD = 2, TRANSLATE = 3, ROTATE = 4, SWITCH = 6, SCALE = 30 };

struct Pt { float x, y, z, r; uint32_t node; int src; };

struct Walker
{
    std::vector<Pt> pts;
    int src = 0;
    uint32_t perSrc[4] = {};
    uint32_t visited = 0, byOp[64] = {};
    uint32_t seen[8192];
    uint32_t nseen = 0;

    bool already(uint32_t n)
    {
        for (uint32_t i = 0; i < nseen; ++i) if (seen[i] == n) return true;
        if (nseen < 8192) seen[nseen++] = n;
        return false;
    }

    void kids(uint32_t arr, uint32_t n, float x, float y, float z, int depth)
    {
        if (n > 512) n = 512;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint32_t c = u32(arr + 4 * i);
            if (okAddr(c)) walk(c, x, y, z, depth + 1);
        }
    }

    void walk(uint32_t node, float x, float y, float z, int depth)
    {
        if (depth > 48 || visited > 60000 || !okAddr(node)) return;
        if (already(node)) return;
        ++visited;
        const uint32_t op = u32(node) & 0x3Fu;
        ++byOp[op & 63];
        switch (op)
        {
        case OBJECT:
        {
            float r2 = f32(node + 8);
            pts.push_back({x, y, z, (r2 > 0 && r2 < 1e12f) ? std::sqrt(r2) : 0.f, node, src});
            ++perSrc[src & 3];
            break;
        }
        case GROUP: kids(node + 0x20, u16(node + 8), x, y, z, depth); break;
        case TRANSLATE:
            kids(node + 0x1C, u32(node + 8), x + f32(node + 0x10), y + f32(node + 0x14), z + f32(node + 0x18), depth);
            break;
        case ROTATE: kids(node + 0x50, u32(node + 8), x, y, z, depth); break;       // rotacao ignorada neste marco
        case SCALE: kids(node + 0x2C, u32(node + 8), x, y, z, depth); break;        // escala ignorada neste marco
        case SWITCH:
        {
            uint32_t which = u8(node + 8), n = u8(node + 0xB);
            if (which < n) { uint32_t c = u32(node + 0xC + 4 * which); if (okAddr(c)) walk(c, x, y, z, depth + 1); }
            break;
        }
        case LOD:
        {
            uint32_t n = u32(node + 4);
            if (n > 0 && n < 16) { uint32_t c = u32(node + 0x20 + 8); if (okAddr(c)) walk(c, x, y, z, depth + 1); } // lod mais detalhado
            break;
        }
        default: break; // nos de colisao, luz, particulas, animacao... (proximos marcos)
        }
    }
};
} // namespace
using namespace wotm_scene_ns;

static void wotmDrawScene(const uint8_t *rdram)
{
    static long frame = 0;
    ++frame;
    g_rd = rdram;
    if (!g_rd) return;
    {
        // Sem VU1 ninguem conclui o pacote: o jogo espera `objsInPacket | objsInAlphaPacket` zerarem (rtWaitForVu1, e um laco
        // embutido em particleDraw). A apresentacao (60 Hz) faz o papel da conclusao do VU1.
        static const bool s_noVu1 = std::getenv("PS2X_NO_VU1") != nullptr;
        if (s_noVu1)
        {
            const uint32_t zero = 0;
            std::memcpy(const_cast<uint8_t *>(rdram) + 0x6F87A0u, &zero, 4);
            std::memcpy(const_cast<uint8_t *>(rdram) + 0x6F87A4u, &zero, 4);
        }
    }
    if (wotmDrawNative(rdram)) return; // camada nativa (native/wotm_native.hpp): fase lida da RAM, sem VU1/GS
    const uint32_t root = u32(0x006F87C4u); // world
    Walker w;
    if (okAddr(root))
    {
        w.src = 0; w.walk(root, 0, 0, 0, 0);                        // mundo
        uint32_t sky = u32(root + 4);
        if (okAddr(sky)) { w.src = 3; w.walk(sky, 0, 0, 0, 0); }    // ceu
    }
    auto csList = [&](uint32_t head, int src) {                     // listas de _cs ativas (personagens/instancias)
        uint32_t n = u32(head + 4);
        for (int guard = 0; okAddr(n) && n != head && guard < 4096; ++guard, n = u32(n + 4))
        {
            uint32_t cs = u32(n);
            if (!okAddr(cs) || !u8(cs + 0xC)) continue;               // drawMe
            uint32_t ep = u32(cs);
            if (okAddr(ep)) { w.src = src; w.walk(ep, f32(cs + 0x10), f32(cs + 0x14), f32(cs + 0x18), 0); }
        }
    };
    csList(0x0043A2C0u, 1);   // CsPool::m_activeList
    csList(0x0043A2E0u, 2);   // CsPool::m_HPActiveList

    if (frame % 300 == 0 && std::getenv("PS2X_STATS"))
    {
        std::fprintf(stderr, "[scene] frame %ld currScreen=%u world=%06x visited=%u objetos=%zu (mundo=%u cs=%u hp=%u ceu=%u) | ops: obj=%u grp=%u lod=%u trn=%u rot=%u sw=%u scl=%u\n", frame,
                     u32(0x006F8464u), root, w.visited, w.pts.size(), w.perSrc[0], w.perSrc[1], w.perSrc[2], w.perSrc[3], w.byOp[0], w.byOp[1], w.byOp[2], w.byOp[3], w.byOp[4], w.byOp[6], w.byOp[30]);
    }

    const int W = GetScreenWidth(), H = GetScreenHeight();
    DrawRectangle(0, 0, W, H, Color{10, 12, 20, 255});
    if (w.pts.empty())
    {
        DrawText(okAddr(root) ? "cena: 0 objetos" : "cena: world == null", 12, 12, 20, Color{200, 200, 200, 255});
        return;
    }
    float minx = 1e30f, maxx = -1e30f, minz = 1e30f, maxz = -1e30f;
    for (const Pt &p : w.pts)
    {
        if (!std::isfinite(p.x) || !std::isfinite(p.z) || std::fabs(p.x) > 1e7f || std::fabs(p.z) > 1e7f) continue;
        minx = std::fmin(minx, p.x); maxx = std::fmax(maxx, p.x);
        minz = std::fmin(minz, p.z); maxz = std::fmax(maxz, p.z);
    }
    if (minx > maxx) return;
    const float sx = (W - 40.f) / std::fmax(maxx - minx, 1.f), sz = (H - 60.f) / std::fmax(maxz - minz, 1.f);
    const float s = std::fmin(sx, sz);
    for (const Pt &p : w.pts)
    {
        if (!std::isfinite(p.x) || !std::isfinite(p.z) || std::fabs(p.x) > 1e7f || std::fabs(p.z) > 1e7f) continue;
        float px = 20.f + (p.x - minx) * s, py = 40.f + (p.z - minz) * s;
        float rad = std::fmax(1.5f, std::fmin(p.r * s, 14.f));
        static const Color cols[4] = {Color{90, 200, 255, 255}, Color{255, 220, 80, 255}, Color{120, 255, 120, 255}, Color{170, 170, 170, 255}};
        DrawCircleLines((int)px, (int)py, rad, cols[p.src & 3]);
        DrawPixel((int)px, (int)py, Color{255, 255, 255, 255});
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "cena: %zu obj (mundo %u amarelo=cs %u verde=hp %u cinza=ceu %u) x[%.0f..%.0f] z[%.0f..%.0f]", w.pts.size(), w.perSrc[0], w.perSrc[1], w.perSrc[2], w.perSrc[3], minx, maxx, minz, maxz);
    DrawText(buf, 10, 10, 16, Color{255, 220, 120, 255});
}
