long bios_read_key(void)
{
    long key;

    __asm__ {
        move.w  #2,-(sp)         ; Console device
        move.w  #2,-(sp)         ; BIOS Bconin
        trap    #13
        addq.l  #4,sp
        move.l  d0,key
    }
    return key;
}

int main(void)
{
    /* Low byte = ASCII; bits 16..23 = keyboard scan code. */
    return (int)(bios_read_key() & 255L);
}
