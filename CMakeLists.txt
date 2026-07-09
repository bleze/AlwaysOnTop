cmake_minimum_required(VERSION 3.25)

project(AlwaysOnTop VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(AlwaysOnTopHook SHARED
    src/hook/dllmain.cpp
    src/hook/HookDll.cpp
)

target_include_directories(AlwaysOnTopHook PRIVATE include)
target_compile_definitions(AlwaysOnTopHook PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(AlwaysOnTopHook PRIVATE user32)

add_executable(AlwaysOnTop WIN32
    src/host/main.cpp
    resources/AlwaysOnTop.rc
)

target_include_directories(AlwaysOnTop PRIVATE include resources)
target_compile_definitions(AlwaysOnTop PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(AlwaysOnTop PRIVATE user32 shell32)

add_dependencies(AlwaysOnTop AlwaysOnTopHook)

add_custom_command(TARGET AlwaysOnTop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:AlwaysOnTopHook>
        $<TARGET_FILE_DIR:AlwaysOnTop>
    COMMENT "Copy AlwaysOnTopHook.dll next to AlwaysOnTop.exe"
)