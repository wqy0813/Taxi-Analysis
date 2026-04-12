cmake_minimum_required(VERSION 3.16)

project(TaxiDataAnalysis VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Support both Qt5 and Qt6.
# Let CMake pick the major version, then load the matching modules.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql)

include_directories(include)

set(SOURCES
    src/main.cpp
    src/appconfig.cpp
    src/databasemanager.cpp
    src/datamanager.cpp
    src/quadtree.cpp
    src/densityanalysis.cpp
    src/httpserver.cpp
    include/appconfig.h
    include/databasemanager.h
    include/datamanager.h
    include/quadtree.h
    include/utils.h
    include/densityanalysis.h
    include/httpserver.h
)

add_executable(TaxiDataAnalysis ${SOURCES})

target_compile_definitions(TaxiDataAnalysis PRIVATE
    APP_SOURCE_WEB_ROOT="${CMAKE_SOURCE_DIR}/web"
)

target_link_libraries(TaxiDataAnalysis PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_custom_command(TARGET TaxiDataAnalysis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.ini
        $<TARGET_FILE_DIR:TaxiDataAnalysis>/config.ini
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:TaxiDataAnalysis>/data
    COMMENT "Copy runtime config and data assets next to the executable"
)

# Keep installation simple: the full install tree should run as-is.
install(TARGETS TaxiDataAnalysis
    RUNTIME DESTINATION bin
)

install(FILES ${CMAKE_SOURCE_DIR}/config.ini DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data DESTINATION bin)
