cmake_minimum_required(VERSION 3.20)
project(StorageCleaner LANGUAGES CXX)

if(NOT WIN32)
    message(WARNING
        "Este projeto usa APIs do Windows (Win32, COM, Shell, VSS) e um backend "
        "grafico DirectX 11 - so pode ser compilado e executado em uma maquina "
        "Windows. Configurar aqui so serve para revisao de CMake, nao para build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.0
)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(imgui nlohmann_json)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

add_executable(StorageCleaner WIN32
    src/main.cpp
    src/Utils.cpp
    src/ProgressState.cpp
    src/Config.cpp
    src/ConfigStore.cpp
    src/HistoryStore.cpp
    src/DuplicateGrouping.cpp
    src/scanners/TempFilesScanner.cpp
    src/scanners/BrowserCacheScanner.cpp
    src/scanners/DuplicateScanner.cpp
    src/scanners/OldLogsScanner.cpp
    src/scanners/RecycleBinScanner.cpp
    src/scanners/RestorePointsScanner.cpp
    src/scanners/OrphanedAppDataScanner.cpp
    src/scanners/ScanEngine.cpp
    src/cleaner/Cleaner.cpp
    src/cleaner/RecycleBinOps.cpp
    src/cleaner/VssRestorePoints.cpp
    src/ui/App.cpp
    src/ui/Theme.cpp
    src/ui/UiState.cpp
    src/ui/TrayIcon.cpp
    src/ui/DashboardPanel.cpp
    src/ui/ResultsPanel.cpp
    src/ui/SettingsPanel.cpp
    src/ui/HistoryPanel.cpp
    resources/app.rc
)

target_include_directories(StorageCleaner PRIVATE include src resources)

# UNICODE/_UNICODE: todo o codigo usa APIs Win32 explicitamente wide (CreateFileW,
# RegOpenKeyExW etc.), mas macros genericas de recurso como IDI_APPLICATION dependem
# dessas defines para resolver para a variante wide (sem isso, LoadIconW recebe um
# LPSTR e nao compila). NOMINMAX evita que windows.h defina macros `max`/`min` que
# colidem com chamadas a std::max/std::min (ex.: UiState.cpp).
target_compile_definitions(StorageCleaner PRIVATE UNICODE _UNICODE NOMINMAX)

# /EHsc: sem essa flag o MSVC nao habilita semantica padrao de excecao C++
# (warning C4530 em todo arquivo que inclui <string>/<chrono>) e o try/catch
# real do projeto (ex.: parsing de JSON em HistoryStore.cpp) fica sem garantia
# de unwind correto.
if(MSVC)
    target_compile_options(StorageCleaner PRIVATE /EHsc)
endif()

target_link_libraries(StorageCleaner PRIVATE
    imgui
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    d3dcompiler
    shell32
    ole32
    oleaut32
    bcrypt
    crypt32
    shlwapi
)

enable_testing()
add_subdirectory(tests)
