/**
 * Hook de sincronización de fotogramas y muestreo espacial 3D del jugador en GTA IV.
 *
 * - Qué hace: Intercepta `sub_82A46D70` en el bucle principal del juego para:
 *   1) Evitar esperas activas (busy-wait) innecesarias cuando la GPU tiene fotogramas pendientes.
 *   2) Alimentar el motor de voz por proximidad (`PollAndSyncGta4PlayerSpatialStates`) con el
 *      puntero base real de la memoria guest (`base`).
 * - Cómo funciona: Se ejecuta en cada paso del bucle principal, pero el muestreo de posiciones
 *   XYZ tiene un limitador interno de 50 ms (20 Hz) para no afectar los FPS.
 * - Por qué: Aprovecha el punto exacto donde el hilo principal del juego tiene acceso seguro
 *   a `base` sin crear hilos adicionales ni condiciones de carrera.
 */
#include <time.h>

#include <rex/cvar.h>

#include "gta4_init.h"
#include "voice/gta4_player_position_hook.h"

REXCVAR_DEFINE_BOOL(gta4_frame_wait_sleep, false, "GTA IV/Performance",
                    "Sleep instead of spinning while the title waits for a frame slot");
REXCVAR_DEFINE_UINT32(gta4_extra_frames_in_flight, 0, "GTA IV/Performance",
                      "Frames the title may run ahead beyond its own limit of two pending "
                      "frames (more throughput, one frame of input latency each)")
    .range(0, 2);

/**
 * Hook del motor sobre `sub_82A46D70` que sincroniza fotogramas y coordenadas 3D de voz.
 *
 * - Qué hace: Ejecuta la función original, actualiza las posiciones XYZ de los jugadores para
 *   el chat de proximidad y regula la cola de fotogramas en vuelo.
 * - Cómo funciona: Recibe `PPCContext& ctx` y `uint8_t* base` directamente del runtime RexGlue.
 * - Por qué: Conecta la simulación física de GTA IV con `Gta4ProximityEngine` y WebRTC Android.
 */
extern "C" void sub_82A46D70(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r4.u32;
  __imp__sub_82A46D70(ctx, base);

  // Actualiza playerId + XYZ -> Gta4ProximityEngine -> WebRTC Android (regulado a 20 Hz).
  gta4::voice::PollAndSyncGta4PlayerSpatialStates(base);

  uint32_t pending = REX_LOAD_U32(out + 8);
  if (pending >= 0x80000000u) return;
  const uint32_t extra = REXCVAR_GET(gta4_extra_frames_in_flight);
  if (extra) {
    pending = pending > extra ? pending - extra : 0;
    REX_STORE_U32(out + 8, pending);
  }
  if (pending >= 2 && REXCVAR_GET(gta4_frame_wait_sleep)) {
    const timespec delay{0, 250 * 1000};
    nanosleep(&delay, nullptr);
  }
}
