file(GLOB OPENKIT_TYCOON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(openkit_tycoon STATIC ${OPENKIT_TYCOON_SOURCES})
target_include_directories(openkit_tycoon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openkit_tycoon PUBLIC openkit_common blueboat_server)
