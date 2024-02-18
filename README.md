# Create native Wayland Window Example
* <b>NOTE: work in progress</b>
* Client side decoration if needed
* No OpenGL/Vulkan requirements and just pure Wayland buffers
* Writen in C

## Install headers
sudo apt install wayland-protocols

## HowTo: Generate Wayland headers
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.c

wayland-scanner client-header /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-unstable-v1.h

wayland-scanner private-code /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-unstable-v1.c
