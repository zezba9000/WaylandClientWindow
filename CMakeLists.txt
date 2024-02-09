cmake_minimum_required(VERSION 3.27)
project(WaylandClientWindow)

set(CMAKE_CXX_STANDARD 17)

add_executable(WaylandClientWindow main.c xdg-shell-client-protocol.c)
target_link_libraries(WaylandClientWindow wayland-client wayland-cursor wayland-egl GL EGL)