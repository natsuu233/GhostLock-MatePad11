cmake_minimum_required(VERSION 3.28)
project(GhostLock)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "$ENV{HOME}/ghostlock/pad-res/share")

add_executable(poc main.cpp
        tools.cpp
        leak_slide.cpp
        init_addrs.cpp
        signal/setup_sig.cpp
        signal/sig_handler.cpp
        prim/prim.cpp
        selinux/parent.cpp
        selinux/misc.cpp
        prim/misc.cpp
        selinux/child.cpp)
target_link_options(poc PRIVATE -static)