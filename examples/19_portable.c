/* The same source also compiles with a host C compiler. */
#if defined(__STCC__) && defined(__ATARI_ST__)
#if __SIZEOF_INT__ != 2 || __SIZEOF_LONG__ != 4
#error Unexpected stcc integer model
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error Atari ST must be big-endian
#endif
typedef unsigned long counter_t;
#else
typedef unsigned int counter_t;
#endif

int main(void)
{
    counter_t pixels = 320UL * 200UL;
    counter_t bytes = pixels / 2UL;
    return bytes == 32000UL ? 42 : 1;
}
