// ============================================================================
// Archivo: android_thread_pinning.cpp
// Propósito: Asignar los hilos más pesados del emulador (renderizado, audio,
//            lógica de GTA IV) a los núcleos grandes (Big Cores) del procesador
//            ARM en Android para evitar caídas de FPS (cuellos de botella).
// ============================================================================
#include <dirent.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_map>
#include <vector>

#include <android/log.h>
#include <rex/cvar.h>

// Variable de configuración (CVar) que define cuántos hilos fijar a núcleos rápidos (0 = desactivado).
REXCVAR_DEFINE_UINT32(gta4_thread_pinning, 0, "GTA IV/Performance",
                      "Give the N busiest threads a big core each and keep the others off them "
                      "(0 = leave scheduling to Android)")
    .range(0, 4);

namespace {

// Bandera atómica para detener el hilo limpiamente al cerrar el juego (evita procesos zombi).
std::atomic<bool> g_stop_pinning{false};

// Función: ThreadCpuTicks
// Qué hace: Lee el tiempo de CPU consumido por un hilo específico (utime + stime).
// Cómo funciona: Abre /proc/self/task/<tid>/stat y extrae los campos 14 y 15.
// Por qué: Permite identificar cuáles hilos están trabajando más en el último intervalo.
uint64_t ThreadCpuTicks(pid_t tid) {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
  FILE* file = std::fopen(path, "r");
  if (!file) return 0;
  char buffer[512];
  const size_t length = std::fread(buffer, 1, sizeof(buffer) - 1, file);
  std::fclose(file);
  buffer[length] = 0;
  const char* p = std::strrchr(buffer, ')');
  if (!p) return 0;
  unsigned long long utime = 0, stime = 0;
  int field = 2;
  for (const char* c = p + 1; *c && field < 15; ++c) {
    if (*c == ' ') {
      ++field;
      if (field == 14) utime = std::strtoull(c + 1, nullptr, 10);
      if (field == 15) stime = std::strtoull(c + 1, nullptr, 10);
    }
  }
  return utime + stime;
}

// Función: CoresByMaxFrequency
// Qué hace: Obtiene la lista de núcleos de CPU ordenados de mayor a menor frecuencia máxima.
// Cómo funciona: Consulta cpuinfo_max_freq de cada núcleo y ordena de forma descendente.
// Por qué: En arquitecturas big.LITTLE (Snapdragon), necesitamos saber cuáles son los Big Cores.
std::vector<int> CoresByMaxFrequency() {
  std::vector<std::pair<long, int>> cores;
  const long count = sysconf(_SC_NPROCESSORS_CONF);
  for (int cpu = 0; cpu < count && cpu < CPU_SETSIZE; ++cpu) {
    char path[96];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    long frequency = 0;
    if (FILE* file = std::fopen(path, "r")) {
      if (std::fscanf(file, "%ld", &frequency) != 1) frequency = 0;
      std::fclose(file);
    }
    cores.emplace_back(frequency, cpu);
  }
  std::stable_sort(cores.begin(), cores.end(),
                   [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<int> result;
  for (const auto& [frequency, cpu] : cores) result.push_back(cpu);
  return result;
}

// Función: SetAffinity
// Qué hace: Aplica la máscara de afinidad de CPU a un hilo mediante syscall del kernel Linux.
void SetAffinity(pid_t tid, const cpu_set_t& set) {
  syscall(__NR_sched_setaffinity, tid, sizeof(set), &set);
}

// Función: PinningLoop
// Qué hace: Monitorea cada 2 segundos los hilos activos y asigna los más pesados a núcleos rápidos.
// Optimización de cuello de botella: Si gta4_thread_pinning es 0 y no hay afinidad previa aplicada,
// salta inmediatamente sin abrir /proc/self/task (ahorra cientos de lecturas de archivos por minuto).
void PinningLoop() {
  const std::vector<int> cores = CoresByMaxFrequency();
  if (cores.size() < 6) return;
  std::unordered_map<pid_t, uint64_t> previous;
  uint32_t applied = 0;
  while (!g_stop_pinning.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    if (g_stop_pinning.load(std::memory_order_acquire)) break;

    const uint32_t pinned = std::min<uint32_t>(REXCVAR_GET(gta4_thread_pinning), 4);
    // Corrección de cuello de botella: No escanear /proc si la función está desactivada.
    if (!pinned && !applied) {
      previous.clear();
      continue;
    }

    std::vector<std::pair<uint64_t, pid_t>> usage;
    std::unordered_map<pid_t, uint64_t> current;
    if (DIR* directory = opendir("/proc/self/task")) {
      while (dirent* entry = readdir(directory)) {
        const pid_t tid = pid_t(std::atoi(entry->d_name));
        if (tid <= 0) continue;
        const uint64_t ticks = ThreadCpuTicks(tid);
        current[tid] = ticks;
        const auto old = previous.find(tid);
        usage.emplace_back(old != previous.end() && ticks >= old->second ? ticks - old->second : 0,
                           tid);
      }
      closedir(directory);
    }
    previous.swap(current);
    std::sort(usage.rbegin(), usage.rend());
    cpu_set_t rest;
    CPU_ZERO(&rest);
    for (size_t i = pinned; i < cores.size(); ++i) CPU_SET(cores[i], &rest);
    for (size_t i = 0; i < usage.size(); ++i) {
      cpu_set_t set;
      CPU_ZERO(&set);
      if (!pinned) {
        for (int cpu : cores) CPU_SET(cpu, &set);
      } else if (i < pinned) {
        CPU_SET(cores[i], &set);
      } else {
        set = rest;
      }
      SetAffinity(usage[i].second, set);
    }
    if (pinned != applied) {
      __android_log_print(ANDROID_LOG_WARN, "LibertyRecomp",
                          "thread-pinning: %u threads on cores %d,%d,%d",
                          pinned, cores[0], cores[1], cores[2]);
    }
    applied = pinned;
  }
}

const bool g_pinning_started = [] {
  std::thread(PinningLoop).detach();
  return true;
}();

}  // namespace

namespace gta4::android {
// Función llamada al cerrar la aplicación para detener el bucle de afinidad sin dejar hilos zombi.
void StopAndroidThreadPinning() noexcept {
  g_stop_pinning.store(true, std::memory_order_release);
}
}  // namespace gta4::android

