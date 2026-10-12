#include "session.h"
#include <errno.h>
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>

static void output_bytes(void *context, const uint8_t *bytes, size_t size) {
    FILE *output = context;
    if (fwrite(bytes, 1, size, output) != size)
        return;
}
static void output_message(void *context, uint8_t type, uint16_t id,
                           const uint8_t *payload, size_t size) {
    write_frame(output_bytes, context, type, id, payload, size);
    fflush(context);
}
int main(void) {
    static Session session;
    session_init(&session, output_message, stdout);
    for (;;) {
        session_tick(&session);
        fd_set input;
        FD_ZERO(&input);
        FD_SET(STDIN_FILENO, &input);
        struct timeval timeout = {0, 1000};
        int ready = select(STDIN_FILENO + 1, &input, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (!ready)
            continue;
        uint8_t byte;
        ssize_t size = read(STDIN_FILENO, &byte, 1);
        if (size <= 0)
            break;
        session_feed(&session, &byte, 1);
    }
    session_destroy(&session);
    return 0;
}
