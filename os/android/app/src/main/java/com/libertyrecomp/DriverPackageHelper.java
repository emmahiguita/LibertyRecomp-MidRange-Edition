package com.libertyrecomp;

import android.content.Context;
import android.os.Process;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Gestor de extracción y verificación SHA-256 de paquetes de drivers Vulkan (Turnip y Custom).
 *
 * - Qué hace: Descomprime el paquete `turnip-r6.zip` o copia drivers personalizados hacia el
 *   almacenamiento privado ejecutable de la app, validando su integridad criptográfica.
 * - Cómo funciona: Calcula el hash SHA-256 de `libvulkan_freedreno.so` y `meta.json` antes de reutilizarlos.
 * - Por qué: Separa la lógica de sistema de archivos y criptografía respecto a `DriverBridge` (SOLID),
 *   manteniendo ambos archivos por debajo de 200 líneas.
 */
final class DriverPackageHelper {

    static final String LIBRARY = "libvulkan_freedreno.so";
    private static final String BUNDLE_ASSET = "drivers/turnip-r6.zip";
    private static final String META = "meta.json";
    private static final String ZIP_HASH =
            "5fa2305eb9eff64d9347a670e6ca5ef7c518f60e9e782a4db7ee5588c9852d81";
    private static final String LIB_HASH =
            "4fd2fd30311af301ae5564139bc24cc727390d4dc3c90a20192c25ec91287c59";
    private static final String META_HASH =
            "b6dae160802aec1ab48a0953fd9b09d76465a282c4a1d134645218af2a62d9d3";

    private DriverPackageHelper() {}

    /**
     * Copia un paquete AdrenoTools personalizado desde almacenamiento externo hacia directorio privado.
     * - Qué hace: Valida el nombre del paquete y duplica sus archivos `.so` y `meta.json`.
     * - Cómo funciona: Limpia el directorio destino y copia con buffer de 64 KB.
     * - Por qué: Android exige que las librerías dinámicas cargadas con `dlopen` residan en almacenamiento privado.
     */
    static File prepareCustom(Context context, String name) throws IOException {
        if (name.isEmpty() || name.contains("/") || name.contains("..")) {
            throw new IOException("bad custom driver name: " + name);
        }
        File source = new File(context.getExternalFilesDir(null), "drivers/" + name);
        File[] files = source.listFiles();
        if (files == null || files.length == 0) {
            throw new IOException("no custom driver files in " + source);
        }
        File destination = new File(new File(context.getFilesDir(), "drivers"), "custom-" + name);
        deleteRecursively(destination);
        if (!destination.mkdirs()) throw new IOException("cannot create " + destination);
        for (File file : files) {
            if (!file.isFile()) continue;
            try (InputStream in = new FileInputStream(file);
                 OutputStream out = new FileOutputStream(new File(destination, file.getName()))) {
                copy(in, out);
            }
        }
        return destination;
    }

    /**
     * Obtiene el nombre del archivo `.so` principal dentro de un paquete de driver personalizado.
     * - Qué hace: Lee `libraryName` desde `meta.json` o busca el único archivo `.so` de la carpeta.
     * - Cómo funciona: Parsea el JSON del paquete AdrenoTools en memoria.
     * - Por qué: Permite cargar drivers Turnip/Qualcomm de distintas versiones sin nombres fijos.
     */
    static String customLibrary(File dir) throws IOException {
        File meta = new File(dir, META);
        if (meta.isFile()) {
            try (InputStream in = new FileInputStream(meta)) {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                copy(in, bytes);
                String name = new JSONObject(bytes.toString("UTF-8")).optString("libraryName", "");
                if (!name.isEmpty() && new File(dir, name).isFile()) return name;
            } catch (JSONException ignored) {}
        }
        File[] libraries = dir.listFiles((d, n) -> n.endsWith(".so"));
        if (libraries == null || libraries.length != 1) {
            throw new IOException("cannot pick the driver library in " + dir);
        }
        return libraries[0].getName();
    }

    /**
     * Extrae el paquete Turnip incluido en los assets del APK si aún no está validado en caché.
     * - Qué hace: Descomprime `turnip-r6.zip` y verifica los hashes SHA-256 de sus miembros.
     * - Cómo funciona: Usa un directorio temporal por PID y lo renombra atómicamente al finalizar.
     * - Por qué: Evita corromper el driver si el proceso se cierra a mitad de la primera extracción.
     */
    static File prepareBundle(Context context) throws IOException {
        File parent = new File(context.getFilesDir(), "drivers");
        File destination = new File(parent, "r6-" + LIB_HASH.substring(0, 12));
        if (valid(new File(destination, LIBRARY), LIB_HASH)
                && valid(new File(destination, META), META_HASH)) {
            return destination;
        }
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("cannot create " + parent);
        File archive = File.createTempFile("turnip-r6-", ".zip", context.getCacheDir());
        File staging = new File(parent, ".r6-staging-" + Process.myPid());
        try {
            try (InputStream in = context.getAssets().open(BUNDLE_ASSET);
                 OutputStream out = new FileOutputStream(archive)) {
                copy(in, out);
            }
            if (!ZIP_HASH.equals(digest(archive))) {
                throw new IOException("bundled Turnip archive failed integrity check");
            }
            deleteRecursively(staging);
            if (!staging.mkdirs()) throw new IOException("cannot create " + staging);
            try (ZipFile zip = new ZipFile(archive)) {
                Set<String> names = new HashSet<>();
                for (Enumeration<? extends ZipEntry> e = zip.entries(); e.hasMoreElements(); ) {
                    names.add(e.nextElement().getName());
                }
                if (!names.contains(META) || !names.contains(LIBRARY) || names.size() != 2) {
                    throw new IOException("unexpected Turnip archive members: " + names);
                }
                extract(zip, META, new File(staging, META), META_HASH);
                extract(zip, LIBRARY, new File(staging, LIBRARY), LIB_HASH);
            }
            deleteRecursively(destination);
            if (!staging.renameTo(destination)) throw new IOException("cannot activate " + destination);
            return destination;
        } finally {
            if (!archive.delete()) archive.deleteOnExit();
            deleteRecursively(staging);
        }
    }

    private static void extract(ZipFile zip, String name, File target, String hash) throws IOException {
        try (InputStream in = zip.getInputStream(zip.getEntry(name));
             OutputStream out = new FileOutputStream(target)) {
            copy(in, out);
        }
        if (!valid(target, hash)) throw new IOException(name + " failed integrity check");
        if (!target.setReadOnly()) throw new IOException("cannot protect " + target);
    }

    private static boolean valid(File file, String hash) {
        try { return file.isFile() && hash.equals(digest(file)); }
        catch (IOException e) { return false; }
    }

    private static String digest(File file) throws IOException {
        try {
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            try (InputStream in = new FileInputStream(file)) {
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0) sha.update(buffer, 0, read);
            }
            StringBuilder hex = new StringBuilder();
            for (byte b : sha.digest()) hex.append(String.format("%02x", b & 0xff));
            return hex.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new IOException(e);
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        int read;
        while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (file.exists()) {
            file.setWritable(true);
            file.delete();
        }
    }
}
