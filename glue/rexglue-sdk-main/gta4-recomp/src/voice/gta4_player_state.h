#pragma once

#include <cstdint>

namespace gta4::voice {

/**
 * Canales de comunicación de voz dentro de GTA IV.
 *
 * - Qué hace: Clasifica los flujos de voz entre proximidad 3D, grupo (Party),
 *   llamada telefónica directa y radio táctica.
 * - Cómo funciona: Determina si el motor acústico aplica atenuación espacial por distancia
 *   o ganancia constante con ducking sobre el resto de canales.
 * - Por qué: Separa la política del canal respecto al códec Opus y transporte WebRTC (SOLID).
 */
enum class VoiceChannel : uint8_t {
  VOICE_PROXIMITY = 0,
  VOICE_PARTY = 1,
  VOICE_PHONE = 2,
  VOICE_RADIO = 3,
};

/**
 * Estado espacial y físico de un jugador en GTA IV.
 *
 * - Qué hace: Almacena el identificador del jugador, sus coordenadas 3D (x, y, z),
 *   el ángulo de mirada (heading), el interior actual y el identificador de vehículo.
 * - Cómo funciona: Se rellena directamente desde los hooks de memoria de LibertyRecomp
 *   y alimenta el motor de proximidad (`updatePlayer`).
 * - Por qué: Estructura limpia de dominio compartida entre los hooks del juego y el motor de voz.
 */
struct GTAPlayerState {
  uint32_t playerId = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float heading = 0.0f;
  int interior = 0;
  int vehicleId = 0;
};

/**
 * Resultado acústico calculado entre el jugador oyente y un jugador hablante.
 *
 * - Qué hace: Contiene la distancia euclidiana en metros, el volumen [0.0..1.0],
 *   el balance estéreo [-1.0..1.0] y si el jugador es audible.
 * - Cómo funciona: Es devuelto por `Gta4ProximityEngine::evaluatePair` en cada actualización.
 * - Por qué: Permite aplicar tanto mezcla PCM nativa como control de ganancia en WebRTC Android.
 */
struct ProximityVoiceEval {
  float distanceMeters = 0.0f;
  float volume = 0.0f;
  float pan = 0.0f;
  bool audible = false;
};

}  // namespace gta4::voice
