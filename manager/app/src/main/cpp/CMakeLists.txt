cmake_minimum_required(VERSION 3.10.0)
project("nkjni")

# Shared kernel/userspace UAPI headers (repo-root uapi/), included as
# <uapi/ioctl.h> by the JNI layer.
get_filename_component(NKSU_UAPI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
include_directories("${NKSU_UAPI_INCLUDE_DIR}")

add_subdirectory(libjni)
