# Native Metal presentation shares one device/queue with the selected title renderer.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(METAL_UI_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(METAL_UI_LIBRARY "${METAL_UI_GENERATED}/metal_ui_library.h")
add_custom_command(
    OUTPUT "${METAL_UI_LIBRARY}"
    COMMAND "${Python3_EXECUTABLE}" "${REXGLUE_ROOT}/tools/compile_metal_library.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.metal"
        --output "${METAL_UI_LIBRARY}" --symbol kMetalUiLibrary
        --sdk macosx --deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    DEPENDS shaders/ui.metal "${REXGLUE_ROOT}/tools/compile_metal_library.py"
    VERBATIM
    COMMENT "Compiling native Metal UI shaders")
# rexui is created in the parent directory. Give this directory's generated
# header an explicit target so its rule is emitted and runs before consumers.
add_custom_target(rexui_metal_library DEPENDS "${METAL_UI_LIBRARY}")
add_dependencies(rexui rexui_metal_library)
set(METAL_UI_SOURCES
    context.mm frame_ring.mm upload_arena.mm provider.mm presenter.mm immediate_drawer.mm
    presentation_effects.mm output_transfer.mm presentation_image_cache.mm)
list(TRANSFORM METAL_UI_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set_source_files_properties(${METAL_UI_SOURCES} TARGET_DIRECTORY rexui
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_sources(rexui PRIVATE ${METAL_UI_SOURCES} "${METAL_UI_LIBRARY}")
target_include_directories(rexui PRIVATE "${METAL_UI_GENERATED}")
target_link_libraries(rexui PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation")

# Translate the active Vulkan reference passes offline, never during rendering.
find_program(REX_METAL_SPIRV_CROSS NAMES spirv-cross REQUIRED)
find_program(REX_METAL_GLSLANG NAMES glslangValidator REQUIRED)
set(METAL_PASS_LIBRARY "${METAL_UI_GENERATED}/passes.metallib")
file(GLOB METAL_PRESENT_INPUTS CONFIGURE_DEPENDS
    "${REXGLUE_ROOT}/src/ui/shaders/vulkan_spirv/guest_output_*_ps.h")
file(GLOB_RECURSE METAL_NATIVE_PASS_INPUTS CONFIGURE_DEPENDS
    "${REXGLUE_ROOT}/src/graphics/gta4_native/*.glsl")
add_custom_command(OUTPUT "${METAL_PASS_LIBRARY}"
    COMMAND "${Python3_EXECUTABLE}" "${LIBERTY_RECOMP_WORKSPACE_ROOT}/tools/metal/compile_passes.py"
        --repo "${LIBERTY_RECOMP_WORKSPACE_ROOT}" --output "${METAL_PASS_LIBRARY}"
        --work "${METAL_UI_GENERATED}/passes" --deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
        --spirv-cross "${REX_METAL_SPIRV_CROSS}" --glslang "${REX_METAL_GLSLANG}"
    DEPENDS "${LIBERTY_RECOMP_WORKSPACE_ROOT}/tools/metal/compile_passes.py"
        ${METAL_PRESENT_INPUTS} ${METAL_NATIVE_PASS_INPUTS}
        "${LIBERTY_RECOMP_WORKSPACE_ROOT}/LibertyRecompLib/postfx/smaa/SMAA.hlsl"
    COMMENT "Compiling Metal presentation and native utility passes" VERBATIM)
add_custom_target(rexui_metal_passes DEPENDS "${METAL_PASS_LIBRARY}")
add_dependencies(rexui rexui_metal_passes)
set_property(GLOBAL PROPERTY REX_METAL_PASSES_LIBRARY "${METAL_PASS_LIBRARY}")
