# The Game Boy / Game Boy Color core (milestone 17): plain C11, depends only on libc.
file(GLOB GBCORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(gbcore STATIC ${GBCORE_SOURCES})
target_include_directories(gbcore PUBLIC include PRIVATE src)
target_link_libraries(gbcore PUBLIC corecommon) # the rewind ring both cores share
target_compile_options(gbcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
# Every machine cycle passes through gb_tick into the timer, screen and sound files: optimised builds let the
# compiler inline across them (as the Android build does for everything).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT GBCORE_IPO OUTPUT GBCORE_IPO_WHY LANGUAGES C)
    if(GBCORE_IPO)
        set_property(TARGET gbcore PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()
