# Create native Wayland Window Example
* <b>NOTE: work in progress</b>
* Client side decoration if needed
* No OpenGL/Vulkan requirements and just pure Wayland buffers
* Writen in C

## HowTo: Generate Wayland headers
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.c
