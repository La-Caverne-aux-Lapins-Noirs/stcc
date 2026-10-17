#ifndef INTERRUPT_OS_H
#define INTERRUPT_OS_H
/* BIOS Setexc : numero de vecteur (pas adresse en octets), ancien pointeur. */
static void *set_vector(int vector, void *handler)
{
    void *previous;
    __asm__ (d0,d1,d2,a0,a1,a2,cc,memory) {
        move.l handler,-(sp)
        move.w vector,-(sp)
        move.w #5,-(sp)
        trap #13
        addq.l #8,sp
        move.l d0,previous
    }
    return previous;
}
#endif
