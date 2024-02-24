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
#include <math.h>

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

void xdg_toplevelconfigure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height);
void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states);
void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel);
struct xdg_toplevel_listener xdg_toplevel_listener = {.configure_bounds = xdg_toplevelconfigure_bounds, .configure = xdg_toplevel_handle_configure, .close = xdg_toplevel_handle_close};

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

struct wl_surface* mouseHoverSurface = NULL;
uint32_t mouseHoverSerial = -1;
int mouseX = -1, mouseY = -1;
int mouseX_Client = -1, mouseY_Client = -1;

typedef struct Rect
{
    int x, y, width, height;
}Rect;

typedef struct SurfaceBuffer
{
    int width, height;
    uint32_t color;
    char* name;
    int fd;
    int stride, size;
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    uint32_t *pixels;
}SurfaceBuffer;

typedef struct Window
{
    int width, height;
    int compositeWidth, compositeHeight;
    int isMaximized;

    Rect clientRect_Drag_TopBar;
    Rect clientRect_Resize_LeftBar, clientRect_Resize_RightBar, clientRect_Resize_BottomBar, clientRect_Resize_TopBar;
    Rect clientRect_Resize_BottomLeft, clientRect_Resize_BottomRight, clientRect_Resize_TopLeft, clientRect_Resize_TopRight;
    Rect clientRect_ButtonMin, clientRect_ButtonMax, clientRect_ButtonClose;

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

#define DECORATIONS_BAR_SIZE 8
#define DECORATIONS_TOPBAR_SIZE 32
#define DECORATIONS_BUTTON_SIZE 32

Rect CreateRect(int x, int y, int width, int height)
{
    Rect rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}

int WithinRect(Rect rect, int x, int y)
{
    return x >= rect.x && x <= (rect.x + rect.width) && y >= rect.y && y <= (rect.y + rect.height);
}

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

void BlitPoint(uint32_t* pixels, int x, int y, uint32_t color)
{
    int i = x + (y * window->compositeWidth);
    if (i >= 0 && i < window->surfaceBuffer.size) pixels[i] = color;
}

void BlitLine(uint32_t* pixels, int x, int y, int velX, int velY, int stepCount, uint32_t color)
{
    for (int i = 0; i != stepCount; ++i)
    {
        BlitPoint(pixels, x, y, color);
        x += velX;
        y += velY;
    }
}

void BlitRect(uint32_t* pixels, int x, int y, int width, int height, uint32_t color)
{
    int widthOffset = width + x;
    int heightOffset = height + y;
    for (int yi = y; yi < heightOffset; ++yi)
    {
        for (int xi = x; xi < widthOffset; ++xi)
        {
            BlitPoint(pixels, xi, yi, color);
        }
    }
}

void DrawButtons()
{
    int x = window->compositeWidth - (24 + 4);
    Rect rect = window->clientRect_ButtonClose;
    BlitRect(window->surfaceBuffer.pixels, rect.x, rect.y, rect.width, rect.height, ToColor(255, 0, 0, 255));
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 4, 1, 1, 16, ToColor(0, 0, 0, 255));// cross-right
    BlitLine(window->surfaceBuffer.pixels, rect.x + 3, rect.y + 4, 1, 1, 16, ToColor(0, 0, 0, 255));// cross-right 2
    BlitLine(window->surfaceBuffer.pixels, rect.x + 5, rect.y + 4, 1, 1, 16, ToColor(0, 0, 0, 255));// cross-right 3
    BlitLine(window->surfaceBuffer.pixels, rect.x + 18, rect.y + 4, -1, 1, 16, ToColor(0, 0, 0, 255));// cross-left
    BlitLine(window->surfaceBuffer.pixels, rect.x + 17, rect.y + 4, -1, 1, 16, ToColor(0, 0, 0, 255));// cross-left 2
    BlitLine(window->surfaceBuffer.pixels, rect.x + 19, rect.y + 4, -1, 1, 16, ToColor(0, 0, 0, 255));// cross-left 3

    x -= 24 + 4;
    rect = window->clientRect_ButtonMax;
    BlitRect(window->surfaceBuffer.pixels, rect.x, rect.y, rect.width, rect.height, ToColor(0, 255, 0, 255));
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 20, 1, 0, 16, ToColor(0, 0, 0, 255));// bottom
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 4, 1, 0, 16, ToColor(0, 0, 0, 255));// top
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 5, 1, 0, 16, ToColor(0, 0, 0, 255));// top 2
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 6, 1, 0, 16, ToColor(0, 0, 0, 255));// top 3
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 4, 0, 1, 16, ToColor(0, 0, 0, 255));// left
    BlitLine(window->surfaceBuffer.pixels, rect.x + 19, rect.y + 4, 0, 1, 16, ToColor(0, 0, 0, 255));// right

    x -= 24 + 4;
    rect = window->clientRect_ButtonMin;
    BlitRect(window->surfaceBuffer.pixels, rect.x, rect.y, rect.width, rect.height, ToColor(0, 0, 255, 255));
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 20, 1, 0, 16, ToColor(0, 0, 0, 255));// line
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 19, 1, 0, 16, ToColor(0, 0, 0, 255));// line 2
    BlitLine(window->surfaceBuffer.pixels, rect.x + 4, rect.y + 18, 1, 0, 16, ToColor(0, 0, 0, 255));// line 3
}

int CreateSurfaceBuffer(struct SurfaceBuffer* buffer, struct wl_surface* surface, char* name, uint32_t color)
{
    // get buffer sizes
    int oldSize = buffer->size;
    buffer->stride = buffer->width * sizeof(uint32_t);
    buffer->size = buffer->height * buffer->stride;

    // alloc name if needed
    if (name != NULL)
    {
        if (buffer->name != NULL) free(buffer->name);
        size_t nameSize = strlen(name);
        buffer->name = malloc(nameSize);
        memcpy(buffer->name, name, nameSize);
    }

    // only create new file if needed
    if (buffer->fd < 0)
    {
        buffer->fd = shm_open(buffer->name, O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (buffer->fd < 0 || errno == EEXIST) return 0;
    }

    // set file size
    if (buffer->size > oldSize)// only increase file size or we can get buffer access violations in pool
    {
        int result = ftruncate(buffer->fd, buffer->size);
        if (result < 0 || errno == EINTR) return 0;
    }

    // map memory
    buffer->pixels = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->fd, 0);
    shm_unlink(buffer->name);// call after mmap according to docs
    memset(buffer->pixels, color, buffer->width * buffer->height * sizeof(uint32_t));// clear to color

    // create pool
    buffer->pool = wl_shm_create_pool(shm, buffer->fd, buffer->size);
    buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, 0, buffer->width, buffer->height, buffer->stride, WL_SHM_FORMAT_XRGB8888);
    buffer->color = color;

    wl_surface_attach(surface, buffer->buffer, 0, 0);
    return 1;
}

int ResizeSurfaceBuffer(struct SurfaceBuffer* buffer, struct wl_surface* surface)
{
    // dispose old buffer
    munmap(buffer->pixels, buffer->size);
    wl_shm_pool_destroy(buffer->pool);
    wl_buffer_destroy(buffer->buffer);

    // create new buffer
    return CreateSurfaceBuffer(buffer, surface, NULL, buffer->color);
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

        // CSD rects
        window->clientRect_Drag_TopBar = CreateRect(0, 0, window->compositeWidth, DECORATIONS_TOPBAR_SIZE);

        window->clientRect_Resize_LeftBar = CreateRect(0, 0, DECORATIONS_BAR_SIZE, window->compositeHeight);
        window->clientRect_Resize_RightBar = CreateRect(window->compositeWidth - DECORATIONS_BAR_SIZE, 0, DECORATIONS_BAR_SIZE, window->compositeHeight);
        window->clientRect_Resize_BottomBar = CreateRect(0, window->compositeHeight - DECORATIONS_BAR_SIZE, window->compositeWidth, DECORATIONS_BAR_SIZE);
        window->clientRect_Resize_TopBar = CreateRect(0, 0, window->compositeWidth, DECORATIONS_BAR_SIZE);

        window->clientRect_Resize_TopLeft = CreateRect(0, 0, DECORATIONS_BAR_SIZE, DECORATIONS_BAR_SIZE);
        window->clientRect_Resize_TopRight = CreateRect(window->compositeWidth - DECORATIONS_BAR_SIZE, 0, DECORATIONS_BAR_SIZE, DECORATIONS_BAR_SIZE);
        window->clientRect_Resize_BottomLeft = CreateRect(0, window->compositeHeight - DECORATIONS_BAR_SIZE, DECORATIONS_BAR_SIZE, DECORATIONS_BAR_SIZE);
        window->clientRect_Resize_BottomRight = CreateRect(window->compositeWidth - DECORATIONS_BAR_SIZE, window->compositeHeight - DECORATIONS_BAR_SIZE, DECORATIONS_BAR_SIZE, DECORATIONS_BAR_SIZE);

        int x = window->compositeWidth - (24 + 4);
        window->clientRect_ButtonClose = CreateRect(x, 4, 24, 24);
        x -= 24 + 4;
        window->clientRect_ButtonMax = CreateRect(x, 4, 24, 24);
        x -= 24 + 4;
        window->clientRect_ButtonMin = CreateRect(x, 4, 24, 24);
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
    window->surfaceBuffer.fd = -1;
    window->clientSurfaceBuffer.fd = -1;
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
    xdg_toplevel_set_min_size(window->xdg_toplevel, 100, 100);

    // get server-side decorations
    if (!useClientDecorations && decoration_manager != NULL)
    {
        decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, window->xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(decoration, &decoration_listener, NULL);
    }

    // surface buffers
    uint32_t color = useClientDecorations ? ToColor(127, 127, 127, 255) : ToColor(255, 255, 255, 255);
    if (CreateSurfaceBuffer(&window->surfaceBuffer, window->surface, "WaylandClientWindow_Decorations", color) != 1) return 0;
    if (useClientDecorations)
    {
        window->clientSurface = wl_compositor_create_surface(compositor);
        window->clientSubSurface = wl_subcompositor_get_subsurface(subcompositor, window->clientSurface, window->surface);
        wl_subsurface_set_desync(window->clientSubSurface);
        wl_subsurface_set_position(window->clientSubSurface, DECORATIONS_BAR_SIZE, DECORATIONS_TOPBAR_SIZE);
        if (CreateSurfaceBuffer(&window->clientSurfaceBuffer, window->clientSurface, "WaylandClientWindow_Client", ToColor(255, 255, 255, 255)) != 1) return 0;
        DrawButtons();
    }

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
        munmap(window->clientSurfaceBuffer.pixels, window->clientSurfaceBuffer.size);
        wl_shm_pool_destroy(window->clientSurfaceBuffer.pool);
        wl_buffer_destroy(window->clientSurfaceBuffer.buffer);
    }
    munmap(window->surfaceBuffer.pixels, window->surfaceBuffer.size);
    wl_shm_pool_destroy(window->surfaceBuffer.pool);
    wl_buffer_destroy(window->surfaceBuffer.buffer);

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

void SetMousePos(wl_fixed_t x, wl_fixed_t y)
{
    if (useClientDecorations)
    {
        if (mouseHoverSurface == NULL) return;
        if (mouseHoverSurface == window->surface)
        {
            mouseX = wl_fixed_to_int(x);
            mouseY = wl_fixed_to_int(y);
        }
        else if (mouseHoverSurface == window->clientSurface)
        {
            mouseX_Client = wl_fixed_to_int(x);
            mouseY_Client = wl_fixed_to_int(y);
        }
    }
    else
    {
        mouseX = mouseX_Client = wl_fixed_to_int(x);
        mouseY = mouseY_Client = wl_fixed_to_int(y);
    }
}

void SetCursor(struct wl_pointer *pointer, uint32_t serial, char* name)
{
    struct wl_cursor* cursor = wl_cursor_theme_get_cursor(cursor_theme, name);
    if (cursor != NULL)
    {
        struct wl_cursor_image* image = cursor->images[0];
        wl_pointer_set_cursor(pointer, serial, cursor_surface, image->hotspot_x, image->hotspot_y);
        wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(image), 0, 0);
        wl_surface_damage(cursor_surface, 0, 0, image->width, image->height);
        wl_surface_commit(cursor_surface);
    }
}

void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    mouseHoverSurface = surface;
    mouseHoverSerial = serial;
    SetMousePos(x, y);
    SetCursor(pointer, serial, "left_ptr");
}

void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
    mouseHoverSurface = NULL;
    serial = -1;
}

void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    SetMousePos(x, y);
}

void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    if (!useClientDecorations) return;
    if (button == BTN_LEFT)
    {
        if (mouseHoverSurface == window->surface)
        {
            if (state == WL_POINTER_BUTTON_STATE_RELEASED)
            {
                // buttons
                if (WithinRect(window->clientRect_ButtonClose, mouseX, mouseY))
                {
                    running = 0;
                }
                else if (WithinRect(window->clientRect_ButtonMax, mouseX, mouseY))
                {
                    if (!window->isMaximized)
                    {
                        xdg_toplevel_set_maximized(window->xdg_toplevel);
                        //xdg_toplevel_set_fullscreen(window->xdg_toplevel, NULL);
                    }
                    else
                    {
                        xdg_toplevel_unset_maximized(window->xdg_toplevel);
                        //xdg_toplevel_unset_fullscreen(window->xdg_toplevel);
                    }
                }
                else if (WithinRect(window->clientRect_ButtonMin, mouseX, mouseY))
                {
                    xdg_toplevel_set_minimized(window->xdg_toplevel);
                }
            }
            else if (state == WL_POINTER_BUTTON_STATE_PRESSED)
            {
                // drag
                if
                (
                    !WithinRect(window->clientRect_ButtonClose, mouseX, mouseY) && !WithinRect(window->clientRect_ButtonMax, mouseX, mouseY) && !WithinRect(window->clientRect_ButtonMin, mouseX, mouseY) &&
                    !WithinRect(window->clientRect_Resize_BottomBar, mouseX, mouseY) && !WithinRect(window->clientRect_Resize_TopBar, mouseX, mouseY) && !WithinRect(window->clientRect_Resize_LeftBar, mouseX, mouseY) && !WithinRect(window->clientRect_Resize_RightBar, mouseX, mouseY) &&
                    !WithinRect(window->clientRect_Resize_TopLeft, mouseX, mouseY) && !WithinRect(window->clientRect_Resize_TopRight, mouseX, mouseY) && !WithinRect(window->clientRect_Resize_BottomLeft, mouseX, mouseY) && !WithinRect(window->clientRect_Resize_BottomRight, mouseX, mouseY)
                )
                {
                    if (WithinRect(window->clientRect_Drag_TopBar, mouseX, mouseY)) xdg_toplevel_move(window->xdg_toplevel, seat, serial);
                }
                // resize corners
                else if (WithinRect(window->clientRect_Resize_TopLeft, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT);
                }
                else if (WithinRect(window->clientRect_Resize_TopRight, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT);
                }
                else if (WithinRect(window->clientRect_Resize_BottomLeft, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT);
                }
                else if (WithinRect(window->clientRect_Resize_BottomRight, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
                }
                // resize edges
                else if (WithinRect(window->clientRect_Resize_BottomBar, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
                }
                else if (WithinRect(window->clientRect_Resize_TopBar, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_TOP);
                }
                else if (WithinRect(window->clientRect_Resize_LeftBar, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_LEFT);
                }
                else if (WithinRect(window->clientRect_Resize_RightBar, mouseX, mouseY))
                {
                    xdg_toplevel_resize(window->xdg_toplevel, seat, serial, XDG_TOPLEVEL_RESIZE_EDGE_RIGHT);
                }
            }
        }
    }
}

void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    // TODO
}

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
    if (decoration_manager != NULL) zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

    // must commit here
    if (useClientDecorations) wl_surface_commit(window->clientSurface);
    wl_surface_commit(window->surface);
    wl_display_flush(display);
}

void xdg_toplevelconfigure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height)
{
    // do nothing...
}

void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states)
{
    int activated = 0;
    int maximized = 0;
    int fullscreen = 0;
    int resizing = 0;
    int floating = 1;
    const uint32_t *state = NULL;
    wl_array_for_each(state, states)
    {
        switch (*state)
        {
            case XDG_TOPLEVEL_STATE_ACTIVATED: activated = 1; break;
            case XDG_TOPLEVEL_STATE_RESIZING: resizing = 1; break;
            case XDG_TOPLEVEL_STATE_MAXIMIZED: maximized = 1; break;
            case XDG_TOPLEVEL_STATE_FULLSCREEN: fullscreen = 1; break;

            case XDG_TOPLEVEL_STATE_TILED_LEFT:
            case XDG_TOPLEVEL_STATE_TILED_RIGHT:
            case XDG_TOPLEVEL_STATE_TILED_TOP:
            case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
                floating = 0;
                break;
        }
    }

    // manage maximized state
    if (maximized)
    {
        window->isMaximized = 1;
    }
    else if (floating)
    {
        window->isMaximized = 0;
    }

    // resize window
    if (activated || resizing || maximized || fullscreen)
    {
        if (width >= 100 && height >= 100 && (window->compositeWidth != width || window->compositeHeight != height))
        {
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
            if (useClientDecorations) DrawButtons();
            wl_surface_damage(window->surface, 0, 0, window->surfaceBuffer.width, window->surfaceBuffer.height);
            wl_surface_commit(window->surface);

            wl_display_flush(display);
        }
    }
}

void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
    running = 0;
}

void xdg_wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    xdg_wm_base_pong(base, serial);
}

int decorationConfigureCount = 0;
void decoration_handle_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, enum zxdg_toplevel_decoration_v1_mode mode)
{
    current_mode = mode;

    if (decorationConfigureCount != 2)// for some reason this is spammed on KDE (so ignore after a couple iterations)
    {
        printf("decoration_handle_configure: %d\n", mode);
        decorationConfigureCount++;
        if (useClientDecorations) wl_surface_commit(window->clientSurface);
        wl_surface_commit(window->surface);
        wl_display_flush(display);
    }
}
