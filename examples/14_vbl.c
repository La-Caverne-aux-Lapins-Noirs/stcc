/* Callback VBL differe appele par TOS : fonction C ordinaire, retour RTS.
   Aucun GEMDOS/BIOS depuis le callback. Installation/retrait en superviseur.
   Ne modifie ni la taille ni les autres entrees de la file VBL. */
typedef void (*Vbl)(void);
volatile unsigned int frames;
static Vbl volatile *installed_slot;

static void on_vbl(void)
{
    frames++;
}

static long install_vbl(void)
{
    Vbl volatile *queue;
    int count, i, saved_sr;
    long result = -1;
    __asm__ {
        move.w sr,saved_sr
        ori.w #$0700,sr
    }
    queue = *(Vbl volatile * volatile *)0x456L;
    count = *(volatile int *)0x454L;
    for (i = 0; i < count; i++) {
        if (!queue[i]) {
            installed_slot = &queue[i];
            queue[i] = on_vbl;
            result = 0;
            break;
        }
    }
    __asm__ {
        move.w saved_sr,sr
    }
    return result;
}

static long remove_vbl(void)
{
    int saved_sr;
    __asm__ {
        move.w sr,saved_sr
        ori.w #$0700,sr
    }
    if (installed_slot && *installed_slot == on_vbl)
        *installed_slot = 0;
    installed_slot = 0;
    __asm__ {
        move.w saved_sr,sr
    }
    return 0;
}

static long supervisor(long (*function)(void))
{
    long result;
    __asm__ (d0,d1,d2,a0,a1,a2,cc,memory) {
        move.l function,-(sp)
        move.w #38,-(sp)
        trap #14
        addq.l #6,sp
        move.l d0,result
    }
    return result;
}

int main(void)
{
    int i;
    if (supervisor(install_vbl) < 0)
        return 1;
    for (i = 0; i < 5; i++) {
        __asm__ (d0,d1,d2,a0,a1,a2,cc,memory) {
            move.w #37,-(sp)
            trap #14
            addq.l #2,sp
        }
    }
    supervisor(remove_vbl);
    return frames < 5;
}
