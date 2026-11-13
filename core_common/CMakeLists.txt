# What both cores share (milestone 17): the rewind history ring and its packer. Plain C11, libc only.
add_library(corecommon STATIC delta_pack.c rewind_ring.c)
target_include_directories(corecommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(corecommon PRIVATE -Wall -Wextra)
