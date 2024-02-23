cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_COMPILER /bin/g++)
set(CMAKE_C_COMPILER /bin/gcc)

project(WaylandClientWindow)

add_executable(WaylandClientWindow main.c xdg-shell-client-protocol.c xdg-decoration-unstable-v1.c)
target_link_libraries(WaylandClientWindow wayland-client wayland-cursor wayland-egl GL EGL m)