add_executable(gbaview main.c)
target_link_libraries(gbaview PRIVATE machine gbatools gdi32 winmm)
target_compile_options(gbaview PRIVATE -Wall -Wextra)
