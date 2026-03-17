cmake_minimum_required(VERSION 3.16)
 
 project(TaxiAnalysis)
 
 set(CMAKE_CXX_STANDARD 17)
 set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
 # 尝试查找Qt6，如果找不到则尝试Qt5
 find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
 if(NOT Qt6_FOUND)
     find_package(Qt5 5.15 COMPONENTS Core Gui Widgets REQUIRED)
 endif()
 
 set(CMAKE_AUTOUIC ON)
 set(CMAKE_AUTOMOC ON)
 set(CMAKE_AUTORCC ON)
 
 add_executable(TaxiAnalysis main.cpp)
 
 if(Qt6_FOUND)
     target_link_libraries(TaxiAnalysis PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
 else()
     target_link_libraries(TaxiAnalysis PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
 endif()
 
 # 为了方便在不同环境中运行，添加安装规则
 install(TARGETS TaxiAnalysis
     RUNTIME DESTINATION bin
 )