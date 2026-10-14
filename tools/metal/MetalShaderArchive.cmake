# Opt-in integration. Include from the existing macOS application CMake path
# after declaring its target; do not create a second application build tree.
function(liberty_add_metal_shader_archive app_target workspace_root)
    if(NOT APPLE OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        message(FATAL_ERROR "This initial shader archive target is for native macOS builds")
    endif()
    if(NOT TARGET "${app_target}")
        message(FATAL_ERROR "Metal archive application target does not exist: ${app_target}")
    endif()
    get_target_property(is_bundle "${app_target}" MACOSX_BUNDLE)
    if(NOT is_bundle)
        message(FATAL_ERROR "Metal shader archive staging requires the actual macOS app target")
    endif()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(archive_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/gta4_metal")
    set(archive "${archive_dir}/title_shader_archive.bin")
    set(builder "${workspace_root}/tools/metal/shader_archive.py")
    set(stock_cache "${workspace_root}/LibertyRecompLib/shader/shader_cache.cpp")
    set(coverage "${workspace_root}/glue/rexglue-sdk-main/src/graphics/gta4_native/alpha_to_coverage_util.h")
    foreach(required IN ITEMS "${builder}" "${stock_cache}" "${coverage}")
        if(NOT EXISTS "${required}")
            message(FATAL_ERROR "Missing Metal archive source: ${required}")
        endif()
    endforeach()
    add_custom_command(
        OUTPUT "${archive}"
        COMMAND "${Python3_EXECUTABLE}" "${builder}"
                --repo "${workspace_root}" --output "${archive}"
                --work "${archive_dir}/work" --jobs 2
        DEPENDS "${builder}" "${stock_cache}" "${coverage}"
                "${workspace_root}/tools/recover_shader_sources.py"
                "${workspace_root}/tools/validate_shader_preservation.py"
                "${workspace_root}/tools/validate_shader_cache_candidate.py"
        COMMENT "Compiling complete Metal shader variants without modifying Vulkan shaders"
        VERBATIM
    )
    add_custom_target(liberty_metal_shader_archive DEPENDS "${archive}")
    add_dependencies("${app_target}" liberty_metal_shader_archive)
    # A file dependency restages changed archives even when C++ has not changed.
    set_property(TARGET "${app_target}" APPEND PROPERTY LINK_DEPENDS "${archive}")
    # Bundle resources are staged by CMake before the existing signing steps.
    # Appending a post-build copy after signing would invalidate the signature.
    set_source_files_properties("${archive}" PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION "Resources/metal"
    )
    target_sources("${app_target}" PRIVATE "${archive}")

    # Consume the same validated override SPIR-V built for the maintained
    # renderer. SPIRV-Cross exists only in this offline executable.
    add_executable(liberty-metal-shader-translator EXCLUDE_FROM_ALL
        "${workspace_root}/tools/metal/spirv_to_metal.cpp"
        "${workspace_root}/tools/metal/color_output_transform.cpp")
    target_include_directories(liberty-metal-shader-translator PRIVATE
        "${workspace_root}/glue/rexglue-sdk-main/src/graphics/gta4_native")
    target_link_libraries(liberty-metal-shader-translator PRIVATE
        spirv-cross-msl spirv-cross-glsl spirv-cross-core SPIRV-Headers::SPIRV-Headers)
    set(override_cache "${CMAKE_BINARY_DIR}/glue/rexglue-sdk/src/graphics/generated/gta4_native/shader_override_cache.cpp")
    set(override_archive "${archive_dir}/override_shader_archive.bin")
    set(override_header "${archive_dir}/override_metadata.h")
    add_custom_command(OUTPUT "${override_archive}" "${override_header}"
        COMMAND "${Python3_EXECUTABLE}" "${workspace_root}/tools/metal/override_archive.py"
            --repo "${workspace_root}" --cache "${override_cache}"
            --compiler "$<TARGET_FILE:liberty-metal-shader-translator>"
            --output "${override_archive}" --header "${override_header}"
            --work "${archive_dir}/overrides"
        DEPENDS liberty-metal-shader-translator "${override_cache}"
            "${workspace_root}/tools/metal/override_archive.py" "${builder}"
            "${workspace_root}/LibertyRecompLib/shader_overrides/manifest.json"
        COMMENT "Compiling native Metal shader overrides" VERBATIM)
    add_custom_target(liberty_metal_shader_overrides DEPENDS "${override_archive}" "${override_header}")
    add_dependencies("${app_target}" liberty_metal_shader_overrides)
    add_dependencies(rex-gta4-metal-renderer liberty_metal_shader_overrides)
    target_include_directories(rex-gta4-metal-renderer PRIVATE "${archive_dir}"
        "${workspace_root}/glue/rexglue-sdk-main/src/graphics/gta4_native")
    set_source_files_properties("${override_archive}" PROPERTIES
        GENERATED TRUE MACOSX_PACKAGE_LOCATION "Resources/metal")
    target_sources("${app_target}" PRIVATE "${override_archive}")
    set_property(TARGET "${app_target}" APPEND PROPERTY LINK_DEPENDS "${override_archive}")
    set(temporal_builder "${workspace_root}/tools/metal/temporal_shader_variants.py")
    foreach(kind IN ITEMS stock override)
        if(kind STREQUAL "stock")
            set(temporal_input "${archive}")
            set(temporal_sources "${archive_dir}/work/sources")
        else()
            set(temporal_input "${override_archive}")
            set(temporal_sources "${archive_dir}/overrides")
        endif()
        set(temporal_archive "${archive_dir}/${kind}_temporal_shader_archive.bin")
        add_custom_command(OUTPUT "${temporal_archive}"
            COMMAND "${Python3_EXECUTABLE}" "${temporal_builder}"
                --archive "${temporal_input}" --sources "${temporal_sources}"
                --work "${archive_dir}/${kind}-temporal-work" --output "${temporal_archive}"
                --jobs 2 --deployment "${CMAKE_OSX_DEPLOYMENT_TARGET}"
            DEPENDS "${temporal_input}" "${temporal_builder}" "${builder}"
            COMMENT "Compiling ${kind} motion and isolated-UI shader variants" VERBATIM)
        add_custom_target(liberty_${kind}_temporal_archive DEPENDS "${temporal_archive}")
        add_dependencies("${app_target}" liberty_${kind}_temporal_archive)
        set_source_files_properties("${temporal_archive}" PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION "Resources/metal")
        target_sources("${app_target}" PRIVATE "${temporal_archive}")
        set_property(TARGET "${app_target}" APPEND PROPERTY LINK_DEPENDS "${temporal_archive}")
    endforeach()
endfunction()
