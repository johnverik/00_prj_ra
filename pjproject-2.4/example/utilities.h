#ifndef _UTILITIES_H_
#define _UTILITIES_H_

void hexDump (char *desc, void *addr, int len);
int is_valid_int(const char *str);
char *fgets_wrapper(char *s, int size, FILE *stream);


#endif 
