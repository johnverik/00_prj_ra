#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "httpwrapper.h"
#include "xml2wrapper.h"
#include "icewrapper.h"





/* Utility to display error messages */
static void natclient_perror(const char *title, pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(1,(THIS_FILE, "%s: %s", title, errmsg));
}

/* Utility: display error message and exit application (usually
 * because of fatal error.
 */
void err_exit( const char *title, pj_status_t status , struct ice_trans_s* icetrans)
{

    int i;
        if (status != PJ_SUCCESS) {
            natclient_perror(title, status);
        }
        PJ_LOG(3,(THIS_FILE, "Shutting down.."));

        if (icetrans->icest)
            pj_ice_strans_destroy(icetrans->icest);

        pj_thread_sleep(500);

        icetrans->thread_quit_flag = PJ_TRUE;
        if (icetrans->thread) {
            pj_thread_join(icetrans->thread);
            pj_thread_destroy(icetrans->thread);
        }

        if (icetrans->ice_cfg.stun_cfg.ioqueue)
            pj_ioqueue_destroy(icetrans->ice_cfg.stun_cfg.ioqueue);

        if (icetrans->ice_cfg.stun_cfg.timer_heap)
            pj_timer_heap_destroy(icetrans->ice_cfg.stun_cfg.timer_heap);

        pj_caching_pool_destroy(&icetrans->cp);

        pj_shutdown();

        if (icetrans->log_fhnd) {
            fclose(icetrans->log_fhnd);
            icetrans->log_fhnd = NULL;
        }
    exit(status != PJ_SUCCESS);

}

#define CHECK(expr, icetrans)	status=expr; \
    if (status!=PJ_SUCCESS) { \
    err_exit(#expr, status, icetrans); \
    }

/*
 * This function checks for events from both timer and ioqueue (for
 * network events). It is invoked by the worker thread.
 */
static pj_status_t handle_events(struct ice_trans_s* icetrans, unsigned max_msec, unsigned *p_count)
{
    enum { MAX_NET_EVENTS = 1 };
    pj_time_val max_timeout = {0, 0};
    pj_time_val timeout = { 0, 0};
    unsigned count = 0, net_event_count = 0;
    int c;

    max_timeout.msec = max_msec;

    /* Poll the timer to run it and also to retrieve the earliest entry. */
    timeout.sec = timeout.msec = 0;
    c = pj_timer_heap_poll( icetrans->ice_cfg.stun_cfg.timer_heap, &timeout );
    if (c > 0)
        count += c;

    /* timer_heap_poll should never ever returns negative value, or otherwise
     * ioqueue_poll() will block forever!
     */
    pj_assert(timeout.sec >= 0 && timeout.msec >= 0);
    if (timeout.msec >= 1000) timeout.msec = 999;

    /* compare the value with the timeout to wait from timer, and use the
     * minimum value.
    */
    if (PJ_TIME_VAL_GT(timeout, max_timeout))
        timeout = max_timeout;

    /* Poll ioqueue.
     * Repeat polling the ioqueue while we have immediate events, because
     * timer heap may process more than one events, so if we only process
     * one network events at a time (such as when IOCP backend is used),
     * the ioqueue may have trouble keeping up with the request rate.
     *
     * For example, for each send() request, one network event will be
     *   reported by ioqueue for the send() completion. If we don't poll
     *   the ioqueue often enough, the send() completion will not be
     *   reported in timely manner.
     */
    do {
        c = pj_ioqueue_poll( icetrans->ice_cfg.stun_cfg.ioqueue, &timeout);
        if (c < 0) {
            pj_status_t err = pj_get_netos_error();
            pj_thread_sleep(PJ_TIME_VAL_MSEC(timeout));
            if (p_count)
                *p_count = count;
            return err;
        } else if (c == 0) {
            break;
        } else {
            net_event_count += c;
            timeout.sec = timeout.msec = 0;
        }
    } while (c > 0 && net_event_count < MAX_NET_EVENTS);

    count += net_event_count;
    if (p_count)
        *p_count = count;

    return PJ_SUCCESS;

}

/*
 * This is the worker thread that polls event in the background.
 */
static int natclient_worker_thread( void *unused)
{
    PJ_UNUSED_ARG(unused);

    ice_trans_t* tmp_icetrans =  (ice_trans_t *)(unused);

    while (!tmp_icetrans->thread_quit_flag) {
        handle_events(tmp_icetrans, 500, NULL);
    }

    return 0;
}

/* log callback to write to file */
static void log_func(struct ice_trans_s* icetrans,  int level, const char *data, int len)
{
    pj_log_write(level, data, len);
    if (icetrans->log_fhnd) {
        if (fwrite(data, len, 1, icetrans->log_fhnd) != 1)
            return;
    }
}

/*
 * This is the main application initialization function. It is called
 * once (and only once) during application initialization sequence by
 * main().
 */
// Note: this natclient_init is called just one time

pj_status_t natclient_init(ice_trans_t *icetrans, ice_option_t opt)
{
    pj_status_t status;


    /* Initialize the libraries before anything else */
    CHECK( pj_init(), icetrans );
    CHECK( pjlib_util_init(), icetrans );
    CHECK( pjnath_init(), icetrans );


#if 0  //FIXME: consider if we need to log
        if (natclient.opt.log_file) {
            icetrans->log_fhnd = fopen(natclient.opt.log_file, "a");
            pj_log_set_log_func(&log_func);
        }
#endif

        pj_caching_pool_init(&icetrans->cp, NULL, 0);

        /* Init our ICE settings with null values */
        pj_ice_strans_cfg_default(&icetrans->ice_cfg);

        icetrans->ice_cfg.stun_cfg.pf = &icetrans->cp.factory;

        /* Create application memory pool */
        icetrans->pool = pj_pool_create(&icetrans->cp.factory, "natclient",
                                      512, 512, NULL);

        /* Create timer heap for timer stuff */
        CHECK( pj_timer_heap_create(icetrans->pool, 100,
                                    &icetrans->ice_cfg.stun_cfg.timer_heap), icetrans );

        /* and create ioqueue for network I/O stuff */
        CHECK( pj_ioqueue_create(icetrans->pool, 16,
                                 &icetrans->ice_cfg.stun_cfg.ioqueue), icetrans );

        /* something must poll the timer heap and ioqueue,
     * unless we're on Symbian where the timer heap and ioqueue run
     * on themselves.
     */
        CHECK( pj_thread_create(icetrans->pool, "natclient", &natclient_worker_thread,
                                icetrans, 0, 0, &icetrans->thread), icetrans );

        icetrans->ice_cfg.af = pj_AF_INET();

        /* Create DNS resolver if nameserver is set */
        if (opt.ns.slen) {
            CHECK( pj_dns_resolver_create(&icetrans->cp.factory,
                                          "resolver",
                                          0,
                                          icetrans->ice_cfg.stun_cfg.timer_heap,
                                          icetrans->ice_cfg.stun_cfg.ioqueue,
                                          &icetrans->ice_cfg.resolver), icetrans );

            CHECK( pj_dns_resolver_set_ns(icetrans->ice_cfg.resolver, 1,
                                          &opt.ns, NULL) , icetrans);
        }


        /* -= Start initializing ICE stream transport config =- */

        /* Maximum number of host candidates */
        if (opt.max_host != -1)
            icetrans->ice_cfg.stun.max_host_cands = opt.max_host;

        /* Nomination strategy */
        if (opt.regular)
            icetrans->ice_cfg.opt.aggressive = PJ_FALSE;
        else
            icetrans->ice_cfg.opt.aggressive = PJ_TRUE;

        /* Configure STUN/srflx candidate resolution */
        if (opt.stun_srv.slen) {
            char *pos;

            /* Command line option may contain port number */
            if ((pos=pj_strchr(&opt.stun_srv, ':')) != NULL) {
                icetrans->ice_cfg.stun.server.ptr = opt.stun_srv.ptr;
                icetrans->ice_cfg.stun.server.slen = (pos - opt.stun_srv.ptr);

                icetrans->ice_cfg.stun.port = (pj_uint16_t)atoi(pos+1);
            } else {
                icetrans->ice_cfg.stun.server = opt.stun_srv;
                icetrans->ice_cfg.stun.port = PJ_STUN_PORT;
            }

            /* For this demo app, configure longer STUN keep-alive time
     * so that it does't clutter the screen output.
     */
            icetrans->ice_cfg.stun.cfg.ka_interval = KA_INTERVAL;
        }

        /* Configure TURN candidate */
        if (opt.turn_srv.slen) {
            char *pos;

            /* Command line option may contain port number */
            if ((pos=pj_strchr(&opt.turn_srv, ':')) != NULL) {
                icetrans->ice_cfg.turn.server.ptr = opt.turn_srv.ptr;
                icetrans->ice_cfg.turn.server.slen = (pos - opt.turn_srv.ptr);

                icetrans->ice_cfg.turn.port = (pj_uint16_t)atoi(pos+1);
            } else {
                icetrans->ice_cfg.turn.server = opt.turn_srv;
                icetrans->ice_cfg.turn.port = PJ_STUN_PORT;
            }

            /* TURN credential */
            icetrans->ice_cfg.turn.auth_cred.type = PJ_STUN_AUTH_CRED_STATIC;
            icetrans->ice_cfg.turn.auth_cred.data.static_cred.username = opt.turn_username;
            icetrans->ice_cfg.turn.auth_cred.data.static_cred.data_type = PJ_STUN_PASSWD_PLAIN;
            icetrans->ice_cfg.turn.auth_cred.data.static_cred.data = opt.turn_password;

            /* Connection type to TURN server */
            if (opt.turn_tcp)
                icetrans->ice_cfg.turn.conn_type = PJ_TURN_TP_TCP;
            else
                icetrans->ice_cfg.turn.conn_type = PJ_TURN_TP_UDP;

            /* For this demo app, configure longer keep-alive time
     * so that it does't clutter the screen output.
     */
            icetrans->ice_cfg.turn.alloc_param.ka_interval = KA_INTERVAL;
        }

    /* -= That's it for now, initialization is complete =- */
    return PJ_SUCCESS;
}


/*
 * Create ICE stream transport instance, invoked from the menu.
 */
void natclient_create_instance(struct ice_trans_s* icetrans, ice_option_t opt)
{
    pj_ice_strans_cb icecb;
    pj_status_t status;

    if (icetrans->icest != NULL) {
        puts("ICE instance already created, destroy it first");
        return;
    }

    /* init the callback */
    pj_bzero(&icecb, sizeof(icecb));
    icecb.on_rx_data = icetrans->cb_on_rx_data;
    icecb.on_ice_complete = icetrans->cb_on_ice_complete;

    /* create the instance */
    // TODO: just wonder if the object name should be unique among ICE transation

    status = pj_ice_strans_create("natclient",		    /* object name  */
                                  &icetrans->ice_cfg,	    /* settings	    */
                                  opt.comp_cnt,	    /* comp_cnt	    */
                                  NULL,			    /* user data    */
                                  &icecb,			    /* callback	    */
                                  &icetrans->icest)		    /* instance ptr */
            ;
    if (status != PJ_SUCCESS)
        natclient_perror("error creating ice", status);
    else
        PJ_LOG(3,(THIS_FILE, "ICE instance successfully created"));
}

/* Utility to nullify parsed remote info */
void reset_rem_info(struct ice_trans_s* icetrans)
{
    pj_bzero(&icetrans->rem, sizeof(icetrans->rem));
}


/*
 * Destroy ICE stream transport instance, invoked from the menu.
 */
void natclient_destroy_instance(struct ice_trans_s* icetrans)
{
    if (icetrans->icest == NULL) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
        return;
    }

    pj_ice_strans_destroy(icetrans->icest);
    icetrans->icest = NULL;

    reset_rem_info(icetrans);

    PJ_LOG(3,(THIS_FILE, "ICE instance destroyed"));
}


/*
 * Create ICE session, invoked from the menu.
 */
void natclient_init_session(struct ice_trans_s* icetrans, unsigned rolechar)
{
    pj_ice_sess_role role = (pj_tolower((pj_uint8_t)rolechar)=='o' ?
                                 PJ_ICE_SESS_ROLE_CONTROLLING :
                                 PJ_ICE_SESS_ROLE_CONTROLLED);
    pj_status_t status;

    if (icetrans->icest == NULL) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
        return;
    }

    if (pj_ice_strans_has_sess(icetrans->icest)) {
        PJ_LOG(1,(THIS_FILE, "Error: Session already created"));
        return;
    }

    status = pj_ice_strans_init_ice(icetrans->icest, role, NULL, NULL);
    if (status != PJ_SUCCESS)
        natclient_perror("error creating session", status);
    else
        PJ_LOG(3,(THIS_FILE, "ICE session created"));

    reset_rem_info(icetrans);
}


/*
 * Stop/destroy ICE session, invoked from the menu.
 */
void natclient_stop_session(struct ice_trans_s* icetrans)
{
    pj_status_t status;

    if (icetrans->icest == NULL) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
        return;
    }

    if (!pj_ice_strans_has_sess(icetrans->icest)) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE session, initialize first"));
        return;
    }

    status = pj_ice_strans_stop_ice(icetrans->icest);
    if (status != PJ_SUCCESS)
        natclient_perror("error stopping session", status);
    else
        PJ_LOG(3,(THIS_FILE, "ICE session stopped"));

    reset_rem_info(icetrans);
}

#define PRINT(...)	    \
    printed = pj_ansi_snprintf(p, maxlen - (p-buffer),  \
    __VA_ARGS__); \
    if (printed <= 0 || printed >= (int)(maxlen - (p-buffer))) \
    return -PJ_ETOOSMALL; \
    p += printed


static int print_cand_to_xml(char buffer[], unsigned maxlen,
                      const pj_ice_sess_cand *cand)
{
    char ipaddr[PJ_INET6_ADDRSTRLEN];
    char *p = buffer;
    int printed;

    PRINT("<candidate><foundation>%.*s</foundation> <comp_id>%u</comp_id> <transport>UDP</transport> <prio>%u</prio> <ip>%s</ip> <port>%u</port>",
          (int)cand->foundation.slen,
          cand->foundation.ptr,
          (unsigned)cand->comp_id,
          cand->prio,
          pj_sockaddr_print(&cand->addr, ipaddr,
                            sizeof(ipaddr), 0),
          (unsigned)pj_sockaddr_get_port(&cand->addr));

    PRINT("<type>%s</type> </candidate> \n",
          pj_ice_get_cand_type_name(cand->type));

    if (p == buffer+maxlen)
        return -PJ_ETOOSMALL;

    *p = '\0';

    return (int)(p-buffer);
}



/* 
 * Encode ICE information in SDP.
 */

static int extract_sdp_to_xml(struct ice_trans_s* icetrans,char buffer[], unsigned maxlen, ice_option_t opt, char *usrid)
{
    char *p = buffer;
    unsigned comp;
    int printed;
    pj_str_t local_ufrag, local_pwd;
    pj_status_t status;

    //Me: add

    PRINT(" <registerPeer>");
    PRINT("<device> <uniqueId>%s</uniqueId> </device>", usrid);



    /* Write "dummy" SDP v=, o=, s=, and t= lines */
    // Me: comment
    //PRINT("v=0\no=- 3414953978 3414953978 IN IP4 localhost\ns=ice\nt=0 0\n");

    /* Get ufrag and pwd from current session */
    pj_ice_strans_get_ufrag_pwd(icetrans->icest, &local_ufrag, &local_pwd,
                                NULL, NULL);


    /* Write the a=ice-ufrag and a=ice-pwd attributes */
    // Me: comment
    //PRINT("a=ice-ufrag:%.*s\na=ice-pwd:%.*s\n",
    //      (int)local_ufrag.slen,
    //      local_ufrag.ptr,
    //      (int)local_pwd.slen,
    //      local_pwd.ptr);

    PRINT("<ufrag>%.*s</ufrag> <pwd>%.*s</pwd>",
          (int)local_ufrag.slen,
          local_ufrag.ptr,
          (int)local_pwd.slen,
          local_pwd.ptr);


    PRINT(" <candidateList>");

    /* Write each component */
    for (comp=0; comp<opt.comp_cnt; ++comp) {
        unsigned j, cand_cnt;
        pj_ice_sess_cand cand[PJ_ICE_ST_MAX_CAND];
        char ipaddr[PJ_INET6_ADDRSTRLEN];

        /* Get default candidate for the component */
        status = pj_ice_strans_get_def_cand(icetrans->icest, comp+1, &cand[0]);
        if (status != PJ_SUCCESS)
            return -status;

        /* Write the default address */
        if (comp==0) {
            /* For component 1, default address is in m= and c= lines */
            // Me: comment
            // PRINT("m=audio %d RTP/AVP 0\n"
            //      "c=IN IP4 %s\n",
            //      (int)pj_sockaddr_get_port(&cand[0].addr),
            //      pj_sockaddr_print(&cand[0].addr, ipaddr,
            //                        sizeof(ipaddr), 0));

            PRINT("<comp_1> <port>%d</port>"
                  "<ip>%s</ip> </comp_1>",
                  (int)pj_sockaddr_get_port(&cand[0].addr),
                  pj_sockaddr_print(&cand[0].addr, ipaddr,
                                    sizeof(ipaddr), 0));

        } else if (comp==1) {
            /* For component 2, default address is in a=rtcp line */
            //Me: comment
            //PRINT("a=rtcp:%d IN IP4 %s\n",
            //      (int)pj_sockaddr_get_port(&cand[0].addr),
            //      pj_sockaddr_print(&cand[0].addr, ipaddr,
            //                        sizeof(ipaddr), 0));
        } else {
            /* For other components, we'll just invent this.. */
            // Me: comment
            //PRINT("a=Xice-defcand:%d IN IP4 %s\n",
            //      (int)pj_sockaddr_get_port(&cand[0].addr),
            //      pj_sockaddr_print(&cand[0].addr, ipaddr,
            //                        sizeof(ipaddr), 0));
        }

        /* Enumerate all candidates for this component */
        cand_cnt = PJ_ARRAY_SIZE(cand);
        status = pj_ice_strans_enum_cands(icetrans->icest, comp+1,
                                          &cand_cnt, cand);
        if (status != PJ_SUCCESS)
            return -status;

        /* And encode the candidates as SDP */
        //const int buffer_xml_len = 2048;
        //char buffer_xml[buffer_xml_len];
        for (j=0; j<cand_cnt; ++j) {
            printed = print_cand_to_xml(p, maxlen - (unsigned)(p-buffer), &cand[j]);
            if (printed < 0)
                return -PJ_ETOOSMALL;
         //   printed = print_cand_to_xml(p, maxlen - (unsigned)(p-buffer), &cand[j]);
         //   if (printed < 0)
          //      return -PJ_ETOOSMALL;
            p += printed;
        }
    }
    PRINT("</candidateList>");

    PRINT(" </registerPeer>");

    if (p == buffer+maxlen)
        return -PJ_ETOOSMALL;

    *p = '\0';

    //printf("DEBUGGGGGG: %s \n", (p - buffer);
    return (int)(p - buffer);
}


/*
 * Show information contained in the ICE stream transport. This is
 * invoked from the menu.
 */


//FIXME: migrate to iceController
extern char gUrl[];
void get_and_register_SDP_to_cloud(struct ice_trans_s* icetrans, ice_option_t opt, char *usrid)
{
    static char buffer[2048];
    int len;


    if (icetrans->icest == NULL) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
        return;
    }

    PJ_LOG(4, (__FUNCTION__, "General info"));
    PJ_LOG(4, (__FUNCTION__,"---------------"));
    PJ_LOG(4, (__FUNCTION__,"Component count    : %d\n", opt.comp_cnt));
    PJ_LOG(4, (__FUNCTION__,"Status             : "));
    if (pj_ice_strans_sess_is_complete(icetrans->icest))
        puts("negotiation complete");
    else if (pj_ice_strans_sess_is_running(icetrans->icest))
        puts("negotiation is in progress");
    else if (pj_ice_strans_has_sess(icetrans->icest))
        puts("session ready");
    else
        puts("session not created");

    if (!pj_ice_strans_has_sess(icetrans->icest)) {
        puts("Create the session first to see more info");
        return;
    }

    printf("Negotiated comp_cnt: %d\n",
           pj_ice_strans_get_running_comp_cnt(icetrans->icest));
    printf("Role               : %s\n",
           pj_ice_strans_get_role(icetrans->icest)==PJ_ICE_SESS_ROLE_CONTROLLED ?
               "controlled" : "controlling");


    len = extract_sdp_to_xml(icetrans, buffer, 2048, opt, usrid);
    if (len < 0)
        err_exit("not enough buffer to show ICE status", -len, icetrans);

    // Register this local SDP to cloud
    char full_url[256];
    //printf("[DEBUG] %s, %d  \n", __FUNCTION__, __LINE__ );

    strcpy(full_url, gUrl); // plus URL
    strcpy(&full_url[strlen(full_url)], "/peer/registerPeer"); // plus API
    http_post_request(full_url, buffer);




    PJ_LOG(4, (__FUNCTION__,"Local SDP (paste this to remote host):\n"
           "--------------------------------------\n"
           "%s\n", buffer));
}


void natclient_connect_with_user(struct ice_trans_s* icetrans, const char *usr_id)
{
    char linebuf[80];
    unsigned media_cnt = 0;
    unsigned comp0_port = 0;
    char     comp0_addr[80];
    pj_bool_t done = PJ_FALSE;

    reset_rem_info(icetrans);

    comp0_addr[0] = '\0';

                int af, cnt;
                char foundation[32], transport[12], ipaddr[80], type[32];
                pj_str_t tmpaddr;
                int comp_id, prio, port;
                pj_ice_sess_cand *cand;
                pj_status_t status;

                char full_url[1024];
                char buff[5*1024];


                strcpy(full_url, gUrl); // plus URL
                sprintf(&full_url[strlen(full_url)], "/peer/getPeer/%s", usr_id); // plus API

                PJ_LOG(4, ("[Debug] URL: %s \n", full_url));

                http_get_request(full_url, &buff[0]);

                char *value;


                xmlNode *cur_node = NULL;


                xmlNode *a_node = xml_get_node_by_name(buff, "registerPeer");
                assert(a_node != NULL);

                //printf("DEBUG %s, %d \n", __FILE__, __LINE__);

                value = (char *)xml_xmlnode_get_content_by_name(a_node->children, "ufrag");
                strcpy(icetrans->rem.ufrag, value);
                free(value);

                value = (char *)xml_xmlnode_get_content_by_name(a_node->children, "pwd");
                strcpy(icetrans->rem.pwd, value);
                free(value);


                a_node = xml_get_node_by_name(buff, "candidateList");


                //printf("DEBUG %s, %d \n", __FILE__, __LINE__);

                for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
                   // printf("[DEBUG] %s \n", cur_node->name);

                    if (cur_node->type == XML_ELEMENT_NODE)
                    {
                        if (strcmp(cur_node->name, "comp_1") == 0)
                        {
                            value = (char *)xml_xmlnode_get_content_by_name(cur_node, "ip");
                            strcpy(comp0_addr, value);
                            free(value);

                            value = (char *)xml_xmlnode_get_content_by_name(cur_node, "port");
                            comp0_port = atoi(value);
                            free(value);


                        }else
                        {


                        value = (char *)xml_xmlnode_get_content_by_name(cur_node, "foundation");
                        strcpy(foundation, value);
                        free(value);

                        value = (char *)xml_xmlnode_get_content_by_name(cur_node, "comp_id");
                        comp_id = atoi(value);
                        free(value);

                        value = (char *)xml_xmlnode_get_content_by_name(cur_node, "transport");
                        strcpy(transport, value);
                        free(value);

                        value = (char *)xml_xmlnode_get_content_by_name(cur_node, "prio");
                        prio = atoi(value);
                        free(value);


                        value = (char *)xml_xmlnode_get_content_by_name(cur_node, "ip");
                        strcpy(ipaddr, value);
                        //if (cur_node == a_node->children)
                        //    strcpy(comp0_addr, value);
                        free(value);

                        value = (char *)xml_xmlnode_get_content_by_name(cur_node, "port");
                        port = atoi(value);
                        if (cur_node == a_node->children)
                            comp0_port = atoi(value);
                        free(value);

                        value = (char *)xml_xmlnode_get_content_by_name(cur_node, "type");
                        strcpy(type, value);
                        free(value);

                        PJ_LOG(4,(__FUNCTION__, "DEBUG %s %d %s %d %s %d typ %s",
                               foundation,
                               comp_id,
                               transport,
                               prio,
                               ipaddr,
                               port,
                               type));


                        cand = &icetrans->rem.cand[icetrans->rem.cand_cnt];
                        pj_bzero(cand, sizeof(*cand));

                        if (strcmp(type, "host")==0)
                            cand->type = PJ_ICE_CAND_TYPE_HOST;
                        else if (strcmp(type, "srflx")==0)
                            cand->type = PJ_ICE_CAND_TYPE_SRFLX;
                        else if (strcmp(type, "relay")==0)
                            cand->type = PJ_ICE_CAND_TYPE_RELAYED;
                        else {
                            PJ_LOG(1, (THIS_FILE, "Error: invalid candidate type '%s'",
                                       type));
                            goto on_error;
                        }

                        cand->comp_id = (pj_uint8_t)comp_id;
                        pj_strdup2(icetrans->pool, &cand->foundation, foundation);
                        cand->prio = prio;

                        if (strchr(ipaddr, ':'))
                            af = pj_AF_INET6();
                        else
                            af = pj_AF_INET();

                        tmpaddr = pj_str(ipaddr);
                        pj_sockaddr_init(af, &cand->addr, NULL, 0);
                        status = pj_sockaddr_set_str_addr(af, &cand->addr, &tmpaddr);
                        if (status != PJ_SUCCESS) {
                            PJ_LOG(1,(THIS_FILE, "Error: invalid IP address '%s'",
                                      ipaddr));
                            goto on_error;
                        }

                        pj_sockaddr_set_port(&cand->addr, (pj_uint16_t)port);

                        ++icetrans->rem.cand_cnt;

                        if (cand->comp_id > icetrans->rem.comp_cnt)
                            icetrans->rem.comp_cnt = cand->comp_id;
                    }
                }
                }


    if (icetrans->rem.cand_cnt==0 ||
            icetrans->rem.ufrag[0]==0 ||
            icetrans->rem.pwd[0]==0 ||
            icetrans->rem.comp_cnt == 0)
    {
        PJ_LOG(1, (THIS_FILE, "Error: not enough info"));
        goto on_error;
    }

    if (comp0_port==0 || comp0_addr[0]=='\0') {
        PJ_LOG(1, (THIS_FILE, "Error: default address for component 0 not found"));
        goto on_error;
    } else {
        int af;
        pj_str_t tmp_addr;
        pj_status_t status;

        if (strchr(comp0_addr, ':'))
            af = pj_AF_INET6();
        else
            af = pj_AF_INET();

        pj_sockaddr_init(af, &icetrans->rem.def_addr[0], NULL, 0);
        tmp_addr = pj_str(comp0_addr);
        status = pj_sockaddr_set_str_addr(af, &icetrans->rem.def_addr[0],
                                          &tmp_addr);
        if (status != PJ_SUCCESS) {
            PJ_LOG(1,(THIS_FILE, "Invalid IP address in c= line"));
            goto on_error;
        }
        pj_sockaddr_set_port(&icetrans->rem.def_addr[0], (pj_uint16_t)comp0_port);
    }

    PJ_LOG(3, (THIS_FILE, "Done, %d remote candidate(s) added",
               icetrans->rem.cand_cnt));

    return;

on_error:
    reset_rem_info(icetrans);
}


/*
 * Start ICE negotiation! This function is invoked from the menu.
 */
void natclient_start_nego(struct ice_trans_s* icetrans)
{
    pj_str_t rufrag, rpwd;
    pj_status_t status;

    if (icetrans->icest == NULL) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
        return;
    }

    if (!pj_ice_strans_has_sess(icetrans->icest)) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE session, initialize first"));
        return;
    }

    if (icetrans->rem.cand_cnt == 0) {
        PJ_LOG(1,(THIS_FILE, "Error: No remote info, input remote info first"));
        return;
    }

    PJ_LOG(3,(THIS_FILE, "Starting ICE negotiation.."));

    status = pj_ice_strans_start_ice(icetrans->icest,
                                     pj_cstr(&rufrag, icetrans->rem.ufrag),
                                     pj_cstr(&rpwd, icetrans->rem.pwd),
                                     icetrans->rem.cand_cnt,
                                     icetrans->rem.cand);
    if (status != PJ_SUCCESS)
        natclient_perror("Error starting ICE", status);
    else
        PJ_LOG(3,(THIS_FILE, "ICE negotiation started"));
}


/*
 * Send application data to remote agent.
 */
void natclient_send_data(struct ice_trans_s* icetrans, unsigned comp_id, const char *data)
{
    pj_status_t status;

    if (icetrans->icest == NULL) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
        return;
    }

    if (!pj_ice_strans_has_sess(icetrans->icest)) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE session, initialize first"));
        return;
    }

    /*
    if (!pj_ice_strans_sess_is_complete(icetrans->icest)) {
    PJ_LOG(1,(THIS_FILE, "Error: ICE negotiation has not been started or is in progress"));
    return;
    }
    */

    if (comp_id<1||comp_id>pj_ice_strans_get_running_comp_cnt(icetrans->icest)) {
        PJ_LOG(1,(THIS_FILE, "Error: invalid component ID"));
        return;
    }

    status = pj_ice_strans_sendto(icetrans->icest, comp_id, data, strlen(data),
                                  &icetrans->rem.def_addr[comp_id-1],
                                  pj_sockaddr_get_len(&icetrans->rem.def_addr[comp_id-1]));
    if (status != PJ_SUCCESS)
        natclient_perror("Error sending data", status);
    else
        PJ_LOG(3,(THIS_FILE, "Data sent"));
}

