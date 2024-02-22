#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

#include <wayland-util.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
//#include <wayland-egl.h>
#include <wayland-cursor.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1.h"
#include <linux/input.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

void registry_add_object(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name);
struct wl_registry_listener registry_listener = {&registry_add_object, &registry_remove_object};

void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
struct wl_seat_listener seat_listener = {&seat_capabilities};

void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
struct wl_pointer_listener pointer_listener = {&pointer_enter, &pointer_leave, &pointer_motion, &pointer_button, &pointer_axis};

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
struct xdg_surface_listener xdg_surface_listener = {.configure = xdg_surface_handle_configure};

void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states);
void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel);
struct xdg_toplevel_listener xdg_toplevel_listener = {.configure = xdg_toplevel_handle_configure, .close = xdg_toplevel_handle_close};

void xdg_wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial);
struct xdg_wm_base_listener xdg_wm_base_listener = {.ping = xdg_wm_base_ping};

enum zxdg_toplevel_decoration_v1_mode current_mode = 0;
void decoration_handle_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, enum zxdg_toplevel_decoration_v1_mode mode);
static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {.configure = decoration_handle_configure};

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_subcompositor *subcompositor = NULL;
struct xdg_wm_base *xdg_wm_base = NULL;
struct wl_seat *seat = NULL;
struct wl_shm *shm = NULL;
struct wl_surface *cursor_surface = NULL;
struct wl_cursor_theme *cursor_theme = NULL;
struct zxdg_decoration_manager_v1* decoration_manager = NULL;
struct zxdg_toplevel_decoration_v1* decoration = NULL;
int running = 1;

typedef struct SurfaceBuffer
{
    int width, height;
    uint32_t color;
    char* name;
    int fd;
    int stride, shm_pool_size;
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    uint32_t *pixels;
}SurfaceBuffer;

typedef struct Window
{
    int width, height;
    int compositeWidth, compositeHeight;
    struct wl_surface* clientSurface;
    struct wl_subsurface* clientSubSurface;
    struct SurfaceBuffer clientSurfaceBuffer;

    struct wl_surface* surface;
    struct SurfaceBuffer surfaceBuffer;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

}Window;
Window* window = NULL;
int useClientDecorations = 1;

#define DECORATIONS_BAR_SIZE 4
#define DECORATIONS_TOPBAR_SIZE 32
#define DECORATIONS_BUTTON_SIZE 32

uint32_t ToColor(char r, char g, char b, char a)
{
    uint32_t result = 0;
    char* c = (char*)&result;
    c[0] = b;
    c[1] = g;
    c[2] = r;
    c[3] = a;
    return result;
}

void BlitRect(uint32_t* pixels, int x, int y, int width, int height, uint32_t color)
{
    int widthOffset = width + x;
    int heightOffset = height + y;
    for (int yi = y; yi < heightOffset; ++yi)
    {
        for (int xi = x; xi < widthOffset; ++xi)
        {
            int i = xi + (yi * window->compositeWidth);
            pixels[i] = color;
        }
    }
}

void DrawButtons()
{
    int x = window->compositeWidth - (24 + 4);
    BlitRect(window->surfaceBuffer.pixels, x, 4, 24, 24, ToColor(255, 0, 0, 255));

    x -= 24 + 4;
    BlitRect(window->surfaceBuffer.pixels, x, 4, 24, 24, ToColor(0, 255, 0, 255));

    x -= 24 + 4;
    BlitRect(window->surfaceBuffer.pixels, x, 4, 24, 24, ToColor(0, 0, 255, 255));
}

int CreateSurfaceBuffer(struct SurfaceBuffer* buffer, struct wl_surface* surface, char* name, uint32_t color)
{
    buffer->stride = buffer->width * sizeof(uint32_t);
    buffer->shm_pool_size = buffer->height * buffer->stride;

    if (name != NULL)
    {
        if (buffer->name != NULL) free(buffer->name);
        size_t nameSize = strlen(name);
        buffer->name = malloc(nameSize);
        memcpy(buffer->name, name, nameSize);
    }

    buffer->fd = shm_open(buffer->name, O_RDWR | O_CREAT | O_EXCL, 0600);
    shm_unlink(buffer->name);
    if (buffer->fd < 0 || errno == EEXIST) return 0;
    int result = ftruncate(buffer->fd, buffer->shm_pool_size);
    if (result < 0 || errno == EINTR) return 0;

    buffer->pixels = mmap(NULL, buffer->shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->fd, 0);
    buffer->pool = wl_shm_create_pool(shm, buffer->fd, buffer->shm_pool_size);

    int index = 0;
    int offset = buffer->height * buffer->stride * index;
    buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, offset, buffer->width, buffer->height, buffer->stride, WL_SHM_FORMAT_XRGB8888);
    memset(buffer->pixels, 0, buffer->width * buffer->height);
    buffer->color = color;
    for (int i = 0; i < buffer->width * buffer->height; ++i)
    {
        buffer->pixels[i] = color;
    }

    wl_surface_attach(surface, buffer->buffer, 0, 0);
    return 1;
}

int ResizeSurfaceBuffer(struct SurfaceBuffer* buffer, struct wl_surface* surface)
{
    // dispose old buffer
    munmap(buffer->pixels, buffer->shm_pool_size);
    wl_shm_pool_destroy(buffer->pool);

    // create new buffer
    CreateSurfaceBuffer(buffer, surface, NULL, buffer->color);
    return 1;
}

void SetWindowSize(int width, int height)
{
    window->width = width;
    window->height = height;
    if (useClientDecorations)
    {
        window->compositeWidth = window->width + (DECORATIONS_BAR_SIZE * 2);
        window->compositeHeight = window->height + (DECORATIONS_BAR_SIZE + DECORATIONS_TOPBAR_SIZE);
        window->surfaceBuffer.width = window->compositeWidth;
        window->surfaceBuffer.height = window->compositeHeight;
        window->clientSurfaceBuffer.width = width;
        window->clientSurfaceBuffer.height = height;
    }
    else
    {
        window->compositeWidth = width;
        window->compositeHeight = height;
        window->surfaceBuffer.width = width;
        window->surfaceBuffer.height = height;
        window->clientSurfaceBuffer.width = -1;
        window->clientSurfaceBuffer.height = -1;
    }
}

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
    useClientDecorations = (decoration_manager == NULL) ? 1 : 0;

    // create window
    window = (struct Window*)calloc(1, sizeof(Window));
    SetWindowSize(320, 240);

    // create window surface
    window->surface = wl_compositor_create_surface(compositor);
    window->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, window->surface);
    window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
    xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);
    xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener, window);
    xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, window);
    xdg_toplevel_set_title(window->xdg_toplevel, "WaylandClientWindow");
    xdg_toplevel_set_app_id(window->xdg_toplevel, "WaylandClientWindow");

    // get server-side decorations
    if (!useClientDecorations)
    {
        decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, window->xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(decoration, &decoration_listener, NULL);
    }

    // surface buffers
    if (CreateSurfaceBuffer(&window->surfaceBuffer, window->surface, "WaylandClientWindow_Decorations", ToColor(127, 127, 127, 255)) != 1) return 0;
    if (useClientDecorations)
    {
        window->clientSurface = wl_compositor_create_surface(compositor);
        window->clientSubSurface = wl_subcompositor_get_subsurface(subcompositor, window->clientSurface, window->surface);
        wl_subsurface_set_desync(window->clientSubSurface);
        wl_subsurface_set_position(window->clientSubSurface, DECORATIONS_BAR_SIZE, DECORATIONS_TOPBAR_SIZE);
        if (CreateSurfaceBuffer(&window->clientSurfaceBuffer, window->clientSurface, "WaylandClientWindow_Client", ToColor(255, 255, 255, 255)) != 1) return 0;
    }

    // draw buttons
    DrawButtons();

    // finalize surfaces
    wl_surface_damage(window->surface, 0, 0, window->surfaceBuffer.width, window->surfaceBuffer.height);
    wl_surface_commit(window->surface);
    if (useClientDecorations)
    {
        wl_surface_damage(window->clientSurface, 0, 0, window->clientSurfaceBuffer.width, window->clientSurfaceBuffer.height);
        wl_surface_commit(window->clientSurface);
    }
    wl_display_flush(display);

    // event loop
    while (running)
    {
        //wl_display_dispatch_pending (display);
        if (wl_display_dispatch(display) < 0) break;
    }

    // shutdown
    if (useClientDecorations)
    {
        munmap(window->clientSurfaceBuffer.pixels, window->clientSurfaceBuffer.shm_pool_size);
        wl_shm_pool_destroy(window->clientSurfaceBuffer.pool);
    }
    munmap(window->surfaceBuffer.pixels, window->surfaceBuffer.shm_pool_size);
    wl_shm_pool_destroy(window->surfaceBuffer.pool);

    if(xdg_wm_base != NULL)
    {
        xdg_toplevel_destroy(window->xdg_toplevel);
        xdg_surface_destroy(window->xdg_surface);
    }

    if (decoration != NULL) zxdg_toplevel_decoration_v1_destroy(decoration);
    if (useClientDecorations) wl_surface_destroy(window->clientSurface);
    wl_surface_destroy(window->surface);
    wl_display_disconnect(display);
    return 0;
}

void registry_add_object(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (!strcmp(interface,wl_compositor_interface.name))
    {
        compositor = (struct wl_compositor*)(wl_registry_bind (registry, name, &wl_compositor_interface, 1));
    }
    else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
    {
        subcompositor = (struct wl_subcompositor*)(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    }
    else if (!strcmp(interface,wl_seat_interface.name))
    {
        seat = (struct wl_seat*)(wl_registry_bind (registry, name, &wl_seat_interface, 1));
        wl_seat_add_listener (seat, &seat_listener, data);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        shm = (struct wl_shm*)(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        cursor_theme = wl_cursor_theme_load(NULL, 32, shm);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        xdg_wm_base = (struct xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, MIN(version, 2));
    }
    else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
    {
        decoration_manager = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
    }
}

void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name)
{
    // do nothing...
}

void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    if (capabilities & WL_SEAT_CAPABILITY_POINTER)
    {
        struct wl_pointer *pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener, data);
        cursor_surface = wl_compositor_create_surface(compositor);
    }

    /*if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
    {
        struct wl_keyboard *keyboard = wl_seat_get_keyboard (seat);
        wl_keyboard_add_listener (keyboard, &keyboard_listener, NULL);
    }*/
}

void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y)
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

void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
    // TODO
}

void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    // TODO
}

void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        printf("xdg_toplevel_move\n");
        xdg_toplevel_move(window->xdg_toplevel, seat, serial);
        //xdg_toplevel_set_minimized(window->xdg_toplevel);
        //xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
    }

    /*window *w = static_cast<window*>(data);
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

void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    // TODO
}

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
    if (decoration_manager != NULL) zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states)
{
    if (width <= 0 || height <= 0) return;
    if (width == window->compositeWidth && height == window->compositeHeight) return;

    int clientWidth = width;
    int clientHeight = height;
    if (useClientDecorations)
    {
        clientWidth = width - (DECORATIONS_BAR_SIZE * 2);
        clientHeight = height - (DECORATIONS_BAR_SIZE + DECORATIONS_TOPBAR_SIZE);
    }
    SetWindowSize(clientWidth, clientHeight);

    if (useClientDecorations)
    {
        ResizeSurfaceBuffer(&window->clientSurfaceBuffer, window->clientSurface);
        wl_surface_damage(window->clientSurface, 0, 0, window->clientSurfaceBuffer.width, window->clientSurfaceBuffer.height);
        wl_surface_commit(window->clientSurface);
    }

    ResizeSurfaceBuffer(&window->surfaceBuffer, window->surface);
    wl_surface_damage(window->surface, 0, 0, window->surfaceBuffer.width, window->surfaceBuffer.height);
    wl_surface_commit(window->surface);

    xdg_surface_set_window_geometry(window->xdg_surface, 0, 0, width, height);
    wl_display_flush(display);
}

void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
    running = 0;
}

void xdg_wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    xdg_wm_base_pong(base, serial);
}

void decoration_handle_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, enum zxdg_toplevel_decoration_v1_mode mode)
{
	printf("decoration_handle_configure: %d\n", mode);
	current_mode = mode;

    if (useClientDecorations) wl_surface_commit(window->clientSurface);
    wl_surface_commit(window->surface);
    wl_display_flush(display);
}
