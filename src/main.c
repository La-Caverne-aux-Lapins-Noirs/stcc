#define _POSIX_C_SOURCE 200809L
#include "stcc.h"
#include <unistd.h>
#ifndef STCC_DATADIR
#define STCC_DATADIR "/usr/local/share/stcc"
#endif
static void help(void)
{
    puts("stcc " VERSION " - C / Devpac 68000 -> Atari TOS\n"
         "usage: stcc [options] file.c file.s file.o ...\n"
         "  -o FILE       output name\n  -I DIR        include search directory\n"
         "  -D NAME[=VAL] preprocessor definition\n  -E            preprocess only\n"
         "  -S            emit Devpac assembly\n  -c            emit STCO objects\n"
         "  --nostartfiles  omit TOS startup (assembly entry first)\n"
         "  --map FILE    write link map\n  --entry NAME  assert entry at TEXT offset 0\n"
         "  --data-dir DIR runtime and include directory\n"
         "  -v, --version version\n  --help        this help");
}
static char *replace_ext(const char *s, const char *ext)
{
    const char *b = strrchr(s, '/'), *d;
    char *p;
    b = b ? b + 1 : s;
    d = strrchr(b, '.');
    p = fmt("%.*s%s", d ? (int)(d - b) : (int)strlen(b), b, ext);
    return p;
}
static int ends(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && !strcmp(s + a - b, ext);
}
int main(int argc, char **argv)
{
    char **inputs = xcalloc((size_t)argc, sizeof *inputs),
         **incs = xcalloc((size_t)argc + 2, sizeof *incs),
         **defs = xcalloc((size_t)argc + 16, sizeof *defs);
    int ni = 0, ninc = 0, nd = 0, i, mode = 0, no_start = 0, no = 0;
    char *out = NULL, *map = NULL, *entry = NULL, *datadir = NULL;
    Object **objects = xcalloc((size_t)argc + 4, sizeof *objects);
    char exe[4096];
    ssize_t z;
    for (i = 1; i < argc; i++) {
        char *s = argv[i];
        if (!strcmp(s, "--help")) {
            help();
            return 0;
        }
        if (!strcmp(s, "-v") || !strcmp(s, "--version")) {
            puts("stcc " VERSION " (original 68000, ILP16/32, freestanding C subset)");
            return 0;
        }
        if (!strcmp(s, "-E") || !strcmp(s, "-S") || !strcmp(s, "-c")) {
            int m = s[1] == 'E' ? 1 : s[1] == 'S' ? 2 : 3;
            if (mode && mode != m)
                die("incompatible output modes");
            mode = m;
            continue;
        }
        if (!strcmp(s, "--nostartfiles")) {
            no_start = 1;
            continue;
        }
        if (!strcmp(s, "-o") || !strcmp(s, "--map") || !strcmp(s, "--entry") ||
            !strcmp(s, "--data-dir")) {
            char *v;
            if (++i == argc)
                die("missing value for %s", s);
            v = argv[i];
            if (!strcmp(s, "-o"))
                out = v;
            else if (!strcmp(s, "--map"))
                map = v;
            else if (!strcmp(s, "--entry"))
                entry = v;
            else
                datadir = v;
            continue;
        }
        if (!strncmp(s, "-I", 2) || !strncmp(s, "-D", 2)) {
            char *v = s + 2;
            if (!*v) {
                if (++i == argc)
                    die("missing value for %s", s);
                v = argv[i];
            }
            if (s[1] == 'I')
                incs[ninc++] = v;
            else
                defs[nd++] = v;
            continue;
        }
        if (*s == '-')
            die("unknown option '%s'", s);
        inputs[ni++] = s;
    }
    if (!ni)
        die("no input files");
    if (mode && out && ni != 1)
        die("-o with -E/-S/-c requires one input");
    if (!datadir) {
        datadir = getenv("STCC_DATA");
        if (!datadir) {
            z = readlink("/proc/self/exe", exe, 4095);
            if (z > 0) {
                char *p;
                exe[z] = 0;
                p = strrchr(exe, '/');
                if (p) {
                    *p = 0;
                    if (access(fmt("%s/runtime/start.s", exe), R_OK) == 0)
                        datadir = exe;
                }
            }
        }
        if (!datadir)
            datadir = STCC_DATADIR;
    }
    incs[ninc++] = fmt("%s/include", datadir);
    if (!mode && !no_start) {
        char *p = fmt("%s/runtime/start.s", datadir);
        objects[no++] = assemble(read_text(p), p);
        if (!entry)
            entry = "__stcc_start";
    }
    for (i = 0; i < ni; i++) {
        char *s = inputs[i], *text, *asmtext;
        Object *o;
        diag_file = s;
        diag_line = 1;
        if (ends(s, ".o")) {
            if (mode)
                die("object input requires linking");
            objects[no++] = object_read(s);
            continue;
        }
        text = read_text(s);
        if (ends(s, ".c") || ends(s, ".i")) {
            text = ends(s, ".i") ? text : preprocess(s, incs, ninc, defs, nd);
            if (mode == 1) {
                if (out)
                    write_text(out, text);
                else
                    fputs(text, stdout);
                continue;
            }
            asmtext = compile_c(text, s);
        } else if (ends(s, ".s") || ends(s, ".asm")) {
            if (mode == 1)
                die("-E requires C input");
            asmtext = text;
        } else
            die("unrecognized input extension: %s", s);
        if (mode == 2) {
            write_text(out ? out : replace_ext(s, ".s"), asmtext);
            continue;
        }
        o = assemble(asmtext, s);
        if (mode == 3)
            object_write(o, out ? out : replace_ext(s, ".o"));
        else
            objects[no++] = o;
    }
    if (!mode) {
        if (!no_start) {
            char *p = fmt("%s/runtime/integer.s", datadir);
            objects[no++] = assemble(read_text(p), p);
        }
        link_tos(objects, no, out ? out : "a.TOS", entry, map);
    }
    return 0;
}
