#include "session.h"
#include "port.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#if LIGHTVM_HTTP
#include "http_server.h"
#endif

#if LIGHTVM_HTTP
typedef struct { App *app; bool override, listening; const char *address; unsigned port; } HostNetwork;
static bool configure_network(void *context,const NetworkConfig *config) {
    HostNetwork *host=context;
    if (host->override) return true;
    (void)config;
    bool enabled=network_http_enabled(&host->app->network); unsigned port=network_http_port(&host->app->network);
    if (host->listening && (!enabled || host->port!=port)) { host_http_stop(); host->listening=false; }
    if (!host->listening && enabled) {
        host->listening=host_http_start(host->app,host->address,port); host->port=port;
        if (!host->listening) strcpy(host->app->network_error,"HTTP listener could not start");
        return host->listening;
    }
    return true;
}
#endif
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
int main(int argc, char **argv) {
    const char *storage=NULL,*listen_address=NULL; unsigned port=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--storage") && i+1<argc) storage=argv[++i];
        else if (!strcmp(argv[i],"--http") && i+1<argc) {
            char *end; unsigned long number=strtoul(argv[++i],&end,10);
            if (*end || number>65535) { fprintf(stderr,"Invalid HTTP port\n"); return 2; }
            port=(unsigned)number; if (!listen_address) listen_address="127.0.0.1";
        } else if (!strcmp(argv[i],"--http-bind") && i+1<argc) listen_address=argv[++i];
        else { fprintf(stderr,"Usage: lightvm [--storage PATH] [--http PORT] [--http-bind IPV4]\n"); return 2; }
    }
    if (storage && !host_storage_open(storage)) { fprintf(stderr,"Cannot open storage (size, lock or I/O error)\n"); return 1; }
#if !LIGHTVM_HTTP
    if (listen_address) { fprintf(stderr,"HTTP is excluded from this build\n"); return 2; }
    (void)port;
#endif
    static App app;
    static Session session;
    app_init(&app, output_message, stdout);
    session_init(&session, &app, 1, output_message, stdout);
#if LIGHTVM_HTTP
    HostNetwork network={.app=&app,.override=listen_address!=NULL,.address="127.0.0.1",.port=port};
    if (!listen_address && network_http_enabled(&app.network)) { listen_address="127.0.0.1"; port=network_http_port(&app.network); }
    network.listening=listen_address && host_http_start(&app,listen_address,port); network.port=port;
    if (listen_address && !network.listening) { fprintf(stderr,"Cannot listen for HTTP\n"); app_destroy(&app); host_storage_close(); return 1; }
    app.network.driver.context=&network; app.network.driver.configure=configure_network;
#endif
    bool stdin_open=true;
    for (;;) {
        app_tick(&app);
        fd_set input;
        FD_ZERO(&input);
        if (stdin_open) FD_SET(STDIN_FILENO, &input);
        struct timeval timeout = {0, 1000};
        int ready = select(STDIN_FILENO + 1, &input, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
#if LIGHTVM_HTTP
        host_http_poll();
#endif
        if (!ready || !stdin_open) continue;
        uint8_t byte;
        ssize_t size = read(STDIN_FILENO, &byte, 1);
        if (size<=0) {
            session_destroy(&session); stdin_open=false;
#if LIGHTVM_HTTP
            if (!network.listening) break;
#else
            break;
#endif
            continue;
        }
        session_feed(&session, &byte, 1); app_reply_delivered(&app,1);
    }
    session_destroy(&session);
#if LIGHTVM_HTTP
    if (network.listening) host_http_stop();
#endif
    app_destroy(&app);
    host_storage_close();
    return 0;
}
