#ifndef STCC_TARGET_H
#define STCC_TARGET_H
/* TOS basepage and argument vector, available before main. Atari ABI offsets. */
struct stcc_basepage {
    void *lowtpa;
    void *hitpa;
    void *text;
    unsigned long text_length;
    void *data;
    unsigned long data_length;
    void *bss;
    unsigned long bss_length;
    void *dta;
    struct stcc_basepage *parent;
    void *reserved;
    char *environment;
    unsigned char unused[80];
    unsigned char command_line[128];
};
extern struct stcc_basepage *stcc_basepage;
extern int stcc_argc;
extern char **stcc_argv;
#endif
