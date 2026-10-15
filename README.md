<p align="center">
    <img src="docs/android/images/banner_android_v4.png" alt="Liberty Recompiled for Android" width="800"/>
</p>

# LibertyRecomp - Mid-Range Edition 🎮📱

[![GitHub Release](https://img.shields.io/github/v/release/emmahiguita/LibertyRecomp-MidRange-Edition?style=flat-square)](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition/releases)
[![Platform](https://img.shields.io/badge/Platform-Android%2010%2B-green?style=flat-square)](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition)
[![GPU Support](https://img.shields.io/badge/GPU-Mali%20%7C%20Adreno-blue?style=flat-square)](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition)
[![Author](https://img.shields.io/badge/Author-Emmanuel%20Higuita-orange?style=flat-square)](https://github.com/emmahiguita)

Adaptación de **Liberty Recompiled** (recompilación de GTA IV de Xbox 360) organizada y verificada para correr en **dispositivos Android de gama media**, solucionando el problema de compatibilidad gráfica en **GPUs ARM Mali** (probado y funcionando en **OPPO A79 5G**).

---

## 📸 Capturas del Juego Corriendo (OPPO A79 5G - GPU Mali-G57 MC2)

Capturas tomadas directamente del dispositivo ejecutando el juego en tiempo real:

| Cinemática de Inicio (Niko Bellic en el Platypus) | Llegada al Muelle de Liberty City (Coche de Roman) |
|:---:|:---:|
| <img src="docs/android/images/oppo_niko_cargo.png" alt="Niko Bellic en Barco Platypus" width="450"/> | <img src="docs/android/images/oppo_roman_docks.png" alt="Roman Bellic Docks" width="450"/> |

---

## 🛠️ ¿Qué se solucionó para que corra el juego?

1. **Corrección del fallo en GPUs Mali (Mali-G57 / Dimensity):**
   - El juego original en Android sufría un cierre forzado (*crash SIGSEGV*) en `libGLES_mali.so` durante la compilación de shaders SPIR-V debido a que las GPUs Mali no soportan la instrucción `ClipDistance`.
   - Se integró un proxy Vulkan nativo (`libvulkan.so`) que parchea el bytecode de los shaders en memoria al vuelo, adaptándolos para que el driver de Mali los compile correctamente.
2. **Ajustes de rendimiento para gama media:**
   - Escala de renderizado interna optimizada a 360p que el Presenter de Vulkan escala a pantalla completa sin pérdida de controles ni deformaciones.
   - Reducción de cascadas de sombras pesadas para aliviar el consumo de CPU y GPU.
   - Buffer de audio AAudio a 2048 muestras para evitar cortes y chirridos en el sonido.

---

## 📱 Dispositivo Comprobado y Requisitos

* **Dispositivo Probado y Verificado:** **OPPO A79 5G**
  - **Procesador:** MediaTek Dimensity 6020 (MT6833)
  - **GPU:** ARM Mali-G57 MC2 (Driver del sistema / Vulkan 1.2)
  - **RAM:** 8 GB
  - **Sistema Operativo:** Android 15 (ColorOS / API 35)
* **Requisitos Generales:**
  - Teléfono Android de 64 bits con soporte de instrucciones atómicas (ARMv8.2-A o superior).
  - Mínimo 6 GB de memoria RAM.
  - GPU compatible con Vulkan 1.2 (Mali o Snapdragon Adreno).
  - ~15 GB de espacio libre para la instalación (~7 GB una vez instalado).

---

## 💿 Archivos Necesarios

1. **Copia del Juego Xbox 360 (.iso):** Versión retail **USA (NTSC-U)** de GTA IV.
   - Fuente de referencia: [Vimm's Lair - Grand Theft Auto IV (Xbox 360)](https://vimm.net/vault/78373)
   - Archivo probado: `Grand Theft Auto IV (USA) (En,Fr,De,Es,It).iso` (~7.8 GB)
2. **Title Update 8 (TU8):** Versión **0.0.8.5** para la edición USA (`TU_1A581VI_000000K000000.0000000000205`). Incluida directamente en la sección de [Releases](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition/releases).

---

## 🚀 Guía de Instalación

### 1. Instalar el APK
Descarga el archivo APK verificado desde nuestros **[Releases Oficiales](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition/releases)**:
* ⭐ **`LibertyRecompAndroid-v0.5.9-mali-fixed.apk`** (53.7 MB) ➔ **APK 100% FUNCIONAL Y COMPROBADO** (con el parche nativo para GPUs Mali y optimización de rendimiento para gama media).

### 2. Extraer el Juego en el Teléfono
Puedes instalarlo de dos formas:

* **Método 1 (Directo desde la App):**
  1. Descarga el archivo `.iso` y la actualización `TU_...` a la carpeta **Descargas** de tu teléfono.
  2. Abre la app **Liberty Recompiled**.
  3. En **Base game**, pulsa **Choose file...** y selecciona tu `.iso`.
  4. En **Title Update 8**, selecciona el archivo `TU_...`.
  5. Pulsa **Install**. Tardará unos minutos en extraer los archivos.
  6. Cuando finalice, pulsa **Play**.

* **Método 2 (Automático por Carpeta):**
  1. Conecta el teléfono al PC o usa un explorador de archivos.
  2. Coloca el archivo `.iso` y la actualización `TU_...` en:
     ```text
     Android/data/com.libertyrecomp/files/install/
     ```
  3. Abre la app: detectará los archivos, realizará la extracción y activará el botón **Play**.
  *(Una vez completada la instalación, puedes borrar la ISO de 7.8 GB para recuperar espacio).*

---

## 🎮 Controles

* **En Pantalla:** Incluye controles táctiles virtuales completos tipo mando de Xbox 360 (Stick izquierdo, Stick derecho, cruceta D-pad, gatillos y botones de acción).
* **Mandos Físicos:** Compatible con mandos Bluetooth y USB (Xbox, PlayStation, mandos telescópicos).

---

## 👤 Autor y Repositorio
* **Mantenimiento y adaptación:** Emmanuel Higuita ([@emmahiguita](https://github.com/emmahiguita))
* **Repositorio Oficial:** [github.com/emmahiguita/LibertyRecomp-MidRange-Edition](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition)
* **Descarga de APKs:** [GitHub Releases](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition/releases)

---

## ⚖️ Reconocimientos
* Basado en [Liberty Recompiled](https://github.com/OZORDI/LibertyRecomp) de OZORDI y en el framework [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
* Este proyecto es una recompilación estática comunitaria sin fines de lucro. Grand Theft Auto IV es una marca de Rockstar Games y Take-Two Interactive.
