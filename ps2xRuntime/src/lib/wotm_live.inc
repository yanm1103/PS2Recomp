// wotm_live.inc: desenho nativo ao vivo (incluido por wotm_scene.inc, no escopo global de ps2_runtime.cpp).
// Usa native/wotm_native.hpp (do repo WoTM; passe -DWOTM_NATIVE_DIR=<WoTM>/native ao CMake). Sem o header, nao faz nada.
#if __has_include("wotm_native.hpp")
#include "wotm_native.hpp"
#include <filesystem>
#define WOTM_HAVE_NATIVE 1

namespace wotm_live
{
// Diagnostico de queda: sem windows.h (conflita com o raylib), espelha so o que o filtro precisa.
struct ExcRecord
{
    unsigned long code, flags;
    ExcRecord *next;
    void *address;
    unsigned long nparams;
    unsigned long long info[15];
};
struct ExcPointers
{
    ExcRecord *rec;
    void *ctx;
};
extern "C" __declspec(dllimport) void *__stdcall SetUnhandledExceptionFilter(long(__stdcall *)(ExcPointers *));
extern "C" __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
inline const char *g_stage = "idle";   // etapa da camada nativa em andamento (para localizar a queda)
inline long __stdcall crashFilter(ExcPointers *e)
{
    const char *base = static_cast<const char *>(GetModuleHandleA(nullptr));
    std::fprintf(stderr, "[CRASH] code=0x%08lx addr=%p (exe+0x%llx) stage=%s", e->rec->code, e->rec->address,
                 (unsigned long long)(static_cast<const char *>(e->rec->address) - base), g_stage);
    if (e->rec->code == 0xC0000005)
        std::fprintf(stderr, " acesso=%s endereco=0x%llx", e->rec->info[0] == 1 ? "escrita" : (e->rec->info[0] == 8 ? "execucao" : "leitura"), e->rec->info[1]);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    return 0; // EXCEPTION_CONTINUE_SEARCH
}
struct State
{
    wotm::Scene scene;
    RenderTexture2D rt{};
    uint32_t levelSig = 0;
    bool rtxChosen = false;
    std::string rtxPath;
    long frame = 0;
    wotm::Snapshot A, B;   // quadros do jogo anterior e atual (interpolacao: o jogo anda a 30/s, a tela desenha a 60)
    bool interp = true;    // F4 liga/desliga a interpolacao
};

inline uint32_t levelSignature(const wotm::Ram &ram)
{
    uint32_t h = ram.u32(wotm::addr::tempVramTexAddr) * 2654435761u;
    for (uint32_t a = 0xA00000; a < 0xA00400; a += 4)
        h = (h ^ ram.u32(a)) * 16777619u;
    return h;
}

} // namespace wotm_live

// Desenha a fase lida da RAM (sem VU1/GS) por cima do quadro; devolve falso (e nao desenha) quando nao ha fase carregada.
static bool wotmDrawNative(const uint8_t *rdram)
{
    static wotm_live::State S;
    static bool filterInstalled = false;
    if (!filterInstalled)
    {
        filterInstalled = true;
        wotm_live::SetUnhandledExceptionFilter(wotm_live::crashFilter);
    }
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
        S.A = wotm::Snapshot{};
        S.B = wotm::Snapshot{};
    }
    // Quadro novo do jogo (g_frame em 0x6F7E38 mudou): le a cena da RAM e guarda como instantaneo B (o anterior vira A). Nos desenhos
    // seguintes so mistura A e B: o jogo termina o quadro e espera o pacing, entao a RAM e consistente logo depois do g_frame++.
    const uint32_t gNow = ram.u32(0x6F7E38);
    const double tNow = GetTime();
    if (!S.B.valid || gNow != S.B.g)
    {
        wotm_live::g_stage = "collect";
        S.scene.collect(ram);
        if (S.scene.items.size() < 20)
            return false;
        {
            // nivel + monstros: nomes e bases das paletas vem de fileStatus (idempotente; refaz so quando mudam)
            std::string log;
            wotm_live::g_stage = "loadResources";
            S.rtxChosen = S.scene.loadResources(ram, "", &log);
            if (!log.empty() && log != S.rtxPath)
            {
                S.rtxPath = log;
                std::fprintf(stderr, "[native] recursos: %s\n", log.c_str());
            }
        }
        wotm_live::g_stage = "snapshot";
        wotm::Snapshot sn = S.scene.takeSnapshot(ram, tNow, gNow);
        if (!sn.cam.ok)
            return false;
        S.A = S.B.valid ? std::move(S.B) : sn;
        S.B = std::move(sn);
    }
    if (!S.B.valid)
        return false;
    if (IsKeyPressed(KEY_F4))
        S.interp = !S.interp;
    float alpha = 1.f;
    {
        const double span = S.B.t - S.A.t;   // duracao do quadro anterior do jogo; o atual deve durar parecido
        if (S.interp && span > 0.004 && span < 0.25)
            alpha = static_cast<float>((tNow - S.B.t) / span);
    }
    const wotm::GameCamera gc = S.scene.blend(S.A, S.B, alpha);
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
    wotm_live::g_stage = "draw";
    S.scene.draw(ram, false);
    wotm_live::g_stage = "idle";
    EndMode3D();
    EndTextureMode();

    // letterbox 4:3 na janela
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    const float k = std::min(sw / RW, sh / RH);
    const Rectangle src{0.f, 0.f, static_cast<float>(RW), -static_cast<float>(RH)};
    const Rectangle dst{(sw - RW * k) * 0.5f, (sh - RH * k) * 0.5f, RW * k, RH * k};
    DrawTexturePro(S.rt.texture, src, dst, Vector2{0.f, 0.f}, 0.f, WHITE);

    {   // contador de FPS (F3 liga/desliga): "jogo" = quadros do jogo por segundo (g_frame, 0x6F7E38); "tela" = desenhos nativos por segundo
        static bool show = true;
        static uint32_t lastG = 0;
        static double lastT = 0.0;
        static int drawn = 0;
        static float gameFps = 0.f, screenFps = 0.f;
        if (IsKeyPressed(KEY_F3))
            show = !show;
        ++drawn;
        const double now = GetTime();
        static const bool camLog = std::getenv("WOTM_CAMLOG") != nullptr;   // diagnostico: serie temporal da camera lida (um [cam] por desenho)
        if (camLog)
            std::fprintf(stderr, "[cam] t=%.4f g=%u fov=%.2f pos=%.2f,%.2f,%.2f fwd=%.4f,%.4f,%.4f\n", now, ram.u32(0x6F7E38), gc.fovy, gc.pos[0],
                         gc.pos[1], gc.pos[2], gc.fwd[0], gc.fwd[1], gc.fwd[2]);
        if (lastT == 0.0)
        {
            lastT = now;
            lastG = ram.u32(0x6F7E38);
        }
        else if (now - lastT >= 0.5)
        {
            const uint32_t g = ram.u32(0x6F7E38);
            gameFps = static_cast<float>(g - lastG) / static_cast<float>(now - lastT);
            screenFps = static_cast<float>(drawn) / static_cast<float>(now - lastT);
            lastG = g;
            drawn = 0;
            lastT = now;
        }
        if (show)
        {
            char line[128];
            std::snprintf(line, sizeof line, "jogo %.1f fps | tela %.1f fps | fov %.1f | interp %s (F4)", gameFps, screenFps, gc.fovy, S.interp ? "ligada" : "desligada");
            DrawText(line, 11, 11, 20, BLACK);
            DrawText(line, 10, 10, 20, gameFps >= 55.f ? GREEN : (gameFps >= 30.f ? YELLOW : RED));
        }
    }

    if (++S.frame % 300 == 0 && std::getenv("PS2X_STATS"))
        std::fprintf(stderr, "[native] frame %ld itens=%zu ceu=%zu nos=%u rtx=%s\n", S.frame, S.scene.items.size(), S.scene.sky.size(),
                     S.scene.visited, S.rtxPath.c_str());
    return true;
}
#else
static bool wotmDrawNative(const uint8_t *) { return false; }
#endif
