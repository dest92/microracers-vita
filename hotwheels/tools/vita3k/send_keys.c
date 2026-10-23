/* Inyecta teclas en el Xvfb headless de vita3k_run.sh usando XTest. */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int XTestFakeKeyEvent(Display *, unsigned int, int, unsigned long);
extern int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);
extern int XTestFakeButtonEvent(Display *, unsigned int, int, unsigned long);

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static void wait_until(double deadline) {
    double left = deadline - now();
    if (left <= 0) return;
    struct timespec t = {(time_t)left, (long)((left - (time_t)left) * 1e9)};
    while (nanosleep(&t, &t) && t.tv_sec >= 0) {}
}
static int keycode(Display *d, const char *name) {
    KeySym sym = XStringToKeysym(name);
    return sym == NoSymbol ? 0 : (int)XKeysymToKeycode(d, sym);
}
static void focus_app_window(Display *d) {
    Window root = DefaultRootWindow(d), parent = 0, *children = NULL;
    unsigned count = 0;
    if (!XQueryTree(d, root, &root, &parent, &children, &count)) return;
    Window best = 0;
    long best_area = 0;
    for (unsigned i = 0; i < count; ++i) {
        XWindowAttributes a;
        if (XGetWindowAttributes(d, children[i], &a) && a.map_state == IsViewable) {
            long area = (long)a.width * a.height;
            if (area > best_area) { best_area = area; best = children[i]; }
        }
    }
    if (best) XSetInputFocus(d, best, RevertToParent, CurrentTime);
    if (children) XFree(children);
    XFlush(d);
}
static void click_at(Display *d, int x, int y) {
    XTestFakeMotionEvent(d, -1, x, y, CurrentTime);
    XTestFakeButtonEvent(d, 1, True, CurrentTime);
    XTestFakeButtonEvent(d, 1, False, CurrentTime);
    XFlush(d);
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "uso: %s 'segundo:keysym:duracion,...'\n", argv[0]);
        return 2;
    }
    Display *d = XOpenDisplay(NULL);
    if (!d) { fputs("no se pudo abrir DISPLAY\n", stderr); return 1; }
    /* Vita3K's first-run update dialog is modal and steals the game focus. */
    click_at(d, 550, 375); /* OK */
    struct timespec settle = {0, 100000000};
    nanosleep(&settle, NULL);
    focus_app_window(d);
    /* Focus the Vita game viewport inside Vita3K's combined Qt/game window. */
    click_at(d, 500, 420);
    /* Leave the clicked child focused. Refocusing the outer Qt window here
       steals key delivery from a viewport widget that took focus on click. */
    XSync(d, False);
    nanosleep(&settle, NULL);
    char *save = NULL, *event = strtok_r(argv[1], ",", &save);
    const double start = now();
    while (event) {
        char *a = event, *b = strchr(a, ':');
        if (!b) return 2;
        *b++ = 0;
        char *c = strchr(b, ':');
        if (!c) return 2;
        *c++ = 0;
        const double at = strtod(a, NULL), duration = strtod(c, NULL);
        const int code = keycode(d, b);
        if (!code) { fprintf(stderr, "keysym inválido: %s\n", b); return 2; }
        wait_until(start + at);
        XTestFakeKeyEvent(d, (unsigned)code, True, CurrentTime);
        XFlush(d);
        struct timespec t = {(time_t)duration, (long)((duration - (time_t)duration) * 1e9)};
        while (nanosleep(&t, &t) && t.tv_sec >= 0) {}
        XTestFakeKeyEvent(d, (unsigned)code, False, CurrentTime);
        XFlush(d);
        event = strtok_r(NULL, ",", &save);
    }
    XCloseDisplay(d);
    return 0;
}
