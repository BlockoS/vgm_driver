cmake_minimum_required (VERSION 3.22)

project(vgmstrip)
    
if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -D _CRT_NONSTDC_NO_DEPRECATE)
endif()

add_subdirectory(extern)

add_executable(vgm_strip vgm_strip.c vgm.c vgm.h)
target_link_libraries(vgm_strip argparse)
if(NOT MSVC)
    target_compile_definitions(vgm_strip PRIVATE LOG_TERM_COLOR)
	target_compile_options(vgm_strip PRIVATE -Wall -Werror)
endif()

install(TARGETS vgm_strip RUNTIME DESTINATION bin)
