# rexglue_fidelityfx.cmake — Optional AMD FidelityFX integration via FetchContent
#
# Expects REXGLUE_ENABLE_FIDELITYFX to be set before inclusion.
# On success, creates amd_fidelityfx_vk and/or amd_fidelityfx_dx12 targets
# and sets REXGLUE_FIDELITYFX_SOURCE_DIR to the fetched SDK root.

if(NOT REXGLUE_ENABLE_FIDELITYFX)
    return()
endif()

# Generated overlay paths are SDK dependency inputs. Preserve their timestamps
# across ordinary reconfiguration so Ninja keeps shader permutations up to date.
function(_rexglue_ffx_write_if_changed path content)
    if(EXISTS "${path}")
        file(READ "${path}" existing)
        if("${existing}" STREQUAL "${content}")
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
endfunction()

# ── Dependency validation ────────────────────────────────────────────────
if(NOT WIN32)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(WARNING
            "REXGLUE_ENABLE_FIDELITYFX requires the Vulkan SDK but it was not found.\n"
            "Install the LunarG Vulkan SDK: https://vulkan.lunarg.com/sdk/home\n"
            "Disabling FidelityFX.")
        set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "" FORCE)
        return()
    elseif(Vulkan_VERSION VERSION_LESS "1.3.250")
        message(WARNING
            "REXGLUE_ENABLE_FIDELITYFX requires Vulkan SDK >= 1.3.250 "
            "(found ${Vulkan_VERSION}).\n"
            "Update via: https://vulkan.lunarg.com/sdk/home\n"
            "Disabling FidelityFX.")
        set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "" FORCE)
        return()
    endif()
    # This pinned SDK generates permutation headers with its bundled Windows
    # FidelityFX_SC/glslangValidator tools. A system glslc does not replace the
    # permutation compiler. The fork explicitly supports running it via Wine.
    find_program(FFX_WINE_EXECUTABLE NAMES wine wine64)
    if(NOT FFX_WINE_EXECUTABLE)
        message(FATAL_ERROR
            "The pinned FidelityFX SDK requires Wine for shader permutation generation "
            "on non-Windows hosts. Install Wine or disable REXGLUE_ENABLE_FIDELITYFX.")
    endif()
endif()

# ── Fetch FidelityFX SDK ─────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    fidelityfx
    GIT_REPOSITORY https://github.com/rexglue/FidelityFX-SDK.git
    GIT_TAG        eee08db1688ac3d1275a70b728f4a8ba22914213
    GIT_SHALLOW    OFF
)
FetchContent_GetProperties(fidelityfx)
if(NOT fidelityfx_POPULATED)
    FetchContent_Populate(fidelityfx)
endif()

if(NOT WIN32)
    # Ninja's shell expands the SDK's unquoted -DOPTION={0,1} into two
    # definitions. VERBATIM alone does not escape braces on Unix. Overlay this build
    # file and symlink the remaining pinned tree; leave FetchContent pristine.
    function(_rexglue_ffx_overlay source destination changed_file)
        file(MAKE_DIRECTORY "${destination}")
        string(REGEX MATCH "^[^/]+" changed_head "${changed_file}")
        set(changed_tail "")
        if(changed_file MATCHES "^[^/]+/(.*)$")
            set(changed_tail "${CMAKE_MATCH_1}")
        endif()
        file(GLOB entries RELATIVE "${source}" "${source}/*")
        foreach(entry IN LISTS entries)
            if(entry STREQUAL changed_head)
                if(changed_tail)
                    _rexglue_ffx_overlay("${source}/${entry}" "${destination}/${entry}" "${changed_tail}")
                endif()
            elseif(NOT EXISTS "${destination}/${entry}")
                file(CREATE_LINK "${source}/${entry}" "${destination}/${entry}" SYMBOLIC)
            endif()
        endforeach()
    endfunction()
    set(_rexglue_ffx_shader_build "sdk/include/FidelityFX/gpu/CMakeCompileShaders.txt")
    set(_rexglue_ffx_portable_source "${fidelityfx_BINARY_DIR}/portable-source")
    _rexglue_ffx_overlay("${fidelityfx_SOURCE_DIR}" "${_rexglue_ffx_portable_source}"
        "${_rexglue_ffx_shader_build}")
    file(READ "${fidelityfx_SOURCE_DIR}/${_rexglue_ffx_shader_build}" _rexglue_ffx_shader_commands)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    string(REPLACE "set(_FFX_PERMUTATION_OUTPUTS)"
        "set(_FFX_PERMUTATION_OUTPUTS)\nset(EXECUTABLE \"${Python3_EXECUTABLE}\" \"${CMAKE_CURRENT_LIST_DIR}/fidelityfx_shader_compiler.py\" \${EXECUTABLE})"
        _rexglue_ffx_shader_commands "${_rexglue_ffx_shader_commands}")
    string(REPLACE "set(SC_ARGS \${BASE_ARGS} \${API_BASE_ARGS} \${PERMUTATION_ARGS})"
        "set(SC_ARGS \${BASE_ARGS} \${API_BASE_ARGS} \${PERMUTATION_ARGS})\nstring(REPLACE \"{\" \"@FFX_OPEN@\" SC_ARGS \"\${SC_ARGS}\")\nstring(REPLACE \"}\" \"@FFX_CLOSE@\" SC_ARGS \"\${SC_ARGS}\")"
        _rexglue_ffx_shader_commands "${_rexglue_ffx_shader_commands}")
    string(REPLACE "add_custom_command(" "add_custom_command(VERBATIM"
        _rexglue_ffx_shader_commands "${_rexglue_ffx_shader_commands}")
    string(REPLACE "DEPENDS \${PASS_SHADER}"
        "DEPENDS \${PASS_SHADER} \"${CMAKE_CURRENT_LIST_DIR}/fidelityfx_shader_compiler.py\""
        _rexglue_ffx_shader_commands "${_rexglue_ffx_shader_commands}")
    _rexglue_ffx_write_if_changed("${_rexglue_ffx_portable_source}/${_rexglue_ffx_shader_build}"
        "${_rexglue_ffx_shader_commands}")
    set(fidelityfx_SOURCE_DIR "${_rexglue_ffx_portable_source}")
endif()

set(REXGLUE_FIDELITYFX_SOURCE_DIR "${fidelityfx_SOURCE_DIR}" CACHE INTERNAL
    "Root of the fetched FidelityFX SDK source tree")

# ── Backend selection ────────────────────────────────────────────────────
set(REXGLUE_FIDELITYFX_BACKEND "auto" CACHE STRING
    "FidelityFX backend to build (auto, vk, dx12)")
set_property(CACHE REXGLUE_FIDELITYFX_BACKEND PROPERTY STRINGS auto vk dx12)
set(_rexglue_fidelityfx_backend "${REXGLUE_FIDELITYFX_BACKEND}")
string(TOLOWER "${_rexglue_fidelityfx_backend}" _rexglue_fidelityfx_backend)

if(_rexglue_fidelityfx_backend STREQUAL "auto")
    if(WIN32 AND REXGLUE_USE_D3D12)
        set(_rexglue_fidelityfx_backend "dx12")
    elseif(REXGLUE_USE_VULKAN)
        set(_rexglue_fidelityfx_backend "vk")
    elseif(REXGLUE_USE_D3D12)
        set(_rexglue_fidelityfx_backend "dx12")
    endif()
endif()

if(_rexglue_fidelityfx_backend STREQUAL "vk")
    if(NOT REXGLUE_USE_VULKAN)
        message(FATAL_ERROR
            "REXGLUE_FIDELITYFX_BACKEND=vk requires REXGLUE_USE_VULKAN=ON")
    endif()
    set(FFX_API_BACKEND VK_X64 CACHE STRING "" FORCE)
elseif(_rexglue_fidelityfx_backend STREQUAL "dx12")
    if(NOT REXGLUE_USE_D3D12)
        message(FATAL_ERROR
            "REXGLUE_FIDELITYFX_BACKEND=dx12 requires REXGLUE_USE_D3D12=ON")
    endif()
    set(FFX_API_BACKEND DX12_X64 CACHE STRING "" FORCE)
elseif(_rexglue_fidelityfx_backend STREQUAL "")
    message(FATAL_ERROR "FidelityFX requires a supported graphics backend")
else()
    message(FATAL_ERROR
        "Invalid REXGLUE_FIDELITYFX_BACKEND='${REXGLUE_FIDELITYFX_BACKEND}' "
        "(expected auto, vk, or dx12)")
endif()

if(REXGLUE_USE_VULKAN AND REXGLUE_USE_D3D12)
    message(STATUS
        "FidelityFX backend selected for this build: ${_rexglue_fidelityfx_backend}")
endif()

# ── Build FidelityFX ─────────────────────────────────────────────────────
option(REXGLUE_FIDELITYFX_FRAME_GENERATION "Build the FidelityFX frame-generation provider" ON)
set(FFX_API_ENABLE_FRAMEGEN_PROVIDER ${REXGLUE_FIDELITYFX_FRAME_GENERATION} CACHE BOOL "" FORCE)
# Never build a runtime whose required shader permutation headers are absent.
set(FFX_API_AUTO_COMPILE_SHADERS ON CACHE BOOL "" FORCE)
add_subdirectory("${fidelityfx_SOURCE_DIR}/ffx-api" "${fidelityfx_BINARY_DIR}/ffx-api" EXCLUDE_FROM_ALL)

if(TARGET amd_fidelityfx_vk AND REXGLUE_FIDELITYFX_FRAME_GENERATION AND NOT WIN32)
    # The SDK's Vulkan proxy swapchain is Win32-only. Direct interpolation is
    # portable and uses NO_SWAPCHAIN_CONTEXT_NOTIFY. Build the real interpolation
    # provider while excluding only the unsupported proxy from the registry.
    # Generate an overlay rather than modifying the pinned dependency checkout.
    set(_rexglue_ffx_provider "${fidelityfx_SOURCE_DIR}/ffx-api/src/ffx_provider.cpp")
    file(READ "${_rexglue_ffx_provider}" _rexglue_ffx_provider_source)
    string(REPLACE "#ifdef FFX_BACKEND_VK"
        "#if defined(FFX_BACKEND_VK) && defined(_WIN32)"
        _rexglue_ffx_provider_source "${_rexglue_ffx_provider_source}")
    set(_rexglue_ffx_provider_overlay "${fidelityfx_BINARY_DIR}/ffx_provider_direct_vk.cpp")
    _rexglue_ffx_write_if_changed("${_rexglue_ffx_provider_overlay}" "${_rexglue_ffx_provider_source}")
    get_target_property(_rexglue_ffx_sources amd_fidelityfx_vk SOURCES)
    list(REMOVE_ITEM _rexglue_ffx_sources "${_rexglue_ffx_provider}")
    list(FILTER _rexglue_ffx_sources EXCLUDE REGEX ".*/ffx_provider_framegenerationswapchain_vk\\.(h|cpp)$")
    list(APPEND _rexglue_ffx_sources "${_rexglue_ffx_provider_overlay}")
    set_property(TARGET amd_fidelityfx_vk PROPERTY SOURCES "${_rexglue_ffx_sources}")
    target_include_directories(amd_fidelityfx_vk PRIVATE "${fidelityfx_SOURCE_DIR}/ffx-api/src")
endif()

# The upstream FidelityFX targets expose source-tree include paths in
# INTERFACE_INCLUDE_DIRECTORIES, which breaks our install export checks.
# We only link against these targets internally, so no public includes are
# needed on the exported interface.
if(TARGET amd_fidelityfx_vk)
    set_target_properties(amd_fidelityfx_vk PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ""
    )
    # An internal interface target gives renderer adapters the correct headers
    # and provider availability without exporting the SDK's source paths.
    add_library(rexglue_fidelityfx_vulkan INTERFACE)
    target_link_libraries(rexglue_fidelityfx_vulkan INTERFACE amd_fidelityfx_vk)
    target_include_directories(rexglue_fidelityfx_vulkan INTERFACE
        "$<BUILD_INTERFACE:${fidelityfx_SOURCE_DIR}/ffx-api/include>")
    target_compile_definitions(rexglue_fidelityfx_vulkan INTERFACE REX_HAS_FIDELITYFX_VULKAN=1)
    if(REXGLUE_FIDELITYFX_FRAME_GENERATION)
        target_compile_definitions(rexglue_fidelityfx_vulkan INTERFACE REX_HAS_FIDELITYFX_FRAMEGENERATION=1)
    endif()
endif()
if(TARGET amd_fidelityfx_dx12)
    set_target_properties(amd_fidelityfx_dx12 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ""
    )
endif()

# FidelityFX's toolchain.cmake force-sets CMAKE_GENERATOR_PLATFORM (for VS generators).
# With Ninja this variable is invalid and poisons every subsequent try_compile() call.
# Clear it from the cache.
unset(CMAKE_GENERATOR_PLATFORM CACHE)
