package com.libertyrecomp;

import android.content.Intent;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import com.libertyrecomp.voice.NanoDeviceTier;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Configurador de argumentos (`args.txt`), variables de entorno (`env.txt`) y fuentes de instalación.
 *
 * - Qué hace: Prepara la lista de banderas CLI que recibe `SDL_main` y exporta variables POSIX.
 * - Cómo funciona: Migra valores obsoletos de `args.txt`, aplica ajustes de rendimiento del launcher
 *   y detecta archivos `.iso` o actualizaciones en `files/install/`.
 * - Por qué: Desacopla el parseo de configuración respecto al ciclo de vida de `LibertyActivity` (SOLID).
 */
final class LibertyArgsConfig {

    private static final String TAG = "LibertyRecomp";
    private static final String[][] ARGUMENT_MIGRATIONS = {
            {"--gta4_dynamic_draw_distance_min=0.75", "--gta4_dynamic_draw_distance_min=0.80"},
            {"--gta4_dynamic_draw_distance_min=0.85", "--gta4_dynamic_draw_distance_min=0.80"},
    };

    private LibertyArgsConfig() {}

    /**
     * Exporta una variable de entorno al proceso nativo antes de cargar `libmain.so`.
     *
     * - Qué hace: Llama a `Os.setenv(name, value, true)` manejando errores del sistema.
     * - Cómo funciona: Define rutas como `XDG_DATA_HOME`, `REX_RESOURCES_DIR` y `TU_DEBUG`.
     * - Por qué: El motor C++ y Mesa Turnip leen su configuración desde el entorno del proceso.
     */
    static void setenv(String name, String value) {
        try {
            Os.setenv(name, value, true);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv " + name + " failed", e);
        }
    }

    /**
     * Lee el archivo opcional `env.txt` y exporta cada par `CLAVE=VALOR` al proceso.
     *
     * - Qué hace: Permite sobreescribir banderas de Turnip o Vulkan sin recompilar el APK.
     * - Cómo funciona: Ignora líneas vacías o comentarios `#` y separa por el primer `=`.
     * - Por qué: Facilita depurar drivers gráficos en distintos modelos de teléfonos Android.
     */
    static void applyEnvironmentFile(File file) {
        if (!file.isFile()) return;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = LibertyResourceSync.cleanLine(line);
                int equals = line.indexOf('=');
                if (line.isEmpty() || line.startsWith("#") || equals <= 0) continue;
                String name = line.substring(0, equals).trim();
                String value = line.substring(equals + 1).trim();
                setenv(name, value);
            }
        } catch (IOException e) {
            Log.e(TAG, "reading " + file + " failed", e);
        }
    }

    /**
     * Actualiza valores antiguos por defecto en `args.txt` creados por versiones previas.
     *
     * - Qué hace: Sustituye líneas exactas obsoletas conservando cualquier ajuste manual del usuario.
     * - Cómo funciona: Compara cada línea con `ARGUMENT_MIGRATIONS` y reescribe el archivo si hubo cambios.
     * - Por qué: Evita que actualizaciones del emulador hereden parámetros que rompían sombras dinámicas.
     */
    static void migrateArguments(File file) {
        if (!file.isFile()) return;
        try {
            List<String> lines = Files.readAllLines(file.toPath(), StandardCharsets.UTF_8);
            boolean changed = false;
            for (int i = 0; i < lines.size(); ++i) {
                for (String[] migration : ARGUMENT_MIGRATIONS) {
                    if (LibertyResourceSync.cleanLine(lines.get(i)).equals(migration[0])) {
                        lines.set(i, migration[1]);
                        changed = true;
                    }
                }
            }
            if (changed) Files.write(file.toPath(), lines, StandardCharsets.UTF_8);
        } catch (IOException e) {
            Log.w(TAG, "cannot update " + file, e);
        }
    }

    /**
     * Lee todas las banderas activas de `args.txt` omitiendo comentarios y líneas en blanco.
     *
     * - Qué hace: Convierte las líneas de `args.txt` en una lista mutable de cadenas.
     * - Cómo funciona: Filtra líneas que comienzan con `#` o están vacías.
     * - Por qué: Alimenta `getArguments()` de `SDLActivity` para configurar cvars del motor.
     */
    static List<String> readArgumentsList(File file) {
        List<String> arguments = new ArrayList<>();
        if (!file.isFile()) return arguments;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = LibertyResourceSync.cleanLine(line);
                if (!line.isEmpty() && !line.startsWith("#")) arguments.add(line);
            }
        } catch (IOException e) {
            Log.w(TAG, "cannot read " + file, e);
        }
        return arguments;
    }

    /**
     * Aplica automáticamente el perfil gráfico de Gama Baja (`480p`), Gama Media (`720p`) o las pruebas del Launcher.
     * - Qué hace: Ajusta resolución (`--android_surface`), sombras, reflejos, distancia de dibujado y GPU turbo.
     * - Cómo funciona: Si el usuario no forzó extras desde el launcher y el móvil es `LOW_END` o `MID_RANGE`,
     *   inyecta los parámetros óptimos de `NanoDeviceTier` en memoria.
     * - Por qué: Garantiza que GTA IV funcione fluido en gama baja y media sin editar `args.txt`.
     */
    static void applyPerformanceTestArguments(Intent intent, NanoDeviceTier tier, List<String> arguments) {
        if (intent != null && intent.hasExtra(LibertyActivity.EXTRA_SHADOWS_OFF)) {
            boolean shadowsOff = intent.getBooleanExtra(LibertyActivity.EXTRA_SHADOWS_OFF, false);
            int water = Math.max(0, Math.min(intent.getIntExtra(LibertyActivity.EXTRA_WATER_REFLECTION_INTERVAL, 1), 3));
            int env = Math.max(0, Math.min(intent.getIntExtra(LibertyActivity.EXTRA_OTHER_REFLECTION_INTERVAL, 1), 3));
            replaceEngineFlags(arguments, shadowsOff ? 0 : 1, water, env,
                    intent.getBooleanExtra(LibertyActivity.EXTRA_GPU_TURBO, false),
                    intent.getBooleanExtra(LibertyActivity.EXTRA_HALF_DRAW_DISTANCE, false) ? 0.5f : -1f);
        } else if (tier != NanoDeviceTier.HIGH_END) {
            arguments.removeIf(a -> a.startsWith("--android_surface="));
            arguments.add("--android_surface=" + tier.getSurfaceResolution());
            replaceEngineFlags(arguments, tier.getShadowsInterval(), tier.getReflectionInterval(),
                    tier.getReflectionInterval(), true, tier.getDrawDistanceScale());
        }
    }

    private static void replaceEngineFlags(List<String> args, int shadows, int water, int env, boolean turbo, float drawScale) {
        args.removeIf(a -> a.startsWith("--gta4_native_shadow_interval")
                || a.startsWith("--gta4_native_water_reflection_interval")
                || a.startsWith("--gta4_native_environment_reflection_interval")
                || a.startsWith("--android_gpu_turbo"));
        args.add("--gta4_native_shadow_interval=" + shadows);
        args.add("--gta4_native_water_reflection_interval=" + water);
        args.add("--gta4_native_environment_reflection_interval=" + env);
        args.add("--android_gpu_turbo=" + turbo);
        if (drawScale > 0f) {
            args.removeIf(a -> a.startsWith("--gta4_draw_distance_scale") || a.startsWith("--gta4_dynamic_draw_distance="));
            args.add("--gta4_draw_distance_scale=" + drawScale);
            args.add("--gta4_dynamic_draw_distance=false");
        }
    }

    /**
     * Busca discos `.iso` y actualizaciones en `files/install/` para pasarlos al instalador nativo.
     *
     * - Qué hace: Agrega `--install_game_source` y `--install_update_source` si existen archivos allí.
     * - Cómo funciona: Escanea la carpeta `install` cuando no se definieron rutas manuales en `args.txt`.
     * - Por qué: Permite instalar GTA IV copiando el ISO por USB sin requerir permisos de almacenamiento.
     */
    static void addInstallSources(File dir, List<String> arguments) {
        if (!dir.isDirectory() && !dir.mkdirs()) return;
        for (String arg : arguments) {
            if (arg.startsWith("--install_")) return;
        }
        File[] files = dir.listFiles();
        if (files == null) return;
        File disc = null;
        File update = null;
        for (File file : files) {
            if (!file.isFile() || file.getName().startsWith(".")) continue;
            if (file.getName().toLowerCase(Locale.ROOT).endsWith(".iso")) {
                if (disc == null) disc = file;
            } else if (update == null || file.length() > update.length()) {
                update = file;
            }
        }
        if (disc != null) arguments.add("--install_game_source=" + disc.getAbsolutePath());
        if (update != null) arguments.add("--install_update_source=" + update.getAbsolutePath());
    }
}
