
cmake_minimum_required(VERSION 3.16)

project(SRMS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(SQLite3 REQUIRED)
find_package(GTest CONFIG REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

# Core application modules
add_library(srms_core
    src/core/validator.cpp
    src/core/record_service.cpp
    src/core/report_service.cpp
    src/core/database.cpp
    src/core/student_dao.cpp
    src/core/search_service.cpp
)

target_include_directories(srms_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)

target_link_libraries(srms_core
    PUBLIC SQLite3::SQLite3
)

target_compile_options(srms_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Unit and integration tests
add_executable(srms_tests
    tests/validator_test.cpp
    tests/record_service_test.cpp
    tests/report_service_test.cpp
    tests/student_dao_test.cpp
    tests/search_service_test.cpp
    tests/student_dao_search_test.cpp
    tests/database_test.cpp
    tests/database_test.cpp
)

target_link_libraries(srms_tests PRIVATE
    srms_core
    GTest::gtest_main
)

target_compile_options(srms_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

include(GoogleTest)

gtest_discover_tests(
    srms_tests
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
