void tos_print(const char *message)
{
    __asm__ (d0, d1, a0, a1, cc, memory) {
        move.l  message,-(sp)
        move.w  #9,-(sp)
        trap    #1
        addq.l  #6,sp
    }
}

int main(void)
{
    tos_print("Bonjour depuis stcc et le 68000 !\r\n");
    return 0;
}
