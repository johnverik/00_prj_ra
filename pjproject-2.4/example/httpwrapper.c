#include <stdio.h>
#include <string.h>
#include "curl/curl.h"

#include "utilities.h"




struct BufferData {
    size_t size;
    char* data;
};



static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct BufferData *pooh = (struct BufferData *)userp;

//    printf("Debug size: %d, nmemb: %d \n", size, nmemb);

    if(size*nmemb < 1)
        return 0;

    int read = 0;
    if (pooh->size > size*nmemb)
        read = size*nmemb;
    else
        read = pooh->size;
  //  printf("Debug read: %d \n", read);

    if(read) {

        // *(char *)ptr = pooh->readptr[0]; /* copy one single byte */
        strncpy(ptr, pooh->data, read);
        pooh->data += read;                 /* advance pointer */
        pooh->size = pooh->size - read;                /* less data left */
        return read;                        /* we return 1 byte at a time! */
    }

 //   printf("Debug read  aaaa: %s \n", (char *)ptr);

    return 0;                          /* no more data left to deliver */
}






static size_t write_data(void *ptr, size_t size, size_t nmemb, struct BufferData *data)
{
    size_t index = data->size;
    size_t n = (size * nmemb);
    char* tmp;

    data->size += (size * nmemb);

    tmp = realloc(data->data, data->size + 1); /* +1 for '\0' */

    if(tmp) {
        data->data = tmp;
    } else {
        if(data->data) {
            free(data->data);
        }
        fprintf(stderr, "Failed to allocate memory.\n");
        return 0;
    }

    memcpy((data->data + index), ptr, n);
    data->data[data->size] = '\0';

    return size * nmemb;
}


int http_get_request(char *url, char *buff)
{
    CURL *curl;
    CURLcode res;

    struct BufferData data;
    data.size = 0;
    data.data = malloc(1024*1024); /* reasonable size initial buffer */
    if(NULL == data.data) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return NULL;
    }

    data.data[0] = '\0';

    //hexDump(NULL, url, strlen(url));

    //printf("[DEBUG] curl: %s \n", url);

    curl = curl_easy_init();
    if(curl) {
        //printf("[DEBUG] %s, %d \n", __FILE__, __LINE__);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        /* example.com is redirected, so we tell libcurl to follow redirection */
        //curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        //printf("[DEBUG] %s, %d \n", __FILE__, __LINE__);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

        //printf("[DEBUG] %s, %d \n", __FILE__, __LINE__);
        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);
        //printf("[DEBUG] %s, %d \n", __FILE__, __LINE__);
        /* Check for errors */
        if(res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));

        //printf("[DEBUG] %s, %d \n", __FILE__, __LINE__);

        /* always cleanup */
        curl_easy_cleanup(curl);
        //printf("[DEBUG] %s, %d \n", __FILE__, __LINE__);

        //printf("[Received data: %s \n", data.data);
    }

    strcpy(buff, data.data);
    free(data.data);

    return 0;
}


int http_post_request(char *url, char *str)
{
    CURL *curl;
    CURLcode res;

    struct BufferData pooh;

    pooh.data = str;
    pooh.size = (long)strlen(str);

    /* In windows, this will init the winsock stuff */
    res = curl_global_init(CURL_GLOBAL_DEFAULT);
    /* Check for errors */
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_global_init() failed: %s\n",
                curl_easy_strerror(res));
        return 1;
    }

    /* get a curl handle */
    curl = curl_easy_init();
    if(curl) {
        /* First set the URL that is about to receive our POST. */
        curl_easy_setopt(curl, CURLOPT_URL, url);

        /* Now specify we want to POST data */
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION , CURL_HTTP_VERSION_1_0 );

        /* we want to use our own read function */
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);

        /* pointer to pass to our read function */
        curl_easy_setopt(curl, CURLOPT_READDATA, &pooh);

        /* get verbose debug output please */
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        /*
      If you use POST to a HTTP 1.1 server, you can send data without knowing
      the size before starting the POST if you use chunked encoding. You
      enable this by adding a header like "Transfer-Encoding: chunked" with
      CURLOPT_HTTPHEADER. With HTTP 1.0 or without chunked transfer, you must
      specify the size in the request.
    */
#ifdef USE_CHUNKED
        {
            struct curl_slist *chunk = NULL;

            chunk = curl_slist_append(chunk, "Transfer-Encoding: chunked");
            res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
            /* use curl_slist_free_all() after the *perform() call to free this
         list again */
        }
#else
        /* Set the expected POST size. If you want to POST large amounts of data,
       consider CURLOPT_POSTFIELDSIZE_LARGE */
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, pooh.size);
#endif

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);
        /* Check for errors */
        if(res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return 0;
}

#if 0 
int main(int argc, char *argv[])
{

    // register a peer
    char post_str[] = "<?xml version=\"1.0\"?> \
            <registerPeer> \
            <device> \
            <uniqueId>device1</uniqueId> \
            </device> \
            <candidateList> \
            <candidate> \
            <port> 23456 </port> \
            <address> 192.168.1.1 </address> \
            </candidate> \
            <candidate> \
            <port> 23457 </port> \
            <address> 192.168.1.2 </address> \
            </candidate> \
            </candidateList> \
            </registerPeer>";

            char url[] = "http://115.77.49.188:5001/peer/registerPeer";

    // example of http POST request
    http_post_request(url, post_str);

    // example of http GET request
    char url_get[] = "http://115.77.49.188:5001/peer/getPeer/device1";

    char *buff;
    http_get_request(url_get, &buff);
    printf("Debug the received GET \n: %s \n", buff);


}

#endif
