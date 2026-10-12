// Correcoes especificas de War of the Monsters (SCUS_971.97) aplicadas por substituicao de funcoes do codigo recompilado.
//
// Pelo do Congar (e de outros monstros): `BoneSpring::update` (0x11B5C0) integra uma mola por Euler com
// `dt = 1 / timerGetUpdateRate()` (D_006F8DB8). No retail o updateRate e 60 (ou 30); neste runtime o jogo roda a ~22 fps
// logicos e o valor chega a 8-15 quando o host atrasa, ou seja, `dt` ate 7x maior que o previsto. Com esse dt a mola e
// instavel: o deslocamento cresce a cada quadro ate explodir (vertices do pelo esticados por toda a cena).
// A correcao limita so o dt da mola a no maximo 1/30 s. Nada muda quando o updateRate ja e >= 30 (retail).
//
// Dentro de BoneSpring::update `timerGetUpdateRate` e chamada duas vezes: a primeira escala a velocidade da ancora
// (precisa do rate real) e a segunda define o dt (`1 / rate`): so a segunda e limitada.
//
// WOTM_SPRINGMIN=<n>: minimo do rate usado no dt (padrao 30; 0 desliga a correcao).
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    constexpr uint32_t kBoneSpringUpdate = 0x11B5C0u;
    constexpr uint32_t kTimerGetUpdateRate = 0x2236D8u;
    constexpr uint32_t kUpdateRateVar = 0x6F8DB8u;   // D_006F8DB8, lido pelo gp

    PS2Runtime::RecompiledFunction g_origBoneSpringUpdate = nullptr;
    int g_springMinRate = 30;
    thread_local int t_timerCallsInSpring = -1;   // -1: fora de BoneSpring::update
    unsigned long long g_clamped = 0;

    void boneSpringUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        t_timerCallsInSpring = 0;
        g_origBoneSpringUpdate(rdram, ctx, runtime);
        t_timerCallsInSpring = -1;
    }

    // Mesmo comportamento da original (`jr $ra; lw $v0, gp_rel(D_006F8DB8)`), exceto o limite do dt acima.
    void timerGetUpdateRate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int32_t rate = static_cast<int32_t>(READ32(kUpdateRateVar));
        if (t_timerCallsInSpring >= 0 && ++t_timerCallsInSpring == 2 && rate > 0 && rate < g_springMinRate)
        {
            rate = g_springMinRate;
            if ((++g_clamped % 20000ull) == 0 && std::getenv("PS2X_STATS"))
                std::fprintf(stderr, "[wotm] mola do pelo: dt limitado a 1/%d em %llu atualizacoes\n", g_springMinRate, g_clamped);
        }
        SET_GPR_S32(ctx, 2, rate);
        ctx->pc = GPR_U32(ctx, 31);
    }
}

void wotmInstallOverrides(PS2Runtime &runtime, const std::string &elfPath)
{
    const size_t slash = elfPath.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? elfPath : elfPath.substr(slash + 1);
    if (name.find("SCUS_971.97") == std::string::npos)
        return;

    if (const char *e = std::getenv("WOTM_SPRINGMIN"))
        g_springMinRate = std::atoi(e);
    if (g_springMinRate <= 0)
        return;

    g_origBoneSpringUpdate = runtime.lookupFunction(kBoneSpringUpdate);
    if (!g_origBoneSpringUpdate)
        return;
    runtime.registerFunction(kBoneSpringUpdate, &boneSpringUpdate);
    runtime.registerFunction(kTimerGetUpdateRate, &timerGetUpdateRate);
    std::fprintf(stderr, "[wotm] correcao da mola do pelo ativa (dt <= 1/%d)\n", g_springMinRate);
}
