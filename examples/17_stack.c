/* A deliberately non-reentrant demonstration of a temporary stack switch.
 * The naked trampoline saves the caller's actual SP, including its return PC.
 * The C routine runs on the other stack, then the trampoline restores SP.
 */
static unsigned long alternate_stack[8192];
static void *previous_sp;
static volatile int result;

void work(void)
{
    volatile unsigned char buffer[20000];
    buffer[0] = 19;
    buffer[19999] = 23;
    result = buffer[0] + buffer[19999];
}

__naked__ void on_alternate_stack(void)
{
    __asm__ {
        move.l  sp,previous_sp
        lea     alternate_stack+32768,sp
        jsr     work
        move.l  previous_sp,sp
        rts
    }
}

int main(void)
{
    on_alternate_stack();
    return result;
}
