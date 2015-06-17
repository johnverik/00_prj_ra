APP=iceController
APP2 = xml2test

CFLAGS += -L../pjlib/lib 
CFLAGS += -L../pjlib-util/lib 
CFLAGS += -L../pjnath/lib 
CFLAGS += -L../pjmedia/lib 
CFLAGS += -L../pjsip/lib 
CFLAGS += -L../third_party/lib            

CFLAGS += -lpjsua-x86_64-unknown-linux-gnu -lpjsip-ua-x86_64-unknown-linux-gnu -lpjsip-simple-x86_64-unknown-linux-gnu -lpjsip-x86_64-unknown-linux-gnu -lpjmedia-codec-x86_64-unknown-linux-gnu -lpjmedia-x86_64-unknown-linux-gnu -lpjmedia-videodev-x86_64-unknown-linux-gnu -lpjmedia-audiodev-x86_64-unknown-linux-gnu -lpjmedia-x86_64-unknown-linux-gnu -lpjnath-x86_64-unknown-linux-gnu -lpjlib-util-x86_64-unknown-linux-gnu  -lsrtp-x86_64-unknown-linux-gnu -lresample-x86_64-unknown-linux-gnu -lgsmcodec-x86_64-unknown-linux-gnu -lspeex-x86_64-unknown-linux-gnu -lilbccodec-x86_64-unknown-linux-gnu -lg7221codec-x86_64-unknown-linux-gnu -lportaudio-x86_64-unknown-linux-gnu  -lpj-x86_64-unknown-linux-gnu -lssl -lcrypto -ldl -lz -luuid -lm -lrt -lpthread  -lasound


#include 
CFLAGS += -I../pjlib/include/ -I../pjlib-util/include  -I../pjnath/include
CFLAGS += -I./include

#dynamic link 
CFLAGS += -L./ -lcurl 
CFLAGS += -L./ -lxml2 
CFLAGS += -Wl,-rpath=./ 

#debug
CFLAGS += -g

SRCS =  verikdemo.c 
SRCS +=  httpwrapper.c 
#SRCS +=  icecontroller.c 
SRCS +=  getline.c 
SRCS +=  xml2wrapper.c 

all: 
	gcc -o $(APP) $(SRCS)  $(CFLAGS)  
	gcc -o $(APP2) -DBUILD_XMLWRAPPER xml2wrapper.c $(CFLAGS)  
	

clean:
	rm -f $(APP)
