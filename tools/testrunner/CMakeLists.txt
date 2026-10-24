add_executable(testrunner main.c)
target_link_libraries(testrunner PRIVATE gbacore gbatools)
target_compile_options(testrunner PRIVATE -Wall -Wextra)
