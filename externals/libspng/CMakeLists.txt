add_library(spng STATIC spng.h spng.c)
target_compile_definitions(spng PUBLIC SPNG_STATIC)
target_include_directories(spng PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(spng PRIVATE ZLIB::ZLIB)

# Enable SSE4.1 on x64
if ("x86_64" IN_LIST ARCHITECTURE)
    target_compile_definitions(spng PRIVATE SPNG_SSE=4)
    if (NOT MSVC)
        target_compile_options(spng PRIVATE -msse4.1)
    endif()
endif()

add_library(spng::spng ALIAS spng)
