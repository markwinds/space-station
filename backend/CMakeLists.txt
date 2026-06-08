cmake_minimum_required(VERSION 3.20)
project(SpaceStation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

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

add_executable(space-station
        src/main.cpp
        src/config/config_store.cpp
        src/http/http_server.cpp
        src/logging/logger.cpp
        "${SPACESTATION_EMBED_CPP}")

target_include_directories(space-station
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${SPACESTATION_GENERATED_DIR}")

target_link_libraries(space-station
        PRIVATE
        Drogon::Drogon
        nlohmann_json::nlohmann_json)

if (WIN32)
    target_compile_definitions(space-station PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif ()

if (MSVC)
    target_compile_options(space-station PRIVATE /EHsc)
endif ()
