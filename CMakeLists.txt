cmake_minimum_required(VERSION 3.14)
project(openkit_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cl-bootstrap.cmake)
cl_repo()

cl_import_source(
  NAME bundle_cmake
  DOWNLOAD_ONLY
  REPO https://github.com/gradylink/bundle.cmake.git
  REF 50814e22ed67c0069ed9826f84a6817e0fd56c2b
)
include("${CL_SOURCE_DIR}/cmake/bundle.cmake")

bundle_add(openkit_data
  BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data"
  DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/data"
)

cl_add_dep(gh:OpenKit-OSS/blueboat-cpp)

add_subdirectory(common)
add_subdirectory(gamemodes/tycoon)

add_executable(openkit_server main.cpp)
target_link_libraries(openkit_server PRIVATE openkit_common openkit_tycoon deps::blueboat-cpp::server)
