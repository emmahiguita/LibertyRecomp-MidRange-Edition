#pragma once

#include "network/gta4_microphone_permission.h"
#include "voice/gta4_player_state.h"

namespace gta4::voice {

/**
 * Consulta en tiempo real a Android (vía JNI) si el permiso `RECORD_AUDIO` fue otorgado.
 *
 * - Qué hace: Invoca `VoiceLifecycleCoordinator.checkMicPermissionFromJni()` en la capa Kotlin.
 * - Cómo funciona: Obtiene el `JNIEnv*` del hilo actual mediante SDL3 y llama al método estático.
 * - Por qué: Sustituye el stub que devolvía `kDenied` fijo en `android_platform_stubs.cpp`.
 */
MicrophonePermissionStatus AndroidCheckMicPermission() noexcept;

/**
 * Solicita al sistema Android mostrar el diálogo de permiso `RECORD_AUDIO` en tiempo de ejecución.
 *
 * - Qué hace: Invoca `VoiceLifecycleCoordinator.requestMicPermissionFromJni()` en Kotlin.
 * - Cómo funciona: Despacha la solicitud al hilo principal de `LibertyActivity`.
 * - Por qué: Permite que tanto el motor SDL3 como WebRTC obtengan acceso legítimo al micrófono.
 */
void AndroidRequestMicPermission() noexcept;

/**
 * Envía el estado espacial 3D y el volumen/paneo calculados en C++ hacia `VoiceSignalingBridge`.
 *
 * - Qué hace: Notifica a la capa WebRTC de Android la posición y ganancia de un jugador remoto.
 * - Cómo funciona: Invoca `VoiceSignalingBridge.onNativePeerSpatialUpdate(...)` por JNI.
 * - Por qué: Conecta el motor `omp-voice` C++ con los `AudioTrack` de WebRTC en Android.
 */
void AndroidDispatchPeerSpatialUpdate(const GTAPlayerState& state, float volume,
                                      float pan) noexcept;

}  // namespace gta4::voice
