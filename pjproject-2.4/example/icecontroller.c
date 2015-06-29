#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icewrapper.h"
#include "httpwrapper.h"
#include "xml2wrapper.h"
#include "utilities.h"


#define MAX_ICE_TRANS  2

struct nat_client_t
{

    ice_option_t opt;

    ice_trans_t ice_receive;

    ice_trans_t ice_trans_list[MAX_ICE_TRANS];

} ;

//  Global variable

struct nat_client_t natclient;


char gUrl[] = "http://115.77.49.188:5001";
char usrid[256];
char host_name[256];
int portno;



/*
 * This is the callback that is registered to the ICE stream transport to
 * receive notification about incoming data. By "data" it means application
 * data such as RTP/RTCP, and not packets that belong to ICE signaling (such
 * as STUN connectivity checks or TURN signaling).
 */

static void cb_on_rx_data(pj_ice_strans *ice_st,
                          unsigned comp_id,
                          void *pkt, pj_size_t size,
                          const pj_sockaddr_t *src_addr,
                          unsigned src_addr_len)
{
    char ipstr[PJ_INET6_ADDRSTRLEN+10];

    PJ_UNUSED_ARG(ice_st);
    PJ_UNUSED_ARG(src_addr_len);
    PJ_UNUSED_ARG(pkt);

    // Don't do this! It will ruin the packet buffer in case TCP is used!
    //((char*)pkt)[size] = '\0';

    PJ_LOG(3,(THIS_FILE, "Component %d: received %d bytes data from %s: \"%.*s\"",
              comp_id, size,
              pj_sockaddr_print(src_addr, ipstr, sizeof(ipstr), 3),
              (unsigned)size,
              (char*)pkt));

    // TODO: how to know which session this RX belongs to
    hexDump(NULL, pkt, size);


}

/*
 * This is the callback that is registered to the ICE stream transport to
 * receive notification about ICE state progression.
 */
static void cb_on_ice_complete(pj_ice_strans *ice_st,
                               pj_ice_strans_op op,
                               pj_status_t status)
{
    const char *opname =
            (op==PJ_ICE_STRANS_OP_INIT? "initialization" :
                                        (op==PJ_ICE_STRANS_OP_NEGOTIATION ? "negotiation" : "unknown_op"));

    if (status == PJ_SUCCESS) {
        PJ_LOG(3,(THIS_FILE, "ICE %s successful", opname));
    } else {
        char errmsg[PJ_ERR_MSG_SIZE];

        pj_strerror(status, errmsg, sizeof(errmsg));
        PJ_LOG(1,(THIS_FILE, "ICE %s failed: %s", opname, errmsg));
        pj_ice_strans_destroy(ice_st);

        // FIXME: update the ICE transaction
        //natclient.icest = NULL;
    }
}





static int get_ice_tran_from_name(char *name)
{
    int i;
    for (i = 0; i < MAX_ICE_TRANS; i++)
        if (strcmp(name, natclient.ice_trans_list[i].name) == 0)
            return i;
    return i;
}



enum COMMAND_IDX {
    CMD_HOME_GET = 0,
    CMD_DEVICE_GET,
    CMD_DEVICE_REGISTER,
    CMD_PEER_CONNECT,
    CMD_PEER_SEND,
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
    sprintf(&full_url[strlen(full_url)], "%s", (char *)arg); // plus agrument
    //printf("[DEBUG] API: %s \n", full_url);
    http_get_request(full_url, buff);

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
    sprintf(&full_url[strlen(full_url)], "%s", (char*)arg);
    //printf("[DEBUG] API: %s \n", full_url);

    http_get_request(full_url, buff);
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

static int api_peer_connect(void *arg)
{

    int index;

    index = get_ice_tran_from_name(arg);

    if (index < MAX_ICE_TRANS)
    { // do no thing as the
/*
        printf("[DEBUG] %s index: %d \n", __FUNCTION__, index);
        // re-initialize
        ice_trans_t *ice_trans = &natclient.ice_trans_list[index];
        natclient_connect_with_user(ice_trans, arg);
        natclient_start_nego(ice_trans);
        */
    }else if ((index = get_ice_tran_from_name("")) < MAX_ICE_TRANS){
        // Get an empty ice

        printf("DEBUG start initialization \n", __FILE__, __LINE__);

        printf("[DEBUG] %s index: %d \n", __FUNCTION__, index);
        ice_trans_t *ice_trans = &natclient.ice_trans_list[index];
        strcpy(ice_trans->name, arg);
        natclient_connect_with_user(ice_trans, arg);
        natclient_start_nego(ice_trans);
    }else
        return -1;

    return 0;
}


typedef struct _MSG_S{
    char username[256];
    char msg[256];
}MSG_T;

static int api_peer_send(void *arg)
{
    MSG_T *msg = (MSG_T *)arg;

    int index = get_ice_tran_from_name(msg->username);

    if (index < MAX_ICE_TRANS)
    {

        printf("[DEBUG] %s index: %d \n", __FUNCTION__, index);
        // TODO: Send to a particular user
        natclient_send_data(&natclient.ice_trans_list[index], 1, msg->msg);
    }else if ((index = get_ice_tran_from_name("")) < MAX_ICE_TRANS){

        printf("[DEBUG] %s index: %d \n", __FUNCTION__, index);
        natclient_send_data(&natclient.ice_trans_list[index], 1, msg->msg);
    }else
        return -1;
    return 0;
}



cmd_handler_t cmd_list[CMD_MAX] = {
    {.cmd_idx = CMD_HOME_GET, .help = "Get all devices in a homenetwork", .cmd_func = api_home_get},
    {.cmd_idx = CMD_DEVICE_GET, .help = "Get full information of a registered device", .cmd_func = api_device_get },
    {.cmd_idx = CMD_DEVICE_REGISTER, .help = "Register a device to cloud", .cmd_func = api_device_register },
    {.cmd_idx = CMD_PEER_CONNECT, .help = "Create a ICE connectionto peer", .cmd_func = api_peer_connect },
    {.cmd_idx = CMD_PEER_SEND, .help = "Send a message to peer", .cmd_func = api_peer_send },
    {.cmd_idx = CMD_EXIT, .help = "Exit program", .cmd_func = NULL}
};

void cmd_print_help()
{
    int i = 0;
    printf("\n\n===============%s=======================\n", usrid);
    for (i = 0; i < CMD_MAX; i++)
        printf("%d: \t %s \n", cmd_list[i].cmd_idx, cmd_list[i].help);
}






static void natclient_console(void)
{
    pj_bool_t app_quit = PJ_FALSE;

    printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    struct ice_trans_s* icetrans = &natclient.ice_receive;

    strcpy(icetrans->name, usrid);
    natclient_create_instance(icetrans,  natclient.opt);

    usleep(1*1000*1000);
    natclient_init_session(icetrans, 'o');
    usleep(4*1000*1000);
    get_and_register_SDP_to_cloud(icetrans, natclient.opt, usrid);
    int i;

    for (i = 0; i < MAX_ICE_TRANS; i++)
    {
        icetrans = &natclient.ice_trans_list[i];
        natclient_create_instance(icetrans, natclient.opt);
        usleep(1*1000*1000);
        natclient_init_session(icetrans, 'o');
        strcpy(icetrans->name, "");
    }

    char cmd[256];
    memset(cmd, 0, 256);
    while (printf(">>>") && fgets(&cmd[0], 256, stdin) != NULL)
    {
        printf("cmd: %s \n", cmd);
        //if (is_valid_int(cmd))
        if ( cmd[0] >= '0' && cmd[0] <= '9')
        {
            int idx = atoi(cmd);
            printf("[DEBUG] command index : %d \n", idx );
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
            case CMD_PEER_CONNECT:
                printf("which user: ");
                char user[256];
		memset(user, 256, 0);
                fgets(user, 256, stdin);
                if (strlen(user) > 2)
                    api_peer_connect(user);
                break;
            case CMD_PEER_SEND:
            {
                MSG_T *msg = (MSG_T *)calloc(sizeof(MSG_T), 1);
                printf("[USR]: ");
                fgets(msg->username, 256, stdin);
                printf("[MSG]: ");
                fgets(msg->msg, 256, stdin);
                if (strlen(msg->msg) > 1)
                    cmd_list[idx].cmd_func(msg);
                break;
            }
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


/*
                   * Display program usage.
                   */
static void natclient_usage()
{
    puts("Usage: natclient [optons]");
    printf("natclient v%s by pjsip.org\n", pj_get_version());
    puts("");
    puts("General options:");
    puts(" --comp-cnt, -c N          Component count (default=1)");
    puts(" --nameserver, -n IP       Configure nameserver to activate DNS SRV");
    puts("                           resolution");
    puts(" --max-host, -H N          Set max number of host candidates to N");
    puts(" --regular, -R             Use regular nomination (default aggressive)");
    puts(" --log-file, -L FILE       Save output to log FILE");
    puts(" --help, -h                Display this screen.");
    puts("");
    puts("STUN related options:");
    puts(" --stun-srv, -s HOSTDOM    Enable srflx candidate by resolving to STUN server.");
    puts("                           HOSTDOM may be a \"host_or_ip[:port]\" or a domain");
    puts("                           name if DNS SRV resolution is used.");
    puts("");
    puts("TURN related options:");
    puts(" --turn-srv, -t HOSTDOM    Enable relayed candidate by using this TURN server.");
    puts("                           HOSTDOM may be a \"host_or_ip[:port]\" or a domain");
    puts("                           name if DNS SRV resolution is used.");
    puts(" --turn-tcp, -T            Use TCP to connect to TURN server");
    puts(" --turn-username, -u UID   Set TURN username of the credential to UID");
    puts(" --turn-password, -p PWD   Set password of the credential to WPWD");
    puts("Signalling Server related options:");
    puts(" --usrid, -U usrid    user id ");
    puts(" --signalling, -S    Signalling server");
    puts(" --signalling-port, -P    Use fingerprint for outgoing TURN requests");

    puts("Device specific option:");
    puts("");
}



int main(int argc, char *argv[])
{
    struct pj_getopt_option long_options[] = {
    { "comp-cnt",           1, 0, 'c'},
    { "nameserver",		1, 0, 'n'},
    { "max-host",		1, 0, 'H'},
    { "help",		0, 0, 'h'},
    { "stun-srv",		1, 0, 's'},
    { "turn-srv",		1, 0, 't'},
    { "turn-tcp",		0, 0, 'T'},
    { "turn-username",	1, 0, 'u'},
    { "turn-password",	1, 0, 'p'},
    { "turn-fingerprint",	0, 0, 'F'},
    { "regular",		0, 0, 'R'},
    { "log-file",		1, 0, 'L'},
    { "userid",   1, 0, 'U'},
    { "singalling",   1, 0, 'S'},
    { "singalling-port",   1, 0, 'P'},


};
    int c, opt_id;


    // default initialization

    strcpy(usrid, "userid");
    strcpy(host_name, "116.100.11.109");
    portno = 12345;


    pj_status_t status;

    natclient.opt.comp_cnt = 1;
    natclient.opt.max_host = -1;

    while((c=pj_getopt_long(argc,argv, "c:n:s:t:u:p:H:L:U:S:P:hTFR", long_options, &opt_id))!=-1) {
        switch (c) {
        case 'c':
            natclient.opt.comp_cnt = atoi(pj_optarg);
            if (natclient.opt.comp_cnt < 1 || natclient.opt.comp_cnt >= PJ_ICE_MAX_COMP) {
                puts("Invalid component count value");
                return 1;
            }
            break;
        case 'n':
            natclient.opt.ns = pj_str(pj_optarg);
            break;
        case 'H':
            natclient.opt.max_host = atoi(pj_optarg);
            break;
        case 'h':
            natclient_usage();
            return 0;
        case 's':
            printf("[Debug] %s, %d, option's value: %s \n", __FILE__, __LINE__, pj_optarg);
            natclient.opt.stun_srv = pj_str(pj_optarg);
            break;
        case 't':
            natclient.opt.turn_srv = pj_str(pj_optarg);
            break;
        case 'T':
            natclient.opt.turn_tcp = PJ_TRUE;
            break;
        case 'u':
            natclient.opt.turn_username = pj_str(pj_optarg);
            break;
        case 'p':
            natclient.opt.turn_password = pj_str(pj_optarg);
            break;
        case 'F':
            natclient.opt.turn_fingerprint = PJ_TRUE;
            break;
        case 'R':
            natclient.opt.regular = PJ_TRUE;
            break;
        case 'L':
            natclient.opt.log_file = pj_optarg;
            break;
        case 'U':
            printf("[Debug] %s, %d \n", __FILE__, __LINE__);
            strcpy(usrid, pj_optarg);
            break;
        case 'S':
            printf("[Debug] %s, %d, option's value: %s \n", __FILE__, __LINE__, pj_optarg);
            strcpy(host_name, pj_optarg);
            break;
        case 'P':
            printf("[Debug] %s, %d \n", __FILE__, __LINE__);
            portno = atoi(pj_optarg);
            break;

        default:
            printf("Argument \"%s\" is not valid. Use -h to see help",
                   argv[pj_optind]);
            return 1;
        }
    }

    // initialization for receiving
    status = natclient_init(&natclient.ice_receive, natclient.opt);
    get_and_register_SDP_to_cloud(&natclient.ice_receive, natclient.opt, usrid);

    natclient.ice_receive.cb_on_ice_complete = cb_on_ice_complete;
    natclient.ice_receive.cb_on_rx_data = cb_on_rx_data;


    int i;
    for (i = 0; i < MAX_ICE_TRANS; i++)
    {
        natclient.ice_trans_list[i].cb_on_ice_complete = cb_on_ice_complete;
        natclient.ice_trans_list[i].cb_on_rx_data = cb_on_rx_data;
        natclient_init(&natclient.ice_trans_list[i], natclient.opt);
    }


    if (status != PJ_SUCCESS)
        return 1;

    natclient_console();

    err_exit("Quitting..", PJ_SUCCESS, &natclient.ice_receive);
    for (i = 0; i < MAX_ICE_TRANS; i++)
        err_exit("Quitting..", PJ_SUCCESS, &natclient.ice_trans_list[i]);

    // FIXME: exit all opened ice session


    return 0;
}
