#pragma once

#include <cstdint>

#include "voice/gta4_player_state.h"

namespace gta4::voice {

/**
 * Extrae el estado espacial 3D (`GTAPlayerState`) de un jugador desde la memoria guest de GTA IV.
 *
 * - Qué hace: Lee las estructuras `CPlayerInfo`, `CPed` y `CMatrix` de la memoria Big-Endian Xbox 360.
 * - Cómo funciona: Aplica los desplazamientos reales verificados del motor:
 *   `kPlayer = 0x82A98778`, `kPlayerInfo = 0x82C01C70`, `CPed = +1400`, `CMatrix = +32`,
 *   vector forward `(+16, +20)` y posición 3D `(+48, +52, +56)`.
 * - Por qué: Conecta las coordenadas reales de GTA IV con el motor de proximidad sin simulaciones.
 */
bool ReadPlayerSpatialStateFromGuest(const uint8_t* base, uint32_t slotIndex,
                                     GTAPlayerState* outState) noexcept;

/**
 * Muestrea periódicamente (20 Hz) las coordenadas de todos los jugadores activos en la partida.
 *
 * - Qué hace: Obtiene la posición del jugador local y de los demás jugadores, actualiza
 *   `Gta4ProximityEngine` (`updatePlayer`) y envía la telemetría espacial a WebRTC Android vía JNI.
 * - Cómo funciona: Se invoca desde el bucle del juego pero limita su ejecución a cada 50 ms
 *   mediante un reloj monotónico para no introducir ningún cuello de botella en los FPS.
 * - Por qué: Garantiza actualización fluida de volumen/paneo 3D con impacto cero en el rendimiento.
 */
void PollAndSyncGta4PlayerSpatialStates(const uint8_t* base) noexcept;

}  // namespace gta4::voice
