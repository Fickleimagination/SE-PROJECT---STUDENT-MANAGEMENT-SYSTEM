cmake_minimum_required(VERSION 3.20)

project(SRMS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

enable_testing()

add_executable(srms_tests
    src/core/validator.cpp
    src/core/record_service.cpp
    src/core/report_service.cpp
    src/core/console_ui.cpp
    src/core/auth_service.cpp
    tests/validator_test.cpp
    tests/auth_service_test.cpp
    tests/record_service_test.cpp
    tests/report_service_test.cpp
)

target_include_directories(srms_tests PRIVATE
    src/core
)

target_link_libraries(srms_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
    OpenSSL::Crypto
)

add_test(NAME srms_tests COMMAND srms_tests)