#ifndef _ICE_WRAPPER_H_
#define _ICE_WRAPPER_H_

#include "pj/types.h"

#define THIS_FILE   "icewrapper.c"

/* For this demo app, configure longer STUN keep-alive time
 * so that it does't clutter the screen output.
 */
#define KA_INTERVAL 300

#define MULTIPLE 1


#define MAX_ICE_TRANS 3


typedef struct ice_trans_s{

    char name[256];
    enum {
        ICE_TRAN_STATE_NO = 0,
        ICE_TRAN_STATE_CONNECTED,
    }state;


    /* Our global variables */
    pj_caching_pool	 cp;
    pj_pool_t		*pool;
    pj_thread_t		*thread;
    pj_bool_t		 thread_quit_flag;

    pj_ice_strans_cfg	 ice_cfg;
    pj_ice_strans	*icest;

    // TODO: should be an array so that it can support each ICE trans
    struct rem_info
    {
        char		 ufrag[80];
        char		 pwd[80];
        unsigned	 comp_cnt;
        pj_sockaddr	 def_addr[PJ_ICE_MAX_COMP];
        unsigned	 cand_cnt;
        pj_ice_sess_cand cand[PJ_ICE_ST_MAX_CAND];
    } rem;

    FILE		*log_fhnd;

} ice_trans_t;

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

    ice_trans_t ice_trans_list[MAX_ICE_TRANS];

} ;

#endif
