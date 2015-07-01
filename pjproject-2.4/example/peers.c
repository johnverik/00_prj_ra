#include "peers.h"
#include "httpwrapper.h"
#include "xml2wrapper.h"

extern char gUrl[];

int get_peers_from_network(char *network_id,  peer_list_t *peer_list)
{
	char full_url[1000];
	char buff[1024];
	strcpy(full_url, gUrl);
 	sprintf(&full_url[strlen(full_url)], "/device/getDevicesFromNetwork/%s", network_id); // plus API	
	int rc = http_get_request(full_url, buff);

    peer_list->count = 0;

	xmlNode *a_node =  xml_get_node_by_name(buff, "DeviceList");
    xmlNode *cur_node;
    char *value;

	for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) 
	{
        if (cur_node->type == XML_ELEMENT_NODE)
        {
            value = (char *)xml_xmlnode_get_content_by_name(cur_node, "Device");
            strcpy(peer_list->peer[peer_list->count].name, value);
            free(value);

        }

	}

    return peer_list->count;
} 
