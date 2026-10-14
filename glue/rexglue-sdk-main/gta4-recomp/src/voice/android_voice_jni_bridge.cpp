#include "voice/android_voice_jni_bridge.h"

#include <SDL3/SDL_system.h>
#include <jni.h>

namespace gta4::voice {

namespace {
/**
 * Obtiene el puntero `JNIEnv*` adjunto al hilo actual a través del runtime Android de SDL3.
 *
 * - Qué hace: Recupera de forma segura el entorno JNI sin bloquear ni crear hilos adicionales.
 * - Cómo funciona: Llama a `SDL_GetAndroidJNIEnv()` y limpia cualquier excepción pendiente.
 * - Por qué: Permite invocar métodos Kotlin desde hilos nativos del motor de GTA IV.
 */
JNIEnv* AcquireJniEnv() noexcept {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env && env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  return env;
}
}  // namespace

MicrophonePermissionStatus AndroidCheckMicPermission() noexcept {
  JNIEnv* env = AcquireJniEnv();
  if (!env) return MicrophonePermissionStatus::kDenied;

  jclass cls = env->FindClass("com/libertyrecomp/voice/VoiceLifecycleCoordinator");
  if (!cls) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return MicrophonePermissionStatus::kDenied;
  }

  jmethodID mid = env->GetStaticMethodID(cls, "checkMicPermissionFromJni", "()I");
  jint granted = 0;
  if (mid) {
    granted = env->CallStaticIntMethod(cls, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
  return granted == 1 ? MicrophonePermissionStatus::kAuthorized
                      : MicrophonePermissionStatus::kDenied;
}

void AndroidRequestMicPermission() noexcept {
  JNIEnv* env = AcquireJniEnv();
  if (!env) return;

  jclass cls = env->FindClass("com/libertyrecomp/voice/VoiceLifecycleCoordinator");
  if (!cls) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return;
  }

  jmethodID mid = env->GetStaticMethodID(cls, "requestMicPermissionFromJni", "()V");
  if (mid) {
    env->CallStaticVoidMethod(cls, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
}

void AndroidDispatchPeerSpatialUpdate(const GTAPlayerState& state, float volume,
                                      float pan) noexcept {
  JNIEnv* env = AcquireJniEnv();
  if (!env) return;

  jclass cls = env->FindClass("com/libertyrecomp/voice/VoiceSignalingBridge");
  if (!cls) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return;
  }

  jmethodID mid = env->GetStaticMethodID(cls, "onNativePeerSpatialUpdate", "(IFFFFIIFF)V");
  if (mid) {
    env->CallStaticVoidMethod(cls, mid, static_cast<jint>(state.playerId),
                              static_cast<jfloat>(state.x), static_cast<jfloat>(state.y),
                              static_cast<jfloat>(state.z), static_cast<jfloat>(state.heading),
                              static_cast<jint>(state.interior),
                              static_cast<jint>(state.vehicleId), static_cast<jfloat>(volume),
                              static_cast<jfloat>(pan));
    if (env->ExceptionCheck()) env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
}

extern "C" JNIEXPORT void JNICALL
Java_com_libertyrecomp_voice_VoiceSignalingBridge_nativeOnOutgoingSignal(JNIEnv* env, jclass,
                                                                         jstring jsonPayload) {
  if (!env || !jsonPayload) return;
  if (env->ExceptionCheck()) env->ExceptionClear();
}

}  // namespace gta4::voice
