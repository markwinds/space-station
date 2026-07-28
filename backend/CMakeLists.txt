cmake_minimum_required(VERSION 3.20)
project(SpaceStation LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(c-ares CONFIG REQUIRED)
find_package(libssh2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(qjs CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

set(SPACESTATION_FRONTEND_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../frontend/dist")
set(SPACESTATION_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SPACESTATION_GENERATED_DIR}")

set(SPACESTATION_EMBED_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake")
set(SPACESTATION_EMBED_CPP "${SPACESTATION_GENERATED_DIR}/embedded_assets.cpp")
file(GLOB_RECURSE SPACESTATION_FRONTEND_DIST_FILES
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${SPACESTATION_FRONTEND_DIST_DIR}/*")

add_custom_command(
        OUTPUT "${SPACESTATION_EMBED_CPP}"
        COMMAND "${CMAKE_COMMAND}"
        -DINPUT_DIR=${SPACESTATION_FRONTEND_DIST_DIR}
        -DOUTPUT_CPP=${SPACESTATION_EMBED_CPP}
        -P "${SPACESTATION_EMBED_SCRIPT}"
        DEPENDS
        "${SPACESTATION_EMBED_SCRIPT}"
        ${SPACESTATION_FRONTEND_DIST_FILES}
        COMMENT "Embedding frontend assets"
        VERBATIM
)

add_library(space-transfer-core STATIC
        src/transfer/transfer.cpp)

target_include_directories(space-transfer-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(space-transfer-core
        PUBLIC
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
        OpenSSL::SSL
        Threads::Threads
        ZLIB::ZLIB)

add_executable(space-transfer-server src/transfer/server_main.cpp)
target_link_libraries(space-transfer-server PRIVATE space-transfer-core)

add_executable(space-transfer-client src/transfer/client_main.cpp)
target_link_libraries(space-transfer-client PRIVATE space-transfer-core)

add_executable(space-station
        src/main.cpp
        src/authenticator/authenticator_service.cpp
        src/cert/certificate_service.cpp
        src/config/config_store.cpp
        src/http/http_server.cpp
        src/logging/logger.cpp
        src/plugins/terminal_plugin_service.cpp
        src/serial/serial_service.cpp
        src/serial/serial_websocket.cpp
        src/serial/browser_serial_share_service.cpp
        src/serial/browser_serial_share_websocket.cpp
        src/ssh/ssh_session.cpp
        src/ssh/network_utils.cpp
        src/ssh/sftp_service.cpp
        src/ssh/ssh_websocket.cpp
        src/transfer/transfer_manager.cpp
        "${SPACESTATION_EMBED_CPP}")

target_include_directories(space-station
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${SPACESTATION_GENERATED_DIR}")

target_link_libraries(space-station
        PRIVATE
        Drogon::Drogon
        CURL::libcurl
        c-ares::cares
        nlohmann_json::nlohmann_json
        unofficial::sqlite3::sqlite3
        OpenSSL::Crypto
        libssh2::libssh2
        qjs
        space-transfer-core)

function(strip_release_executable target)
    add_custom_command(
            TARGET "${target}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                    "-DBUILD_CONFIG=$<CONFIG>"
                    "-DSTRIP_TOOL=${CMAKE_STRIP}"
                    "-DTARGET_FILE=$<TARGET_FILE:${target}>"
                    "-DTARGET_NAME=${target}"
                    "-DTARGET_PLATFORM=${CMAKE_SYSTEM_NAME}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/strip_release.cmake"
            COMMENT "Post-processing executable ${target}"
            VERBATIM)
endfunction()

strip_release_executable(space-station)
strip_release_executable(space-transfer-client)
strip_release_executable(space-transfer-server)

if (BUILD_TESTING)
    add_executable(config-store-vault-test
            tests/config_store_vault_test.cpp
            src/config/config_store.cpp)
    target_include_directories(config-store-vault-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(config-store-vault-test
            PRIVATE
            nlohmann_json::nlohmann_json
            unofficial::sqlite3::sqlite3
            OpenSSL::Crypto)
    add_test(NAME config-store-vault-test COMMAND config-store-vault-test)

    add_executable(ssh-network-utils-test
            tests/ssh_network_utils_test.cpp
            src/ssh/network_utils.cpp)
    target_include_directories(ssh-network-utils-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(ssh-network-utils-test PRIVATE c-ares::cares)
    add_test(NAME ssh-network-utils-test COMMAND ssh-network-utils-test)

    add_executable(authenticator-totp-test
            tests/authenticator_totp_test.cpp
            src/authenticator/authenticator_service.cpp
            src/config/config_store.cpp)
    target_include_directories(authenticator-totp-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(authenticator-totp-test
            PRIVATE
            nlohmann_json::nlohmann_json
            unofficial::sqlite3::sqlite3
            OpenSSL::Crypto)
    add_test(NAME authenticator-totp-test COMMAND authenticator-totp-test)

    add_executable(certificate-service-test
            tests/certificate_service_test.cpp
            src/cert/certificate_service.cpp)
    target_include_directories(certificate-service-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(certificate-service-test
            PRIVATE
            nlohmann_json::nlohmann_json
            OpenSSL::Crypto)
    add_test(NAME certificate-service-test COMMAND certificate-service-test)

    add_executable(terminal-plugin-test
            tests/terminal_plugin_test.cpp
            src/plugins/terminal_plugin_service.cpp
            src/logging/logger.cpp)
    target_include_directories(terminal-plugin-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(terminal-plugin-test
            PRIVATE
            CURL::libcurl
            nlohmann_json::nlohmann_json
            OpenSSL::Crypto
            qjs
            Threads::Threads)
    add_test(NAME terminal-plugin-test COMMAND terminal-plugin-test)
endif ()

if (WIN32)
    target_compile_definitions(space-transfer-core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(space-transfer-core PUBLIC ws2_32)
    target_compile_definitions(space-station PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif ()

if (MSVC)
    target_compile_options(space-transfer-core PRIVATE /EHsc)
    target_compile_options(space-transfer-server PRIVATE /EHsc)
    target_compile_options(space-transfer-client PRIVATE /EHsc)
    target_compile_options(space-station PRIVATE /EHsc)
endif ()
