add_library(spng STATIC spng.h spng.c)
target_compile_definitions(spng PUBLIC SPNG_STATIC)
target_include_directories(spng PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# Try to find zlib, use miniz otherwise
find_package(ZLIB REQUIRED)

if (ZLIB_FOUND)
    target_include_directories(spng PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(spng PRIVATE ${ZLIB_LIBRARIES})
else()
    message(STATUS "Unable to find zlib for libspng, using miniz")
    target_sources(spng PRIVATE miniz.c miniz.h)
    target_compile_definitions(spng PRIVATE SPNG_USE_MINIZ)
endif()

# Enable SSE4.1 on x64
if ("x86_64" IN_LIST ARCHITECTURE)
    target_compile_definitions(spng PRIVATE SPNG_SSE=4)
    if (NOT MSVC)
        target_compile_options(spng PRIVATE -msse4.1)
    endif()
endif()

add_library(spng::spng ALIAS spng)
