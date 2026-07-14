cmake_minimum_required(VERSION 3.24)

project(PseudoJoy VERSION 1.1.0 LANGUAGES CXX RC)

add_executable(PseudoJoy WIN32
    src/main.cpp
    src/AppWindow.cpp
    src/AppWindow.h
    src/InputEngine.cpp
    src/InputEngine.h
    res/PseudoJoy.rc
    res/resource.h
)

target_compile_features(PseudoJoy PRIVATE cxx_std_20)
target_compile_definitions(PseudoJoy PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(PseudoJoy PRIVATE /W4 /permissive- /utf-8 /EHsc)
    set_property(TARGET PseudoJoy PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(PseudoJoy PRIVATE
    d2d1
    dwrite
    dwmapi
    shcore
    comctl32
    user32
    gdi32
    shell32
    ole32
    advapi32
)

set_target_properties(PseudoJoy PROPERTIES
    OUTPUT_NAME "PseudoJoy"
    VS_GLOBAL_EnableCppCoreCheck false
)

install(TARGETS PseudoJoy RUNTIME DESTINATION .)

option(PSEUDOJOY_BUILD_TESTS "Build the native input smoke test" ON)
if(PSEUDOJOY_BUILD_TESTS)
    enable_testing()
    add_executable(PseudoJoyInputSmoke
        tests/InputEngineSmoke.cpp
        src/InputEngine.cpp
        src/InputEngine.h
    )
    target_compile_features(PseudoJoyInputSmoke PRIVATE cxx_std_20)
    target_compile_definitions(PseudoJoyInputSmoke PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(PseudoJoyInputSmoke PRIVATE /W4 /permissive- /utf-8 /EHsc)
        set_property(TARGET PseudoJoyInputSmoke PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    target_link_libraries(PseudoJoyInputSmoke PRIVATE user32)
    add_test(NAME input-smoke COMMAND PseudoJoyInputSmoke)
endif()
