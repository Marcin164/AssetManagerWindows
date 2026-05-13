cmake_minimum_required(VERSION 3.20)
project(lanventory-agent CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "lanventory-agent is Windows-only.")
endif()

# Static-link the CRT so the .exe drops on any Windows 10+ host without
# needing vcredist installed.
if(MSVC)
    foreach(flag CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
                 CMAKE_C_FLAGS   CMAKE_C_FLAGS_DEBUG   CMAKE_C_FLAGS_RELEASE)
        string(REPLACE "/MD"  "/MT"  ${flag} "${${flag}}")
        string(REPLACE "/MDd" "/MTd" ${flag} "${${flag}}")
    endforeach()
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX
                            _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()

# ── Dependencies ──────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann/json -- header-only, vendored via FetchContent so the build
# is self-contained on a clean checkout.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Target ────────────────────────────────────────────────────────────
add_executable(lanventory-agent
    src/main.cpp
    src/log.cpp
    src/wmi.cpp
    src/winhttp_client.cpp
    src/crypto.cpp
    src/dpapi.cpp
    src/config.cpp
    src/fingerprint.cpp
    src/enrollment.cpp
    src/transport.cpp
    src/task_scheduler.cpp
    src/scanner/system.cpp
    src/scanner/hardware.cpp
    src/scanner/software.cpp
    src/scanner/network.cpp
    src/scanner/security.cpp
    src/scanner/peripherals.cpp
    src/scanner/events.cpp
    src/scanner/users.cpp
)

target_include_directories(lanventory-agent PRIVATE include)
target_link_libraries(lanventory-agent PRIVATE
    nlohmann_json::nlohmann_json
    winhttp
    crypt32
    bcrypt
    wbemuuid
    oleaut32
    ole32
    iphlpapi
    advapi32
    shell32
    user32
    ws2_32
    rpcrt4
)

set_target_properties(lanventory-agent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
