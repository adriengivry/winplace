#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Program to tile/move and resize windows on Linux Mint Cinnamon (X11)
 * Usage: ./move_window <x> <y> <width> <height>
 * Example: ./move_window 0 0 960 1080
 * 
 * This program properly accounts for window decorations including:
 * - _GTK_FRAME_EXTENTS (invisible borders/shadows used by GTK apps)
 * - _NET_FRAME_EXTENTS (standard frame extents)
 * 
 * The x, y, width, and height parameters refer to the VISIBLE window area
 * you want on screen (excluding invisible shadows/borders).
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

int get_frame_extents(Display *display, Window window, FrameExtents *extents) {
    // Try _GTK_FRAME_EXTENTS first (includes invisible shadows on GTK apps)
    if (get_gtk_frame_extents(display, window, extents)) {
        return 1;
    }
    
    // Fall back to _NET_FRAME_EXTENTS (standard visible decorations)
    if (get_net_frame_extents(display, window, extents)) {
        return 1;
    }
    
    // No frame extents found
    printf("No frame extents found - window may be undecorated\n");
    extents->left = extents->right = extents->top = extents->bottom = 0;
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

void move_resize_window(Display *display, Window window, int target_x, int target_y, 
                        unsigned int target_width, unsigned int target_height) {
    FrameExtents extents;
    
    printf("Target position/size (visible area): x=%d, y=%d, w=%u, h=%u\n",
           target_x, target_y, target_width, target_height);
    
    // First, unmaximize the window if needed
    unmaximize_window(display, window);
    
    // Get frame extents to account for window decorations
    get_frame_extents(display, window, &extents);
    
    // Calculate the client window position and size
    // The key insight: XMoveResizeWindow operates on the client window, 
    // NOT the outer frame. The window manager adds decorations around it.
    //
    // If we want the VISIBLE content at (target_x, target_y), we need to
    // account for invisible borders/shadows:
    // - GTK apps often have invisible shadows (_GTK_FRAME_EXTENTS)
    // - The client window needs to be positioned offset by these extents
    //
    // For the visible area to start at target_x, the client window x must be:
    // client_x = target_x - left_extent
    // (because the WM adds left_extent pixels to the left of the client window)
    
    int client_x = target_x - extents.left;
    int client_y = target_y - extents.top;
    
    // For the visible area to have target_width, the client window width must be:
    // client_width = target_width + left_extent + right_extent
    // (because the WM subtracts the extents from the outer frame)
    unsigned int client_width = target_width + extents.left + extents.right;
    unsigned int client_height = target_height + extents.top + extents.bottom;
    
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
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <x> <y> <width> <height>\n", argv[0]);
        fprintf(stderr, "Example: %s 0 0 960 1080\n", argv[0]);
        fprintf(stderr, "\nNote: Coordinates refer to the visible window area (excluding shadows).\n");
        fprintf(stderr, "The program automatically accounts for _GTK_FRAME_EXTENTS and _NET_FRAME_EXTENTS.\n");
        return 1;
    }

    // Parse command line arguments
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    unsigned int width = atoi(argv[3]);
    unsigned int height = atoi(argv[4]);

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

    // Get the active window
    Window active_window = get_active_window(display);
    if (!active_window) {
        fprintf(stderr, "Error: No active window found\n");
        XCloseDisplay(display);
        return 1;
    }

    printf("Moving active window (0x%lx) to visible position (%d, %d) with size %ux%u\n",
           active_window, x, y, width, height);

    // Move and resize the window
    move_resize_window(display, active_window, x, y, width, height);

    // Close connection to X server
    XCloseDisplay(display);

    printf("Window tiled successfully\n");
    return 0;
}
