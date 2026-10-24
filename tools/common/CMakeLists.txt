add_library(gbatools STATIC png.c file_util.c wav.c key_script.c)
target_include_directories(gbatools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gbatools PRIVATE -Wall -Wextra)
target_link_libraries(gbatools PRIVATE gbacore) # key_script.c: the GBA_KEY_* values
