cmake_minimum_required(VERSION 3.16)
project(minikv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Sanitizer options: -DMINIKV_SANITIZER=address  or  thread
set(MINIKV_SANITIZER "" CACHE STRING "address, thread, or empty")

add_compile_options(-Wall -Wextra -Wpedantic)
if(MINIKV_SANITIZER)
  add_compile_options(-fsanitize=${MINIKV_SANITIZER} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${MINIKV_SANITIZER})
endif()

# ---- Library ----
file(GLOB MINIKV_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(minikv ${MINIKV_SOURCES})
target_include_directories(minikv PUBLIC include)

# ---- Tests ----
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
file(GLOB MINIKV_TESTS CONFIGURE_DEPENDS tests/*_test.cpp)
add_executable(minikv_tests ${MINIKV_TESTS})
target_link_libraries(minikv_tests PRIVATE minikv GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(minikv_tests)
