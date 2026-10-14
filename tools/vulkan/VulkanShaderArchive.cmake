include_guard(GLOBAL)

# The stock cache is read-only input. All temporal variants live in a separate
# archive, generated and validated offline with the native override compilers.
function(liberty_add_vulkan_temporal_archive)
    if(TARGET rexglue_fidelityfx_vulkan OR TARGET rexglue_dlss_sdk)
        if(NOT GTA4_NATIVE_DXC OR NOT EXISTS "${GTA4_NATIVE_DXC}" OR
           NOT GTA4_NATIVE_SPIRV_VAL OR NOT GTA4_NATIVE_GLSLANG_VALIDATOR)
            message(FATAL_ERROR
                "Vulkan temporal reconstruction requires host DXC, spirv-val, and glslangValidator")
        endif()
    endif()
    set(builder "${LIBERTY_RECOMP_WORKSPACE_ROOT}/tools/vulkan/temporal_shader_variants.py")
    set(archive_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/gta4_native/temporal")
    set(archive "${archive_dir}/vulkan-temporal.bin")
    file(GLOB_RECURSE override_inputs CONFIGURE_DEPENDS
        "${GTA4_NATIVE_SHADER_OVERRIDE_ROOT}/*.hlsl"
        "${GTA4_NATIVE_SHADER_OVERRIDE_ROOT}/*.hlsli"
        "${GTA4_NATIVE_SHADER_OVERRIDE_ROOT}/*.h"
        "${GTA4_NATIVE_SHADER_OVERRIDE_ROOT}/*.glsl")
    set(compiler_inputs "${GTA4_NATIVE_DXC}")
    if(CMAKE_HOST_WIN32 AND NOT GTA4_NATIVE_DXC_LIBRARY_PATH)
        get_filename_component(GTA4_NATIVE_DXC_LIBRARY_PATH "${GTA4_NATIVE_DXC}" DIRECTORY)
    endif()
    foreach(name IN ITEMS libdxcompiler.dylib libdxcompiler.so dxcompiler.dll)
        if(EXISTS "${GTA4_NATIVE_DXC_LIBRARY_PATH}/${name}")
            list(APPEND compiler_inputs "${GTA4_NATIVE_DXC_LIBRARY_PATH}/${name}")
        endif()
    endforeach()
    set(command "${Python3_EXECUTABLE}" "${builder}"
        --work "${archive_dir}/work" --output "${archive}" --jobs 4)
    if(GTA4_NATIVE_DXC)
        list(APPEND command --dxc "${GTA4_NATIVE_DXC}")
    endif()
    if(GTA4_NATIVE_DXC_LIBRARY_PATH)
        list(APPEND command --dxc-library-path "${GTA4_NATIVE_DXC_LIBRARY_PATH}")
    endif()
    if(GTA4_NATIVE_SPIRV_VAL)
        list(APPEND command --spirv-val "${GTA4_NATIVE_SPIRV_VAL}")
        list(APPEND compiler_inputs "${GTA4_NATIVE_SPIRV_VAL}")
    endif()
    if(GTA4_NATIVE_GLSLANG_VALIDATOR)
        list(APPEND command --glslang "${GTA4_NATIVE_GLSLANG_VALIDATOR}")
        list(APPEND compiler_inputs "${GTA4_NATIVE_GLSLANG_VALIDATOR}")
    endif()
    # ctypes must load a host-architecture shared library. On Apple Silicon,
    # /usr/local can contain an Intel installation alongside native Homebrew.
    if(CMAKE_HOST_WIN32)
        find_file(GTA4_TEMPORAL_ZSTD_LIBRARY NAMES zstd.dll libzstd.dll
            HINTS "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin"
            PATHS ENV PATH)
    elseif(CMAKE_HOST_APPLE AND GTA4_NATIVE_DXC_ARCH STREQUAL "arm64")
        find_library(GTA4_TEMPORAL_ZSTD_LIBRARY NAMES zstd
            PATHS /opt/homebrew/lib NO_DEFAULT_PATH)
    else()
        find_library(GTA4_TEMPORAL_ZSTD_LIBRARY NAMES zstd libzstd)
    endif()
    if(GTA4_TEMPORAL_ZSTD_LIBRARY MATCHES "\\.(a|lib)$")
        if(TARGET rexglue_fidelityfx_vulkan OR TARGET rexglue_dlss_sdk)
            message(FATAL_ERROR
                "Set GTA4_TEMPORAL_ZSTD_LIBRARY to a host shared zstd library; Python cannot load a static/import archive")
        endif()
        set(GTA4_TEMPORAL_ZSTD_LIBRARY "")
    endif()
    if(GTA4_TEMPORAL_ZSTD_LIBRARY)
        list(APPEND command --zstd-library "${GTA4_TEMPORAL_ZSTD_LIBRARY}")
        list(APPEND compiler_inputs "${GTA4_TEMPORAL_ZSTD_LIBRARY}")
    endif()
    add_custom_command(OUTPUT "${archive}"
        BYPRODUCTS "${archive}.json"
        COMMAND ${command}
        DEPENDS "${builder}" ${compiler_inputs} ${override_inputs}
            "${GTA4_NATIVE_SHADER_CACHE_ROOT}/shader/shader_cache.cpp"
            "${GTA4_NATIVE_SHADER_OVERRIDE_MANIFEST}"
            "${GTA4_NATIVE_SHADER_OVERRIDE_COMPILER}"
            "${REXGLUE_ROOT}/src/graphics/gta4_native/alpha_to_coverage_util.h"
            "${LIBERTY_RECOMP_WORKSPACE_ROOT}/tools/metal/shader_archive.py"
            "${LIBERTY_RECOMP_WORKSPACE_ROOT}/tools/recover_shader_sources.py"
            "${LIBERTY_RECOMP_WORKSPACE_ROOT}/tools/validate_shader_preservation.py"
            "${LIBERTY_RECOMP_WORKSPACE_ROOT}/tools/validate_shader_cache_candidate.py"
        COMMENT "Compiling and validating the complete additive Vulkan temporal shader archive"
        VERBATIM)
    add_custom_target(liberty_vulkan_temporal_archive DEPENDS "${archive}")
    set_property(GLOBAL PROPERTY LIBERTY_VULKAN_TEMPORAL_ARCHIVE "${archive}")
endfunction()

function(liberty_stage_vulkan_temporal_archive app_target)
    if(NOT TARGET rexglue_fidelityfx_vulkan AND NOT TARGET rexglue_dlss_sdk)
        return()
    endif()
    get_property(archive GLOBAL PROPERTY LIBERTY_VULKAN_TEMPORAL_ARCHIVE)
    if(NOT archive OR NOT TARGET liberty_vulkan_temporal_archive)
        message(FATAL_ERROR "Temporal SDKs require the complete Vulkan shader archive")
    endif()
    add_dependencies("${app_target}" liberty_vulkan_temporal_archive)
    set(frontend "${REXSDK_DIR}/gta4-recomp/src/gta4_frontend_hooks.cpp")
    if(TARGET rexglue_fidelityfx_vulkan)
        set_property(SOURCE "${frontend}" TARGET_DIRECTORY "${app_target}"
            APPEND PROPERTY COMPILE_DEFINITIONS LIBERTY_HAS_FSR3=1)
    endif()
    if(TARGET rexglue_dlss_sdk)
        set_property(SOURCE "${frontend}" TARGET_DIRECTORY "${app_target}"
            APPEND PROPERTY COMPILE_DEFINITIONS LIBERTY_HAS_DLSS=1)
    endif()
    set_property(TARGET "${app_target}" APPEND PROPERTY LINK_DEPENDS "${archive}")
    get_target_property(bundle "${app_target}" MACOSX_BUNDLE)
    if(bundle)
        # CMake copies resources before the existing bundle-signing commands.
        set_source_files_properties("${archive}" PROPERTIES GENERATED TRUE
            MACOSX_PACKAGE_LOCATION "Resources/shaders")
        target_sources("${app_target}" PRIVATE "${archive}")
    else()
        add_custom_command(TARGET "${app_target}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${app_target}>/shaders"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${archive}"
                "$<TARGET_FILE_DIR:${app_target}>/shaders/vulkan-temporal.bin"
            VERBATIM)
    endif()
    install(FILES "${archive}" DESTINATION "${CMAKE_INSTALL_BINDIR}/shaders")
endfunction()
