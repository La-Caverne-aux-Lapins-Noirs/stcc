int tos_create(const char *path)
{
    int handle;

    __asm__ {
        clr.w   -(sp)           ; Attributes
        move.l  path,-(sp)
        move.w  #$3c,-(sp)      ; Fcreate
        trap    #1
        addq.l  #8,sp
        move.w  d0,handle
    }
    return handle;
}

long tos_write(int handle, long count, const void *data)
{
    long result;

    __asm__ {
        move.l  data,-(sp)
        move.l  count,-(sp)
        move.w  handle,-(sp)
        move.w  #$40,-(sp)      ; Fwrite
        trap    #1
        lea     12(sp),sp
        move.l  d0,result
    }
    return result;
}

void tos_close(int handle)
{
    __asm__ {
        move.w  handle,-(sp)
        move.w  #$3e,-(sp)
        trap    #1
        addq.l  #4,sp
    }
}

int main(void)
{
    const char message[] = "Ecrit par un programme compile avec stcc.\r\n";
    int handle = tos_create("STCC.TXT");
    long count;

    if (handle < 0)
        return 1;
    count = tos_write(handle, sizeof(message) - 1L, message);
    tos_close(handle);
    return count == sizeof(message) - 1L ? 0 : 2;
}
