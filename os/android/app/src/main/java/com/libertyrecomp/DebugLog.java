package com.libertyrecomp;

import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

/**
 * Componente: DebugLog
 * Qué hace: Graba el registro del sistema (logcat) del proceso del juego en un archivo local.
 * Cómo funciona: Lanza un subproceso 'logcat' apuntando a files/debug_logs/debug-FECHA.log.
 * Por qué: Permite diagnosticar fallos de Vulkan, SDL3, audio WebRTC y JNI sin necesitar PC/ADB.
 * Prevención de procesos zombi: Guarda la referencia de 'sActiveProcess' para cerrarlo en onDestroy().
 */
final class DebugLog {

    private static final String TAG = "LibertyDebugLog";
    static final String DIRECTORY = "debug_logs";
    private static final int KEEP = 5;
    private static final String ROTATE_KB = "8192";

    // Referencia al proceso logcat activo para evitar que quede huérfano (zombi) al cerrar el juego.
    private static volatile Process sActiveProcess;

    private DebugLog() {}

    // Devuelve la carpeta donde se guardan los archivos de registro.
    static File directory(File dataRoot) {
        return new File(dataRoot, DIRECTORY);
    }

    // Inicia la grabación de logcat y limpia cualquier proceso anterior que haya quedado abierto.
    static synchronized File start(File dataRoot) {
        stopActive();
        stopLeftovers(dataRoot);
        File dir = directory(dataRoot);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            Log.e(TAG, "cannot create " + dir);
            return null;
        }
        prune(dir);
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        File file = new File(dir, "debug-" + stamp + ".log");
        try {
            sActiveProcess = new ProcessBuilder(
                    "logcat", "-v", "threadtime", "-b", "main,system,crash", "-T", "500",
                    "-f", file.getAbsolutePath(), "-r", ROTATE_KB, "-n", "1")
                    .redirectErrorStream(true)
                    .start();
            Log.i(TAG, "recording the game log to " + file);
            return file;
        } catch (IOException e) {
            Log.e(TAG, "logcat could not start", e);
            return null;
        }
    }

    // Detiene inmediatamente el proceso logcat activo cuando el ciclo de vida de la Activity termina.
    static synchronized void stopActive() {
        Process proc = sActiveProcess;
        sActiveProcess = null;
        if (proc != null) {
            try {
                proc.destroy();
                if (proc.isAlive()) {
                    proc.destroyForcibly();
                }
            } catch (RuntimeException ignored) {
            }
        }
    }

    // Busca y mata procesos logcat huérfanos en /proc que hayan sobrevivido a un cierre inesperado.
    static void stopLeftovers(File dataRoot) {
        stopActive();
        if (dataRoot == null) return;
        String marker = new File(dataRoot, DIRECTORY).getAbsolutePath();
        File[] processes = new File("/proc").listFiles();
        if (processes == null) return;
        for (File process : processes) {
            String name = process.getName();
            if (name.isEmpty() || !Character.isDigit(name.charAt(0))) continue;
            String commandLine = read(new File(process, "cmdline"));
            if (!commandLine.startsWith("logcat") || !commandLine.contains(marker)) continue;
            try {
                android.os.Process.sendSignal(Integer.parseInt(name), android.os.Process.SIGNAL_KILL);
                Log.i(TAG, "ended leftover log writer " + name);
            } catch (RuntimeException ignored) {
            }
        }
    }

    // Conserva únicamente los últimos 5 archivos de log para no llenar el almacenamiento del teléfono.
    private static void prune(File dir) {
        File[] files = dir.listFiles((d, n) -> n.startsWith("debug-"));
        if (files == null || files.length < KEEP) return;
        Arrays.sort(files);
        for (int i = 0; i <= files.length - KEEP; ++i) {
            //noinspection ResultOfMethodCallIgnored
            files[i].delete();
        }
    }

    // Lee la línea de comandos de un proceso desde /proc/<pid>/cmdline.
    private static String read(File file) {
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[4096];
            int length = in.read(buffer);
            if (length <= 0) return "";
            return new String(buffer, 0, length, StandardCharsets.UTF_8).replace('\0', ' ');
        } catch (IOException e) {
            return "";
        }
    }
}

