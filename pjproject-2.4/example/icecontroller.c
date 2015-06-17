#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "httpwrapper.h"



char gUrl[] = "http://115.77.49.188:5001";

enum COMMAND_IDX {
    CMD_HOME_GET = 0,
    CMD_DEVICE_GET,
    CMD_HOME_GETT,
    CMD_EXIT,
    CMD_MAX
};


typedef struct cmd_handler_s{
    enum COMMAND_IDX cmd_idx;
    char help[256];
    int (*cmd_func)(void *arg);

}cmd_handler_t;

static int api_home_get(void* arg)
{
	char full_url[256];
	char *buff;
	
	printf("[DEBUG] %s, %d  \n", __FUNCTION__, __LINE__ );

	strcpy(full_url, gUrl); // plus URL 
	strcpy(&full_url[strlen(full_url)], "/device/getDevicesFromNetwork/"); // plus API 
	sprintf(&full_url[strlen(full_url)], "%s", arg); // plus agrument 	
	printf("[DEBUG] API: %s \n", full_url);
	http_get_request(full_url, &buff);
	// TODO: fine-tuning the result by using libxml 
	printf("[Debug] received GET response: \n %s \n", buff);
	free(buff);
    return 0;
}

static int api_device_get(void* arg)
{
	char full_url[256];
	char *buff;
	
	strcpy(full_url, gUrl);
	strcpy(&full_url[strlen(full_url)], "/device/getDevice/");	
	sprintf(&full_url[strlen(full_url)], "%s", arg);	
	printf("[DEBUG] API: %s \n", full_url);

	http_get_request(full_url, &buff);
	// TODO: fine-tuning the result by using libxml 
	printf("[Debug] received GET response: \n %s \n", buff);
	free(buff);
    return 0;

}

int cmd_more(void* arg);

cmd_handler_t cmd_list[CMD_MAX] = {
    {.cmd_idx = CMD_HOME_GET, .help = "Get all devices in a homenetwork", .cmd_func = api_home_get},
    {.cmd_idx = CMD_DEVICE_GET, .help = "Get full information of a registered device", .cmd_func = api_device_get },
    {.cmd_idx = CMD_HOME_GETT, .help = "Get all devices in a homenetwork", .cmd_func = api_home_get},
    {.cmd_idx = CMD_EXIT, .help = "Exit program", .cmd_func = NULL}
};

void cmd_print_help()
{
    int i = 0;
    for (i = 0; i < CMD_MAX; i++)
        printf("%d: \t %s \n", cmd_list[i].cmd_idx, cmd_list[i].help);
}



int is_valid_int(const char *str)
{
    //
    if (!*str)
        return 0;
    while (*str)
    {
        if (!isdigit(*str))
            return 0;
        else
            ++str;
    }

    return 1;
}

int main(int argc, char *argv[])
{
#if 1
    char cmd[256];
    memset(cmd, 0, 256);
    while (printf(">>>") && gets(&cmd[0]) != NULL)
#else
	char *cmd;
    while (printf(">>>") && getline1(&cmd, = NULL)
#endif
    {
        //printf("cmd: %s \n", cmd);
        if (is_valid_int(cmd))
        {
            int idx = atoi(cmd);
		printf("[DEBUG] command index : %d \n", idx );
            switch (idx)
            {
            case CMD_HOME_GET:
		api_home_get("networkID1");
			break;
            case CMD_HOME_GETT:
		api_home_get("networkID1");
			break;
                //cmd_list[idx].cmd_func("networkID1");
            case CMD_DEVICE_GET:
                cmd_list[idx].cmd_func("device1");
                break;
            case CMD_EXIT:
                printf("BYE BYE :-*, :-*\n");
                exit(0);
            default:
                cmd_print_help();
                break;
            }
        }else
            cmd_print_help();

        memset(cmd, 0, 256);
    }

}
