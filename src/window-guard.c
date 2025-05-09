
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

Display *dpy;
Window root;

typedef struct {
    int x, y, width, height;
} Monitor;

Monitor main_monitor, secondary_monitor;
Window ticket_window = 0;

char TARGET_TITLE[256] = "";

void load_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "⚠️ Can't open config file: %s. Using default name.\n", filename);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "title=", 6) == 0) {
            strncpy(TARGET_TITLE, line + 6, sizeof(TARGET_TITLE) - 1);
            TARGET_TITLE[strcspn(TARGET_TITLE, "\r\n")] = 0;
        }
    }

    fclose(file);
}

char *get_window_title(Window w) {
    Atom prop = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop_ret = NULL;

    if (XGetWindowProperty(dpy, w, prop, 0, (~0L), False, AnyPropertyType,
                           &actual_type, &actual_format, &nitems,
                           &bytes_after, &prop_ret) == Success && prop_ret) {
        return (char *)prop_ret;
    }

    return NULL;
}

int is_dock_or_desktop(Window w) {
    Atom type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom dock_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom desktop_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    Atom *prop = NULL;

    if (XGetWindowProperty(dpy, w, type_atom, 0, 1024, False, XA_ATOM,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, (unsigned char **)&prop) != Success) {
        return 0;
    }

    int is_special = 0;
    for (unsigned long i = 0; i < nitems; ++i) {
        if (prop[i] == dock_atom || prop[i] == desktop_atom) {
            is_special = 1;
            break;
        }
    }

    if (prop) XFree(prop);
    return is_special;
}

void detect_monitors() {
    XRRScreenResources *res = XRRGetScreenResources(dpy, root);
    int count = 0;

    for (int i = 0; i < res->noutput; i++) {
        XRROutputInfo *output = XRRGetOutputInfo(dpy, res, res->outputs[i]);
        if (output->connection == RR_Connected && output->crtc) {
            XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, res, output->crtc);
            Monitor m = {crtc->x, crtc->y, crtc->width, crtc->height};
            if (count == 0)
                main_monitor = m;
            else
                secondary_monitor = m;
            count++;
            XRRFreeCrtcInfo(crtc);
        }
        XRRFreeOutputInfo(output);
    }
    XRRFreeScreenResources(res);
}

int get_window_geometry(Window w, int *x, int *y, int *width, int *height) {
    Window child;
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, w, &attr)) return 0;

    *width = attr.width;
    *height = attr.height;

    XTranslateCoordinates(dpy, w, root, 0, 0, x, y, &child);
    return 1;
}

int window_frame_outside_main_monitor(Window w) {
    int x, y, w_width, w_height;
    if (!get_window_geometry(w, &x, &y, &w_width, &w_height)) return 0;

    int mx1 = main_monitor.x;
    int my1 = main_monitor.y;
    int mx2 = main_monitor.x + main_monitor.width;
    int my2 = main_monitor.y + main_monitor.height;

    int wx1 = x;
    int wy1 = y;
    int wx2 = x + w_width;
    int wy2 = y + w_height;

    return (wx1 < mx1 || wy1 < my1 || wx2 > mx2 || wy2 > my2);
}

void move_window(Window w, Monitor *m) {
    int x = m->x + 100;
    int y = m->y + 100;
    XMoveWindow(dpy, w, x, y);
}

void set_window_always_on_top(Window w) {
    Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);

    XChangeProperty(dpy, w, wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&above, 1);
}

void restrict_mouse_to_main_monitor() {
    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;

    XQueryPointer(dpy, root, &root_ret, &child_ret,
                  &root_x, &root_y, &win_x, &win_y, &mask);

    int min_x = main_monitor.x;
    int min_y = main_monitor.y;
    int max_x = main_monitor.x + main_monitor.width - 1;
    int max_y = main_monitor.y + main_monitor.height - 1;

    int new_x = root_x;
    int new_y = root_y;

    if (root_x < min_x) new_x = min_x + 2;
    else if (root_x > max_x) new_x = max_x - 2;

    if (root_y < min_y) new_y = min_y + 2;
    else if (root_y > max_y) new_y = max_y - 2;

    if (new_x != root_x || new_y != root_y) {
        XWarpPointer(dpy, None, root, 0, 0, 0, 0, new_x, new_y);
    }
}

void process_windows() {
    Atom clientListAtom = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    Atom type_ret;
    int format_ret;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    ticket_window = 0;

    if (XGetWindowProperty(dpy, root, clientListAtom, 0, ~0, False, XA_WINDOW,
                           &type_ret, &format_ret, &nitems,
                           &bytes_after, &data) == Success && data) {
        Window *list = (Window *)data;

        for (unsigned long i = 0; i < nitems; i++) {
            Window w = list[i];
            char *title = get_window_title(w);
            if (title) {
                if (strstr(title, TARGET_TITLE)) {
                    ticket_window = w;
                    move_window(w, &secondary_monitor);
                    set_window_always_on_top(w);
                } else if (!is_dock_or_desktop(w) && window_frame_outside_main_monitor(w)) {
                    move_window(w, &main_monitor);
                }
                XFree(title);
            }
        }

        XFree(data);
    }

    restrict_mouse_to_main_monitor();
}

int main() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "💥 Can't connect to X-server.\n");
        return 1;
    }

    root = DefaultRootWindow(dpy);
    load_config("/etc/window-guard.conf");
    if (strlen(TARGET_TITLE) == 0) strcpy(TARGET_TITLE, "Ticket Info");

    while (1) {
        detect_monitors();
        process_windows();
        XFlush(dpy);
        usleep(50000);
    }

    XCloseDisplay(dpy);
    return 0;
}
