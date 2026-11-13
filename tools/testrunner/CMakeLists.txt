add_executable(testrunner main.c)
target_link_libraries(testrunner PRIVATE machine gbatools)
target_compile_options(testrunner PRIVATE -Wall -Wextra)
