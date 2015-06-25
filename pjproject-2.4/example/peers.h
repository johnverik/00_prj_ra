#ifndef __PEERS__
#define __PEERS__


#define MAX_PEER 10

typedef struct peer_s {
	char name[256];	
}peer_t;

typedef struct peer_list_s{

	int count; 
	peer_t peer[MAX_PEER];
} peer_list_t; 

#endif
