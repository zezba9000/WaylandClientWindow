#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

#include <wayland-client.h>
//#include <wayland-egl.h>
#include <wayland-cursor.h>
#include "xdg-shell-client-protocol.h"

#include <linux/input.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name);
struct wl_registry_listener registry_listener = {&registry_add_object, &registry_remove_object};

void seat_capabilities (void *data, struct wl_seat *seat, uint32_t capabilities);
struct wl_seat_listener seat_listener = {&seat_capabilities};

void pointer_enter (void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
void pointer_leave (void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
void pointer_motion (void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
void pointer_button (void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
void pointer_axis (void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
struct wl_pointer_listener pointer_listener = {&pointer_enter, &pointer_leave, &pointer_motion, &pointer_button, &pointer_axis};

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
struct xdg_surface_listener xdg_surface_listener = {.configure = xdg_surface_handle_configure};

void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states);
void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel);
struct xdg_toplevel_listener xdg_toplevel_listener = {.configure = xdg_toplevel_handle_configure, .close = xdg_toplevel_handle_close};

void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);
struct xdg_wm_base_listener xdg_wm_base_listener = {.ping = xdg_wm_base_ping};

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_subcompositor *subcompositor = NULL;
struct xdg_wm_base *xdg_wm_base = NULL;
struct wl_seat *seat = NULL;
struct wl_shm *shm = NULL;
struct wl_surface *cursor_surface = NULL;
struct wl_cursor_theme *cursor_theme = NULL;
int running = 1;

typedef struct Window
{
    struct wl_surface* surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    int width, height;
}Window;
Window* window = NULL;

int main(void)
{
    printf("Hello, World!\n");

    // get display
    display = wl_display_connect(NULL);
    if(!display)
    {
        printf("cannot connect to Wayland server");
        return 1;
    }

    // que registry
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &window);
    wl_display_roundtrip(display);

    // output interface
    //wl_global_create(display, );

    // create window
    window = (struct Window*)calloc(1, sizeof(Window));
    window->width = 320;
    window->height = 240;
    window->surface = wl_compositor_create_surface (compositor);
    if (xdg_wm_base)
    {
        window->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, window->surface);
        window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
        xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);
        xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener, window);
        xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, window);
        xdg_toplevel_set_title(window->xdg_toplevel, "example");
        xdg_toplevel_set_app_id(window->xdg_toplevel, "example");
    }
    else
    {
        printf("no xdg_wm_base");
    }

    xdg_toplevel_set_title(window->xdg_toplevel, "Test");
    xdg_surface_set_window_geometry(window->xdg_surface, 0, 0, 256, 256);
    //xdg_toplevel_show_window_menu(window->xdg_toplevel, NULL, 0, 0, 0);

    // shared memory buffer
    const int stride = window->width * sizeof(uint32_t);
    const int shm_pool_size = window->height * stride;// * 2;

    char name[] = "TestWaylandApp";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0 || errno == EEXIST) return 0;
    shm_unlink(name);
    int result = ftruncate(fd, shm_pool_size);
    if (result < 0 || errno == EINTR) return 0;

    uint8_t *pool_data = mmap(NULL, shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, shm_pool_size);

    int index = 0;
    int offset = window->height * stride * index;
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, offset, window->width, window->height, stride, WL_SHM_FORMAT_XRGB8888);
    uint32_t *pixels = (uint32_t*)&pool_data[offset];
    memset(pixels, 0, window->width * window->height);
    for (int i = 0; i < window->width * window->height; ++i)
    {
        pixels[i] = INT32_MAX / 100;
    }

    wl_surface_attach(window->surface, buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, UINT32_MAX, UINT32_MAX);
    wl_surface_commit(window->surface);

    // event loop
    while (running)
    {
        //wl_display_dispatch_pending (display);
        //draw_window(&window);

        //wl_display_flush(display);
        if (wl_display_dispatch(display) < 0) break;
        //sleep(1);
    }

    // shutdown
    if(xdg_wm_base)
    {
        xdg_toplevel_destroy(window->xdg_toplevel);
        xdg_surface_destroy(window->xdg_surface);
    }
    wl_surface_destroy (window->surface);
    wl_display_disconnect (display);

    return 0;
}

void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (!strcmp(interface,"wl_compositor")) {
        compositor = (struct wl_compositor*)(wl_registry_bind (registry, name, &wl_compositor_interface, 1));
    }
    else if (strcmp(interface, "wl_subcompositor") == 0) {
        subcompositor = (struct wl_subcompositor*)(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    }
    else if (!strcmp(interface,"wl_seat")) {
        printf("wl_seat\n");
        seat = (struct wl_seat*)(wl_registry_bind (registry, name, &wl_seat_interface, 1));
        wl_seat_add_listener (seat, &seat_listener, data);
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        shm = (struct wl_shm*)(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        cursor_theme = wl_cursor_theme_load(NULL, 32, shm);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm_base = (struct xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, MIN(version, 2));
    }
}

void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name)
{
    // TODO
}

void seat_capabilities (void *data, struct wl_seat *seat, uint32_t capabilities)
{
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        printf("seat_capabilities\n");
        struct wl_pointer *pointer = wl_seat_get_pointer (seat);
        wl_pointer_add_listener (pointer, &pointer_listener, data);
        cursor_surface = wl_compositor_create_surface(compositor);
    }
    //    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
    //        struct wl_keyboard *keyboard = wl_seat_get_keyboard (seat);
    //        wl_keyboard_add_listener (keyboard, &keyboard_listener, NULL);
    //    }
}

void pointer_enter (void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    /*window *w = static_cast<window*>(data);
    w->current_surface = surface;

    std::string cursor = "left_ptr";

    for(const decoration &d: w->decorations) {
        if(d.surface==surface) {
            if(resize_cursor.count(d.function)) {
                cursor = resize_cursor.at(d.function);
            }
        }
    }

    const auto image = wl_cursor_theme_get_cursor(cursor_theme, cursor.c_str())->images[0];
    wl_pointer_set_cursor(pointer, serial, cursor_surface, image->hotspot_x, image->hotspot_y);
    wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(image), 0, 0);
    wl_surface_damage(cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(cursor_surface);*/
}

void pointer_leave (void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
    /*window *w = static_cast<window*>(data);
    w->button_pressed = false;*/
}

void pointer_motion (void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    //    std::cout << "pointer motion " << wl_fixed_to_double(x) << " " << wl_fixed_to_double(y) << std::endl;
}

void pointer_button (void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        printf("xdg_toplevel_move\n");
        xdg_toplevel_move(window->xdg_toplevel, seat, serial);
        //xdg_toplevel_set_minimized(window->xdg_toplevel);
    }

    /*//    std::cout << "pointer button " << button << ", state " << state << std::endl;

    window *w = static_cast<window*>(data);
    w->button_pressed = (button==BTN_LEFT) && (state==WL_POINTER_BUTTON_STATE_PRESSED);

    if(w->button_pressed) {
        for(int i = 0; i<w->decorations.size(); i++) {
            if(w->decorations[i].surface==w->current_surface) {
                switch(w->decorations[i].function) {
                    case XDG_TOPLEVEL_RESIZE_EDGE_NONE:
                        if(w->xdg_toplevel) {
                            xdg_toplevel_move(w->xdg_toplevel, seat, serial);
                        }
                        break;
                    default:
                        if(w->xdg_toplevel) {
                            xdg_toplevel_resize(w->xdg_toplevel, seat, serial, w->decorations[i].function);
                        }
                        break;
                }
            }
        }

        for(const struct button &b: w->buttons) {
            if(b.surface==w->current_surface) {
                switch (b.function) {
                    case button::type::CLOSE:
                        running = false;
                        break;
                    case button::type::MAXIMISE:
                        if(w->maximised) {
                            if(w->xdg_toplevel) {
                                xdg_toplevel_unset_maximized(w->xdg_toplevel);
                            }
                        }
                        else {
                            // store original window size
//                        wl_egl_window_get_attached_size(w->egl_window, &w->width, &w->height);
                            if(w->xdg_toplevel) {
                                xdg_toplevel_set_maximized(w->xdg_toplevel);
                            }
                        }
                        w->maximised = !w->maximised;
                        break;
                    case button::type::MINIMISE:
                        if(w->xdg_toplevel) {
                            xdg_toplevel_set_minimized(w->xdg_toplevel);
                        }
                        break;
                }
            }
        }
    }*/
}

void pointer_axis (void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    // TODO
}

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states)
{
    if (width <= 0 || height <= 0) return;
    //struct Window *window = (struct window*)(data);
    //window_resize(window, width, height, true);
    xdg_surface_set_window_geometry(window->xdg_surface, 0, 0, width, height);
}

void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
    running = 0;
}

void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}