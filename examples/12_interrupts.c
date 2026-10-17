/* Deux vraies exceptions CPU imbriquees, sans reserver un peripherique.
   TRAP #3 -> vecteur 35 ; TRAP #4 -> vecteur 36. Les deux sont restaures.
   Test du contexte utilisateur : les 15 registres, SR et SP. */
#include "interrupt_os.h"

volatile unsigned int outer_hits, inner_hits;
volatile unsigned long product;
unsigned long before_sp, after_sp, registers[15];
unsigned int before_sr, after_sr;

static unsigned long calculate(unsigned long n)
{
    return n * 1234567UL / 17UL;
}

__interrupt__ void inner(void)
{
    inner_hits++;
    product = calculate(19UL);
}

__interrupt__ void outer(void)
{
    int local[3] = {1, 2, 3};
    outer_hits += local[1] - local[0];
    __asm__ {
        trap #4
        moveq #-1,d0
        moveq #-1,d1
        suba.l a0,a0
        suba.l a1,a1
    }
    if (local[2] != 3)
        outer_hits = 999;
    if (outer_hits == 1)
        return;
}

__naked__ void probe(void)
{
    __asm__ {
        movem.l d2-d7/a2-a6,-(sp)
        move.l sp,before_sp
        move.l #$12340000,d0
        move.l #$12340001,d1
        move.l #$12340002,d2
        move.l #$12340003,d3
        move.l #$12340004,d4
        move.l #$12340005,d5
        move.l #$12340006,d6
        move.l #$12340007,d7
        move.l #$12340008,a0
        move.l #$12340009,a1
        move.l #$1234000a,a2
        move.l #$1234000b,a3
        move.l #$1234000c,a4
        move.l #$1234000d,a5
        move.l #$1234000e,a6
        move.w #$0015,ccr
        move.w sr,before_sr
        trap #3
        move.w sr,after_sr
        movem.l d0-d7/a0-a6,registers
        move.l sp,after_sp
        movem.l (sp)+,d2-d7/a2-a6
        rts
    }
}

int main(void)
{
    void *old_outer, *old_inner;
    int i;
    old_outer = set_vector(35, (void *)outer);
    old_inner = set_vector(36, (void *)inner);
    probe();
    probe();
    set_vector(36, old_inner);
    set_vector(35, old_outer);
    if (outer_hits != 2 || inner_hits != 2 || product != 1379810UL)
        return 1;
    if (before_sp != after_sp || before_sr != after_sr)
        return 2;
    for (i = 0; i < 15; i++)
        if (registers[i] != 0x12340000UL + i)
            return 3;
    return 0;
}
