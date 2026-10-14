package com.libertyrecomp;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.system.Os;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Sincronizador de recursos del APK y generador de reporte de diagnóstico de lanzamiento.
 *
 * - Qué hace: Copia la carpeta `Resources` del APK al almacenamiento interno cuando cambia la versión
 *   y escribe `last_launch.txt` con información del SoC, GPU Adreno y driver Vulkan.
 * - Cómo funciona: Compara una huella `.stamp` (`versionCode:lastUpdateTime`) para no recopiar en cada inicio.
 * - Por qué: Separa las operaciones de sistema de archivos de `LibertyActivity` (Principio SRP de SOLID).
 */
final class LibertyResourceSync {

    private static final String TAG = "LibertyRecomp";
    static final String RESOURCES = "Resources";
    private static final String RESOURCES_STAMP = ".stamp";
    static final String LAUNCH_REPORT = "last_launch.txt";

    private LibertyResourceSync() {}

    /**
     * Sincroniza los recursos empaquetados en el APK hacia `files/Resources`.
     *
     * - Qué hace: Extrae fuentes, iconos de botones y claves RPF necesarios para iniciar GTA IV.
     * - Cómo funciona: Si `.stamp` coincide con la versión instalada, retorna de inmediato sin copiar.
     * - Por qué: Evita cuellos de botella de E/S en el arranque del juego después de la primera vez.
     */
    static synchronized File syncResources(Context context) throws IOException {
        File target = new File(context.getFilesDir(), RESOURCES);
        String stamp;
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            stamp = info.versionCode + ":" + info.lastUpdateTime;
        } catch (Exception e) {
            stamp = "unknown";
        }
        File stampFile = new File(target, RESOURCES_STAMP);
        if (stamp.equals(readFirstLine(stampFile))) {
            return target;
        }
        deleteRecursively(target);
        copyAssetTree(context.getAssets(), RESOURCES, target);
        try (OutputStream out = new FileOutputStream(stampFile)) {
            out.write(stamp.getBytes(StandardCharsets.UTF_8));
        }
        Log.i(TAG, "bundled resources copied to " + target);
        return target;
    }

    /**
     * Copia recursivamente un archivo o árbol de directorios desde `AssetManager` al disco.
     *
     * - Qué hace: Transfiere archivos binarios del APK usando un búfer de 64 KB.
     * - Cómo funciona: Si la ruta tiene hijos la trata como carpeta; si no, copia el flujo de bytes.
     * - Por qué: Permite que el motor nativo C++ lea recursos mediante rutas POSIX estándar.
     */
    static void copyAssetTree(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            File parent = target.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                throw new IOException("cannot create " + parent);
            }
            try (InputStream in = assets.open(path);
                 OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                }
            }
            return;
        }
        if (!target.isDirectory() && !target.mkdirs()) {
            throw new IOException("cannot create " + target);
        }
        for (String child : children) {
            copyAssetTree(assets, path + "/" + child, new File(target, child));
        }
    }

    /**
     * Escribe el informe técnico `last_launch.txt` con datos del dispositivo y estado del driver Vulkan.
     *
     * - Qué hace: Registra modelo, versión de Android, GPU KGSL y resultado de `DriverBridge`.
     * - Cómo funciona: Guarda el texto en `Android/data/com.libertyrecomp/files/last_launch.txt`.
     * - Por qué: Facilita diagnosticar fallos de drivers Turnip/Adreno sin requerir cable ADB.
     */
    @SuppressWarnings("deprecation")
    static void writeLaunchReport(Context ctx, File dataRoot, String driverText, String driverMode,
                                  String driverStatus, String problem, boolean debugLog, File debugLogFile) {
        if (dataRoot == null) return;
        StringBuilder report = new StringBuilder();
        String version = "?";
        try {
            PackageInfo info = ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0);
            version = info.versionName + " (" + info.versionCode + ")";
        } catch (Exception ignored) {}
        report.append("Liberty Recompiled for Android ").append(version).append('\n');
        report.append("time: ").append(new java.util.Date()).append('\n');
        report.append("device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" (").append(Build.DEVICE).append(", board ").append(Build.BOARD)
                .append(", hardware ").append(Build.HARDWARE).append(")\n");
        if (Build.VERSION.SDK_INT >= 31) {
            report.append("soc: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        }
        report.append("android: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        report.append("abis: ").append(String.join(", ", Build.SUPPORTED_ABIS)).append('\n');
        try {
            report.append("page size: ").append(Os.sysconf(android.system.OsConstants._SC_PAGESIZE)).append('\n');
        } catch (Exception ignored) {}
        String kgsl = readFirstLine(new File("/sys/class/kgsl/kgsl-3d0/gpu_model"));
        report.append("kgsl gpu: ").append(kgsl != null ? kgsl : "(not readable or not Adreno)").append('\n');
        report.append("driver.txt: ").append(driverText == null ? "(none)" : "\"" + driverText + "\"")
                .append(" -> ").append(driverMode).append('\n');
        report.append("driver status: ").append(driverStatus).append('\n');
        if (debugLog) {
            report.append("debug log: ").append(debugLogFile != null ? debugLogFile.getAbsolutePath() : "could not start").append('\n');
        }
        report.append("result: ").append(problem == null ? "driver usable, starting the game"
                : "cannot start: " + problem.replace('\n', ' ')).append('\n');
        try (OutputStream out = new FileOutputStream(new File(dataRoot, LAUNCH_REPORT))) {
            out.write(report.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.w(TAG, "writing the launch report failed", e);
        }
    }

    /**
     * Lee y limpia la primera línea de un archivo de texto (ignorando BOM UTF-8).
     *
     * - Qué hace: Obtiene la primera línea útil de archivos como `driver.txt` o `.stamp`.
     * - Cómo funciona: Abre un `BufferedReader` con `try-with-resources` y recorta espacios.
     * - Por qué: Evita repetir bloques de lectura de una línea en múltiples clases.
     */
    static String readFirstLine(File file) {
        if (!file.isFile()) return null;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line = reader.readLine();
            return line == null ? null : cleanLine(line);
        } catch (IOException e) {
            return null;
        }
    }

    static String cleanLine(String line) {
        return line.replace("\uFEFF", "").trim();
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (file.exists() && !file.delete()) Log.w(TAG, "cannot delete " + file);
    }
}
