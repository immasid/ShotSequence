cmake_minimum_required(VERSION 3.16)
project(ShotSequence VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql)

add_executable(ShotSequence
    src/main.cpp
    src/common.h
    src/style.h
    src/database.h
    src/database.cpp
    src/scanner.h
    src/scanner.cpp
    src/organizer.h
    src/organizer.cpp
    src/reorderable_table.h
    src/reorderable_table.cpp
    src/preview_worker.h
    src/preview_worker.cpp
    src/launcher_window.h
    src/launcher_window.cpp
    src/review_window.h
    src/review_window.cpp
)

target_link_libraries(ShotSequence PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
)

enable_testing()

add_executable(test_scanner
    tests/test_scanner.cpp
    src/scanner.cpp
    src/common.h
)

target_link_libraries(test_scanner PRIVATE
    Qt6::Core
)

add_test(NAME test_scanner COMMAND test_scanner)
