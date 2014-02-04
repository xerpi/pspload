/*
    Copyright (c) 2014, xerpi
*/

#include <pspkernel.h>
#include <psputility.h>
#include <pspdebug.h>
#include <pspsdk.h>
#include <psploadexec.h>
#include <psploadexec_kernel.h>
#include <systemctrl.h>
#include <pspctrl.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <time.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspnet_resolver.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <pspload.h>
#define printf pspDebugScreenPrintf

#define MOD_NAME   "pspload sample"

PSP_MODULE_INFO(MOD_NAME, PSP_MODULE_USER, 1, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(20480);

int exit_callback(int arg1, int arg2, void *common);
int CallbackThread(SceSize args, void *argp);
int SetupCallbacks(void);

int run = 1;

void *load_addr = NULL;
int file_size = 0;

struct {
    int argc;
    char **argv;
} load_args;
int current_argv = 0;

void list_netconfigs();
int select_netconfig();

int event_cb(int event, void *pspload_data, void *usrdata)
{
    //printf ("Event: %d\n", event);
    
    if (event == PSPLOAD_STATUS_NET_DISCONNECTED) {
        printf("Disconnected\n");
        int config_n = select_netconfig();
        pspload_connect(config_n);
        
    } else if (event == PSPLOAD_STATUS_NET_CONNECTING) {
        printf("Connecting to the access point...\n");
        
    } else if (event == PSPLOAD_STATUS_NET_CONNECTED) {
        printf("CONNECTED!\n");
        
    } else if (event == PSPLOAD_STATUS_CONNECT_TIMEOUT) {
        printf("Connection timeout\n");
        
    } else if (event == PSPLOAD_STATUS_NET_CONNECTED) {
        char ip[16];
        pspload_get_ip(ip);
        printf("\nPSP IP: %s    PORT: %d\n\n", ip, PSPLOAD_PORT);
    
    } else if (event == PSPLOAD_STATUS_CLIENT_LISTENING) {
        printf("Waiting for a client to connect...\n");
    
    } else if (event == PSPLOAD_STATUS_DATA_FILE_SIZE) {
        
        file_size = (int)pspload_data;
        load_addr = memalign(64, file_size);
        pspload_set_load_addr(load_addr);
    
    } else if (event == PSPLOAD_STATUS_ARGC_RECEIVED) {
        
        int argc = (int)pspload_data;
        //printf("argc: %d\n", argc);
        load_args.argc = argc;
        load_args.argv = malloc (sizeof(*load_args.argv) * argc);
        current_argv = 0;
        
    } else if (event == PSPLOAD_STATUS_ARGV_RECEIVED) {
        
        int arg_size = (int)pspload_data;
        load_args.argv[current_argv] = malloc(arg_size);
        pspload_set_argv_p(load_args.argv[current_argv]);
        ++current_argv;
    
    } else if (event == PSPLOAD_STATUS_DATA_RECEIVED) {
        
        pspload_deinit();
        
        char FILENAME[] = "ms0:/EBOOT.PBP";
        SceUID fd;
        if(!(fd = sceIoOpen(FILENAME, PSP_O_WRONLY|PSP_O_CREAT, 0777))) {
            printf("Error writing EBOOT: %d\n", fd);
        }
        sceIoWrite(fd, load_addr, file_size);
        sceIoClose(fd);

        struct SceKernelLoadExecVSHParam param; 
        memset(&param, 0, sizeof(param)); 
        param.key = "game"; 
        param.size = sizeof(param);
        param.args = strlen(FILENAME)+1; 
        param.argp = FILENAME;
        
        printf("Launching file...\n");
        sctrlKernelLoadExecVSHMs2(FILENAME, &param);


        /*  struct SceKernelLoadExecVSHParam doesn't support additonal args ?¿ */
        /*
        param.args = 1+load_args.argc;
        param.argp = malloc(sizeof(char*)*(1+load_args.argc));
        ((char**)param.argp)[0] = FILENAME;
        int i;
        for (i = 0; i < load_args.argc; ++i) {
            ((char**)param.argp)[i+1] = load_args.argv[i];
        }*/

        free(load_addr);
    }
    return 1;
}

int main(int argc, char **argv)
{
	SetupCallbacks();
	pspDebugScreenInit();
    
    printf("pspload sample by xerpi\n\n");

	int ret = pspload_init(event_cb, NULL);
    printf ("\npspload_init returned: %d\n", ret);

    int config_n = select_netconfig();
    ret = pspload_connect(config_n);
    printf ("pspload_connect returned: %d\n", ret);
    
    SceCtrlData pad, old_pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    old_pad = pad;
    
    while (run) {
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_TRIANGLE & ~old_pad.Buttons) run = 0;
        old_pad = pad;
        sceKernelDelayThread(10*1000);
    }
    
    printf("Exiting...\n");
	pspload_deinit();
    sceKernelExitGame();
	return 0;
}

int select_netconfig()
{
    #define MAX_CONFIG 10
    struct {
        int config_n;
        char *name;
    } net_list[MAX_CONFIG];
    
    /* Read net config list */
    
    int i, used = 0;
    for (i = 1; i <= MAX_CONFIG; ++i) {
        if (sceUtilityCheckNetParam(i) == 0) {
            netData data;
            sceUtilityGetNetParam(i, PSP_NETPARAM_NAME, &data);
            int name_size = strlen(data.asString) + 1;
            net_list[used].config_n = i;
            net_list[used].name = malloc(name_size);
            memcpy(net_list[used].name, data.asString, name_size);
            ++used;
        }
    }
    
    printf("Select an access point:\n");
    
    int selected = 0, last_y = pspDebugScreenGetY();
    SceCtrlData pad, old_pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    old_pad = pad;
    
    while (run) {
        sceCtrlPeekBufferPositive(&pad, 1);
        for (i = 0; i < used; ++i) {
            if (selected == i) {
                printf("\n-> %s\n", net_list[i].name);
            } else {
                printf("\n   %s\n", net_list[i].name);
            }
        }
        
        if (pad.Buttons & PSP_CTRL_UP & ~old_pad.Buttons) {
            --selected;
            if (selected < 0) selected = used-1;    
        } else if (pad.Buttons & PSP_CTRL_DOWN & ~old_pad.Buttons) {
            ++selected;
            if (selected >= used) selected = 0;    
        }
        
        if (pad.Buttons & PSP_CTRL_CROSS & ~old_pad.Buttons) break;
        
        old_pad = pad;
        pspDebugScreenSetXY(0, last_y);
        sceKernelDelayThread(10*1000);
    }
    
    return net_list[selected].config_n;
}


void list_netconfigs()
{
    #define MAX_CONFIG 10
    int i;
    for (i = 1; i <= MAX_CONFIG; ++i) {
        if (sceUtilityCheckNetParam(i) == 0) {
            printf("->Configuration %i\n", i);
            netData data;
            sceUtilityGetNetParam(i, PSP_NETPARAM_NAME, &data);
            printf("\tNAME: %s\n", data.asString);
            sceUtilityGetNetParam(i, PSP_NETPARAM_SSID, &data);
            printf("\tSSID: %s\n", data.asString);
            sceUtilityGetNetParam(i, PSP_NETPARAM_SECURE, &data);
            printf("\tSECURE: %s\n", data.asUint==2?"WEP":(data.asUint==3?"WPA":"NONE"));
        }
    }
}


/* Exit callback */
int exit_callback(int arg1, int arg2, void *common)
{
	run = 0;
	return 0;
}

/* Callback thread */
int CallbackThread(SceSize args, void *argp)
{
	int cbid;
	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();
	return 0;
}

/* Sets up the callback thread and returns its thread id */
int SetupCallbacks(void)
{
	int thid = 0;
	thid = sceKernelCreateThread("update_thread", CallbackThread,
			 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
	if (thid >= 0) {
		sceKernelStartThread(thid, 0, 0);
	}
	return thid;
}
