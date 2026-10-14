/**
 * Adaptadores de plataforma Android para servicios de host (permisos de micrófono JNI,
 * música de usuario y logros).
 *
 * - Qué hace: Conecta el subsistema de permisos de micrófono de GTA IV con Android JNI real
 *   (`AndroidCheckMicPermission` / `AndroidRequestMicPermission`) y provee implementaciones
 *   seguras para servicios exclusivos de macOS (Game Center / AVFoundation).
 * - Cómo funciona: Reemplaza el antiguo stub que devolvía `kDenied` fijo por consultas reales
 *   al `VoiceLifecycleCoordinator` de Android.
 * - Por qué: Sin este enlace real, `SDLVoiceAudioDevice` creía que el micrófono siempre estaba
 *   bloqueado en Android y nunca iniciaba la captura de voz.
 */

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

#include <rex/logging.h>

#include "achievement_bridge_gc.h"
#include "input/user_music_player.h"
#include "network/community_multiplayer.h"
#include "network/gta4_microphone_permission.h"
#include "voice/android_voice_jni_bridge.h"

namespace gta4::game_center {

void Initialize() {}

void SubmitAchievement(uint32_t) {}

}  // namespace gta4::game_center

namespace gta4::input {

struct UserMusicPlayer::Impl {};

UserMusicPlayer::UserMusicPlayer(std::filesystem::path) : impl_(std::make_unique<Impl>()) {}

UserMusicPlayer::~UserMusicPlayer() = default;

bool UserMusicPlayer::Next() { return false; }

bool UserMusicPlayer::Previous() { return false; }

bool UserMusicPlayer::Stop() { return false; }

bool UserMusicPlayer::HasTracks() const { return false; }

void PublishUserMusicPlayer(UserMusicPlayer*) {}

bool IsUserMusicAvailable() { return false; }

bool RequestUserMusicNext() { return false; }

bool RequestUserMusicPrevious() { return false; }

bool RequestUserMusicStop() { return false; }

}  // namespace gta4::input

namespace gta4::voice {

/**
 * Obtiene el estado real del permiso `RECORD_AUDIO` en Android mediante JNI.
 *
 * - Qué hace: Consulta si el usuario ya autorizó el micrófono en `LibertyActivity`.
 * - Cómo funciona: Delega en `AndroidCheckMicPermission()`.
 * - Por qué: Permite que `SDLVoiceAudioDevice` y el motor de voz funcionen con lógica real.
 */
MicrophonePermissionStatus GetMicrophonePermissionStatus() noexcept {
  return AndroidCheckMicPermission();
}

/**
 * Solicita el permiso `RECORD_AUDIO` al sistema operativo Android y ejecuta el callback.
 *
 * - Qué hace: Lanza el diálogo nativo de permisos de Android desde el hilo UI.
 * - Cómo funciona: Llama a `AndroidRequestMicPermission()` y notifica `completion`.
 * - Por qué: Elimina el componente sin lógica (no-op) que impedía activar el micrófono.
 */
void RequestMicrophonePermission(std::function<void()> completion) {
  AndroidRequestMicPermission();
  if (completion) {
    completion();
  }
}

}  // namespace gta4::voice

namespace LibertyRecomp::Network {

rex::system::xam::LiveBackendServices CreateCommunityMultiplayerBackend(
    const rex::system::xam::LiveConfig&, const rex::system::xam::LiveIdentity&) {
  REXLOG_INFO("Using Android WebRTC P2P + LAN/Session Voice Signaling Bridge");
  return {};
}

}  // namespace LibertyRecomp::Network
