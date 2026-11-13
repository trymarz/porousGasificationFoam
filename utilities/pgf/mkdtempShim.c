// LD_PRELOAD shim for pgf-env --yade: Yade calls mkdtemp("/tmp/yade-XXXXXX"),
// which segfaults Omega::initTemps when /tmp is read-only. Templates under
// /tmp/ are redirected to $TMPDIR; Yade uses the returned pointer, not the buffer.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* mkdtemp(char* tmpl)
{
    static char* (*real)(char*) = NULL;
    if (!real) real = (char* (*)(char*))dlsym(RTLD_NEXT, "mkdtemp");

    const char* dir = getenv("TMPDIR");
    if (!dir || !*dir || strncmp(tmpl, "/tmp/", 5) != 0) return real(tmpl);

    size_t n = strlen(dir) + strlen(tmpl + 4) + 1;
    char* moved = malloc(n);
    if (!moved) return real(tmpl);
    snprintf(moved, n, "%s%s", dir, tmpl + 4);
    return real(moved);
}
