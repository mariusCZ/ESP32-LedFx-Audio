#include "esp_event.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_websocket_client.h"

void wifi_init_sta(void);
void init_wifi(void);
void websocket_app_start(void *pvParameters);
void send_ws(char *dat, int len);
int wait_for_ws(void);
int check_connection(void);
void shutdown_ws(void);
int msg_check();