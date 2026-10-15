#include <stddef.h>
#ifdef LIGHTVM_BULK_MEMORY
void *memcpy(void *d,const void *s,size_t n) { return __builtin_memcpy(d,s,n); }
void *memmove(void *d,const void *s,size_t n) { return __builtin_memmove(d,s,n); }
void *memset(void *d,int c,size_t n) { return __builtin_memset(d,c,n); }
#else
void *memcpy(void *d,const void *s,size_t n) { unsigned char *a=d; const unsigned char *b=s; while (n--) *a++=*b++; return d; }
void *memmove(void *d,const void *s,size_t n) { unsigned char *a=d; const unsigned char *b=s; if (a<b) return memcpy(d,s,n); while (n) { --n; a[n]=b[n]; } return d; }
void *memset(void *d,int c,size_t n) { unsigned char *a=d; while (n--) *a++=(unsigned char)c; return d; }
#endif
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *x=a,*y=b; while (n--) { if (*x!=*y) return *x-*y; ++x; ++y; } return 0; }
size_t strlen(const char *s) { size_t n=0; while (s[n]) ++n; return n; }
