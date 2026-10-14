// LibertyRecomp Android — :app module.
//
// Gradle never runs CMake. The native side (libmain.so, librexruntime.so,
// the GPU plugins, libSDL3.so, libc++_shared.so) is millions of lines of
// recompiled PowerPC and is built by os/android/scripts/build_native.sh,
// which stages the libraries into src/main/jniLibs and the title resources
// into src/main/assets/Resources. This module only packages them.
//
// The SDL Java classes come from the same SDL3 tree the native library is
// built from: SDLActivity refuses to start when the versions differ.

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val sdlJavaDir = rootProject.file(
    "../../glue/rexglue-sdk-main/thirdparty/sdl3/android-project/app/src/main/java")
val nanoSdkJavaDir = rootProject.file("../../../nano-gaming-sdk/src/main/java")

android {
    namespace   = "com.libertyrecomp"
    compileSdk  = 35
    ndkVersion  = "29.0.14206865"

    defaultConfig {
        applicationId = "com.libertyrecomp"
        // 26: Android 8.0+ (ASharedMemory disponible desde API 26 para máxima compatibilidad en Gama Baja/Media).
        minSdk        = 26
        targetSdk     = 35
        versionCode   = 16
        versionName   = "0.5.9"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    sourceSets["main"].java.srcDirs("src/main/java", sdlJavaDir, nanoSdkJavaDir)

    buildTypes {
        debug {
            isDebuggable    = true
            isJniDebuggable = true
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
            signingConfig   = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // Configuración de compilación de Kotlin compatible con JVM 17 para los módulos WebRTC y UI.
    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        jniLibs {
            // Unpack the libraries into nativeLibraryDir: the runtime finds its
            // GPU plugins by real path next to itself, and a Turnip driver
            // loaded through libadrenotools needs real files as well.
            useLegacyPackaging = true
            // build_native.sh decides what is stripped.
            keepDebugSymbols += "**/*.so"
        }
    }

    lint {
        checkReleaseBuilds = false
        abortOnError = false
    }
}

// Dependencias del módulo Android:
// - webrtc-sdk/android: SDK precompilado de WebRTC con soporte nativo para Opus, ICE, STUN/TURN,
//   AudioTrack con control de volumen por par (proximidad 3D) y VideoTrack para llamadas in-game.
dependencies {
    implementation("io.github.webrtc-sdk:android:125.6422.07")
}

