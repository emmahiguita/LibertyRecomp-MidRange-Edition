package com.libertyrecomp;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import com.libertyrecomp.voice.NanoDeviceTierProfiler;
import com.libertyrecomp.voice.VoiceLifecycleCoordinator;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import java.io.File;
import java.io.IOException;
import java.util.List;
import java.util.Locale;

/**
 * Actividad principal que hospeda el runtime de GTA IV (SDL3 + Vulkan/Turnip) y el sistema de voz P2P.
 *
 * - Qué hace: Inicializa el entorno nativo, carga el driver Vulkan seleccionado, fija el modo
 *   horizontal (Landscape) e integra `VoiceLifecycleCoordinator` para voz 3D y HUD Material 3.
 * - Cómo funciona: Delega la sincronización de archivos a `LibertyResourceSync`, los argumentos a
 *   `LibertyArgsConfig` y la voz/permisos/limpieza a `VoiceLifecycleCoordinator`.
 * - Por qué: Cumple SOLID y Clean Architecture manteniendo la clase bajo 200 líneas sin procesos zombi.
 */
public class LibertyActivity extends SDLActivity {

    private static final String TAG = "LibertyRecomp";
    private static final String DEFAULT_ARGS = "default_args.txt";
    private static final String SURFACE_ARGUMENT = "--android_surface=";

    static final String EXTRA_DEBUG_LOG = "debug_log";
    static final String EXTRA_SHADOWS_OFF = "perf_shadows_off";
    static final String EXTRA_HALF_DRAW_DISTANCE = "perf_half_draw_distance";
    static final String EXTRA_GPU_TURBO = "perf_gpu_turbo";
    static final String EXTRA_WATER_REFLECTION_INTERVAL = "perf_water_reflection_interval";
    static final String EXTRA_OTHER_REFLECTION_INTERVAL = "perf_other_reflection_interval";

    private String[] mArguments = new String[0];
    private int mSurfaceWidth, mSurfaceHeight;
    private String mDriverMode = DriverBridge.TURNIP, mDriverText;
    private File mDataRoot, mDebugLogFile;
    private boolean mDebugLog;
    private VoiceLifecycleCoordinator mVoiceCoordinator;

    /**
     * Inicia la actividad del juego, el entorno SDL3 y el coordinador de voz WebRTC + HUD.
     * - Qué hace: Prepara variables de entorno, mantiene la pantalla encendida y levanta el HUD de voz.
     * - Cómo funciona: Se ejecuta al abrir el juego desde el launcher en orientación horizontal.
     * - Por qué: Garantiza que el audio P2P y los controles compactos estén disponibles en la partida.
     */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        prepareEnvironment();
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        mVoiceCoordinator = new VoiceLifecycleCoordinator(this, DebugLog::stopActive);
        mVoiceCoordinator.onStartSession();
    }

    /**
     * Recibe la respuesta del usuario al diálogo de permiso de micrófono (`RECORD_AUDIO`).
     * - Qué hace: Notifica a `VoiceLifecycleCoordinator` cuando el permiso es concedido.
     * - Cómo funciona: Delega el resultado de Android para activar la captura Opus inmediatamente.
     * - Por qué: Permite empezar a hablar en partida sin reiniciar la actividad.
     */
    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (mVoiceCoordinator != null) mVoiceCoordinator.onRequestPermissionsResult(requestCode, grantResults);
    }

    /**
     * Libera todos los recursos de voz, el HUD y detiene cualquier proceso `logcat` al salir.
     * - Qué hace: Invoca `mVoiceCoordinator.onDestroySession()` antes de destruir `SDLActivity`.
     * - Cómo funciona: Cierra sockets UDP, sesiones WebRTC, foco de audio y procesos hijos.
     * - Por qué: Evita procesos zombi y fugas de memoria al cerrar o reiniciar GTA IV.
     */
    @Override
    protected void onDestroy() {
        if (mVoiceCoordinator != null) {
            mVoiceCoordinator.onDestroySession();
            mVoiceCoordinator = null;
        } else {
            DebugLog.stopActive();
        }
        super.onDestroy();
    }

    /**
     * Inicializa el puente del driver Vulkan (Turnip o sistema) antes de que SDL cargue `libmain.so`.
     * - Qué hace: Verifica que la GPU Adreno sea compatible y escribe el informe `last_launch.txt`.
     * - Cómo funciona: Llama a `DriverBridge.initialize` y aborta con mensaje claro si falla.
     * - Por qué: Muestra un diagnóstico legible en vez de cerrar la app silenciosamente.
     */
    @Override
    public void loadLibraries() {
        String status = DriverBridge.initialize(this, mDriverMode);
        String reason = DriverBridge.unusableReason(status);
        LibertyResourceSync.writeLaunchReport(
                this, mDataRoot, mDriverText, mDriverMode, status, reason, mDebugLog, mDebugLogFile);
        if (reason != null) {
            throw new UnsatisfiedLinkError(reason);
        }
        super.loadLibraries();
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        SDLSurface surface = super.createSDLSurface(context);
        if (mSurfaceWidth > 0 && mSurfaceHeight > 0) surface.getHolder().setFixedSize(mSurfaceWidth, mSurfaceHeight);
        return surface;
    }

    @Override
    protected String[] getArguments() { return mArguments; }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        super.setOrientationBis(w, h, resizable, "LandscapeLeft LandscapeRight");
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    static synchronized File syncResources(Context context) throws IOException {
        return LibertyResourceSync.syncResources(context);
    }

    private void prepareEnvironment() {
        File external = getExternalFilesDir(null);
        mDataRoot = external != null ? external : getFilesDir();
        File resources = new File(getFilesDir(), LibertyResourceSync.RESOURCES);
        try { LibertyResourceSync.syncResources(this); } catch (IOException e) { Log.e(TAG, "sync failed", e); }
        LibertyArgsConfig.setenv("XDG_DATA_HOME", mDataRoot.getAbsolutePath());
        LibertyArgsConfig.setenv("HOME", mDataRoot.getAbsolutePath());
        LibertyArgsConfig.setenv("REX_RESOURCES_DIR", resources.getAbsolutePath());
        LibertyArgsConfig.setenv("TU_DEBUG", "sysmem");
        LibertyArgsConfig.applyEnvironmentFile(new File(mDataRoot, "env.txt"));

        File argsFile = new File(mDataRoot, "args.txt");
        if (!argsFile.isFile()) {
            try { LibertyResourceSync.copyAssetTree(getAssets(), DEFAULT_ARGS, argsFile); }
            catch (IOException e) { Log.e(TAG, "writing default args failed", e); }
        }
        LibertyArgsConfig.migrateArguments(argsFile);
        List<String> arguments = LibertyArgsConfig.readArgumentsList(argsFile);
        LibertyArgsConfig.applyPerformanceTestArguments(getIntent(), NanoDeviceTierProfiler.detectTier(this), arguments);

        mDebugLog = getIntent() != null && getIntent().getBooleanExtra(EXTRA_DEBUG_LOG, false);
        if (mDebugLog) {
            arguments.removeIf(a -> a.startsWith("--diagnostics") || a.startsWith("--log_level"));
            arguments.add("--log_level=info");
            mDebugLogFile = DebugLog.start(mDataRoot);
        }
        if (arguments.stream().noneMatch(a -> a.startsWith("--diagnostics"))) {
            arguments.add("--diagnostics=true");
            arguments.add("--diagnostics_categories=logging");
        }
        LibertyArgsConfig.addInstallSources(new File(mDataRoot, "install"), arguments);
        extractSurfaceSize(arguments);
        mArguments = arguments.toArray(new String[0]);
        configureDriverMode(mDataRoot);
    }

    private void extractSurfaceSize(List<String> arguments) {
        for (int i = arguments.size() - 1; i >= 0; --i) {
            String arg = arguments.get(i);
            if (!arg.startsWith(SURFACE_ARGUMENT)) continue;
            arguments.remove(i);
            String[] s = arg.substring(SURFACE_ARGUMENT.length()).split("x");
            if (s.length == 2) {
                try { mSurfaceWidth = Integer.parseInt(s[0].trim()); mSurfaceHeight = Integer.parseInt(s[1].trim()); }
                catch (NumberFormatException ignored) {}
            }
        }
    }

    private void configureDriverMode(File dataRoot) {
        mDriverText = LibertyResourceSync.readFirstLine(new File(dataRoot, "driver.txt"));
        String lower = mDriverText == null ? "" : mDriverText.toLowerCase(Locale.ROOT);
        if (lower.startsWith(DriverBridge.CUSTOM_PREFIX)) {
            mDriverMode = DriverBridge.CUSTOM_PREFIX + mDriverText.substring(DriverBridge.CUSTOM_PREFIX.length()).trim();
        } else if (lower.equals(DriverBridge.SYSTEM)) {
            mDriverMode = DriverBridge.SYSTEM;
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
}
