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
Window target_window = 0;

char TARGET_TITLE[256] = "";

// Load config file
void load_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "⚠️ Can't open config file: %s. Using default title.\n", filename);
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

void unmaximize_window(Window w) {
    Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom max_horz = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom max_vert = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);

    XEvent xev = {0};
    xev.xclient.type = ClientMessage;
    xev.xclient.serial = 0;
    xev.xclient.send_event = True;
    xev.xclient.message_type = wm_state;
    xev.xclient.window = w;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 0;
    xev.xclient.data.l[1] = max_horz;
    xev.xclient.data.l[2] = max_vert;
    xev.xclient.data.l[3] = 0;
    xev.xclient.data.l[4] = 0;

    XSendEvent(dpy, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    XFlush(dpy);
    usleep(10000);
}

void move_window(Window w, Monitor *m) {
    unmaximize_window(w);
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

int is_fully_on_monitor(int x, int y, int w, int h, Monitor *m) {
    return x >= m->x &&
           y >= m->y &&
           (x + w) <= (m->x + m->width) &&
           (y + h) <= (m->y + m->height);
}

void process_windows() {
    Atom clientListAtom = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    Atom type_ret;
    int format_ret;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, root, clientListAtom, 0, ~0, False, XA_WINDOW,
                           &type_ret, &format_ret, &nitems,
                           &bytes_after, &data) == Success && data) {
        Window *list = (Window *)data;

        for (unsigned long i = 0; i < nitems; i++) {
            Window w = list[i];
            char *title = get_window_title(w);
            if (!title) continue;

            int x, y, w_width, w_height;
            if (!get_window_geometry(w, &x, &y, &w_width, &w_height)) {
                XFree(title);
                continue;
            }

            int is_target = strstr(title, TARGET_TITLE) != NULL;
            int is_on_secondary = !is_fully_on_monitor(x, y, w_width, w_height, &main_monitor);

            if (is_target) {
                move_window(w, &secondary_monitor);
                set_window_always_on_top(w);
                printf("✅ Matched window '%s' stays on secondary.\n", title);
            } else if (!is_dock_or_desktop(w) && is_on_secondary) {
                move_window(w, &main_monitor);
                printf("↩️ Window '%s' moved back to main.\n", title);
            }

            XFree(title);
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
