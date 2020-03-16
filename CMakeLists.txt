# Child Process Management
# Build Configuration
# by Wolfram Rösler 2016-10-14

cmake_minimum_required(VERSION 3.5)

project(childprocess)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)

find_package(Boost COMPONENTS
    iostreams
    unit_test_framework
    REQUIRED
)

add_executable(childprocess
    childprocess.cpp
    test.cpp
)

target_link_libraries(childprocess
    ${Boost_LIBRARIES}
    ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
    Threads::Threads
)

enable_testing()
add_test(NAME childprocess COMMAND childprocess random --log_level=test_suite)
