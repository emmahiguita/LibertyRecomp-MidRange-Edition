package com.libertyrecomp;

import android.content.Context;
import android.util.Log;

import com.libertyrecomp.voice.NanoDeviceTierProfiler;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;

/**
 * Puente de selección e inicialización del driver Vulkan para Gama Baja, Gama Media y Gama Alta.
 *
 * - Qué hace: Inicializa el proxy nativo `libvulkan.so` eligiendo entre Mesa Turnip (Adreno),
 *   un driver personalizado (`custom:NAME`) o el driver Vulkan del sistema (`system`) para GPUs
 *   Mali, PowerVR, Xclipse o Adreno de gama baja/media.
 * - Cómo funciona:
 *   1) Si `NanoDeviceTierProfiler.prefersSystemVulkanDriver()` detecta un SoC MediaTek/Exynos/Unisoc,
 *      usa directamente el driver `system` sin fallar intentando cargar Turnip de Qualcomm.
 *   2) Acepta GPUs desde Vulkan 1.1 (`VULKAN_1_1`) para no bloquear teléfonos de gama baja y media.
 * - Por qué: Elimina el bloqueo artificial que solo permitía Adreno 6xx/7xx con Vulkan 1.2,
 *   delegando el empaquetado de archivos a `DriverPackageHelper` (Principio SRP de SOLID).
 */
final class DriverBridge {

    static final String TURNIP = "turnip";
    static final String SYSTEM = "system";
    static final String CUSTOM_PREFIX = "custom:";

    private static final String TAG = "LibertyDriver";
    /** Versión mínima compatible ampliada a Vulkan 1.1 para soportar GPUs Mali/PowerVR/Adreno de gama baja y media. */
    private static final long VULKAN_1_1 = (1L << 22) | (1L << 12);

    private static boolean sInitialized;
    private static String sStatus;

    private DriverBridge() {}

    static native String nativeInit(
            String nativeLibraryDir,
            String internalDriverDir,
            String driverFilename,
            boolean custom,
            boolean allowSystemFallback
    );

    /**
     * Carga e inicializa el proxy Vulkan una única vez por proceso Android.
     * - Qué hace: Configura el driver seleccionado o conmuta automáticamente al driver del sistema
     *   en dispositivos MediaTek/Exynos/Unisoc/Mali de gama baja y media.
     * - Cómo funciona: Invoca `DriverPackageHelper` si se usa Turnip/Custom, carga `libvulkan.so`
     *   y ejecuta `nativeInit` con `allowSystemFallback = true`.
     * - Por qué: Garantiza que si Turnip no es compatible con la GPU del móvil, el driver nativo
     *   del fabricante tome el relevo de forma transparente.
     */
    static synchronized String initialize(Context context, String mode) {
        if (sInitialized) {
            return sStatus;
        }
        String effectiveMode = mode;
        if (TURNIP.equals(effectiveMode) && NanoDeviceTierProfiler.prefersSystemVulkanDriver()) {
            Log.i(TAG, "Non-Adreno SoC detected (Mali/PowerVR/Xclipse); using system Vulkan driver");
            effectiveMode = SYSTEM;
        }

        String nativeDir = context.getApplicationInfo().nativeLibraryDir;
        boolean custom = TURNIP.equals(effectiveMode);
        File driverDir = new File(context.getFilesDir(), "drivers");
        String library = DriverPackageHelper.LIBRARY;

        if (effectiveMode != null && effectiveMode.startsWith(CUSTOM_PREFIX)) {
            try {
                String name = effectiveMode.substring(CUSTOM_PREFIX.length()).trim();
                driverDir = DriverPackageHelper.prepareCustom(context, name);
                library = DriverPackageHelper.customLibrary(driverDir);
                custom = true;
                Log.i(TAG, "custom driver " + name + " library=" + library);
            } catch (IOException | RuntimeException e) {
                Log.e(TAG, "custom driver unusable, falling back", e);
                custom = !NanoDeviceTierProfiler.prefersSystemVulkanDriver();
            }
        }
        if (custom && DriverPackageHelper.LIBRARY.equals(library)) {
            try {
                driverDir = DriverPackageHelper.prepareBundle(context);
            } catch (IOException | RuntimeException e) {
                Log.e(TAG, "Turnip bundle unusable, using system driver", e);
                custom = false;
            }
        }
        if (!driverDir.isDirectory() && !driverDir.mkdirs()) {
            Log.w(TAG, "cannot create " + driverDir);
        }
        System.load(new File(nativeDir, "libvulkan.so").getAbsolutePath());
        String status = nativeInit(
                nativeDir,
                driverDir.getAbsolutePath(),
                custom ? library : "",
                custom,
                true
        );
        sInitialized = true;
        sStatus = status;
        return status;
    }

    /**
     * Verifica si el driver Vulkan inicializado puede ejecutar el motor en el teléfono actual.
     * - Qué hace: Devuelve `null` si hay al menos un dispositivo Vulkan >= 1.1 disponible, o una
     *   explicación legible si el hardware carece por completo de soporte Vulkan.
     * - Cómo funciona: Inspecciona el arreglo `devices` devuelto por `nativeInit` en JSON.
     * - Por qué: Permite que móviles de gama baja y media con Vulkan 1.1 arranquen sin ser bloqueados.
     */
    static String unusableReason(String status) {
        JSONObject json;
        try {
            json = new JSONObject(status);
        } catch (JSONException e) {
            return "The Vulkan driver could not be initialized (unreadable status).";
        }
        if (!json.optBoolean("ok", false)) {
            return "No usable Vulkan driver was found on this device.\n\n"
                    + json.optString("error", "unknown error");
        }
        JSONArray devices = json.optJSONArray("devices");
        long best = 0;
        String name = "unknown GPU";
        for (int i = 0; devices != null && i < devices.length(); ++i) {
            JSONObject device = devices.optJSONObject(i);
            if (device == null) continue;
            long api = device.optLong("api_version", 0);
            if (api >= best) {
                best = api;
                name = device.optString("name", name);
            }
        }
        if (best >= VULKAN_1_1) {
            return null;
        }
        return "This device's GPU (" + name + ") only offers Vulkan " + (best >> 22) + "."
                + ((best >> 12) & 0x3FF) + " and at least Vulkan 1.1 is required.";
    }
}
