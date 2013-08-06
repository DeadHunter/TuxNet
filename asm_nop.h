#ifdef _MSC_VER
#define nop _asm nop
#else
#define nop asm("nop")
#endif
