cmake_minimum_required(VERSION 3.20)

project(BitgetPricePlugin VERSION 1.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "BitgetPricePlugin can only be built for Windows.")
endif()

add_library(BitgetPrice SHARED
    src/BitgetClient.cpp
    src/BitgetPricePlugin.cpp
    src/Config.cpp
    src/Json.cpp
    src/OptionsWindow.cpp
    src/dllmain.cpp
)

target_include_directories(BitgetPrice PRIVATE src)
target_compile_features(BitgetPrice PRIVATE cxx_std_17)
target_compile_definitions(BitgetPrice PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
if(MSVC)
    target_compile_options(BitgetPrice PRIVATE /utf-8 /W4 /permissive- /EHsc)
endif()
target_link_libraries(BitgetPrice PRIVATE winhttp comctl32 user32 gdi32)
set_target_properties(BitgetPrice PROPERTIES
    OUTPUT_NAME "BitgetPrice"
    PREFIX ""
)

include(CTest)
if(BUILD_TESTING)
    add_executable(BitgetPriceTests
        tests/Tests.cpp
        src/Config.cpp
        src/Json.cpp
    )
    target_include_directories(BitgetPriceTests PRIVATE src)
    target_compile_features(BitgetPriceTests PRIVATE cxx_std_17)
    target_compile_definitions(BitgetPriceTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(BitgetPriceTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME BitgetPriceTests COMMAND BitgetPriceTests)

    add_executable(BitgetPriceSmoke tests/PluginSmoke.cpp)
    target_include_directories(BitgetPriceSmoke PRIVATE src)
    target_compile_features(BitgetPriceSmoke PRIVATE cxx_std_17)
    target_compile_definitions(BitgetPriceSmoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(BitgetPriceSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME BitgetPriceSmoke COMMAND BitgetPriceSmoke $<TARGET_FILE:BitgetPrice>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
endif()
