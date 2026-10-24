add_definitions(-Wno-unused-parameter)

include_directories(..)

add_executable(test_rotatefile test_rotatefile.cpp)
target_link_libraries(test_rotatefile utils pthread)
add_test(rotatefile test_rotatefile)

include(CTest)
