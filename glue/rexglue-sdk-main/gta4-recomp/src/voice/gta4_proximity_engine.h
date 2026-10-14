#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "voice/gta4_player_state.h"

namespace gta4::voice {

/**
 * Motor de voz por proximidad 3D adaptado de `omp-voice` (core/proximity, mixer, dsp).
 *
 * - Qué hace: Registra las coordenadas 3D de todos los jugadores activos y calcula
 *   la caída de volumen por distancia, el paneo estéreo según la rotación de la cámara/jugador
 *   y la oclusión acústica cuando un jugador está dentro de un vehículo cerrado.
 * - Cómo funciona: Implementa la curva exacta del proyecto:
 *   <= 5m -> 100%, 15m -> 70%, 30m -> 25%, >= 50m -> 0%, además de ducking para llamadas/radio.
 * - Por qué: Aplica el principio de Responsabilidad Única (SOLID), aislando la matemática
 *   espacial 3D de los hooks de memoria de GTA IV y de las llamadas JNI hacia Android.
 */
class Gta4ProximityEngine {
 public:
  /**
   * Devuelve la instancia única (Singleton thread-safe) del motor de proximidad.
   *
   * - Qué hace: Provee acceso global controlado desde los hooks del juego y el puente JNI.
   * - Cómo funciona: Utiliza inicialización estática local garantizada por C++11/23.
   * - Por qué: Permite que el hilo de simulación de GTA IV y el hilo de audio consulten el mismo estado.
   */
  static Gta4ProximityEngine& Instance();

  /**
   * Actualiza la posición 3D básica de un jugador (firma directa `updatePlayer(playerId, x, y, z)`).
   *
   * - Qué hace: Registra las coordenadas (x, y, z) del jugador indicado conservando sus metadatos previos.
   * - Cómo funciona: Bloquea el mutex interno brevemente y actualiza la tabla `players_`.
   * - Por qué: Cumple exactamente la interfaz `voice.updatePlayer(playerId, x, y, z)` de diseño.
   */
  void updatePlayer(uint32_t playerId, float x, float y, float z);

  /**
   * Actualiza el estado espacial completo de un jugador incluyendo rotación, interior y vehículo.
   *
   * - Qué hace: Guarda el `GTAPlayerState` completo extraído de la memoria de GTA IV.
   * - Cómo funciona: Inserta o reemplaza la entrada en `players_` de forma segura entre hilos.
   * - Por qué: Permite calcular paneo estéreo preciso y atenuación por cabina de vehículo.
   */
  void updatePlayerState(const GTAPlayerState& state);

  /**
   * Elimina a un jugador de la tabla espacial cuando se desconecta de la partida.
   *
   * - Qué hace: Borra la entrada asociada a `playerId`.
   * - Cómo funciona: Invoca `players_.erase(playerId)` bajo exclusión mutua.
   * - Por qué: Evita acumular jugadores fantasma en sesiones largas (limpieza de ciclo de vida).
   */
  void removePlayer(uint32_t playerId);

  /**
   * Calcula el volumen, distancia y paneo estéreo entre el oyente (`listenerId`) y el hablante (`speakerId`).
   *
   * - Qué hace: Evalúa compuerta de interior, distancia 3D, curva de atenuación, oclusión y paneo.
   * - Cómo funciona: Aplica la interpolación `5m=100% -> 15m=70% -> 30m=25% -> 50m=0%` y producto punto lateral.
   * - Por qué: Traduce coordenadas del mundo de Liberty City en parámetros auditivos reales.
   */
  ProximityVoiceEval evaluatePair(uint32_t listenerId, uint32_t speakerId) const;

  /**
   * Aplica ganancia, paneo estéreo (ley de potencia constante) y limitador suave (tanh) a un búfer PCM.
   *
   * - Qué hace: Procesa muestras de audio de 16 bits en el lugar hacia salida estéreo intercalada.
   * - Cómo funciona: Adapta el mezclador y `softClip` de `omp-voice` (`core/include/dsp.h` y `mixer.h`).
   * - Por qué: Evita saturación digital (clipping) cuando varios jugadores hablan cerca al mismo tiempo.
   */
  static void applySpatialPcm16(const int16_t* monoIn, int16_t* stereoOut, int frameCount,
                                float volume, float pan, bool duckActive);

 private:
  Gta4ProximityEngine() = default;
  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, GTAPlayerState> players_;
};

}  // namespace gta4::voice
