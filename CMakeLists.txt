cmake_minimum_required(VERSION 3.20)

project(CryptoPricePlugin VERSION 2.2.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "CryptoPricePlugin can only be built for Windows.")
endif()

add_library(CryptoPrice SHARED
    src/MarketClient.cpp
    src/InstrumentValidation.cpp
    src/CryptoPricePlugin.cpp
    src/Config.cpp
    src/Json.cpp
    src/OptionsWindow.cpp
    src/dllmain.cpp
)

target_include_directories(CryptoPrice PRIVATE src)
target_compile_features(CryptoPrice PRIVATE cxx_std_17)
target_compile_definitions(CryptoPrice PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
if(MSVC)
    target_compile_options(CryptoPrice PRIVATE /utf-8 /W4 /permissive- /EHsc)
endif()
target_link_libraries(CryptoPrice PRIVATE winhttp comctl32 user32 gdi32)
set_target_properties(CryptoPrice PROPERTIES
    OUTPUT_NAME "CryptoPrice"
    PREFIX ""
)

include(CTest)
if(BUILD_TESTING)
    add_executable(CryptoPriceTests
        tests/Tests.cpp
        src/InstrumentValidation.cpp
        src/Config.cpp
        src/Json.cpp
    )
    target_include_directories(CryptoPriceTests PRIVATE src)
    target_compile_features(CryptoPriceTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(CryptoPriceTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceTests COMMAND CryptoPriceTests)

    add_executable(CryptoPriceSmoke tests/PluginSmoke.cpp)
    target_include_directories(CryptoPriceSmoke PRIVATE src)
    target_compile_features(CryptoPriceSmoke PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceSmoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(CryptoPriceSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceSmoke COMMAND CryptoPriceSmoke $<TARGET_FILE:CryptoPrice>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
    add_test(NAME CryptoPriceNoChangeWidthSmoke COMMAND CryptoPriceSmoke $<TARGET_FILE:CryptoPrice>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures-no-change --no-change)
endif()
