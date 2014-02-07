/*
    Copyright (c) 2014, xerpi
*/


#ifndef _PSPLOAD_H_
#define _PSPLOAD_H_

#ifdef __cplusplus
extern "C" {
#endif


enum pspload_status {
    PSPLOAD_STATUS_NET_DISCONNECTED,
    PSPLOAD_STATUS_NET_CONNECTING,
    PSPLOAD_STATUS_NET_CONNECTED,
    PSPLOAD_STATUS_CLIENT_LISTENING,
    PSPLOAD_STATUS_CLIENT_CONNECTED,
    PSPLOAD_STATUS_DATA_RECEIVING,
    PSPLOAD_STATUS_ARGC_RECEIVED,
    PSPLOAD_STATUS_ARGV_RECEIVED,
    PSPLOAD_STATUS_DATA_RECEIVED,
    PSPLOAD_STATUS_RECEIVE_TIMEOUT,
    PSPLOAD_STATUS_CONNECT_TIMEOUT,
    PSPLOAD_STATUS_DATA_FILE_SIZE
};

#define PSPLOAD_PORT 4299

typedef int (*pspload_event_cb)(int event, void *pspload_data, void *usrdata);

int  pspload_init(pspload_event_cb cb, void *usrdata);
int  pspload_deinit();
int  pspload_get_ip(char *ip);
int  pspload_connect(int config_n);
void pspload_set_load_addr(void *load_addr);
void pspload_set_argv_p(char *argv_p);
void pspload_set_event_cb(pspload_event_cb cb, void *usrdata);

#ifdef __cplusplus
}
#endif


#endif
