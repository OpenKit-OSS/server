file(GLOB OPENKIT_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/colyseus/*.cpp)
add_library(openkit_common STATIC ${OPENKIT_COMMON_SOURCES})
target_include_directories(openkit_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openkit_common PUBLIC blueboat_server openkit_data)
