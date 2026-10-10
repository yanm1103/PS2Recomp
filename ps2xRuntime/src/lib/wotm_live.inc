// wotm_live.inc: desenho nativo ao vivo (incluido por wotm_scene.inc, no escopo global de ps2_runtime.cpp).
// Usa native/wotm_native.hpp (do repo WoTM; passe -DWOTM_NATIVE_DIR=<WoTM>/native ao CMake). Sem o header, nao faz nada.
#if __has_include("wotm_native.hpp")
#include "wotm_native.hpp"
#include <filesystem>
#define WOTM_HAVE_NATIVE 1

namespace wotm_live
{
struct State
{
    wotm::Scene scene;
    RenderTexture2D rt{};
    uint32_t levelSig = 0;
    bool rtxChosen = false;
    std::string rtxPath;
    long frame = 0;
};

inline uint32_t levelSignature(const wotm::Ram &ram)
{
    uint32_t h = ram.u32(wotm::addr::tempVramTexAddr) * 2654435761u;
    for (uint32_t a = 0xA00000; a < 0xA00400; a += 4)
        h = (h ^ ram.u32(a)) * 16777619u;
    return h;
}

// Escolhe, entre LVL/*.RTX (relativo ao diretorio de trabalho = raiz do disco), o que cobre as paletas dos objetos texturizados da cena.
inline bool chooseRtx(State &S, const wotm::Ram &ram)
{
    std::vector<uint32_t> nodes;
    {
        std::vector<uint64_t> keys;
        for (const wotm::DrawItem &it : S.scene.items)
        {
            const uint64_t k = wotm::Scene::texKey(ram, it.node);
            if (!k || std::find(keys.begin(), keys.end(), k) != keys.end())
                continue;
            keys.push_back(k);
            nodes.push_back(it.node);
        }
    }
    if (nodes.size() < 8)
        return false; // fase ainda carregando
    const uint32_t base = ram.u32(wotm::addr::tempVramTexAddr) >> 6;
    std::string best;
    size_t bestScore = 0;
    std::error_code ec;
    for (const auto &de : std::filesystem::directory_iterator("LVL", ec))
    {
        std::string ext = de.path().extension().string();
        for (char &c : ext)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (ext != ".RTX")
            continue;
        if (!S.scene.loadRtx(de.path().string().c_str()))
            continue;
        size_t score = 0;
        for (uint32_t node : nodes)
        {
            const uint64_t t0v = wotm::Scene::objectTex0(ram, node);
            const wotm::gs::Tex0 t = wotm::gs::Tex0::decode(t0v);
            if (t.cbp < base)
                continue;
            const auto f = S.scene.rtxPal.find(t.cbp - base);
            if (f == S.scene.rtxPal.end())
                continue;
            const uint32_t want = (t.psm == wotm::gs::T4) ? 16u : 256u;
            if (f->second.size() == want)
                ++score;
        }
        if (score > bestScore)
        {
            bestScore = score;
            best = de.path().string();
        }
    }
    if (best.empty())
        return false;
    S.scene.loadRtx(best.c_str());
    S.rtxPath = best;
    std::fprintf(stderr, "[native] fase: %s (%zu de %zu texturas com paleta)\n", best.c_str(), bestScore, nodes.size());
    return true;
}
} // namespace wotm_live

// Desenha a fase lida da RAM (sem VU1/GS) por cima do quadro; devolve falso (e nao desenha) quando nao ha fase carregada.
static bool wotmDrawNative(const uint8_t *rdram)
{
    static wotm_live::State S;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    const uint32_t wc = ram.u32(wotm::addr::world);
    if (!ram.ok(wc, 0x40) || !ram.ok(ram.u32(wc)))
        return false;

    const uint32_t sig = wotm_live::levelSignature(ram);
    if (sig != S.levelSig)
    {
        S.scene.invalidate();
        S.levelSig = sig;
        S.rtxChosen = false;
    }
    S.scene.collect(ram, 0);
    if (S.scene.items.size() < 20)
        return false;
    if (!S.rtxChosen)
        S.rtxChosen = wotm_live::chooseRtx(S, ram);

    const wotm::GameCamera gc = wotm::readGameCamera(ram, 0);
    if (!gc.ok)
        return false;

    const int RW = 960, RH = 720;
    if (S.rt.id == 0)
        S.rt = LoadRenderTexture(RW, RH);

    BeginTextureMode(S.rt);
    ClearBackground(Color{30, 40, 60, 255});
    rlSetClipPlanes(0.5, 30000.0);
    Camera3D c3{};
    c3.position = {gc.pos[0], gc.pos[1], gc.pos[2]};
    c3.target = {gc.pos[0] + gc.fwd[0], gc.pos[1] + gc.fwd[1], gc.pos[2] + gc.fwd[2]};
    c3.up = {gc.up[0], gc.up[1], gc.up[2]};
    c3.fovy = gc.fovy;
    c3.projection = CAMERA_PERSPECTIVE;
    BeginMode3D(c3);
    S.scene.draw(ram, false);
    EndMode3D();
    EndTextureMode();

    // letterbox 4:3 na janela
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    const float k = std::min(sw / RW, sh / RH);
    const Rectangle src{0.f, 0.f, static_cast<float>(RW), -static_cast<float>(RH)};
    const Rectangle dst{(sw - RW * k) * 0.5f, (sh - RH * k) * 0.5f, RW * k, RH * k};
    DrawTexturePro(S.rt.texture, src, dst, Vector2{0.f, 0.f}, 0.f, WHITE);

    if (++S.frame % 300 == 0 && std::getenv("PS2X_STATS"))
        std::fprintf(stderr, "[native] frame %ld itens=%zu ceu=%zu nos=%u rtx=%s\n", S.frame, S.scene.items.size(), S.scene.sky.size(),
                     S.scene.visited, S.rtxPath.c_str());
    return true;
}
#else
static bool wotmDrawNative(const uint8_t *) { return false; }
#endif
