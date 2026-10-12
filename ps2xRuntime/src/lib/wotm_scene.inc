// wotm_scene.inc (incluido por ps2_runtime.cpp; raylib e ps2_runtime.h ja estao incluidos)
#include "wotm_live.inc"

static void wotmDrawScene(const uint8_t *rdram)
{
    if (!rdram) return;
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
    wotmDrawNative(rdram); // camada nativa (native/wotm_native.hpp): fase lida da RAM, sem VU1/GS; sem fase, o quadro do GS fica como esta
}
