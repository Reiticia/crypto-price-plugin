cmake_minimum_required(VERSION 3.21)

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
    src/ExchangeJson.cpp
    src/OptionsWindow.cpp
    src/Tooltip.cpp
    src/StreamProtocol.cpp
    src/StreamClient.cpp
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
        src/ExchangeJson.cpp
        src/Tooltip.cpp
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

    add_executable(CryptoPriceExchangeTests tests/ExchangeTests.cpp src/ExchangeJson.cpp
        src/Config.cpp src/InstrumentValidation.cpp)
    target_include_directories(CryptoPriceExchangeTests PRIVATE src)
    target_compile_features(CryptoPriceExchangeTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceExchangeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(CryptoPriceExchangeTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceExchangeTests COMMAND CryptoPriceExchangeTests)

    add_executable(CryptoPriceExchangeLiveSmoke tests/ExchangeLiveSmoke.cpp src/MarketClient.cpp
        src/Json.cpp src/ExchangeJson.cpp src/Config.cpp)
    target_include_directories(CryptoPriceExchangeLiveSmoke PRIVATE src)
    target_compile_features(CryptoPriceExchangeLiveSmoke PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceExchangeLiveSmoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CryptoPriceExchangeLiveSmoke PRIVATE winhttp)
    if(MSVC)
        target_compile_options(CryptoPriceExchangeLiveSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()

    add_executable(CryptoPriceOptionsSourceTests
        tests/OptionsSourceTests.cpp src/OptionsWindow.cpp src/Config.cpp
        src/InstrumentValidation.cpp src/MarketClient.cpp src/Json.cpp src/ExchangeJson.cpp)
    target_include_directories(CryptoPriceOptionsSourceTests PRIVATE src)
    target_compile_features(CryptoPriceOptionsSourceTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceOptionsSourceTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CryptoPriceOptionsSourceTests PRIVATE winhttp comctl32 user32 gdi32)
    if(MSVC)
        target_compile_options(CryptoPriceOptionsSourceTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceOptionsSourceTests COMMAND CryptoPriceOptionsSourceTests)
    set_tests_properties(CryptoPriceOptionsSourceTests PROPERTIES TIMEOUT 15)

    add_executable(CryptoPriceStreamTests tests/StreamTests.cpp src/StreamProtocol.cpp src/Config.cpp)
    target_include_directories(CryptoPriceStreamTests PRIVATE src)
    target_compile_features(CryptoPriceStreamTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceStreamTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(CryptoPriceStreamTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceStreamTests COMMAND CryptoPriceStreamTests)

    add_executable(CryptoPriceStreamLifecycleTests tests/StreamLifecycleTests.cpp src/StreamClient.cpp
        src/StreamProtocol.cpp src/MarketClient.cpp src/Json.cpp src/ExchangeJson.cpp src/Config.cpp)
    target_include_directories(CryptoPriceStreamLifecycleTests PRIVATE src)
    target_compile_features(CryptoPriceStreamLifecycleTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceStreamLifecycleTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CryptoPriceStreamLifecycleTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(CryptoPriceStreamLifecycleTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceStreamLifecycleTests COMMAND CryptoPriceStreamLifecycleTests)
    set_tests_properties(CryptoPriceStreamLifecycleTests PROPERTIES TIMEOUT 10)

    get_target_property(crypto_price_sources CryptoPrice SOURCES)
    add_executable(CryptoPricePollingTests tests/PollingTests.cpp src/StreamClient.cpp
        src/StreamProtocol.cpp src/MarketClient.cpp src/Json.cpp src/ExchangeJson.cpp src/Config.cpp)
    target_include_directories(CryptoPricePollingTests PRIVATE src)
    target_compile_features(CryptoPricePollingTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPricePollingTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CryptoPricePollingTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(CryptoPricePollingTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPricePollingTests COMMAND CryptoPricePollingTests)
    set_tests_properties(CryptoPricePollingTests PROPERTIES TIMEOUT 20)

    add_executable(CryptoPriceStreamDisplayTests tests/StreamDisplayTests.cpp ${crypto_price_sources})
    target_include_directories(CryptoPriceStreamDisplayTests PRIVATE src)
    target_compile_features(CryptoPriceStreamDisplayTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceStreamDisplayTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CryptoPriceStreamDisplayTests PRIVATE winhttp comctl32 user32 gdi32)
    if(MSVC)
        target_compile_options(CryptoPriceStreamDisplayTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceStreamDisplayTests COMMAND CryptoPriceStreamDisplayTests)
    set_tests_properties(CryptoPriceStreamDisplayTests PROPERTIES TIMEOUT 10)

    add_executable(CryptoPriceStreamSocketTests tests/StreamSocketTests.cpp)
    target_include_directories(CryptoPriceStreamSocketTests PRIVATE src)
    target_compile_features(CryptoPriceStreamSocketTests PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceStreamSocketTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CryptoPriceStreamSocketTests PRIVATE winhttp ws2_32 crypt32)
    if(MSVC)
        target_compile_options(CryptoPriceStreamSocketTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME CryptoPriceStreamSocketTests COMMAND CryptoPriceStreamSocketTests)
    set_tests_properties(CryptoPriceStreamSocketTests PROPERTIES TIMEOUT 15)

    add_executable(CryptoPriceStreamLiveSmoke tests/StreamLiveSmoke.cpp src/StreamClient.cpp
        src/StreamProtocol.cpp src/MarketClient.cpp src/Json.cpp src/ExchangeJson.cpp src/Config.cpp)
    target_include_directories(CryptoPriceStreamLiveSmoke PRIVATE src)
    target_compile_features(CryptoPriceStreamLiveSmoke PRIVATE cxx_std_17)
    target_compile_definitions(CryptoPriceStreamLiveSmoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CryptoPriceStreamLiveSmoke PRIVATE winhttp)
    if(MSVC)
        target_compile_options(CryptoPriceStreamLiveSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()

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
    add_test(NAME CryptoPriceStockSmoke COMMAND CryptoPriceSmoke $<TARGET_FILE:CryptoPrice>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures-stocks --stocks)
    add_test(NAME CryptoPriceMixedSourceSmoke COMMAND CryptoPriceSmoke $<TARGET_FILE:CryptoPrice>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures-mixed-sources --mixed-sources)
endif()
