# The layer over both cores (milestone 17): what the tools and the app call.
add_library(machine STATIC machine.c)
target_include_directories(machine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(machine PRIVATE -Wall -Wextra)
target_link_libraries(machine PUBLIC gbacore gbcore)
