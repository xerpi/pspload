/*
    Copyright (c) 2014, xerpi
*/


#include "pspload.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pspthreadman.h>
#include <psputility.h>
#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PSPLOAD_CHUNK_LEN       1024
#define PSPLOAD_READ_TIMEOUT    5.0
#define PSPLOAD_CONNECT_TIMEOUT 10.0
#define PSPLOAD_MAGIC_1         'L'
#define PSPLOAD_MAGIC_2         'O'
#define PSPLOAD_MAGIC_3         'A'
#define PSPLOAD_MAGIC_4         'D'

/*
  PROTOCOL:
    1) Read until a valid header is found
    2) Read args
    3) Read file (splitted into PSPLOAD_CHUNK_LEN bytes packet)
*/

struct pspload_header {
    uint8_t  magic[4];
    uint32_t file_size;
    int32_t  argc;
} __attribute__((packed));

#define pspload_read read

#define _pspload_call_cb(event) \
            if (_event_cb) _event_cb(event, NULL, _usrdata)
#define _pspload_call_cb_data(event, data) \
            if (_event_cb) _event_cb(event, data, _usrdata) 
#define _pspload_wait_not_NULL(p) \
            while (p == NULL) sceKernelDelayThread(20)

#define PSPLOAD_CMP_HEADER(a) ((a[0] == PSPLOAD_MAGIC_1) &&  \
                               (a[1] == PSPLOAD_MAGIC_2) &&  \
                               (a[2] == PSPLOAD_MAGIC_3) &&  \
                               (a[3] == PSPLOAD_MAGIC_4))

static int               _status   = PSPLOAD_STATUS_NET_DISCONNECTED;
static pspload_event_cb  _event_cb   = NULL;
static void             *_usrdata    = NULL;
static SceUID            _thid       = -1;
volatile static int      _run_thread = 1;
static int               _thread_created  = 0;
static int               _modules_loaded  = 0;
static int               _net_initalized  = 0;
static int               _connected       = 0;
static int               _con_state       = 0;
static int               _last_con_state  = 0;

static int               _sock_server = 0;
static int               _sock_client = 0;
struct sockaddr_in       _server;

struct pspload_header    _header;
static void             *_user_load_addr = NULL;
static int               _total_read_bytes = 0;
static int               _read_bytes = 0;
static clock_t           _clock_start = 0;
static char             *_user_argv_p = NULL;

static int _pspload_start_server();
static int _pspload_thread(SceSize args, void *argp);
static int _pspload_load_net_modules();
static int _pspload_unload_net_modules();
static int _pspload_init_net();
static int _pspload_deinit_net();


int pspload_init(pspload_event_cb cb, void *usrdata)
{
    pspload_set_event_cb(cb, usrdata);
    
    if (!_modules_loaded) {
        if (_pspload_load_net_modules() < 0) return -1;
        else _modules_loaded = 1; 
    }
    
    if (!_net_initalized) {
        if (_pspload_init_net() < 0) return -1;
        else _net_initalized = 1;      
    }
    
    if (!_thread_created) {
        _thid = sceKernelCreateThread("pspload_thread", _pspload_thread, 0x10, 0x10000, PSP_THREAD_ATTR_USBWLAN, NULL);
         if (_thid < 0) return -2;
         else {
             _thread_created = 1;
             _run_thread = 1;
             _status   = PSPLOAD_STATUS_NET_DISCONNECTED;
             _connected = 0;
             sceKernelStartThread(_thid, 0, NULL);
         }
    }

    return 0;
}

int pspload_deinit()
{
    if (_thread_created) {
        _run_thread = 0;
        sceKernelTerminateThread(_thid);
        sceKernelWaitThreadEnd(_thid, NULL);
        sceKernelDeleteThread(_thid);
        _thread_created = 0;
    }
    
    if (_net_initalized) {
        int ret = _pspload_deinit_net();
        if (ret < 0) return -1;
        else _net_initalized = 0;
    }
    
    if (_modules_loaded) {
        if (_pspload_unload_net_modules() < 0) return -2;
        else _modules_loaded = 0;
    }
    
    _status = PSPLOAD_STATUS_NET_DISCONNECTED;
    _user_load_addr = NULL;
    
    return 0;
}

int pspload_get_ip(char *ip)
{
    int state;
    sceNetApctlGetState(&state);
    
    union SceNetApctlInfo info_ip;
    if ((state !=PSP_NET_APCTL_STATE_GOT_IP) || (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info_ip) != 0)) {
        strcpy(ip, "unknown IP");
        return -1;
    }
    memcpy(ip, info_ip.ip, 16);
    return 0;
}

int pspload_connect(int config_n)
{
    if (_status == PSPLOAD_STATUS_NET_DISCONNECTED) {
        int ret = sceNetApctlConnect(config_n);
        if (ret < 0) return ret;
        _status = PSPLOAD_STATUS_NET_CONNECTING;
        _pspload_call_cb(PSPLOAD_STATUS_NET_CONNECTING);
        return ret;
    }
    return 0;
}

void pspload_set_load_addr(void *load_addr)
{
    _user_load_addr = load_addr;
}

void pspload_set_argv_p(char *argv_p)
{
    _user_argv_p = argv_p;
}

static int _pspload_start_server()
{
    _sock_server = socket(AF_INET, SOCK_STREAM, 0);
    if (_sock_server < 0) return _sock_server;
    
    _server.sin_family = AF_INET;
    _server.sin_port   = htons(PSPLOAD_PORT);
    _server.sin_addr.s_addr = htonl(INADDR_ANY);
 
    int ret = bind(_sock_server, (struct sockaddr *) &_server, sizeof(_server));
    if (ret < 0) return ret;
    
	ret = listen(_sock_server, 1);
	return ret;        
}

static int _pspload_thread(SceSize args, void *argp)
{
    while (_run_thread) {
        switch (_status) {
        case PSPLOAD_STATUS_NET_DISCONNECTED:
            sceKernelDelayThread(100*1000);
            break;
            
        case PSPLOAD_STATUS_NET_CONNECTING:
            sceNetApctlGetState(&_con_state);
            if (_con_state == PSP_NET_APCTL_STATE_GOT_IP) {
                _status = PSPLOAD_STATUS_NET_CONNECTED;
                _connected = 1;
                _pspload_call_cb(PSPLOAD_STATUS_NET_CONNECTED);
                break;  
            } else if (_con_state == PSP_NET_APCTL_STATE_DISCONNECTED) {
                _status = PSPLOAD_STATUS_NET_DISCONNECTED;
                _pspload_call_cb(PSPLOAD_STATUS_NET_DISCONNECTED);
            }
            
            /* Check timeout */
            if (_con_state != _last_con_state) {
                _clock_start = sceKernelLibcClock();
                _last_con_state = _con_state;   
            } else {
                if (((sceKernelLibcClock() - _clock_start)/(double)CLOCKS_PER_SEC) > PSPLOAD_CONNECT_TIMEOUT) {
                    _pspload_call_cb(PSPLOAD_STATUS_CONNECT_TIMEOUT);
                    _status = PSPLOAD_STATUS_NET_DISCONNECTED;
                    _pspload_call_cb(PSPLOAD_STATUS_NET_DISCONNECTED);
                }
            }
            sceKernelDelayThread(50*1000);
            break;
            
        case PSPLOAD_STATUS_NET_CONNECTED:
            if (_pspload_start_server() < 0)
                sceKernelDelayThread(100*1000);
            else {
                _status = PSPLOAD_STATUS_CLIENT_LISTENING;
                _pspload_call_cb(PSPLOAD_STATUS_CLIENT_LISTENING);
            }
            break;
            
        case PSPLOAD_STATUS_CLIENT_LISTENING:
            _sock_client = accept(_sock_server, NULL, NULL);
            if (_sock_client < 0) 
                sceKernelDelayThread(10*1000);
            else {
                _status = PSPLOAD_STATUS_CLIENT_CONNECTED;
                _pspload_call_cb(PSPLOAD_STATUS_CLIENT_CONNECTED);
            }
            break;
            
        case PSPLOAD_STATUS_CLIENT_CONNECTED:
            memset(&_header, 0, sizeof(struct pspload_header));
            pspload_read(_sock_client, &_header, sizeof(struct pspload_header));
            
            /* Wait until we receive a valid header... */
            if (PSPLOAD_CMP_HEADER(_header.magic)) {
                _status = PSPLOAD_STATUS_DATA_RECEIVING;
                _pspload_call_cb(PSPLOAD_STATUS_DATA_RECEIVING);
                _pspload_call_cb_data(PSPLOAD_STATUS_DATA_FILE_SIZE, (void*)_header.file_size);
                _pspload_call_cb_data(PSPLOAD_STATUS_ARGC_RECEIVED,  (void*)_header.argc);
                
                /* Read args */
                int i;
                for (i = 0; i < _header.argc; ++i) {
                    int data_size;
                    pspload_read(_sock_client, &data_size, sizeof(data_size));
                    
                    /* Wait until the user sets a valid argv address*/
                    _user_argv_p = NULL;
                    _pspload_call_cb_data(PSPLOAD_STATUS_ARGV_RECEIVED,  (void*)data_size);
                    _pspload_wait_not_NULL(_user_argv_p);
                    
                    pspload_read(_sock_client, _user_argv_p, data_size);
                }
                
                /* Wait until the user sets a valid load address */
                _pspload_wait_not_NULL(_user_load_addr);
                
                _total_read_bytes = 0;
                _read_bytes = 0;
                _clock_start = sceKernelLibcClock();
            } else {
                sceKernelDelayThread(10*1000);
            }
            break;
            
        case PSPLOAD_STATUS_DATA_RECEIVING:
            if (_total_read_bytes < _header.file_size) {
                _read_bytes = pspload_read(_sock_client, _user_load_addr + _total_read_bytes, PSPLOAD_CHUNK_LEN);
                if (_read_bytes > 0) {
                    _clock_start = sceKernelLibcClock();
                    _total_read_bytes += _read_bytes;
                
                /* Check timeout */
                } else {
                    if (((sceKernelLibcClock() - _clock_start)/(double)CLOCKS_PER_SEC) > PSPLOAD_READ_TIMEOUT) {
                        _pspload_call_cb(PSPLOAD_STATUS_RECEIVE_TIMEOUT);
                        _status = PSPLOAD_STATUS_CLIENT_LISTENING;
                        _pspload_call_cb(PSPLOAD_STATUS_CLIENT_LISTENING);
                    }
                }
            } else { /* Data received */
                sceKernelDcacheWritebackRange(_user_load_addr, _header.file_size);
                _pspload_call_cb(PSPLOAD_STATUS_DATA_RECEIVED);
                _status = PSPLOAD_STATUS_CLIENT_LISTENING;
                _pspload_call_cb(PSPLOAD_STATUS_CLIENT_LISTENING);
            }
            break;
            
        default:
            break;
        }
    }
    
    sceKernelExitDeleteThread(0);
    return 0;
}

static int _pspload_load_net_modules()
{
    int ret = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    if (ret < 0) return ret;
    ret = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
    return ret;
}

static int _pspload_unload_net_modules()
{
    int ret = sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    if (ret < 0) return ret;
    ret = sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    return ret;
}

static int _pspload_init_net()
{
	int ret = sceNetInit(128*1024, 42, 4*1024, 42, 4*1024);
    if (ret < 0) return ret;
	ret = sceNetInetInit();
    if (ret < 0) return ret;
	ret = sceNetApctlInit(0x8000, 48);
    return ret;
}

static int _pspload_deinit_net()
{
    if (_connected) sceNetApctlDisconnect();
    int ret = sceNetApctlTerm();
    if (ret < 0) return ret;
	ret = sceNetInetTerm();
    if (ret < 0) return ret;
	ret = sceNetTerm();
    return ret;
}

void pspload_set_event_cb(pspload_event_cb cb, void *usrdata)
{
    _event_cb = cb;
    _usrdata  = usrdata;  
}
