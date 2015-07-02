#include <stdio.h>
#include <stdlib.h>


#include "utilities.h"

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

void hexDump (char *desc, void *addr, int len) {
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
    printf ("  %s\n", buff);
}

char *fgets_wrapper(char *s, int size, FILE *stream)
{
    char *buff = fgets(s, size, stream);
    int len = 0;
_TRIM_:  // is to trim the new-line character.
    len = strlen(s);
    if (len > 0)
        if (s[len -1] == 0X0a || s[len - 1] == 0X10)
        {
            //printf("[DEBUG] %s, %d, len: %d  \n", __FILE__, __LINE__, len);
            s[len - 1] = NULL; // do not include the new-line character
            goto _TRIM_;
        }


    // hexDump(NULL, s, strlen(s));
    return buff;
}

