#define _POSIX_C_SOURCE 200809L
#include "stcc.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* Host utility. FAT fields are little-endian, independently of the host and
   of the 68000. No mkfs, mtools, mount, disk device or emulator is needed. */
typedef struct Entry Entry;
struct Entry {
    unsigned char name[11];
    char *source;
    int directory;
    uint32_t size, first, clusters;
    Entry *parent, **children;
    size_t count, capacity;
};
typedef struct {
    unsigned kib, sides, sectors_track, sectors_fat, sectors, root_sector;
    unsigned data_sector, cluster_count, next_cluster, media;
    unsigned char *bytes;
} Disk;
static Entry root;
static struct stat output_stat;
static int output_exists;
static const unsigned cluster_bytes = 1024, root_entries = 112;

static NORETURN void fail(const char *message, ...) PRINTF_FMT(1, 2);
static void fail(const char *message, ...)
{
    va_list args;
    fputs("stmkdisk: error: ", stderr);
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}
static void *allocate(size_t size)
{
    void *p = calloc(1, size ? size : 1);
    if (!p)
        fail("out of memory");
    return p;
}
static char *copy_string(const char *s)
{
    char *p = allocate(strlen(s) + 1);
    strcpy(p, s);
    return p;
}
static char *join(const char *a, const char *b)
{
    char *p = allocate(strlen(a) + strlen(b) + 2);
    sprintf(p, "%s/%s", a, b);
    return p;
}
static unsigned char upper_ascii(unsigned char c)
{
    return c >= 'a' && c <= 'z' ? (unsigned char)(c - 'a' + 'A') : c;
}
static int name_character(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("$%'-_@~`!(){}^#&", c));
}
static void short_name(const char *s, unsigned char name[11])
{
    const char *dot = strchr(s, '.');
    size_t stem = dot ? (size_t)(dot - s) : strlen(s);
    size_t ext = dot ? strlen(dot + 1) : 0, i;
    if (!stem || stem > 8 || ext > 3 || (dot && (!ext || strchr(dot + 1, '.'))))
        fail("'%s' is not an Atari 8.3 name; use --add SOURCE DEST", s);
    memset(name, ' ', 11);
    for (i = 0; i < stem + ext; i++) {
        unsigned char c = upper_ascii((unsigned char)(i < stem ? s[i] : dot[1 + i - stem]));
        if (!name_character(c))
            fail("'%s' is not an ASCII Atari 8.3 name; use --add SOURCE DEST", s);
        name[i < stem ? i : 8 + i - stem] = c;
    }
}
static char *leaf_name(const char *path)
{
    char *s = copy_string(path), *leaf;
    size_t n = strlen(s);
    while (n && s[n - 1] == '/')
        s[--n] = 0;
    leaf = strrchr(s, '/');
    leaf = copy_string(leaf ? leaf + 1 : s);
    free(s);
    return leaf;
}
static Entry *find_child(Entry *parent, const unsigned char name[11])
{
    size_t i;
    for (i = 0; i < parent->count; i++)
        if (!memcmp(parent->children[i]->name, name, 11))
            return parent->children[i];
    return NULL;
}
static Entry *new_child(Entry *parent, const unsigned char name[11], int directory)
{
    Entry *entry = allocate(sizeof *entry);
    if (parent->count == parent->capacity) {
        size_t capacity = parent->capacity ? parent->capacity * 2 : 16;
        void *p = realloc(parent->children, capacity * sizeof *parent->children);
        if (!p)
            fail("out of memory");
        parent->children = p;
        parent->capacity = capacity;
    }
    memcpy(entry->name, name, 11);
    entry->directory = directory;
    entry->parent = parent;
    parent->children[parent->count++] = entry;
    return entry;
}
static Entry *ensure_directory(Entry *parent, const unsigned char name[11])
{
    Entry *entry = find_child(parent, name);
    if (entry && !entry->directory)
        fail("file/directory name collision in image: '%.11s'", name);
    return entry ? entry : new_child(parent, name, 1);
}
static unsigned entry_depth(Entry *entry)
{
    unsigned depth = 0;
    while (entry->parent) {
        depth++;
        entry = entry->parent;
    }
    return depth;
}
static void import_path(Entry *, const char *, const unsigned char[11]);
static void import_contents(Entry *parent, const char *path)
{
    struct dirent *item;
    DIR *dir;
    if (entry_depth(parent) > 64)
        fail("directory nesting exceeds 64 levels: '%s'", path);
    dir = opendir(path);
    if (!dir)
        fail("cannot read directory '%s': %s", path, strerror(errno));
    for (;;) {
        unsigned char name[11];
        char *child;
        errno = 0;
        item = readdir(dir);
        if (!item) {
            if (errno)
                fail("cannot read directory '%s': %s", path, strerror(errno));
            break;
        }
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
            continue;
        short_name(item->d_name, name);
        child = join(path, item->d_name);
        import_path(parent, child, name);
        free(child);
    }
    if (closedir(dir))
        fail("cannot close directory '%s': %s", path, strerror(errno));
}
static void import_path(Entry *parent, const char *path, const unsigned char name[11])
{
    struct stat st;
    Entry *entry;
    if (lstat(path, &st))
        fail("cannot inspect '%s': %s", path, strerror(errno));
    if (S_ISDIR(st.st_mode)) {
        entry = ensure_directory(parent, name);
        import_contents(entry, path);
        return;
    }
    if (!S_ISREG(st.st_mode))
        fail("not a regular file or directory (symlinks are not followed): '%s'", path);
    if (output_exists && st.st_dev == output_stat.st_dev && st.st_ino == output_stat.st_ino)
        fail("output image is also an input file: '%s'", path);
    if (st.st_size < 0 || (uint64_t)st.st_size > 800u * 1024u)
        fail("file is larger than the supported disks: '%s'", path);
    if (find_child(parent, name))
        fail("duplicate Atari 8.3 name in image: '%.11s' (source '%s')", name, path);
    entry = new_child(parent, name, 0);
    entry->source = copy_string(path);
    entry->size = (uint32_t)st.st_size;
}
static void import_root(const char *path)
{
    struct stat st;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode))
        fail("--root needs a directory, without following a symlink: '%s'", path);
    import_contents(&root, path);
}
static void import_as(const char *source, const char *destination)
{
    char *path = copy_string(destination), *part = path, *slash;
    Entry *parent = &root;
    unsigned char name[11];
    if (!*part || *part == '/')
        fail("destination must be a relative Atari path: '%s'", destination);
    while ((slash = strchr(part, '/')) != NULL) {
        *slash = 0;
        short_name(part, name);
        parent = ensure_directory(parent, name);
        if (entry_depth(parent) > 64)
            fail("destination nesting exceeds 64 levels: '%s'", destination);
        part = slash + 1;
    }
    short_name(part, name);
    import_path(parent, source, name);
    free(path);
}
static void import_auto(const char *source)
{
    unsigned char name[11], auto_name[11];
    char *leaf = leaf_name(source);
    struct stat st;
    short_name(leaf, name);
    if (memcmp(name + 8, "PRG", 3) && memcmp(name + 8, "TOS", 3))
        fail("--auto needs an 8.3 .PRG or .TOS program: '%s'", source);
    if (lstat(source, &st) || !S_ISREG(st.st_mode))
        fail("--auto needs a regular program file: '%s'", source);
    memcpy(name + 8, "PRG", 3);
    short_name("AUTO", auto_name);
    import_path(ensure_directory(&root, auto_name), source, name);
    free(leaf);
}
static void le16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}
static void le32(unsigned char *p, uint32_t value)
{
    le16(p, value & 65535u);
    le16(p + 2, value >> 16);
}
static void fat12(Disk *disk, unsigned cluster, unsigned value)
{
    unsigned char *p = disk->bytes + 512 + cluster * 3 / 2;
    if (cluster & 1) {
        p[0] = (unsigned char)((p[0] & 15) | (value << 4));
        p[1] = (unsigned char)(value >> 4);
    } else {
        p[0] = (unsigned char)value;
        p[1] = (unsigned char)((p[1] & 240) | (value >> 8));
    }
}
static int compare_entries(const void *a, const void *b)
{
    const Entry *x = *(Entry *const *)a, *y = *(Entry *const *)b;
    return memcmp(x->name, y->name, 11);
}
static void assign_clusters(Disk *disk, Entry *entry)
{
    size_t i;
    if (entry != &root) {
        uint32_t bytes = entry->directory ? (uint32_t)(entry->count + 2) * 32 : entry->size;
        entry->clusters = (bytes + cluster_bytes - 1) / cluster_bytes;
        if (entry->clusters > disk->cluster_count - (disk->next_cluster - 2))
            fail("disk full (%u KiB); choose --size 800 or fewer files", disk->kib);
        if (entry->clusters) {
            entry->first = disk->next_cluster;
            disk->next_cluster += entry->clusters;
            for (i = 0; i < entry->clusters; i++)
                fat12(disk, entry->first + (unsigned)i,
                      i + 1 == entry->clusters ? 0xfff : entry->first + (unsigned)i + 1);
        }
    }
    if (entry->count)
        qsort(entry->children, entry->count, sizeof *entry->children, compare_entries);
    for (i = 0; i < entry->count; i++)
        assign_clusters(disk, entry->children[i]);
}
static unsigned char *cluster_address(Disk *disk, unsigned cluster)
{
    return disk->bytes + disk->data_sector * 512 + (cluster - 2) * cluster_bytes;
}
static void directory_entry(unsigned char *p, const unsigned char name[11], unsigned attributes,
                            unsigned cluster, uint32_t size)
{
    memcpy(p, name, 11);
    p[11] = (unsigned char)attributes;
    le16(p + 24, 33); /* 1980-01-01, fixed for reproducible images. */
    le16(p + 26, cluster);
    le32(p + 28, size);
}
static void write_entry(Disk *disk, Entry *entry, const unsigned char label[11])
{
    unsigned char *p;
    size_t i;
    if (!entry->directory) {
        FILE *file = fopen(entry->source, "rb");
        if (!file)
            fail("cannot read '%s': %s", entry->source, strerror(errno));
        p = entry->size ? cluster_address(disk, entry->first) : NULL;
        if (entry->size && fread(p, 1, entry->size, file) != entry->size)
            fail("short read or changed input file: '%s'", entry->source);
        if (fgetc(file) != EOF || ferror(file))
            fail("read error or changed input file: '%s'", entry->source);
        if (fclose(file))
            fail("cannot close '%s': %s", entry->source, strerror(errno));
        return;
    }
    p = entry == &root ? disk->bytes + disk->root_sector * 512
                       : cluster_address(disk, entry->first);
    if (entry == &root) {
        directory_entry(p, label, 8, 0, 0);
        p += 32;
    } else {
        directory_entry(p, (const unsigned char *)".          ", 16, entry->first, 0);
        directory_entry(p + 32, (const unsigned char *)"..         ", 16, entry->parent->first, 0);
        p += 64;
    }
    for (i = 0; i < entry->count; i++) {
        Entry *child = entry->children[i];
        directory_entry(p, child->name, child->directory ? 16 : 32, child->first,
                        child->directory ? 0 : child->size);
        p += 32;
        write_entry(disk, child, label);
    }
}
static void make_disk(Disk *disk, const unsigned char label[11])
{
    unsigned char *b;
    unsigned needed, sum = 0, i;
    uint32_t serial = 2166136261u;
    disk->sectors = disk->kib * 2;
    disk->sides = disk->kib == 360 ? 1 : 2;
    disk->sectors_track = disk->kib == 800 ? 10 : 9;
    disk->media = disk->sides == 1 ? 0xfc : 0xf9;
    disk->sectors_fat = 1;
    for (;;) {
        disk->root_sector = 1 + 2 * disk->sectors_fat;
        disk->data_sector = disk->root_sector + root_entries * 32 / 512;
        disk->cluster_count = (disk->sectors - disk->data_sector) / 2;
        needed = (((disk->cluster_count + 2) * 3 + 1) / 2 + 511) / 512;
        if (needed <= disk->sectors_fat)
            break;
        disk->sectors_fat = needed;
    }
    disk->bytes = allocate(disk->sectors * 512);
    b = disk->bytes;
    b[0] = 0x60;
    b[1] = 0x1c;
    memcpy(b + 2, "STCC  ", 6);
    le16(b + 11, 512);
    b[13] = 2;
    le16(b + 14, 1);
    b[16] = 2;
    le16(b + 17, root_entries);
    le16(b + 19, disk->sectors);
    b[21] = (unsigned char)disk->media;
    le16(b + 22, disk->sectors_fat);
    le16(b + 24, disk->sectors_track);
    le16(b + 26, disk->sides);
    /* Optional DOS extended BPB lives in unused, non-executable boot space.
       It keeps FAT readers' volume-label checks consistent with the root. */
    b[38] = 0x29;
    memcpy(b + 43, label, 11);
    memcpy(b + 54, "FAT12   ", 8);
    b[510] = 0x55;
    b[511] = 0xaa;
    b[512] = (unsigned char)disk->media;
    b[513] = b[514] = 0xff;
    if (root.count + 1 > root_entries)
        fail("root directory full (maximum %u entries, including the volume label)", root_entries);
    disk->next_cluster = 2;
    assign_clusters(disk, &root);
    memcpy(b + (1 + disk->sectors_fat) * 512, b + 512, disk->sectors_fat * 512);
    write_entry(disk, &root, label);
    /* Content-derived disk serial: stable builds, distinct changed media. */
    for (i = 0; i < disk->sectors * 512; i++)
        serial = (serial ^ b[i]) * 16777619u;
    if (!(serial & 0xffffff))
        serial = 1;
    b[8] = (unsigned char)serial;
    b[9] = (unsigned char)(serial >> 8);
    b[10] = (unsigned char)(serial >> 16);
    le32(b + 39, serial & 0xffffff);
    for (i = 0; i < 512; i += 2)
        sum = (sum + b[i] * 256u + b[i + 1]) & 65535u;
    if (sum == 0x1234)
        b[509] = 1; /* Data disk: never an accidentally executable 68000 boot sector. */
}
static void save_disk(Disk *disk, const char *output, int force)
{
    size_t length = strlen(output);
    char *temporary = allocate(length + sizeof ".tmp.XXXXXX");
    FILE *file;
    int fd, error = 0;
    memcpy(temporary, output, length);
    memcpy(temporary + length, ".tmp.XXXXXX", sizeof ".tmp.XXXXXX");
    fd = mkstemp(temporary);
    if (fd < 0)
        fail("cannot create image '%s': %s", output, strerror(errno));
    file = fdopen(fd, "wb");
    if (!file) {
        error = errno;
        close(fd);
    } else {
        if (fwrite(disk->bytes, 512, disk->sectors, file) != disk->sectors)
            error = errno ? errno : EIO;
        if (fclose(file) && !error)
            error = errno;
    }
    if (!error && (force ? rename(temporary, output) : link(temporary, output)))
        error = errno;
    unlink(temporary);
    free(temporary);
    if (error)
        fail("cannot write image '%s': %s", output, strerror(error));
}
static void free_entry(Entry *entry)
{
    size_t i;
    for (i = 0; i < entry->count; i++) {
        free_entry(entry->children[i]);
        free(entry->children[i]);
    }
    free(entry->children);
    free(entry->source);
}
static void usage(FILE *file)
{
    fputs("Usage: stmkdisk -o IMAGE.ST [options] [FILE|DIRECTORY ...]\n"
          "Create a raw Atari FAT12 floppy image for Hatari / Steem Engine.\n"
          "  --size 360|720|800   Capacity in KiB (default: 720)\n"
          "  --label LABEL        ASCII volume label, 1..11 characters (default: STCC)\n"
          "  --root DIRECTORY     Import its contents into the image root\n"
          "  --add SOURCE DEST    Import under an explicit Atari 8.3 path\n"
          "  --auto PROGRAM       Copy an 8.3 .PRG/.TOS into AUTO as .PRG\n"
          "  --force              Replace an existing output image\n"
          "  --                   End options (following arguments are input paths)\n"
          "  -h, --help           Show help\n"
          "  -v, --version        Show version\n",
          file);
}
int main(int argc, char **argv)
{
    Disk disk = {0};
    unsigned char label[11];
    const char *output = NULL, *label_text = "STCC";
    int i, force = 0, options = 1;
    /* First pass validates options and resolves the output before any import. */
    disk.kib = 720;
    root.directory = 1;
    if (argc == 1) {
        usage(stderr);
        return 1;
    }
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (options && !strcmp(arg, "--")) {
            options = 0;
            continue;
        }
        if (!options || arg[0] != '-')
            continue;
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(arg, "--version") || !strcmp(arg, "-v")) {
            puts("stmkdisk " VERSION " (Atari ST raw FAT12 images)");
            return 0;
        }
        if (!strcmp(arg, "--force")) {
            force = 1;
            continue;
        }
        if (!strcmp(arg, "--add")) {
            if (i + 2 >= argc)
                fail("--add needs SOURCE and DEST");
            i += 2;
            continue;
        }
        if (strcmp(arg, "-o") && strcmp(arg, "--size") && strcmp(arg, "--label") &&
            strcmp(arg, "--root") && strcmp(arg, "--auto"))
            fail("unknown option '%s' (see --help)", arg);
        if (++i >= argc)
            fail("missing argument after '%s'", arg);
        if (!strcmp(arg, "-o")) {
            if (output)
                fail("use -o only once");
            output = argv[i];
        } else if (!strcmp(arg, "--label"))
            label_text = argv[i];
        else if (!strcmp(arg, "--size")) {
            if (strcmp(argv[i], "360") && strcmp(argv[i], "720") && strcmp(argv[i], "800"))
                fail("--size must be 360, 720 or 800 KiB");
            disk.kib = (unsigned)atoi(argv[i]);
        }
    }
    if (!output || !*output)
        fail("specify an output image with -o IMAGE.ST");
    memset(label, ' ', sizeof label);
    if (!*label_text || strlen(label_text) > 11)
        fail("volume label must contain 1..11 ASCII characters");
    for (i = 0; label_text[i]; i++) {
        unsigned char c = upper_ascii((unsigned char)label_text[i]);
        if (c != ' ' && !name_character(c))
            fail("invalid character in volume label '%s'", label_text);
        label[i] = c;
    }
    if (label[0] == ' ')
        fail("volume label must start with a non-space character");
    if (!lstat(output, &output_stat)) {
        output_exists = 1;
        if (!force)
            fail("image '%s' already exists (use --force to replace it)", output);
        if (!S_ISREG(output_stat.st_mode))
            fail("output exists and is not a regular file: '%s'", output);
    } else if (errno != ENOENT)
        fail("cannot inspect output '%s': %s", output, strerror(errno));
    options = 1;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (options && !strcmp(arg, "--"))
            options = 0;
        else if (options &&
                 (!strcmp(arg, "-o") || !strcmp(arg, "--size") || !strcmp(arg, "--label")))
            i++;
        else if (options && !strcmp(arg, "--force"))
            continue;
        else if (options && !strcmp(arg, "--add")) {
            import_as(argv[i + 1], argv[i + 2]);
            i += 2;
        } else if (options && !strcmp(arg, "--root"))
            import_root(argv[++i]);
        else if (options && !strcmp(arg, "--auto"))
            import_auto(argv[++i]);
        else {
            unsigned char name[11];
            char *leaf = leaf_name(arg);
            short_name(leaf, name);
            import_path(&root, arg, name);
            free(leaf);
        }
    }
    make_disk(&disk, label);
    save_disk(&disk, output, force);
    printf("%s: %u KiB, 80 tracks, %u side(s), %u sectors/track; %u/%u KiB allocated\n", output,
           disk.kib, disk.sides, disk.sectors_track, disk.next_cluster - 2, disk.cluster_count);
    free(disk.bytes);
    free_entry(&root);
    return 0;
}
