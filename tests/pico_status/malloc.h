#ifndef TEST_PICO_STATUS_MALLOC_H
#define TEST_PICO_STATUS_MALLOC_H
struct mallinfo { int fordblks; };
struct mallinfo mallinfo(void);
#endif
