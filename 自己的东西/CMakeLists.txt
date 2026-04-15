cmake_minimum_required(VERSION 3.16)

# 建议项目名保持统一，这里用 TaxiDataAnalysis
project(TaxiDataAnalysis VERSION 0.1 LANGUAGES CXX)

# 1. 基础设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 2. 寻找 Qt 组件 (兼容 Qt6 和 Qt5，并加入 Sql 模块)
find_package(Qt6 COMPONENTS Core Gui Widgets Sql WebEngineWidgets  QUIET )
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 COMPONENTS Core Gui Widgets Sql WebEngineWidgets  REQUIRED)
endif()


# 3. 包含头文件目录
# 这样你在写代码时可以直接 #include "databasemanager.h" 而不用写长路径
include_directories(include)
include_directories(TrafficAnalysisSystem)

# 4. 自动扫描所有源文件
# 这样以后组员新加了 .cpp 文件，只要在这些文件夹里，就不用手动改 CMake
file(GLOB_RECURSE SOURCES
    "main.cpp"
    "src/*.cpp"
    "include/*.h"
    "TrafficAnalysisSystem/*.cpp"
    "TrafficAnalysisSystem/*.h"
    "forms/*.ui"
)

# 5. 生成可执行文件
add_executable(TaxiDataAnalysis ${SOURCES})

# 6. 链接 Qt 库 (根据找到的版本自动链接)
if(Qt6_FOUND)
    target_link_libraries(TaxiDataAnalysis PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Sql
        Qt6::WebEngineWidgets
    )
else()
    target_link_libraries(TaxiDataAnalysis PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Sql
        Qt5::WebEngineWidgets
    )
endif()

# 7. 设置输出路径与安装规则
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

install(TARGETS TaxiDataAnalysis
    RUNTIME DESTINATION bin
)