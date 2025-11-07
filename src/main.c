#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * winplace: Move and resize X11 windows with automatic frame compensation
 * 
 * Automatically accounts for window decorations (_GTK_FRAME_EXTENTS, _NET_FRAME_EXTENTS)
 * ensuring the visible window area matches the specified dimensions.
 */

Window get_active_window(Display *display) {
    Window root = DefaultRootWindow(display);
    Atom active_window_atom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop;
    Window active_window = 0;

    if (XGetWindowProperty(display, root, active_window_atom, 0, 1, False,
                          XA_WINDOW, &type, &format, &nitems, &bytes_after,
                          &prop) == Success) {
        if (prop) {
            active_window = *((Window *)prop);
            XFree(prop);
        }
    }

    return active_window;
}

char* get_window_name(Display *display, Window window) {
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop;
    char *name = NULL;

    if (XGetWindowProperty(display, window, net_wm_name, 0, 1024, False,
                          utf8_string, &type, &format, &nitems, &bytes_after,
                          &prop) == Success && prop) {
        name = strdup((char *)prop);
        XFree(prop);
        return name;
    }

    if (XFetchName(display, window, &name) && name) {
        char *result = strdup(name);
        XFree(name);
        return result;
    }

    return NULL;
}

int search_windows_recursive(Display *display, Window window, const char *search_name, Window *result) {
    char *window_name = get_window_name(display, window);
    
    if (window_name) {
        if (strstr(window_name, search_name) != NULL) {
            printf("Found matching window: '%s' (0x%lx)\n", window_name, window);
            free(window_name);
            *result = window;
            return 1;
        }
        free(window_name);
    }

    Window root_return, parent_return, *children = NULL;
    unsigned int nchildren;

    if (XQueryTree(display, window, &root_return, &parent_return, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            if (search_windows_recursive(display, children[i], search_name, result)) {
                XFree(children);
                return 1;
            }
        }
        if (children) XFree(children);
    }

    return 0;
}

Window find_window_by_name(Display *display, const char *name) {
    Window root = DefaultRootWindow(display);
    Window result = 0;

    printf("Searching for window with name containing: '%s'\n", name);
    
    if (search_windows_recursive(display, root, name, &result)) {
        return result;
    }

    return 0;
}


typedef struct {
    long left, right, top, bottom;
} FrameExtents;

int get_frame_extents(Display *display, Window window, const char *atom_name, FrameExtents *extents) {
    Atom type, atom = XInternAtom(display, atom_name, False);
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop;
    
    extents->left = extents->right = extents->top = extents->bottom = 0;
    
    if (XGetWindowProperty(display, window, atom, 0, 4, False, XA_CARDINAL,
                          &type, &format, &nitems, &bytes_after, &prop) == Success) {
        if (prop && nitems == 4) {
            long *borders = (long *)prop;
            extents->left = borders[0];
            extents->right = borders[1];
            extents->top = borders[2];
            extents->bottom = borders[3];
            XFree(prop);
            printf("%s: left=%ld, right=%ld, top=%ld, bottom=%ld\n",
                   atom_name, extents->left, extents->right, extents->top, extents->bottom);
            return 1;
        }
        if (prop) XFree(prop);
    }
    
    return 0;
}

void unmaximize_window(Display *display, Window window) {
    Window root = DefaultRootWindow(display);
    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom max_vert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atom max_horz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    
    XEvent event = {0};
    event.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = wm_state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 0;
    event.xclient.data.l[1] = max_vert;
    event.xclient.data.l[2] = max_horz;
    event.xclient.data.l[3] = 1;
    
    XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    usleep(50000);
}

void set_window_gravity(Display *display, Window window, int gravity) {
    XSizeHints hints;
    long supplied;

    if (XGetWMNormalHints(display, window, &hints, &supplied) == 0) {
        hints.flags = 0;
    }

    hints.flags |= PWinGravity;
    hints.win_gravity = gravity;
    XSetWMNormalHints(display, window, &hints);
}

void move_resize_window(Display *display, Window window, int target_x, int target_y, 
                        unsigned int target_width, unsigned int target_height) {
    printf("Target position/size (visible area): x=%d, y=%d, w=%u, h=%u\n",
           target_x, target_y, target_width, target_height);
    
    unmaximize_window(display, window);
    
    int client_x = target_x;
    int client_y = target_y;
    unsigned int client_width = target_width;
    unsigned int client_height = target_height;
    FrameExtents extents;

    if (get_frame_extents(display, window, "_GTK_FRAME_EXTENTS", &extents)) {
        client_x -= extents.left;
        client_y -= extents.top;
        client_width += extents.left + extents.right;
        client_height += extents.top + extents.bottom;
    }
    
    if (get_frame_extents(display, window, "_NET_FRAME_EXTENTS", &extents)) {
        set_window_gravity(display, window, NorthWestGravity);
        client_width -= extents.left + extents.right;
        client_height -= extents.top + extents.bottom;
    } else {
        printf("No frame extents found - window may be undecorated\n");
    }
    
    if ((int)client_width < 1) client_width = 1;
    if ((int)client_height < 1) client_height = 1;
    
    printf("Calculated client window: x=%d, y=%d, w=%u, h=%u\n",
           client_x, client_y, client_width, client_height);
    
    XMoveResizeWindow(display, window, client_x, client_y, client_width, client_height);
    XFlush(display);
    XSync(display, False);
}

int main(int argc, char *argv[]) {
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s <x> <y> <width> <height> [window_name]\n", argv[0]);
        fprintf(stderr, "Example: %s 0 0 960 1080\n", argv[0]);
        fprintf(stderr, "Example: %s 0 0 960 1080 \"Firefox\"\n", argv[0]);
        fprintf(stderr, "\nCoordinates refer to the visible window area (excluding shadows).\n");
        fprintf(stderr, "If window_name is omitted, the currently active window will be used.\n");
        return 1;
    }

    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    unsigned int width = atoi(argv[3]);
    unsigned int height = atoi(argv[4]);
    const char *window_name = (argc == 6) ? argv[5] : NULL;

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "Error: Width and height must be positive integers\n");
        return 1;
    }

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Error: Cannot open display\n");
        return 1;
    }

    Window target_window = 0;
    if (window_name) {
        target_window = find_window_by_name(display, window_name);
        if (!target_window) {
            fprintf(stderr, "Error: No window found with name containing '%s'\n", window_name);
            XCloseDisplay(display);
            return 1;
        }
    } else {
        target_window = get_active_window(display);
        if (!target_window) {
            fprintf(stderr, "Error: No active window found\n");
            XCloseDisplay(display);
            return 1;
        }
        printf("Using active window (0x%lx)\n", target_window);
    }

    printf("Moving window (0x%lx) to visible position (%d, %d) with size %ux%u\n",
           target_window, x, y, width, height);

    move_resize_window(display, target_window, x, y, width, height);
    XCloseDisplay(display);

    printf("Window tiled successfully\n");
    return 0;
}
