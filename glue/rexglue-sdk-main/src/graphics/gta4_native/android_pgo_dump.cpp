// Profile-guided optimization for the gta4-native renderer, recording build only.
// Android never runs atexit for an app process, so the counters are written
// from a background thread every minute; each write replaces the file with
// the cumulative counters of this process. Pull the .profraw files from the
// app's files/pgo directory and merge them with llvm-profdata.
#if defined(LIBERTY_PGO_GENERATE)

#include <android/log.h>
#include <sys/stat.h>

#include <chrono>
#include <thread>

extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char*);

namespace {

struct PgoDumper {
  PgoDumper() {
    constexpr const char* kDirectory = "/data/data/com.libertyrecomp/files/pgo";
    mkdir(kDirectory, 0700);
    __llvm_profile_set_filename("/data/data/com.libertyrecomp/files/pgo/rexgpu-%p.profraw");
    std::thread([] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        const int result = __llvm_profile_write_file();
        __android_log_print(ANDROID_LOG_WARN, "LibertyRecomp", "pgo: profile written (%d)", result);
      }
    }).detach();
  }
};

PgoDumper pgo_dumper;

}  // namespace

#endif
