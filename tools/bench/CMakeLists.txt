add_executable(gbabench main.c)
target_link_libraries(gbabench PRIVATE machine gbatools)
target_compile_options(gbabench PRIVATE -Wall -Wextra)
# --ra <n> (milestone 14): made-up RetroAchievements checked every frame, through the glue and its pretend server.
include(${PROJECT_SOURCE_DIR}/third_party/rcheevos.cmake)
target_sources(gbabench PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra/ra.c ${PROJECT_SOURCE_DIR}/platform/ra/ra_fake.c)
target_include_directories(gbabench PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra)
target_link_libraries(gbabench PRIVATE rcheevos)
