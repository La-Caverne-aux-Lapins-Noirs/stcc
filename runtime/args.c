/* Freestanding TOS startup support, compiled by stcc itself.
 * No libc. The short command tail is copied, leaving the basepage untouched.
 * Extended ARGV strings belong to the child's environment and need no copy.
 */
#include <stcc.h>

int stcc_argc;
char **stcc_argv;
static char *short_argv[65];
static char short_tail[128];
static char unknown_name[1];

static char *after_string(char *text)
{
    while (*text)
        text++;
    return text + 1;
}

static char *extended_start(void)
{
    char *entry = stcc_basepage->environment;
    if (stcc_basepage->command_line[0] != 127 || !entry)
        return 0;
    while (*entry) {
        if (entry[0] == 'A' && entry[1] == 'R' && entry[2] == 'G' &&
            entry[3] == 'V' && entry[4] == '=')
            return after_string(entry);
        entry = after_string(entry);
    }
    return 0;
}

static char **allocate_vector(unsigned int count)
{
    unsigned long bytes = ((unsigned long)count + 1UL) * sizeof(char *);
    char **result;
    if (count < 65)
        return short_argv;
    __asm__ {
        move.l  bytes,-(sp)
        move.w  #$48,-(sp)
        trap    #1
        addq.l  #6,sp
        move.l  d0,result
    }
    if ((long)result <= 0L)
        return 0;
    return result;
}

static int extended_arguments(char *first)
{
    char *argument;
    unsigned int count = 0;
    for (argument = first; *argument; argument = after_string(argument)) {
        if (count == 32767U)
            return -1;
        count++;
    }
    if (!count)
        return 0;
    stcc_argv = allocate_vector(count);
    if (!stcc_argv)
        return -1;
    stcc_argc = 0;
    for (argument = first; *argument; argument = after_string(argument))
        stcc_argv[stcc_argc++] = argument;
    stcc_argv[stcc_argc] = 0;
    return 1;
}

static void copy_short_tail(void)
{
    unsigned int length = stcc_basepage->command_line[0];
    unsigned int i = 0;
    if (length > 126U)
        length = 126U;
    while (i < length) {
        char ch = stcc_basepage->command_line[i + 1];
        if (!ch || ch == '\r')
            break;
        short_tail[i++] = ch;
    }
    short_tail[i] = 0;
}

static char *copy_argument(char *read, char **write)
{
    char quote = 0;
    while (*read && (quote || (*read != ' ' && *read != '\t'))) {
        char ch = *read++;
        if (quote && ch == quote) {
            if (*read == quote) {
                *(*write)++ = *read++;
                continue;
            }
            quote = 0;
        } else if (!quote && (ch == '\'' || ch == '"'))
            quote = ch;
        else
            *(*write)++ = ch;
    }
    return read;
}

static void short_arguments(void)
{
    char *read = short_tail;
    char *write = short_tail;
    stcc_argc = 1;
    stcc_argv = short_argv;
    stcc_argv[0] = unknown_name;
    while (*read) {
        while (*read == ' ' || *read == '\t')
            read++;
        if (!*read)
            break;
        stcc_argv[stcc_argc++] = write;
        read = copy_argument(read, &write);
        while (*read == ' ' || *read == '\t')
            read++;
        *write++ = 0;
    }
    stcc_argv[stcc_argc] = 0;
}

int __stcc_init_args(void)
{
    char *extended = extended_start();
    int status = extended ? extended_arguments(extended) : 0;
    if (status)
        return status < 0 ? -1 : 0;
    copy_short_tail();
    short_arguments();
    return 0;
}
