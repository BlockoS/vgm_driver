add_library(argparse STATIC argparse/argparse.c argparse/argparse.h)
target_include_directories(argparse PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
