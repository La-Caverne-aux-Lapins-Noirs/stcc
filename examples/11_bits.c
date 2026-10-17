/* Mots 16 bits big-endian, du bit fort vers le bit faible. */
struct Packet {
    unsigned int ready : 1;
    unsigned int channel : 3;
    signed int delta : 4;
    unsigned int value : 8;
};

int main(void)
{
    struct Packet p = {1, 5, -3, 42};
    unsigned char *bytes = (unsigned char *)&p;
    if (sizeof(p) != 2 || bytes[0] != 0xdd || bytes[1] != 42)
        return 1;
    p.channel++;
    p.delta -= 2;
    if (p.ready != 1 || p.channel != 6 || p.delta != -5 || p.value != 42)
        return 2;
    return 0;
}
