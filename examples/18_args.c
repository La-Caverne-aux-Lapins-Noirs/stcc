/* Launch as 18ARGS.TTP, or from a shell with command-line arguments. */
void print(const char *message)
{
    __asm__ {
        move.l  message,-(sp)
        move.w  #9,-(sp)
        trap    #1
        addq.l  #6,sp
    }
}

int main(int argc, char *argv[])
{
    int i;
    for (i = 1; i < argc; i++) {
        print("[");
        print(argv[i]);
        print("]\r\n");
    }
    return 0;
}
