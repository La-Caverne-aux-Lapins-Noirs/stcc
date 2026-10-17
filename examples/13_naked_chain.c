/* Exemple temporaire de chainage : le predecesseur attend une trame CPU,
   on lui transfere donc le controle, sans l'appeler comme une fonction C.
   Une extension residente devra aussi gerer sa memoire et le protocole XBRA. */
#include "interrupt_os.h"

volatile unsigned int first_hits, second_hits;
void *predecessor;

__interrupt__ void final_handler(void)
{
    second_hits++;
}

__interrupt__ __naked__ void first_handler(void)
{
    __asm__ {
        addq.w #1,first_hits
        move.l predecessor,-(sp)
        rts
    }
}

int main(void)
{
    void *original;
    original = set_vector(35, (void *)final_handler);
    predecessor = set_vector(35, (void *)first_handler);
    __asm__ {
        trap #3
    }
    set_vector(35, original);
    return first_hits != 1 || second_hits != 1;
}
