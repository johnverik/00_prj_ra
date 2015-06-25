#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "httpwrapper.h"
#include "xml2wrapper.h"


struct app_t
{
    /* Command line options are stored here */
    struct options
    {
        unsigned    comp_cnt;
        pj_str_t    ns;
        int	    max_host;
        pj_bool_t   regular;
        pj_str_t    stun_srv;
        pj_str_t    turn_srv;
        pj_bool_t   turn_tcp;
        pj_str_t    turn_username;
        pj_str_t    turn_password;
        pj_bool_t   turn_fingerprint;
        const char *log_file;
    } opt;

    ice_trans_t ice_receive;
#ifdef MULTIPLE
    ice_trans_t ice_trans_list[MAX_ICE_TRANS];
#endif

} ;


struct app_t icedemo;


char gUrl[] = "http://115.77.49.188:5001";

enum COMMAND_IDX {
    CMD_HOME_GET = 0,
    CMD_DEVICE_GET,
    CMD_DEVICE_REGISTER,
    CMD_EXIT,
    CMD_MAX
};


typedef struct cmd_handler_s{
    enum COMMAND_IDX cmd_idx;
    char help[256];
    int (*cmd_func)(void *arg);

}cmd_handler_t;


static int api_device_register(void *arg)
{

    char register_device[] = "<?xml version=\"1.0\"?> \
    <deviceRegister> \
    <device> \
    <deviceId/> \
    <uniqueId>Mydevice1</uniqueId> \
    <modelCode>Sensor</modelCode> \
    <home> \
    <description>Test Home</description> \
    <networkID>networkID1</networkID> \
    </home> \
    <firmwareVersion>firmware.01.pvt</firmwareVersion> \
    </device> \
    <reRegister>0</reRegister> \
    <smartDevice> \
    <description>smart phone</description> \
    <uniqueId>unq_2305130636</uniqueId> \
    </smartDevice> \
            </deviceRegister>";

    char full_url[256];
    char *buff;

    //printf("[DEBUG] %s, %d  \n", __FUNCTION__, __LINE__ );

    strcpy(full_url, gUrl); // plus URL
    strcpy(&full_url[strlen(full_url)], "/device/registerDevice"); // plus API
    http_post_request(full_url, register_device);
    //printf("[DEBUG] API: %s \n", full_url);

}



static int api_home_get(void* arg)
{
	char full_url[256];
	char *buff;
	
    //printf("[DEBUG] %s, %d  \n", __FUNCTION__, __LINE__ );

	strcpy(full_url, gUrl); // plus URL 
	strcpy(&full_url[strlen(full_url)], "/device/getDevicesFromNetwork/"); // plus API 
	sprintf(&full_url[strlen(full_url)], "%s", arg); // plus agrument 	
    //printf("[DEBUG] API: %s \n", full_url);
	http_get_request(full_url, &buff);

    xmlNode *device = xml_get_node_by_name(buff, "DeviceList");
    xmlNode *cur_node;
    for (cur_node = device->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE)
        {
            char *device_name = (char *)xmlNodeGetContent(cur_node);
            printf("\t %s \n", device_name);
            free(device_name);
        }
    }


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
    //printf("[DEBUG] API: %s \n", full_url);

	http_get_request(full_url, &buff);
    //printf("DEBUG recieved buffer: \n %s \n", buff);
	// TODO: fine-tuning the result by using libxml 
    char *value = xml_get_content_by_name(buff, "uniqueId");
    printf("Device information: \n");
    printf("\t Device ID: %s \n", value);
    printf("=============================\n");
    free(value);
    free(buff);
    return 0;

}

int cmd_more(void* arg);

cmd_handler_t cmd_list[CMD_MAX] = {
    {.cmd_idx = CMD_HOME_GET, .help = "Get all devices in a homenetwork", .cmd_func = api_home_get},
    {.cmd_idx = CMD_DEVICE_GET, .help = "Get full information of a registered device", .cmd_func = api_device_get },
    {.cmd_idx = CMD_DEVICE_REGISTER, .help = "Register a device to cloud", .cmd_func = api_device_register },
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
        //printf("[DEBUG] command index : %d \n", idx );
            switch (idx)
            {
            case CMD_HOME_GET:
                cmd_list[idx].cmd_func("networkID1");
                break;
            case CMD_DEVICE_GET:
                cmd_list[idx].cmd_func("device1");
                break;
           case CMD_DEVICE_REGISTER:
                cmd_list[idx].cmd_func("registerDevice");
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
