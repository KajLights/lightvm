#ifndef LIGHTVM_PICO_NETWORK_PORT_H
#define LIGHTVM_PICO_NETWORK_PORT_H
#include "app.h"
void pico_network_init(App *);
void pico_network_poll(void);
void pico_mqtt_poll(void);
bool pico_board_vsys_begin(void);
void pico_board_vsys_end(void);
bool pico_board_rssi(double *);
#endif
