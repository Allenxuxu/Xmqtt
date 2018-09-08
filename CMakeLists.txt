
project(mqtt-server)
cmake_minimum_required(VERSION 2.8)


SET(CMAKE_BUILD_TYPE "Debug")

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib-debug)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin-debug)
else()
    SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib-release)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin-release)
endif()


aux_source_directory(${PROJECT_SOURCE_DIR}/Server SERVERFILES)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/Lib/muduo
                    ${PROJECT_SOURCE_DIR}/Lib/cmdline
                    ${PROJECT_SOURCE_DIR}/Server
)

set(CXX_FLAGS
 -g
 -Wall
 -Wextra
 -Werror
 -Wconversion
 -Wno-unused-parameter
 -Wold-style-cast
 -Woverloaded-virtual
 -Wpointer-arith
 -Wshadow
 -Wwrite-strings
 -march=native
 -MMD
 -rdynamic
 -D_GLIBCXX_USE_CXX11_ABI=0
 )


if(CMAKE_BUILD_BITS EQUAL 32)
  list(APPEND CXX_FLAGS "-m32")
endif()
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

set(CMAKE_CXX_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -finline-limit=1000 -DNDEBUG")


link_directories(${PROJECT_SOURCE_DIR}/Lib/build/release/lib)
add_executable(mqtt-server  ${SERVERFILES})
target_link_libraries(mqtt-server muduo_base muduo_net pthread)

