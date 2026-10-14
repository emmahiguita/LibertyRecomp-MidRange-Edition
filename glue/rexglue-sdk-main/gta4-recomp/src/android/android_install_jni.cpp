// The game installer for the Android launcher (com.libertyrecomp.Installer).
//
// libliberty_install.so carries the same source inspection and installation
// code the in-game ImGui installer runs (src/install), without the rest of the
// title, so the launcher can check sources and install the game before the
// game process ever starts. One installation at a time; its progress lives in
// process-wide atomics the launcher polls.
#include <jni.h>

#include <android/log.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#include "install/gta4_installer.h"
#include "install/gta4_source_inspector.h"

namespace {

constexpr const char* kTag = "LibertyInstall";

std::mutex g_install_mutex;
gta4::install::Progress g_progress;
std::atomic<bool> g_running{false};

std::string FromJava(JNIEnv* env, jstring value) {
  if (!value) return {};
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (!chars) return {};
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

jstring ToJava(JNIEnv* env, const std::string& value) { return env->NewStringUTF(value.c_str()); }

std::string JsonEscape(const std::string& value) {
  std::string out;
  for (unsigned char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out += hex[c >> 4];
          out += hex[c & 15];
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

}  // namespace

extern "C" {

// {"supported":bool,"summary":"...","diagnostics":"..."} for a base-game source.
JNIEXPORT jstring JNICALL Java_com_libertyrecomp_Installer_nativeInspect(JNIEnv* env, jclass,
                                                                         jstring path_value) {
  const std::string path = FromJava(env, path_value);
  const auto inspection = gta4::install::InspectGameSource(std::filesystem::path(path));
  const std::string json =
      std::string("{\"supported\":") + (inspection.supported() ? "true" : "false") +
      ",\"summary\":\"" + JsonEscape(gta4::install::FormatGameSourceInspection(inspection)) +
      "\",\"diagnostics\":\"" + JsonEscape(gta4::install::FormatGameSourceDiagnostics(inspection)) +
      "\"}";
  return ToJava(env, json);
}

// Empty when the game under game_root can start, otherwise the reason.
JNIEXPORT jstring JNICALL Java_com_libertyrecomp_Installer_nativeInstallState(JNIEnv* env, jclass,
                                                                              jstring root_value) {
  std::string reason;
  const bool ready =
      gta4::install::IsInstallReady(std::filesystem::path(FromJava(env, root_value)), &reason);
  return ToJava(env, ready ? std::string() : (reason.empty() ? "not installed" : reason));
}

// Installs into install_root (the LibertyRecomp folder; the game lands in
// install_root/game). Blocks; empty result on success, the error otherwise.
JNIEXPORT jstring JNICALL Java_com_libertyrecomp_Installer_nativeInstall(
    JNIEnv* env, jclass, jstring game_value, jstring update_value, jstring tlad_value,
    jstring tbogt_value, jstring root_value) {
  std::unique_lock lock(g_install_mutex, std::try_to_lock);
  if (!lock.owns_lock()) return ToJava(env, "An installation is already running.");
  gta4::install::Selection selection;
  selection.game_source = FromJava(env, game_value);
  selection.update_source = FromJava(env, update_value);
  const std::string tlad = FromJava(env, tlad_value);
  const std::string tbogt = FromJava(env, tbogt_value);
  if (!tlad.empty()) selection.dlc_sources.push_back({gta4::install::Episode::kTlad, tlad});
  if (!tbogt.empty()) selection.dlc_sources.push_back({gta4::install::Episode::kTbogt, tbogt});
  const std::filesystem::path root(FromJava(env, root_value));

  g_progress.copied_bytes = 0;
  g_progress.total_bytes = 0;
  g_progress.cancel_requested = false;
  g_running = true;
  __android_log_print(ANDROID_LOG_INFO, kTag, "install: game=%s update=%s root=%s",
                      selection.game_source.c_str(), selection.update_source.c_str(),
                      root.c_str());
  const auto result = gta4::install::Install(selection, root, g_progress);
  g_running = false;
  if (result.success) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "install: done");
    return ToJava(env, "");
  }
  __android_log_print(ANDROID_LOG_ERROR, kTag, "install: failed: %s", result.error.c_str());
  return ToJava(env, result.error.empty() ? "The installation failed." : result.error);
}

// {copied, total} bytes of the running installation.
JNIEXPORT jlongArray JNICALL Java_com_libertyrecomp_Installer_nativeProgress(JNIEnv* env, jclass) {
  jlongArray out = env->NewLongArray(2);
  if (!out) return nullptr;
  const jlong values[2] = {
      static_cast<jlong>(g_progress.copied_bytes.load(std::memory_order_relaxed)),
      static_cast<jlong>(g_progress.total_bytes.load(std::memory_order_relaxed))};
  env->SetLongArrayRegion(out, 0, 2, values);
  return out;
}

JNIEXPORT void JNICALL Java_com_libertyrecomp_Installer_nativeCancel(JNIEnv*, jclass) {
  g_progress.cancel_requested = true;
}

}  // extern "C"
