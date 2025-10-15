cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0100 NEW)
project(PortfolioAnalytics)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_library(SQLITE3_LIB sqlite3 REQUIRED)

add_executable(my_app
    src/main.cpp
    src/database.cpp
    src/gui.cpp
    src/search.cpp
    src/gui.hh
    src/stock.cpp
)

target_link_libraries(my_app PRIVATE Qt6::Widgets ${SQLITE3_LIB})
