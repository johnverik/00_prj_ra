#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <getopt.h>

#include "icewrapper.h"



#include "httpwrapper.h"
#include "xml2wrapper.h"

struct app_t icedemo;



/* Utility to display error messages */
static void icedemo_perror(const char *title, pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(1,(THIS_FILE, "%s: %s", title, errmsg));
}

/* Utility: display error message and exit application (usually
 * because of fatal error.
 */
static void err_exit( const char *title, pj_status_t status)
{

    int i;
    for (i = 0; i < MAX_ICE_TRANS; i++)
    {
        struct ice_trans_s* icetrans = &icedemo.ice_trans_list[i];

        if (status != PJ_SUCCESS) {
            icedemo_perror(title, status);
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

    }
    exit(status != PJ_SUCCESS);

}

#define CHECK(expr)	status=expr; \
    if (status!=PJ_SUCCESS) { \
    err_exit(#expr, status); \
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
static int icedemo_worker_thread(struct ice_trans_s* icetrans, void *unused)
{
    PJ_UNUSED_ARG(unused);

    while (!icetrans->thread_quit_flag) {
        handle_events(icetrans, 500, NULL);
    }

    return 0;
}

/*
 * This is the callback that is registered to the ICE stream transport to
 * receive notification about incoming data. By "data" it means application
 * data such as RTP/RTCP, and not packets that belong to ICE signaling (such
 * as STUN connectivity checks or TURN signaling).
 */


static void hexDump (char *desc, void *addr, int len) {
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;

    // Output description if given.
    if (desc != NULL)
        printf ("%s:\n", desc);

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printf ("  %s\n", buff);

            // Output the offset.
            printf ("MSGMSG  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf (" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    // And print the final ASCII bit.
    printf ("  %s\n", buff);
}

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

        // TODO: update the ICE transaction
        //icedemo.icest = NULL;
    }
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
// Note: this icedemo_init is called just one time

static pj_status_t icedemo_init(void)
{
    pj_status_t status;


    /* Initialize the libraries before anything else */
    CHECK( pj_init() );
    CHECK( pjlib_util_init() );
    CHECK( pjnath_init() );

    /* Must create pool factory, where memory allocations come from */
    int i;
    for (i = 0; i < MAX_ICE_TRANS; i++)
    {
        struct ice_trans_s* icetrans = &icedemo.ice_trans_list[i];

        if (icedemo.opt.log_file) {
            icetrans->log_fhnd = fopen(icedemo.opt.log_file, "a");
            pj_log_set_log_func(&log_func);
        }


        pj_caching_pool_init(&icetrans->cp, NULL, 0);

        /* Init our ICE settings with null values */
        pj_ice_strans_cfg_default(&icetrans->ice_cfg);

        icetrans->ice_cfg.stun_cfg.pf = &icetrans->cp.factory;

        /* Create application memory pool */
        icetrans->pool = pj_pool_create(&icetrans->cp.factory, "icedemo",
                                      512, 512, NULL);

        /* Create timer heap for timer stuff */
        CHECK( pj_timer_heap_create(icetrans->pool, 100,
                                    &icetrans->ice_cfg.stun_cfg.timer_heap) );

        /* and create ioqueue for network I/O stuff */
        CHECK( pj_ioqueue_create(icetrans->pool, 16,
                                 &icetrans->ice_cfg.stun_cfg.ioqueue) );

        /* something must poll the timer heap and ioqueue,
     * unless we're on Symbian where the timer heap and ioqueue run
     * on themselves.
     */
        CHECK( pj_thread_create(icetrans->pool, "icedemo", &icedemo_worker_thread,
                                NULL, 0, 0, &icetrans->thread) );

        icetrans->ice_cfg.af = pj_AF_INET();

        /* Create DNS resolver if nameserver is set */
        if (icedemo.opt.ns.slen) {
            CHECK( pj_dns_resolver_create(&icetrans->cp.factory,
                                          "resolver",
                                          0,
                                          icetrans->ice_cfg.stun_cfg.timer_heap,
                                          icetrans->ice_cfg.stun_cfg.ioqueue,
                                          &icetrans->ice_cfg.resolver) );

            CHECK( pj_dns_resolver_set_ns(icetrans->ice_cfg.resolver, 1,
                                          &icedemo.opt.ns, NULL) );
        }


        /* -= Start initializing ICE stream transport config =- */

        /* Maximum number of host candidates */
        if (icedemo.opt.max_host != -1)
            icetrans->ice_cfg.stun.max_host_cands = icedemo.opt.max_host;

        /* Nomination strategy */
        if (icedemo.opt.regular)
            icetrans->ice_cfg.opt.aggressive = PJ_FALSE;
        else
            icetrans->ice_cfg.opt.aggressive = PJ_TRUE;

        /* Configure STUN/srflx candidate resolution */
        if (icedemo.opt.stun_srv.slen) {
            char *pos;

            /* Command line option may contain port number */
            if ((pos=pj_strchr(&icedemo.opt.stun_srv, ':')) != NULL) {
                icetrans->ice_cfg.stun.server.ptr = icedemo.opt.stun_srv.ptr;
                icetrans->ice_cfg.stun.server.slen = (pos - icedemo.opt.stun_srv.ptr);

                icetrans->ice_cfg.stun.port = (pj_uint16_t)atoi(pos+1);
            } else {
                icetrans->ice_cfg.stun.server = icedemo.opt.stun_srv;
                icetrans->ice_cfg.stun.port = PJ_STUN_PORT;
            }

            /* For this demo app, configure longer STUN keep-alive time
     * so that it does't clutter the screen output.
     */
            icetrans->ice_cfg.stun.cfg.ka_interval = KA_INTERVAL;
        }

        /* Configure TURN candidate */
        if (icedemo.opt.turn_srv.slen) {
            char *pos;

            /* Command line option may contain port number */
            if ((pos=pj_strchr(&icedemo.opt.turn_srv, ':')) != NULL) {
                icetrans->ice_cfg.turn.server.ptr = icedemo.opt.turn_srv.ptr;
                icetrans->ice_cfg.turn.server.slen = (pos - icedemo.opt.turn_srv.ptr);

                icetrans->ice_cfg.turn.port = (pj_uint16_t)atoi(pos+1);
            } else {
                icetrans->ice_cfg.turn.server = icedemo.opt.turn_srv;
                icetrans->ice_cfg.turn.port = PJ_STUN_PORT;
            }

            /* TURN credential */
            icetrans->ice_cfg.turn.auth_cred.type = PJ_STUN_AUTH_CRED_STATIC;
            icetrans->ice_cfg.turn.auth_cred.data.static_cred.username = icedemo.opt.turn_username;
            icetrans->ice_cfg.turn.auth_cred.data.static_cred.data_type = PJ_STUN_PASSWD_PLAIN;
            icetrans->ice_cfg.turn.auth_cred.data.static_cred.data = icedemo.opt.turn_password;

            /* Connection type to TURN server */
            if (icedemo.opt.turn_tcp)
                icetrans->ice_cfg.turn.conn_type = PJ_TURN_TP_TCP;
            else
                icetrans->ice_cfg.turn.conn_type = PJ_TURN_TP_UDP;

            /* For this demo app, configure longer keep-alive time
     * so that it does't clutter the screen output.
     */
            icetrans->ice_cfg.turn.alloc_param.ka_interval = KA_INTERVAL;
        }
    }

    /* -= That's it for now, initialization is complete =- */
    return PJ_SUCCESS;
}


/*
 * Create ICE stream transport instance, invoked from the menu.
 */
static void icedemo_create_instance(struct ice_trans_s* icetrans)
{
    pj_ice_strans_cb icecb;
    pj_status_t status;

    if (icetrans->icest != NULL) {
        puts("ICE instance already created, destroy it first");
        return;
    }

    /* init the callback */
    pj_bzero(&icecb, sizeof(icecb));
    icecb.on_rx_data = cb_on_rx_data;
    icecb.on_ice_complete = cb_on_ice_complete;

    /* create the instance */
    // TODO: just wonder if the object name should be unique among ICE transation

    status = pj_ice_strans_create("icedemo",		    /* object name  */
                                  &icetrans->ice_cfg,	    /* settings	    */
                                  icedemo.opt.comp_cnt,	    /* comp_cnt	    */
                                  NULL,			    /* user data    */
                                  &icecb,			    /* callback	    */
                                  &icetrans->icest)		    /* instance ptr */
            ;
    if (status != PJ_SUCCESS)
        icedemo_perror("error creating ice", status);
    else
        PJ_LOG(3,(THIS_FILE, "ICE instance successfully created"));
}

/* Utility to nullify parsed remote info */
static void reset_rem_info(struct ice_trans_s* icetrans)
{
    pj_bzero(&icetrans->rem, sizeof(icetrans->rem));
}


/*
 * Destroy ICE stream transport instance, invoked from the menu.
 */
static void icedemo_destroy_instance(struct ice_trans_s* icetrans)
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
static void icedemo_init_session(struct ice_trans_s* icetrans, unsigned rolechar)
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
        icedemo_perror("error creating session", status);
    else
        PJ_LOG(3,(THIS_FILE, "ICE session created"));

    reset_rem_info(icetrans);
}


/*
 * Stop/destroy ICE session, invoked from the menu.
 */
static void icedemo_stop_session(struct ice_trans_s* icetrans)
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
        icedemo_perror("error stopping session", status);
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


/* Utility to create a=candidate SDP attribute */
static int print_cand(char buffer[], unsigned maxlen,
                      const pj_ice_sess_cand *cand)
{
    char ipaddr[PJ_INET6_ADDRSTRLEN];
    char *p = buffer;
    int printed;

    PRINT("a=candidate:%.*s %u UDP %u %s %u typ ",
          (int)cand->foundation.slen,
          cand->foundation.ptr,
          (unsigned)cand->comp_id,
          cand->prio,
          pj_sockaddr_print(&cand->addr, ipaddr,
                            sizeof(ipaddr), 0),
          (unsigned)pj_sockaddr_get_port(&cand->addr));

    PRINT("%s\n",
          pj_ice_get_cand_type_name(cand->type));

    if (p == buffer+maxlen)
        return -PJ_ETOOSMALL;

    *p = '\0';

    return (int)(p-buffer);
}

/* 
 * Encode ICE information in SDP.
 */
static int encode_session(struct ice_trans_s* icetrans,char buffer[], unsigned maxlen)
{
    char *p = buffer;
    unsigned comp;
    int printed;
    pj_str_t local_ufrag, local_pwd;
    pj_status_t status;

    /* Write "dummy" SDP v=, o=, s=, and t= lines */
    PRINT("v=0\no=- 3414953978 3414953978 IN IP4 localhost\ns=ice\nt=0 0\n");

    /* Get ufrag and pwd from current session */
    pj_ice_strans_get_ufrag_pwd(icetrans->icest, &local_ufrag, &local_pwd,
                                NULL, NULL);

    /* Write the a=ice-ufrag and a=ice-pwd attributes */
    PRINT("a=ice-ufrag:%.*s\na=ice-pwd:%.*s\n",
          (int)local_ufrag.slen,
          local_ufrag.ptr,
          (int)local_pwd.slen,
          local_pwd.ptr);

    /* Write each component */
    for (comp=0; comp<icedemo.opt.comp_cnt; ++comp) {
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
            PRINT("m=audio %d RTP/AVP 0\n"
                  "c=IN IP4 %s\n",
                  (int)pj_sockaddr_get_port(&cand[0].addr),
                  pj_sockaddr_print(&cand[0].addr, ipaddr,
                                    sizeof(ipaddr), 0));
        } else if (comp==1) {
            /* For component 2, default address is in a=rtcp line */
            PRINT("a=rtcp:%d IN IP4 %s\n",
                  (int)pj_sockaddr_get_port(&cand[0].addr),
                  pj_sockaddr_print(&cand[0].addr, ipaddr,
                                    sizeof(ipaddr), 0));
        } else {
            /* For other components, we'll just invent this.. */
            PRINT("a=Xice-defcand:%d IN IP4 %s\n",
                  (int)pj_sockaddr_get_port(&cand[0].addr),
                  pj_sockaddr_print(&cand[0].addr, ipaddr,
                                    sizeof(ipaddr), 0));
        }

        /* Enumerate all candidates for this component */
        cand_cnt = PJ_ARRAY_SIZE(cand);
        status = pj_ice_strans_enum_cands(icetrans->icest, comp+1,
                                          &cand_cnt, cand);
        if (status != PJ_SUCCESS)
            return -status;

        /* And encode the candidates as SDP */
        for (j=0; j<cand_cnt; ++j) {
            printed = print_cand(p, maxlen - (unsigned)(p-buffer), &cand[j]);
            if (printed < 0)
                return -PJ_ETOOSMALL;
            p += printed;
        }
    }

    if (p == buffer+maxlen)
        return -PJ_ETOOSMALL;

    *p = '\0';
    return (int)(p - buffer);
}


/*
 * Show information contained in the ICE stream transport. This is
 * invoked from the menu.
 */



static void icedemo_show_ice(struct ice_trans_s* icetrans)
{
    static char buffer[1000];
    int len;

    if (icetrans->icest == NULL) {
        PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
        return;
    }

    puts("General info");
    puts("---------------");
    printf("Component count    : %d\n", icedemo.opt.comp_cnt);
    printf("Status             : ");
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

    len = encode_session(icetrans, buffer, sizeof(buffer));
    if (len < 0)
        err_exit("not enough buffer to show ICE status", -len);


    strcpy(sdp, buffer);

    puts("");
    FILE* fd_sdp = fopen("sdp.txt", "w");
    printf("Local SDP (paste this to remote host):\n"
           "--------------------------------------\n"
           "%s\n", buffer);

    if (fd_sdp)
    {
        printf("[Me][Debug] buffer len: %d \n buffer: %s \n", strlen(buffer), buffer);
        fwrite(buffer, 1, strlen(buffer), fd_sdp);
        fflush(fd_sdp);
        fclose(fd_sdp);
    }




    puts("");
    puts("Remote info:\n"
         "----------------------");
    if (icetrans->rem.cand_cnt==0) {
        puts("No remote info yet");
    } else {
        unsigned i;

        printf("Remote ufrag       : %s\n", icetrans->rem.ufrag);
        printf("Remote password    : %s\n", icetrans->rem.pwd);
        printf("Remote cand. cnt.  : %d\n", icetrans->rem.cand_cnt);

        for (i=0; i<icetrans->rem.cand_cnt; ++i) {
            len = print_cand(buffer, sizeof(buffer), &icetrans->rem.cand[i]);
            if (len < 0)
                err_exit("not enough buffer to show ICE status", -len);

            printf("  %s", buffer);
        }
    }
}


// TODO:
// 1. Register the peer to cloud
// How to get peer information
// Extract IP address along with its port of local address, flxadd, turn address
// 2. Get peer from cloud

static void icedemo_input_remote2(struct ice_trans_s* icetrans, const char *usr_id)
{
    char linebuf[80];
    unsigned media_cnt = 0;
    unsigned comp0_port = 0;
    char     comp0_addr[80];
    pj_bool_t done = PJ_FALSE;

    puts("Paste SDP from remote host, end with empty line");

    reset_rem_info(icetrans);

    comp0_addr[0] = '\0';

    char file_path[256];
    sprintf(file_path, "peer.%s", usr_id);

    FILE *file = fopen(file_path, "r");

    while (!done) {
        pj_size_t len;
        char *line;


        if (fgets(linebuf, sizeof(linebuf), file)==NULL)
            break;

        if (strncmp(linebuf, "ACK:", 4) == 0)
            continue;

        len = strlen(linebuf);
        while (len && (linebuf[len-1] == '\r' || linebuf[len-1] == '\n'))
            linebuf[--len] = '\0';

        line = linebuf;
        while (len && pj_isspace(*line))
            ++line, --len;

        if (len==0)
            break;

        /* Ignore subsequent media descriptors */
        if (media_cnt > 1)
            continue;

        switch (line[0]) {
        case 'm':
        {
            int cnt;
            char media[32], portstr[32];

            ++media_cnt;
            if (media_cnt > 1) {
                puts("Media line ignored");
                break;
            }

            cnt = sscanf(line+2, "%s %s RTP/", media, portstr);
            if (cnt != 2) {
                PJ_LOG(1,(THIS_FILE, "Error parsing media line"));
                goto on_error;
            }

            comp0_port = atoi(portstr);

        }
            break;
        case 'c':
        {
            int cnt;
            char c[32], net[32], ip[80];

            cnt = sscanf(line+2, "%s %s %s", c, net, ip);
            if (cnt != 3) {
                PJ_LOG(1,(THIS_FILE, "Error parsing connection line"));
                goto on_error;
            }

            strcpy(comp0_addr, ip);
        }
            break;
        case 'a':
        {
            char *attr = strtok(line+2, ": \t\r\n");
            if (strcmp(attr, "ice-ufrag")==0) {
                strcpy(icetrans->rem.ufrag, attr+strlen(attr)+1);
            } else if (strcmp(attr, "ice-pwd")==0) {
                strcpy(icetrans->rem.pwd, attr+strlen(attr)+1);
            } else if (strcmp(attr, "rtcp")==0) {
                char *val = attr+strlen(attr)+1;
                int af, cnt;
                int port;
                char net[32], ip[64];
                pj_str_t tmp_addr;
                pj_status_t status;

                cnt = sscanf(val, "%d IN %s %s", &port, net, ip);
                if (cnt != 3) {
                    PJ_LOG(1,(THIS_FILE, "Error parsing rtcp attribute"));
                    goto on_error;
                }

                if (strchr(ip, ':'))
                    af = pj_AF_INET6();
                else
                    af = pj_AF_INET();

                pj_sockaddr_init(af, &icetrans->rem.def_addr[1], NULL, 0);
                tmp_addr = pj_str(ip);
                status = pj_sockaddr_set_str_addr(af, &icetrans->rem.def_addr[1],
                                                  &tmp_addr);
                if (status != PJ_SUCCESS) {
                    PJ_LOG(1,(THIS_FILE, "Invalid IP address"));
                    goto on_error;
                }
                pj_sockaddr_set_port(&icetrans->rem.def_addr[1], (pj_uint16_t)port);

            } else if (strcmp(attr, "candidate")==0) {
                char *sdpcand = attr+strlen(attr)+1;
                int af, cnt;
                char foundation[32], transport[12], ipaddr[80], type[32];
                pj_str_t tmpaddr;
                int comp_id, prio, port;
                pj_ice_sess_cand *cand;
                pj_status_t status;

                cnt = sscanf(sdpcand, "%s %d %s %d %s %d typ %s",
                             foundation,
                             &comp_id,
                             transport,
                             &prio,
                             ipaddr,
                             &port,
                             type);
                if (cnt != 7) {
                    PJ_LOG(1, (THIS_FILE, "error: Invalid ICE candidate line"));
                    goto on_error;
                }

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
            break;
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
    fclose(file);

    return;

on_error:
    reset_rem_info(icetrans);
}


/*
 * Start ICE negotiation! This function is invoked from the menu.
 */
static void icedemo_start_nego(struct ice_trans_s* icetrans)
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
        icedemo_perror("Error starting ICE", status);
    else
        PJ_LOG(3,(THIS_FILE, "ICE negotiation started"));
}


/*
 * Send application data to remote agent.
 */
static void icedemo_send_data(struct ice_trans_s* icetrans, unsigned comp_id, const char *data)
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
        icedemo_perror("Error sending data", status);
    else
        PJ_LOG(3,(THIS_FILE, "Data sent"));
}




/*
 * Display console menu
 */
static void icedemo_print_menu(void)
{

    puts("");
    puts("+----------------------------------------------------------------------+");
    puts("|                    M E N U                                           |");
    puts("+---+------------------------------------------------------------------+");
    puts("| l | list           List all user id                                |");
    puts("| s | start          start conversation with an userid                            |");
    puts("+---+------------------------------------------------------------------+");
    puts("| h |  help            * Help! *                                       |");
    puts("| q |  quit            Quit                                            |");
    puts("+----------------------------------------------------------------------+");

}


//// some functions related to singalling server 
static int peer_put_dsp(char *my_id, char *_dsp)
{
    int sockfd, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[1024];
    /* Create a socket point */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("ERROR opening socket");
        exit(1);
    }
    server = gethostbyname(host_name);

    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    /* Now connect to the server */
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        exit(1);
    }


    bzero(buffer,1024);
    sprintf(buffer, "PUT_%d_%s_%s\n", strlen(my_id) + strlen(_dsp) + 2, my_id, _dsp);
    //fgets(buffer,255,stdin);
    printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    /* Send message to the server */
    n = write(sockfd, buffer, strlen(buffer));

    printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    if (n < 0)
    {
        perror("ERROR writing to socket");
        exit(1);
    }
    printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    //usleep(50*1000*1000);

    /* Now read server response */
    bzero(buffer,1024);
    n = read(sockfd, buffer, 1024);
    printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    hexDump(NULL, buffer, n);


    close(sockfd);

}


static int peer_get_dsps()
{
    int sockfd, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    //printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    char buffer[1024];
    /* Create a socket point */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("ERROR opening socket");
        exit(1);
    }

    //printf("[Debug] %s, %d \n", __FILE__, __LINE__);
    server = gethostbyname(host_name);

    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);


    /* Now connect to the server */
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        exit(1);
    }



    char arg1[256];
    char arg2[256];

    strcpy(arg1, "all");
    strcpy(arg2, "none");


    bzero(buffer,1024);
    sprintf(buffer, "GETALL_%d_%s_%s\n", strlen(arg1) + strlen(arg2) + 2, arg1, arg2);
    n = write(sockfd, buffer, strlen(buffer));


    if (n < 0)
    {
        perror("ERROR writing to socket");
        exit(1);
    }

    //usleep(50*1000*1000);

    /* Now read server response */
    bzero(buffer,1024);
    n = read(sockfd, buffer, 1024);

    char usr[256];

    FILE *file = fopen("list_peer.txt", "w+");
    fwrite(buffer, 1, strlen(buffer), file);
    fflush(file);


    fseek(file, 0, SEEK_SET);

    while (fgets(usr, 256, file) != NULL)
    {
        if (strncmp(usr, "USER=", 5) == 0)
            printf("%s", usr);
    }

    if (file != NULL)
        fclose(file);

    close(sockfd);

}


static int peer_get_dsp(const char *usr_id, char *dsp)
{
    int sockfd, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    //printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    char buffer[1024];
    /* Create a socket point */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("ERROR opening socket");
        exit(1);
    }

    //printf("[Debug] %s, %d \n", __FILE__, __LINE__);
    server = gethostbyname(host_name);

    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);


    /* Now connect to the server */
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        exit(1);
    }


    bzero(buffer,1024);
    sprintf(buffer, "GET_%d_%s_%s\n", strlen(usr_id) + strlen("None") + 2, usr_id, "None");
    n = write(sockfd, buffer, strlen(buffer));


    if (n < 0)
    {
        perror("ERROR writing to socket");
        exit(1);
    }

    //usleep(50*1000*1000);

    /* Now read server response */
    bzero(buffer,1024);
    n = read(sockfd, buffer, 1024);

    char usr[256];

    char file_path[256];
    sprintf(file_path, "peer.%s", usr_id);

    FILE *file = fopen(file_path, "w+");
    fwrite(buffer, 1, strlen(buffer), file);
    fflush(file);

    fseek(file, 0, SEEK_SET);

    while (fgets(usr, 256, file) != NULL)
    {
        if (strncmp(usr, "ACK=", 5) != 0)
            strcpy(sdp, usr);
    }

    if (file != NULL)
        fclose(file);

    close(sockfd);
    return 0;
}


static struct ice_trans_s* get_ice_tran_from_name(char *name)
{
    int i;
    for (i = 0; i < MAX_ICE_TRANS; i++)
        if (strcmp(name, icedemo.ice_trans_list[i].name) == 0)
            return &icedemo.ice_trans_list[i];
    return NULL;
}


/*
 * Main console loop.
 */

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


static void icedemo_console(void)
{
    pj_bool_t app_quit = PJ_FALSE;

    // TODO: iterate
    int i;
    for (i = 0; i < MAX_ICE_TRANS; i++)
    {
        struct ice_trans_s* icetrans = &icedemo.ice_trans_list[i];

        icedemo_create_instance(icetrans);
        usleep(1*1000*1000);
        icedemo_init_session(icetrans, "o");

        usleep(1*1000*1000);
        icedemo_show_ice(icetrans);

    }

#if 1
    char cmd[256];
    memset(cmd, 0, 256);
    while (printf(">>>") && gets(&cmd[0]) != NULL)
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
#else


    peer_put_dsp(usrid, sdp);
    while (!app_quit) {
        char input[80], *cmd;
        const char *SEP = " \t\r\n";
        pj_size_t len;

        icedemo_print_menu();

        printf("Input: ");
        if (stdout) fflush(stdout);

        pj_bzero(input, sizeof(input));
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        len = strlen(input);
        while (len && (input[len-1]=='\r' || input[len-1]=='\n'))
            input[--len] = '\0';

        cmd = strtok(input, SEP);
        if (!cmd)
            continue;

        if (strcmp(cmd, "list") == 0 || strcmp(cmd, "l")==0){

            peer_get_dsps();

        }else if (strcmp(cmd, "start")==0 || strcmp(cmd, "s") == 0)
        {

            printf("Which user: ");
            char _usr_id[256];
            char _usr_sdp[1024];


            gets(_usr_id);

            peer_get_dsp(_usr_id, _usr_sdp);

            // ADD:
            struct ice_trans_s *ice_tran =  get_ice_tran_from_name(_usr_id);
            // start conversation with an user
            // TODO: Get SDP from cloud
            icedemo_input_remote2(ice_tran, _usr_id);
            icedemo_start_nego(ice_tran);

            char msg[256];
            do{
                printf("enter your message (quit to exit) ");
                gets(msg);
                if (strcmp(msg, "quit") == 0)
                    break;
                icedemo_send_data(ice_tran, 1, msg);

            } while(1);


            // start conversation

        }

    }
#endif

}


/*
 * Display program usage.
 */
static void icedemo_usage()
{
    puts("Usage: icedemo [optons]");
    printf("icedemo v%s by pjsip.org\n", pj_get_version());
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
    puts("");
}








/*
 * And here's the main()
 */


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



    strcpy(usrid, "userid");
    strcpy(host_name, "116.100.11.109");
    portno = 12345;
    memset(sdp, 0, 1024);


    pj_status_t status;

    icedemo.opt.comp_cnt = 1;
    icedemo.opt.max_host = -1;

    while((c=pj_getopt_long(argc,argv, "c:n:s:t:u:p:H:L:U:S:P:hTFR", long_options, &opt_id))!=-1) {
        switch (c) {
        case 'c':
            icedemo.opt.comp_cnt = atoi(pj_optarg);
            if (icedemo.opt.comp_cnt < 1 || icedemo.opt.comp_cnt >= PJ_ICE_MAX_COMP) {
                puts("Invalid component count value");
                return 1;
            }
            break;
        case 'n':
            icedemo.opt.ns = pj_str(pj_optarg);
            break;
        case 'H':
            icedemo.opt.max_host = atoi(pj_optarg);
            break;
        case 'h':
            icedemo_usage();
            return 0;
        case 's':
            printf("[Debug] %s, %d, option's value: %s \n", __FILE__, __LINE__, pj_optarg);
            icedemo.opt.stun_srv = pj_str(pj_optarg);
            break;
        case 't':
            icedemo.opt.turn_srv = pj_str(pj_optarg);
            break;
        case 'T':
            icedemo.opt.turn_tcp = PJ_TRUE;
            break;
        case 'u':
            icedemo.opt.turn_username = pj_str(pj_optarg);
            break;
        case 'p':
            icedemo.opt.turn_password = pj_str(pj_optarg);
            break;
        case 'F':
            icedemo.opt.turn_fingerprint = PJ_TRUE;
            break;
        case 'R':
            icedemo.opt.regular = PJ_TRUE;
            break;
        case 'L':
            icedemo.opt.log_file = pj_optarg;
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

    //printf("[Debug] %s, %d \n", __FILE__, __LINE__);

    status = icedemo_init();
    if (status != PJ_SUCCESS)
        return 1;

    icedemo_console();

    err_exit("Quitting..", PJ_SUCCESS);

    return 0;
}
