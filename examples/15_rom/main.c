/* Original ST only: direct video access, no existing operating system.
 * Colour: 320x200, four planes, PAL 50 Hz. Mono: 640x400, one plane.
 * The startup reserves 32000 bytes at 0x78000 and uses the first 512 KiB.
 * All glyphs below are a small, hand-written 5x7 alphabet for this example.
 */
static const unsigned char font[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static void text(const char *s, int x, int y, int mono)
{
    volatile unsigned int *screen = (volatile unsigned int *)0x00078000UL;
    int glyph, row, column, c, px, index;
    while (*s) {
        c = *s++;
        glyph = -1;
        if (c >= 'A' && c <= 'Z')
            glyph = c - 'A';
        if (c >= '0' && c <= '9')
            glyph = 26 + c - '0';
        if (glyph >= 0) {
            for (row = 0; row < 7; row++) {
                for (column = 0; column < 5; column++) {
                    if (font[glyph][row] & (16U >> column)) {
                        px = x + column;
                        if (mono)
                            index = (y + row) * 40 + (px >> 4);
                        else
                            index = (y + row) * 80 + (px >> 4) * 4;
                        screen[index] |= 0x8000U >> (px & 15);
                    }
                }
            }
        }
        x += 6;
    }
}

int main(void)
{
    volatile unsigned char *gpip = (volatile unsigned char *)0xfffffa01UL;
    volatile unsigned char *video_high = (volatile unsigned char *)0xffff8201UL;
    volatile unsigned char *video_mid = (volatile unsigned char *)0xffff8203UL;
    volatile unsigned char *resolution = (volatile unsigned char *)0xffff8260UL;
    volatile unsigned char *frequency = (volatile unsigned char *)0xffff820aUL;
    volatile unsigned int *palette = (volatile unsigned int *)0xffff8240UL;
    volatile unsigned int *screen = (volatile unsigned int *)0x00078000UL;
    int mono = (*gpip & 0x80) == 0;
    int i;

    *video_high = 7;
    *video_mid = 0x80;
    if (mono)
        *resolution = 2;
    else {
        *frequency = 2;
        *resolution = 0;
    }
    palette[0] = 0;
    palette[1] = 0x777;
    for (i = 0; i < 16000; i++)
        screen[i] = 0;

    text("STCC ROM", 16, 16, mono);
    text("68000 C SANS TOS", 16, 32, mono);
    text("INITIALISATION OK", 16, 48, mono);
    text("RETOUR MAIN 42", 16, 64, mono);
    return 42;
}
