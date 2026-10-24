# rcheevos (RetroAchievements' client library, MIT: third_party/rcheevos/LICENSE) as a static library, with only what
# rc_client needs: the client, the web API, the runtime and md5.c. Not its file hashing (the glue hashes the ROM itself),
# RAIntegration or external clients. Included by the unit tests, gbabench and the Android library.
if(NOT TARGET rcheevos)
    set(RC_DIR ${CMAKE_CURRENT_LIST_DIR}/rcheevos)
    file(GLOB RC_RUNTIME_SOURCES ${RC_DIR}/src/rcheevos/*.c)
    add_library(rcheevos STATIC
        ${RC_DIR}/src/rc_client.c ${RC_DIR}/src/rc_compat.c ${RC_DIR}/src/rc_util.c ${RC_DIR}/src/rc_version.c
        ${RC_DIR}/src/rapi/rc_api_common.c ${RC_DIR}/src/rapi/rc_api_info.c ${RC_DIR}/src/rapi/rc_api_runtime.c
        ${RC_DIR}/src/rapi/rc_api_user.c ${RC_RUNTIME_SOURCES} ${RC_DIR}/src/rhash/md5.c)
    target_include_directories(rcheevos PUBLIC ${RC_DIR}/include ${RC_DIR}/src/rhash PRIVATE ${RC_DIR}/src)
    find_package(Threads REQUIRED)
    target_link_libraries(rcheevos PUBLIC Threads::Threads)
    target_compile_options(rcheevos PRIVATE -w) # their code: their warnings are not ours to fix
    # Its parser sizes a set in a first pass with a NULL buffer, taking member addresses of a NULL pointer it never
    # reads through (condset.c); UBSan's null check flags that in the PC tests. All other checks stay on.
    target_compile_options(rcheevos PRIVATE -fno-sanitize=null)
endif()
