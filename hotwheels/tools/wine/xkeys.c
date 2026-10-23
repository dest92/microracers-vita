/* xkeys.c — HW-067: inyecta teclas XTest en el Xvfb del runner Wine (sin clicks).
 * uso: xkeys 'segundo:keysym:duracion,...' (eventos ordenados por segundo). */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int XTestFakeKeyEvent(Display *, unsigned int, int, unsigned long);

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static void sleep_s(double s) {
    if (s <= 0) return;
    struct timespec t = {(time_t)s, (long)((s - (time_t)s) * 1e9)};
    while (nanosleep(&t, &t)) {}
}
static void focus_biggest(Display *d) {
    Window root = DefaultRootWindow(d), parent = 0, *kids = NULL, best = 0;
    unsigned n = 0;
    long best_area = 0;
    if (!XQueryTree(d, root, &root, &parent, &kids, &n)) return;
    for (unsigned i = 0; i < n; ++i) {
        XWindowAttributes a;
        if (XGetWindowAttributes(d, kids[i], &a) && a.map_state == IsViewable &&
            (long)a.width * a.height > best_area) {
            best_area = (long)a.width * a.height;
            best = kids[i];
        }
    }
    if (best) XSetInputFocus(d, best, RevertToParent, CurrentTime);
    if (kids) XFree(kids);
    XSync(d, False);
}
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "uso: %s 'seg:keysym:dur,...'\n", argv[0]); return 2; }
    Display *d = XOpenDisplay(NULL);
    if (!d) { fputs("sin DISPLAY\n", stderr); return 1; }
    const double start = now();
    char *save = NULL;
    for (char *ev = strtok_r(argv[1], ",", &save); ev; ev = strtok_r(NULL, ",", &save)) {
        char *b = strchr(ev, ':'), *c = b ? strchr(b + 1, ':') : NULL;
        if (!c) return 2;
        *b++ = 0; *c++ = 0;
        KeySym sym = XStringToKeysym(b);
        int code = sym == NoSymbol ? 0 : XKeysymToKeycode(d, sym);
        if (!code) { fprintf(stderr, "keysym inválido: %s\n", b); return 2; }
        sleep_s(start + strtod(ev, NULL) - now());
        focus_biggest(d);
        XTestFakeKeyEvent(d, (unsigned)code, True, CurrentTime);
        XFlush(d);
        sleep_s(strtod(c, NULL));
        XTestFakeKeyEvent(d, (unsigned)code, False, CurrentTime);
        XFlush(d);
        printf("%.2f %s\n", now() - start, b);
        fflush(stdout);
    }
    XCloseDisplay(d);
    return 0;
}
