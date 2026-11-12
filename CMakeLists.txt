cmake_minimum_required(VERSION 3.20)

project(SRMS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(GTest CONFIG REQUIRED)

if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_library(srms_core
    src/core/validator.cpp
    src/core/record_service.cpp
    src/core/report_service.cpp
    src/core/database.cpp
    src/core/student_dao.cpp
    src/core/search_service.cpp
    src/core/auth_service.cpp
    src/core/admin_dao.cpp
    src/core/console_ui.cpp
)

target_include_directories(srms_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)

target_link_libraries(srms_core PUBLIC
    SQLite3::SQLite3
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_options(srms_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(srms_tests
    tests/validator_test.cpp
    tests/record_service_test.cpp
    tests/report_service_test.cpp
    tests/auth_service_test.cpp
    tests/admin_dao_test.cpp
    tests/student_dao_test.cpp
    tests/search_service_test.cpp
    tests/student_dao_search_test.cpp
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