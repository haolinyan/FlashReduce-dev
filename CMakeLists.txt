cmake_minimum_required (VERSION 3.16)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(FlashReduce LANGUAGES CXX C)

# 默认构建类型为Release
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if (NOT DEFINED ENV{ENABLE_SANITIZER})
    set(ENV{ENABLE_SANITIZER} "address")
endif()

# 只在Debug模式下启用检查器
if (DEFINED ENV{ENABLE_SANITIZER})
    set(SANITIZER_TYPE "$ENV{ENABLE_SANITIZER}")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        if (SANITIZER_TYPE STREQUAL "address")
            message(STATUS "Building with Address Sanitizer")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -O1")
            set(SANITIZER_LIBRARIES "-fsanitize=address")
        elseif (SANITIZER_TYPE STREQUAL "thread")
            message(STATUS "Building with Thread Sanitizer")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -O1")
            set(SANITIZER_LIBRARIES "-fsanitize=thread")
        else()
            message(FATAL_ERROR "Invalid value for ENABLE_SANITIZER environment variable. Allowed values are 'address' or 'thread'.")
        endif()
    endif()
endif()

find_package (glog REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Found Protobuf: ${Protobuf_VERSION}")
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Found gRPC: ${gRPC_VERSION}")

get_target_property(PROTOBUF_INCLUDE_DIR protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Protobuf include directory: ${PROTOBUF_INCLUDE_DIR}")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/csrc ${PROTOBUF_INCLUDE_DIR})
file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/csrc/*.cc)
add_library(flashreduce SHARED ${sources})
target_compile_options(flashreduce PUBLIC "-libverbs")

target_link_libraries(flashreduce PUBLIC
    glog::glog
    "-libverbs"
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    ${SANITIZER_LIBRARIES}
)

add_subdirectory(examples)
