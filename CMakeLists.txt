cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# Force use of the installed Windows SDK (avoids "SDK not found" errors from submodules)
if(WIN32)
    set(CMAKE_SYSTEM_VERSION "10.0.26100.0" CACHE STRING "Windows SDK version" FORCE)
    set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "10.0.26100.0" CACHE STRING "" FORCE)
endif()

project(ManagementPanel LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include(FetchContent)

# 0. JSON (nlohmann/json)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# --- Dependencies ---

# 1. Vulkan (Required for Client)
find_package(Vulkan)
if(NOT Vulkan_FOUND)
    message(WARNING "Vulkan SDK not found. Client target will not be built.")
endif()

# 2. GLFW
if(Vulkan_FOUND)
    if(WIN32)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.3.8
        )
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
    else()
        find_package(glfw3 QUIET)
        if(NOT glfw3_FOUND)
            # Try pkg-config or find_package(GLFW)
            find_package(GLFW QUIET)
        endif()
    endif()
endif()

# 3. ImGui
if(Vulkan_FOUND)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.89.9
    )
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
        add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        )
        target_include_directories(imgui PUBLIC 
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        if(TARGET glfw)
            target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
        else()
            target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
        endif()
    endif()
endif()

# 4. ENet
if(WIN32)
    FetchContent_Declare(
        enet
        GIT_REPOSITORY https://github.com/lsalzman/enet.git
        GIT_TAG v1.3.17
    )
    FetchContent_MakeAvailable(enet)
    # ENet doesn't export include dirs — add them explicitly
    if(TARGET enet)
        target_include_directories(enet PUBLIC
            $<BUILD_INTERFACE:${enet_SOURCE_DIR}/include>)
    endif()
else()
    find_path(ENET_INCLUDE_DIR NAMES enet/enet.h)
    find_library(ENET_LIBRARY NAMES enet)
    if(NOT ENET_INCLUDE_DIR OR NOT ENET_LIBRARY)
        message(FATAL_ERROR "System enet library not found. Install libenet-dev.")
    endif()
    add_library(enet_lib INTERFACE)
    target_include_directories(enet_lib INTERFACE ${ENET_INCLUDE_DIR})
    target_link_libraries(enet_lib INTERFACE ${ENET_LIBRARY})
endif()

# 5. SQLite3
if(WIN32)
    FetchContent_Declare(
        sqlite3
        URL https://www.sqlite.org/2023/sqlite-amalgamation-3420000.zip
    )
    FetchContent_GetProperties(sqlite3)
    if(NOT sqlite3_POPULATED)
        FetchContent_Populate(sqlite3)
        add_library(sqlite3_lib STATIC
            ${sqlite3_SOURCE_DIR}/sqlite3.c
        )
        target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
    endif()
else()
    find_path(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
    find_library(SQLITE3_LIBRARY NAMES sqlite3)
    if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
        message(FATAL_ERROR "System sqlite3 library not found. Install libsqlite3-dev.")
    endif()
    add_library(sqlite3_lib INTERFACE)
    target_include_directories(sqlite3_lib INTERFACE ${SQLITE3_INCLUDE_DIR})
    target_link_libraries(sqlite3_lib INTERFACE ${SQLITE3_LIBRARY})
endif()

# 6. libdatachannel (Required for Server, and Client voice in future)
# We can use deps/libdatachannel if it exists, on any platform!
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/libdatachannel/CMakeLists.txt")
    set(LIBDATACHANNEL_LOCAL_DIR "${CMAKE_SOURCE_DIR}/deps/libdatachannel")
    set(USE_OPENSSL ON  CACHE BOOL "" FORCE)
    set(USE_GNUTLS  OFF CACHE BOOL "" FORCE)
    set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)
    set(USE_LZ4     OFF CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON  CACHE BOOL "" FORCE)
    set(NO_TESTS    ON  CACHE BOOL "" FORCE)
    if(WIN32)
        # Point to installed OpenSSL-Win64 (ShiningLight.OpenSSL.Dev)
        if(NOT OPENSSL_ROOT_DIR)
            foreach(_ssl_dir
                    "C:/Program Files/OpenSSL-Win64"
                    "C:/Program Files/OpenSSL"
                    "C:/OpenSSL-Win64")
                if(EXISTS "${_ssl_dir}/include/openssl/ssl.h")
                    set(OPENSSL_ROOT_DIR "${_ssl_dir}" CACHE PATH "" FORCE)
                    break()
                endif()
            endforeach()
        endif()
    endif()
    add_subdirectory("${LIBDATACHANNEL_LOCAL_DIR}" libdatachannel EXCLUDE_FROM_ALL)
else()
    find_path(LIBDATACHANNEL_INCLUDE_DIR NAMES rtc/rtc.hpp)
    find_library(LIBDATACHANNEL_LIBRARY NAMES datachannel)
    if(NOT LIBDATACHANNEL_INCLUDE_DIR OR NOT LIBDATACHANNEL_LIBRARY)
        message(FATAL_ERROR "deps/libdatachannel not found and system libdatachannel not found.\nPlease run this in the project root:\n  git clone --depth 1 --recurse-submodules https://github.com/paullouisageneau/libdatachannel.git deps/libdatachannel")
    endif()
    add_library(datachannel_lib INTERFACE)
    target_include_directories(datachannel_lib INTERFACE ${LIBDATACHANNEL_INCLUDE_DIR})
    target_link_libraries(datachannel_lib INTERFACE ${LIBDATACHANNEL_LIBRARY})
endif()

# --- Define Source Files (Only existing files to avoid configuration errors) ---

set(CLIENT_SRC
    src/client/main.cpp
    src/client/App.cpp
    src/client/Auth.cpp
    src/client/Roles.cpp
    src/client/Utils.cpp
    src/client/Chat.cpp
    src/client/Voice.cpp
)

set(SERVER_SRC
    src/server/main.cpp
    src/server/DB.cpp
    src/server/Auth.cpp
    src/server/Roles.cpp
    src/server/Utils.cpp
    src/server/Chat.cpp
    src/server/Voice.cpp
)

# --- Define Targets ---

# Collect libdatachannel include dir for explicit use
if(WIN32)
    set(LIBDATACHANNEL_INCLUDE_HINT "${CMAKE_SOURCE_DIR}/deps/libdatachannel/include")
endif()

# Build Client if Vulkan is found
if(Vulkan_FOUND)
    add_executable(client WIN32 ${CLIENT_SRC})
    target_include_directories(client PRIVATE src/client)
    if(WIN32)
        target_include_directories(client PRIVATE
            ${LIBDATACHANNEL_INCLUDE_HINT})
        target_link_libraries(client PRIVATE
            Vulkan::Vulkan imgui enet sqlite3_lib
            datachannel nlohmann_json::nlohmann_json
            winmm)  # ENet requires winmm for timeGetTime/timeBeginPeriod/timeEndPeriod
    else()
        if(TARGET glfw)
            target_link_libraries(client PRIVATE
                Vulkan::Vulkan imgui glfw enet_lib sqlite3_lib
                nlohmann_json::nlohmann_json)
        else()
            target_link_libraries(client PRIVATE
                Vulkan::Vulkan imgui enet_lib sqlite3_lib
                nlohmann_json::nlohmann_json)
        endif()
    endif()
endif()

# Build Server
add_executable(server ${SERVER_SRC})
target_include_directories(server PRIVATE src/server)
if(WIN32)
    target_include_directories(server PRIVATE
        ${LIBDATACHANNEL_INCLUDE_HINT})
    target_link_libraries(server PRIVATE
        enet sqlite3_lib datachannel nlohmann_json::nlohmann_json
        winmm)  # ENet requires winmm
else()
    target_link_libraries(server PRIVATE
        enet_lib sqlite3_lib datachannel_lib nlohmann_json::nlohmann_json)
endif()

# Install Rules
install(TARGETS server DESTINATION bin)
if(Vulkan_FOUND)
    install(TARGETS client DESTINATION bin)
endif()

