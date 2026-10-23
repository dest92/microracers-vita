/* fwatch.c — HW-067 (SPEC-046 CA1b): registra aperturas (IN_OPEN) en los directorios dados.
 * uso: fwatch <dir>... ; imprime "OPEN <dir>/<archivo>" por línea, sin frenar al proceso observado. */
#include <stdio.h>
#include <sys/inotify.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int fd = inotify_init();
    if (fd < 0) { perror("inotify_init"); return 1; }
    const char *dirs[64] = {0};
    for (int i = 1; i < argc && i < 64; ++i) {
        int wd = inotify_add_watch(fd, argv[i], IN_OPEN);
        if (wd < 0) { perror(argv[i]); continue; }
        if (wd < 64) dirs[wd] = argv[i];
    }
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) return 0;
        for (char *p = buf; p < buf + n;) {
            const struct inotify_event *e = (const struct inotify_event *)p;
            if (e->len) printf("OPEN %s/%s\n", e->wd < 64 && dirs[e->wd] ? dirs[e->wd] : "?", e->name);
            fflush(stdout);
            p += sizeof *e + e->len;
        }
    }
}
