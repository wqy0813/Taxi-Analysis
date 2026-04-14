cmake_minimum_required(VERSION 3.16)

project(TaxiDataAnalysis VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/utf-8)
endif()

include_directories(include)

set(SQLITE_DIR ${CMAKE_SOURCE_DIR}/third_party/sqlite)

add_library(sqlite3 STATIC
    ${SQLITE_DIR}/sqlite3.c
)

target_include_directories(sqlite3 PUBLIC
    ${SQLITE_DIR}
)

set(SOURCES
    src/main.cpp
    src/appconfig.cpp
    src/databasemanager.cpp
    src/datamanager.cpp
    src/quadtree.cpp
    src/densityanalysis.cpp
    src/httpserver.cpp
    src/logger.cpp
    ${SQLITE_DIR}/sqlite3.c

    include/appconfig.h
    include/databasemanager.h
    include/datamanager.h
    include/quadtree.h
    include/utils.h
    include/densityanalysis.h
    include/httpserver.h
    include/logger.h
)

add_executable(TaxiDataAnalysis ${SOURCES})

target_compile_definitions(TaxiDataAnalysis PRIVATE
    APP_SOURCE_WEB_ROOT="${CMAKE_SOURCE_DIR}/web"
)

target_include_directories(TaxiDataAnalysis PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SQLITE_DIR}
)

target_link_libraries(TaxiDataAnalysis PRIVATE sqlite3)

if (WIN32)
    target_link_libraries(TaxiDataAnalysis PRIVATE ws2_32)
endif()