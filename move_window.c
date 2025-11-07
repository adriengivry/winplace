#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Program to tile/move and resize windows on Linux Mint Cinnamon (X11)
 * Usage: ./move_window <x> <y> <width> <height> [window_name]
 * Example: ./move_window 0 0 960 1080
 * Example: ./move_window 0 0 960 1080 "Firefox"
 * 
 * This program properly accounts for window decorations including:
 * - _GTK_FRAME_EXTENTS (invisible borders/shadows used by GTK apps)
 * - _NET_FRAME_EXTENTS (standard frame extents)
 * 
 * The x, y, width, and height parameters refer to the VISIBLE window area
 * you want on screen (excluding invisible shadows/borders).
 * 
 * If window_name is provided, the program will search for a window with that
 * name instead of using the currently active window.
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

    // Try _NET_WM_NAME first (UTF8)
    if (XGetWindowProperty(display, window, net_wm_name, 0, 1024, False,
                          utf8_string, &type, &format, &nitems, &bytes_after,
                          &prop) == Success) {
        if (prop) {
            name = strdup((char *)prop);
            XFree(prop);
            return name;
        }
    }

    // Fall back to WM_NAME
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

    // Search child windows
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int nchildren;

    if (XQueryTree(display, window, &root_return, &parent_return, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            if (search_windows_recursive(display, children[i], search_name, result)) {
                XFree(children);
                return 1;
            }
        }
        if (children) {
            XFree(children);
        }
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
    long left;
    long right;
    long top;
    long bottom;
} FrameExtents;

int get_gtk_frame_extents(Display *display, Window window, FrameExtents *extents) {
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop;
    
    extents->left = extents->right = extents->top = extents->bottom = 0;
    
    Atom gtk_frame_extents_atom = XInternAtom(display, "_GTK_FRAME_EXTENTS", False);
    if (XGetWindowProperty(display, window, gtk_frame_extents_atom, 0, 4, False,
                          XA_CARDINAL, &type, &format, &nitems, &bytes_after,
                          &prop) == Success) {
        if (prop && nitems == 4) {
            long *borders = (long *)prop;
            extents->left = borders[0];
            extents->right = borders[1];
            extents->top = borders[2];
            extents->bottom = borders[3];
            XFree(prop);
            printf("_GTK_FRAME_EXTENTS: left=%ld, right=%ld, top=%ld, bottom=%ld\n",
                   extents->left, extents->right, extents->top, extents->bottom);
            return 1;
        }
        if (prop) {
            XFree(prop);
        }
    }
    
    return 0;
}

int get_net_frame_extents(Display *display, Window window, FrameExtents *extents) {
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop;
    
    extents->left = extents->right = extents->top = extents->bottom = 0;
    
    Atom net_frame_extents_atom = XInternAtom(display, "_NET_FRAME_EXTENTS", False);
    if (XGetWindowProperty(display, window, net_frame_extents_atom, 0, 4, False,
                          XA_CARDINAL, &type, &format, &nitems, &bytes_after,
                          &prop) == Success) {
        if (prop && nitems == 4) {
            long *borders = (long *)prop;
            extents->left = borders[0];
            extents->right = borders[1];
            extents->top = borders[2];
            extents->bottom = borders[3];
            XFree(prop);
            printf("_NET_FRAME_EXTENTS: left=%ld, right=%ld, top=%ld, bottom=%ld\n",
                   extents->left, extents->right, extents->top, extents->bottom);
            return 1;
        }
        if (prop) {
            XFree(prop);
        }
    }
    
    return 0;
}

void unmaximize_window(Display *display, Window window) {
    Window root = DefaultRootWindow(display);
    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom max_vert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atom max_horz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = wm_state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 0; // _NET_WM_STATE_REMOVE
    event.xclient.data.l[1] = max_vert;
    event.xclient.data.l[2] = max_horz;
    event.xclient.data.l[3] = 1; // source indication: application
    
    XSendEvent(display, root, False, 
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    
    XFlush(display);
    usleep(50000); // 50ms delay to let WM process
}

void set_window_gravity(Display *dpy, Window win, int gravity) {
    XSizeHints hints;
    long supplied;

    // Get existing hints if any
    if (XGetWMNormalHints(dpy, win, &hints, &supplied) == 0) {
        hints.flags = 0;
    }

    hints.flags |= PWinGravity;
    hints.win_gravity = gravity;

    XSetWMNormalHints(dpy, win, &hints);
}

void move_resize_window(Display *display, Window window, int target_x, int target_y, 
                        unsigned int target_width, unsigned int target_height) {
    printf("Target position/size (visible area): x=%d, y=%d, w=%u, h=%u\n",
           target_x, target_y, target_width, target_height);
    
    // First, unmaximize the window if needed
    unmaximize_window(display, window);
    
    int client_x = target_x;
    int client_y = target_y;

    unsigned int client_width = target_width;
    unsigned int client_height = target_height;

    FrameExtents extents;

    // _GTK_FRAME_EXTENTS tell you how much EXTRA SPACE to REMOVE from your Window Size calculations.
    if (get_gtk_frame_extents(display, window, &extents)) {
        client_x -= extents.left;
        client_y -= extents.top;
        client_width += extents.left + extents.right;
        client_height += extents.top + extents.bottom;
    }
    
    // _NET_FRAME_EXTENTS tell you how much EXTRA SPACE to ADD to your Window Size calculations.
    if (get_net_frame_extents(display, window, &extents)) {
        set_window_gravity(display, window, NorthWestGravity);
        client_width -= extents.left + extents.right;
        client_height -= extents.top + extents.bottom;
    }
    else {
        // No frame extents found
        printf("No frame extents found - window may be undecorated\n");
    }
    
    // Ensure dimensions remain positive
    if ((int)client_width < 1) client_width = 1;
    if ((int)client_height < 1) client_height = 1;
    
    printf("Calculated client window: x=%d, y=%d, w=%u, h=%u\n",
           client_x, client_y, client_width, client_height);
    
    // Move and resize the window
    XMoveResizeWindow(display, window, client_x, client_y, client_width, client_height);
    
    // Ensure changes are sent to the X server
    XFlush(display);
    XSync(display, False);
}

int main(int argc, char *argv[]) {
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s <x> <y> <width> <height> [window_name]\n", argv[0]);
        fprintf(stderr, "Example: %s 0 0 960 1080\n", argv[0]);
        fprintf(stderr, "Example: %s 0 0 960 1080 \"Firefox\"\n", argv[0]);
        fprintf(stderr, "\nNote: Coordinates refer to the visible window area (excluding shadows).\n");
        fprintf(stderr, "The program automatically accounts for _GTK_FRAME_EXTENTS and _NET_FRAME_EXTENTS.\n");
        fprintf(stderr, "\nIf window_name is not provided, the currently active window will be used.\n");
        fprintf(stderr, "If window_name is provided, the program will search for a window containing that name.\n");
        return 1;
    }

    // Parse command line arguments
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    unsigned int width = atoi(argv[3]);
    unsigned int height = atoi(argv[4]);
    const char *window_name = (argc == 6) ? argv[5] : NULL;

    // Validate dimensions
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "Error: Width and height must be positive integers\n");
        return 1;
    }

    // Open connection to X server
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Error: Cannot open display\n");
        return 1;
    }

    // Get the target window (either by name or active window)
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

    // Move and resize the window
    move_resize_window(display, target_window, x, y, width, height);

    // Close connection to X server
    XCloseDisplay(display);

    printf("Window tiled successfully\n");
    return 0;
}
