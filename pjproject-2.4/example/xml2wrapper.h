#ifndef _XML2WRAPPER_
#define  _XML2WRAPPER_

#include <stdio.h>
#include "libxml/parser.h"
#include "libxml/tree.h"

char *xml_xmlnode_get_content_by_name(xmlNode *node, char *xml_e);
char *xml_get_content_by_name(char *xmlbuff, char *name);
xmlNode* xml_get_node_by_name(char *xmlbuff, char *name);


#endif
