include_guard(GLOBAL)

option(REXGLUE_ENABLE_DLSS "Build native Vulkan DLSS Super Resolution and frame generation" OFF)
set(REXGLUE_DLSS_SOURCE_DIR "" CACHE PATH "Unmodified NVIDIA DLSS v310.9.1 SDK directory")
set(REXGLUE_DLSS_RELEASE "310.9.1")
set(REXGLUE_DLSS_SOURCE_COMMIT "374959484e79a640feaba44c93ac8cfb0a03f5b5")

# Always compile the capability and adapter entry points so unsupported builds
# produce an explicit unavailable result instead of lying about feature support.
function(rexglue_enable_dlss target)
    if(NOT REXGLUE_USE_VULKAN)
        return()
    endif()
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/graphics/gta4_native/temporal/dlss_bootstrap.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/graphics/gta4_native/temporal/dlss_upscaler.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/graphics/gta4_native/temporal/dlss_frame_generation.cpp")
    if(TARGET rexglue_dlss_sdk)
        target_link_libraries(${target} PRIVATE rexglue_dlss_sdk)
        target_compile_definitions(${target} PRIVATE REX_HAS_DLSS_NGX=1)
    endif()
endfunction()

function(rexglue_stage_dlss_runtime target)
    if(NOT TARGET rexglue_dlss_sdk)
        return()
    endif()
    get_target_property(runtime_files rexglue_dlss_sdk REXGLUE_DLSS_RUNTIME_FILES)
    get_target_property(license_file rexglue_dlss_sdk REXGLUE_DLSS_LICENSE_FILE)
    foreach(runtime IN LISTS runtime_files)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime}"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${license_file}"
            "$<TARGET_FILE_DIR:${target}>/NVIDIA-DLSS-LICENSE.txt"
        VERBATIM)
endfunction()

if(NOT REXGLUE_ENABLE_DLSS)
    return()
endif()
include(GNUInstallDirs)
if(NOT REXGLUE_USE_VULKAN)
    message(FATAL_ERROR "REXGLUE_ENABLE_DLSS requires REXGLUE_USE_VULKAN")
endif()
if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux") OR ANDROID)
    message(FATAL_ERROR "The pinned NVIDIA DLSS runtime supports Windows and native Linux only")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(_rex_dlss_arch aarch64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
    set(_rex_dlss_arch x86_64)
else()
    message(FATAL_ERROR "No pinned NVIDIA DLSS package for ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(NOT REXGLUE_DLSS_SOURCE_DIR)
    include(FetchContent)
    # Shallow clone the release tag, then verify every consumed header/library
    # against immutable package hashes below. A moved tag fails verification.
    FetchContent_Declare(rexglue_dlss_source
        GIT_REPOSITORY https://github.com/NVIDIA/DLSS.git
        GIT_TAG v310.9.1
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(rexglue_dlss_source)
    set(REXGLUE_DLSS_SOURCE_DIR "${rexglue_dlss_source_SOURCE_DIR}" CACHE PATH "" FORCE)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/rexglue_dlss_hashes.cmake")
function(_rex_dlss_verify relative)
    set(path "${REXGLUE_DLSS_SOURCE_DIR}/${relative}")
    string(REGEX REPLACE "[/.-]" "_" key "${relative}")
    if(NOT EXISTS "${path}" OR NOT DEFINED REX_DLSS_SHA_${key})
        message(FATAL_ERROR "Missing pinned DLSS SDK file: ${path}")
    endif()
    file(SHA256 "${path}" hash)
    if(NOT hash STREQUAL REX_DLSS_SHA_${key})
        message(FATAL_ERROR "DLSS SDK file differs from v310.9.1 (${REXGLUE_DLSS_SOURCE_COMMIT}): ${path}")
    endif()
endfunction()

file(GLOB _rex_dlss_headers RELATIVE "${REXGLUE_DLSS_SOURCE_DIR}"
    "${REXGLUE_DLSS_SOURCE_DIR}/include/*.h")
foreach(header IN LISTS _rex_dlss_headers)
    _rex_dlss_verify("${header}")
endforeach()
_rex_dlss_verify("include/nvsdk_ngx_helpers_vk.h")
_rex_dlss_verify("include/nvsdk_ngx_helpers_dlssg_vk.h")
_rex_dlss_verify("LICENSE.txt")

add_library(rexglue_dlss_sdk STATIC IMPORTED GLOBAL)
if(WIN32)
    set(_rex_dlss_platform "Windows_${_rex_dlss_arch}")
    set(_rex_dlss_library_dir "lib/${_rex_dlss_platform}")
    if(_rex_dlss_arch STREQUAL "x86_64")
        string(APPEND _rex_dlss_library_dir "/x64")
    endif()
    if(NOT CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
        set(_rex_dlss_crt d)
    else()
        set(_rex_dlss_crt s)
    endif()
    set(_rex_dlss_release "${_rex_dlss_library_dir}/nvsdk_ngx_${_rex_dlss_crt}.lib")
    set(_rex_dlss_debug "${_rex_dlss_library_dir}/nvsdk_ngx_${_rex_dlss_crt}_dbg.lib")
    _rex_dlss_verify("${_rex_dlss_release}")
    _rex_dlss_verify("${_rex_dlss_debug}")
    set_target_properties(rexglue_dlss_sdk PROPERTIES
        IMPORTED_LOCATION "${REXGLUE_DLSS_SOURCE_DIR}/${_rex_dlss_release}"
        IMPORTED_LOCATION_DEBUG "${REXGLUE_DLSS_SOURCE_DIR}/${_rex_dlss_debug}")
    set(_rex_dlss_runtime_names nvngx_dlss.dll nvngx_dlssg.dll)
else()
    set(_rex_dlss_platform "Linux_${_rex_dlss_arch}")
    set(_rex_dlss_library "lib/${_rex_dlss_platform}/libnvsdk_ngx.a")
    _rex_dlss_verify("${_rex_dlss_library}")
    set_target_properties(rexglue_dlss_sdk PROPERTIES
        IMPORTED_LOCATION "${REXGLUE_DLSS_SOURCE_DIR}/${_rex_dlss_library}"
        INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS};stdc++")
    # NGX discovers versioned feature payloads beside the application. The FG
    # guide explicitly names libnvidia-ngx-dlssg.so.<ver>; NVIDIA's native Linux
    # deployment guide uses versioned SR filenames without an unversioned link:
    # https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/gaming.html#dlss-update-native
    set(_rex_dlss_runtime_names
        "libnvidia-ngx-dlss.so.${REXGLUE_DLSS_RELEASE}"
        "libnvidia-ngx-dlssg.so.${REXGLUE_DLSS_RELEASE}")
endif()
set_target_properties(rexglue_dlss_sdk PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${REXGLUE_DLSS_SOURCE_DIR}/include")

set(REXGLUE_DLSS_RUNTIME_FILES "")
foreach(name IN LISTS _rex_dlss_runtime_names)
    set(relative "lib/${_rex_dlss_platform}/rel/${name}")
    _rex_dlss_verify("${relative}")
    list(APPEND REXGLUE_DLSS_RUNTIME_FILES "${REXGLUE_DLSS_SOURCE_DIR}/${relative}")
endforeach()
set_target_properties(rexglue_dlss_sdk PROPERTIES
    REXGLUE_DLSS_RUNTIME_FILES "${REXGLUE_DLSS_RUNTIME_FILES}"
    REXGLUE_DLSS_LICENSE_FILE "${REXGLUE_DLSS_SOURCE_DIR}/LICENSE.txt")
install(FILES ${REXGLUE_DLSS_RUNTIME_FILES} DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${REXGLUE_DLSS_SOURCE_DIR}/LICENSE.txt"
    DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME NVIDIA-DLSS-LICENSE.txt)
message(STATUS "DLSS SDK ${REXGLUE_DLSS_RELEASE}: ${_rex_dlss_platform}; production SR + FG runtimes")
