# The Game Boy / Game Boy Color core (milestone 17): plain C11, depends only on libc.
file(GLOB GBCORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(gbcore STATIC ${GBCORE_SOURCES})
target_include_directories(gbcore PUBLIC include PRIVATE src)
target_compile_options(gbcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
