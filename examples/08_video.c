volatile unsigned short *physical_screen(void)
{
    volatile unsigned short *screen;

    __asm__ {
        move.w  #2,-(sp)         ; XBIOS Physbase
        trap    #14
        addq.l  #2,sp
        move.l  d0,screen
    }
    return screen;
}

int main(void)
{
    volatile unsigned short *screen = physical_screen();
    unsigned short saved[320];
    volatile int delay;
    int i;

    /* The first 640 bytes are valid in every ST video resolution. */
    for (i = 0; i < 320; i++) {
        saved[i] = screen[i];
        screen[i] = (i & 1) ? 0xaaaaU : 0x5555U;
    }
    for (delay = 0; delay < 1000; delay++)
        ;
    for (i = 0; i < 320; i++)
        screen[i] = saved[i];
    return 0;
}
