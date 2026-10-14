#include "voice/gta4_player_position_hook.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>

#include "voice/android_voice_jni_bridge.h"
#include "voice/gta4_proximity_engine.h"

namespace gta4::voice {

namespace {
// Direcciones en memoria guest (Xbox 360 Big-Endian) de GTA IV verificadas en LibertyRecomp.
constexpr uint32_t kLocalPlayerSlotAddr = 0x82A98778u;
constexpr uint32_t kPlayerInfoTableAddr = 0x82C01C70u;
constexpr uint32_t kMaxGamePlayers = 32u;

// Vtables de vehículos para distinguir cabinas abiertas (motos/botes) de cerradas (autos/helicópteros).
constexpr uint32_t kVtableAutomobile = 0x82041D8Cu;
constexpr uint32_t kVtableHelicopter = 0x8200B8D4u;

/**
 * Valida que una dirección virtual pertenezca al rango de memoria guest de GTA IV.
 *
 * - Qué hace: Comprueba que el puntero no sea nulo y esté dentro del espacio `0x00010000..0xFEFFFFFF`.
 * - Cómo funciona: Verificación aritmética en registro antes de desreferenciar `base + addr`.
 * - Por qué: Previene fallos de segmentación (`SIGSEGV`) durante pantallas de carga o transiciones.
 */
inline bool IsValidGuestPtr(uint32_t addr) noexcept {
  return addr >= 0x00010000u && addr <= 0xFEFFFFFFu;
}

/**
 * Lee un entero de 32 bits Big-Endian desde la memoria guest de GTA IV.
 *
 * - Qué hace: Carga 4 bytes en `base + addr` e invierte el orden de bytes a Little-Endian (ARM64).
 * - Cómo funciona: Usa `std::memcpy` alineado y `__builtin_bswap32`.
 * - Por qué: El código original de Xbox 360 almacena todos los punteros y enteros en Big-Endian.
 */
inline uint32_t LoadGuestU32(const uint8_t* base, uint32_t addr) noexcept {
  if (!base || !IsValidGuestPtr(addr)) return 0u;
  uint32_t raw = 0u;
  std::memcpy(&raw, base + addr, sizeof(raw));
  return __builtin_bswap32(raw);
}

/**
 * Lee un número en coma flotante IEEE-754 de 32 bits desde la memoria guest.
 *
 * - Qué hace: Convierte un `uint32_t` Big-Endian en `float` nativo y verifica que sea finito.
 * - Cómo funciona: Usa `std::bit_cast<float>` sobre el entero ya invertido.
 * - Por qué: Las coordenadas (x, y, z) y la matriz de transformación están en formato float de 32 bits.
 */
inline float LoadGuestF32(const uint8_t* base, uint32_t addr) noexcept {
  const float val = std::bit_cast<float>(LoadGuestU32(base, addr));
  return std::isfinite(val) ? val : 0.0f;
}
}  // namespace

bool ReadPlayerSpatialStateFromGuest(const uint8_t* base, uint32_t slotIndex,
                                     GTAPlayerState* outState) noexcept {
  if (!base || !outState || slotIndex >= kMaxGamePlayers) return false;

  const uint32_t playerInfoPtr = LoadGuestU32(base, kPlayerInfoTableAddr + slotIndex * 4u);
  if (!IsValidGuestPtr(playerInfoPtr)) return false;

  const uint32_t pedPtr = LoadGuestU32(base, playerInfoPtr + 1400u);
  if (!IsValidGuestPtr(pedPtr)) return false;

  const uint32_t matrixPtr = LoadGuestU32(base, pedPtr + 32u);
  if (!IsValidGuestPtr(matrixPtr)) return false;

  const float fx = LoadGuestF32(base, matrixPtr + 16u);
  const float fy = LoadGuestF32(base, matrixPtr + 20u);
  const float px = LoadGuestF32(base, matrixPtr + 48u);
  const float py = LoadGuestF32(base, matrixPtr + 52u);
  const float pz = LoadGuestF32(base, matrixPtr + 56u);

  // Determina si el jugador está dentro de un vehículo cerrado para aplicar oclusión acústica.
  int vehicleId = 0;
  const uint32_t pedFlags = LoadGuestU32(base, pedPtr + 572u);
  if ((pedFlags & 0x20000000u) != 0u) {
    const uint32_t vehiclePtr = LoadGuestU32(base, pedPtr + 2688u);
    if (IsValidGuestPtr(vehiclePtr)) {
      const uint32_t vtable = LoadGuestU32(base, vehiclePtr);
      if (vtable == kVtableAutomobile || vtable == kVtableHelicopter) {
        vehicleId = static_cast<int>(vehiclePtr & 0x7FFFFFFFu);
      }
    }
  }

  outState->playerId = slotIndex;
  outState->x = px;
  outState->y = py;
  outState->z = pz;
  outState->heading = std::atan2(fx, fy);
  outState->interior = 0;
  outState->vehicleId = vehicleId;
  return true;
}

void PollAndSyncGta4PlayerSpatialStates(const uint8_t* base) noexcept {
  if (!base) return;
  using Clock = std::chrono::steady_clock;
  static auto sLastPoll = Clock::now();
  const auto now = Clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - sLastPoll).count() < 50) {
    return;
  }
  sLastPoll = now;

  const uint32_t localSlot = LoadGuestU32(base, kLocalPlayerSlotAddr);
  if (localSlot >= kMaxGamePlayers) return;

  GTAPlayerState localState{};
  if (!ReadPlayerSpatialStateFromGuest(base, localSlot, &localState)) return;

  auto& engine = Gta4ProximityEngine::Instance();
  engine.updatePlayer(localState.playerId, localState.x, localState.y, localState.z);
  engine.updatePlayerState(localState);

  for (uint32_t slot = 0; slot < kMaxGamePlayers; ++slot) {
    if (slot == localSlot) continue;
    GTAPlayerState remoteState{};
    if (!ReadPlayerSpatialStateFromGuest(base, slot, &remoteState)) continue;

    engine.updatePlayer(remoteState.playerId, remoteState.x, remoteState.y, remoteState.z);
    engine.updatePlayerState(remoteState);
    const ProximityVoiceEval eval = engine.evaluatePair(localState.playerId, remoteState.playerId);
    AndroidDispatchPeerSpatialUpdate(remoteState, eval.volume, eval.pan);
  }
}

}  // namespace gta4::voice
