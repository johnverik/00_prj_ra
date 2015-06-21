#include <stdio.h>
#include <string.h>

#include "xml2wrapper.h"




/*
 *To compile this file using gcc you can type
 *gcc `xml2-config --cflags --libs` -o xmlexample libxml2-example.c
 */


/**
 * processNode:
 * @reader: the xmlReader
 *
 * Dump information about the current node
 */


xmlNode*
_searching_element_by_name(xmlNode * a_node, char *tag_name)
{
    xmlNode *cur_node = NULL;
    //printf("DEBUG %s, %d \n", __FILE__, __LINE__);

    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
        //printf("DEBUG %s, %d \n", __FILE__, __LINE__);
        if (cur_node->type == XML_ELEMENT_NODE /*|| cur_node->type == XML_ATTRIBUTE_NODE */ )
        //if (1)
        {
            //printf("node type: Element, name: %s, type: %d tag_name: %s node name: %s \n", cur_node->name, cur_node->type, tag_name, cur_node->name);

            if (strcmp(cur_node->name, tag_name) == 0)
            {
          //      printf("DEBUG %s, %d \n", __FILE__, __LINE__);
                //printf("node type: Element, name: %s\n", cur_node->name);
                return cur_node;
            }
        }
        xmlNode *tmp_node;
        if ( (tmp_node = _searching_element_by_name(cur_node->children, tag_name)) != NULL)
            return tmp_node;
    }

    return NULL;
}




/*
 * get element content by name
 * xmlbuff: xml content
 * name: name of element
 * return element content
 * must free pointer which point to return value
 */
char *xml_get_content_by_name(char *xmlbuff, char *name) {

    xmlDocPtr xmldoc;



    xmldoc = xmlReadMemory(xmlbuff, (int) strlen(xmlbuff), NULL, NULL, XML_PARSE_PEDANTIC);

    if (xmldoc == NULL) {
        return NULL;
    }

    xmlNode *node = _searching_element_by_name(xmldoc->children, name);
    if (node == NULL)
          return NULL;

    char *xml_content = (char *)xmlNodeGetContent(node);

  //  print_element_names(xmldoc->children);

    xmlFreeDoc(xmldoc);
    return xml_content;

}


char *xml_xmlnode_get_content_by_name(xmlNode *xml_node, char *xml_e)
{
    xmlNode *node = _searching_element_by_name(xml_node, xml_e);
    if (node == NULL)
          return NULL;
    char *xml_content = (char *)xmlNodeGetContent(node);
    return xml_content;

}

// fixme: free memory
xmlNode* xml_get_node_by_name(char *xmlbuff, char *name)
{


    xmlDocPtr xmldoc;

    xmldoc = xmlReadMemory(xmlbuff, (int) strlen(xmlbuff), NULL, NULL, XML_PARSE_PEDANTIC);

    if (xmldoc == NULL) {
        return NULL;
    }

    return _searching_element_by_name(xmldoc->children, name);

}


#ifdef BUILD_XMLWRAPPER

/**
 * Simple example to parse a file called "file.xml",
 * walk down the DOM, and print the name of the
 * xml elements nodes.
 */
int
main(int argc, char **argv)
{
    char xml_str[] = "<?xml version=\"1.0\"?> <deviceRegister>  <device>    <uniqueId>device1</uniqueId>    <modelCode>Sensor</modelCode>        <firmwareVersion>firmware.01.pvt</firmwareVersion>  </device>  </deviceRegister>";


    char *value = (char *) xml_get_content_by_name(xml_str, "device");

    printf("[Debug] received value: %s  \n", value);
    free(value);

}

#endif


