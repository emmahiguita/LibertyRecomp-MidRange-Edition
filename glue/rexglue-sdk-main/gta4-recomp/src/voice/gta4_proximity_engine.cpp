#include "voice/gta4_proximity_engine.h"

#include <algorithm>
#include <cmath>

namespace gta4::voice {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kFullVolumeDist = 5.0f;
constexpr float kMidVolumeDist = 15.0f;
constexpr float kFarVolumeDist = 30.0f;
constexpr float kMaxAudibleDist = 50.0f;
constexpr float kVehicleOcclusionFactor = 0.45f;
constexpr float kDuckGain = 0.25f;

/**
 * Calcula la ganancia base [0.0..1.0] según la tabla de distancias de GTA IV:
 * <= 5m -> 100%, 15m -> 70%, 30m -> 25%, >= 50m -> 0%.
 *
 * - Qué hace: Interpola suavemente la caída de volumen en función de los metros de separación.
 * - Cómo funciona: Evalúa en qué tramo se encuentra `d` y calcula la proporción lineal exacta.
 * - Por qué: Garantiza que la transición acústica al alejarse sea predecible y natural.
 */
float ComputeDistanceFalloff(float d) noexcept {
  if (d <= kFullVolumeDist) return 1.0f;
  if (d >= kMaxAudibleDist) return 0.0f;
  if (d <= kMidVolumeDist) {
    // De 5 m (1.00) a 15 m (0.70)
    return 1.0f - ((d - kFullVolumeDist) / (kMidVolumeDist - kFullVolumeDist)) * 0.30f;
  }
  if (d <= kFarVolumeDist) {
    // De 15 m (0.70) a 30 m (0.25)
    return 0.70f - ((d - kMidVolumeDist) / (kFarVolumeDist - kMidVolumeDist)) * 0.45f;
  }
  // De 30 m (0.25) a 50 m (0.00)
  return 0.25f * (1.0f - ((d - kFarVolumeDist) / (kMaxAudibleDist - kFarVolumeDist)));
}
}  // namespace

Gta4ProximityEngine& Gta4ProximityEngine::Instance() {
  static Gta4ProximityEngine instance;
  return instance;
}

void Gta4ProximityEngine::updatePlayer(uint32_t playerId, float x, float y, float z) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = players_[playerId];
  state.playerId = playerId;
  state.x = x;
  state.y = y;
  state.z = z;
}

void Gta4ProximityEngine::updatePlayerState(const GTAPlayerState& state) {
  std::lock_guard<std::mutex> lock(mutex_);
  players_[state.playerId] = state;
}

void Gta4ProximityEngine::removePlayer(uint32_t playerId) {
  std::lock_guard<std::mutex> lock(mutex_);
  players_.erase(playerId);
}

ProximityVoiceEval Gta4ProximityEngine::evaluatePair(uint32_t listenerId,
                                                     uint32_t speakerId) const {
  ProximityVoiceEval result{};
  GTAPlayerState listener{};
  GTAPlayerState speaker{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto itL = players_.find(listenerId);
    auto itS = players_.find(speakerId);
    if (itL == players_.end() || itS == players_.end()) return result;
    listener = itL->second;
    speaker = itS->second;
  }

  // Compuerta de interior: si están en interiores distintos, no se filtran voces a través de paredes.
  if (listener.interior != speaker.interior) {
    return result;
  }

  const float dx = speaker.x - listener.x;
  const float dy = speaker.y - listener.y;
  const float dz = speaker.z - listener.z;
  const float distSq = dx * dx + dy * dy + dz * dz;
  if (distSq >= kMaxAudibleDist * kMaxAudibleDist) {
    result.distanceMeters = std::sqrt(distSq);
    return result;
  }

  const float dist = std::sqrt(distSq);
  result.distanceMeters = dist;
  float gain = ComputeDistanceFalloff(dist);

  // Oclusión de vehículo (adaptado de omp-voice): atenúa si uno está en cabina cerrada distinta.
  if (listener.vehicleId != speaker.vehicleId &&
      (listener.vehicleId != 0 || speaker.vehicleId != 0)) {
    gain *= kVehicleOcclusionFactor;
  }

  if (gain <= 0.005f) return result;

  // Cálculo de paneo estéreo [-1..+1] usando el vector derecho del oyente (heading en radianes).
  float pan = 0.0f;
  const float horizLen = std::sqrt(dx * dx + dy * dy);
  if (horizLen > 0.001f) {
    const float invH = 1.0f / horizLen;
    const float rightX = std::cos(listener.heading);
    const float rightY = -std::sin(listener.heading);
    pan = std::clamp((dx * invH) * rightX + (dy * invH) * rightY, -1.0f, 1.0f);
  }

  result.volume = std::clamp(gain, 0.0f, 1.0f);
  result.pan = pan;
  result.audible = true;
  return result;
}

void Gta4ProximityEngine::applySpatialPcm16(const int16_t* monoIn, int16_t* stereoOut,
                                            int frameCount, float volume, float pan,
                                            bool duckActive) {
  if (!monoIn || !stereoOut || frameCount <= 0) return;
  const float effectiveGain = std::clamp(volume, 0.0f, 1.0f) * (duckActive ? kDuckGain : 1.0f);
  const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
  const float leftGain = std::cos(angle) * effectiveGain;
  const float rightGain = std::sin(angle) * effectiveGain;

  for (int i = 0; i < frameCount; ++i) {
    const float normalized = static_cast<float>(monoIn[i]) / 32768.0f;
    const float l = std::tanh(normalized * leftGain);
    const float r = std::tanh(normalized * rightGain);
    stereoOut[i * 2] = static_cast<int16_t>(std::clamp(l * 32767.0f, -32768.0f, 32767.0f));
    stereoOut[i * 2 + 1] = static_cast<int16_t>(std::clamp(r * 32767.0f, -32768.0f, 32767.0f));
  }
}

}  // namespace gta4::voice
