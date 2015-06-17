APP=verikdemo

CFLAGS += -L../pjlib/lib 
CFLAGS += -L../pjlib-util/lib 
CFLAGS += -L../pjnath/lib 
CFLAGS += -L../pjmedia/lib 
CFLAGS += -L../pjsip/lib 
CFLAGS += -L../third_party/lib            

CFLAGS += -lpjsua-x86_64-unknown-linux-gnu -lpjsip-ua-x86_64-unknown-linux-gnu -lpjsip-simple-x86_64-unknown-linux-gnu -lpjsip-x86_64-unknown-linux-gnu -lpjmedia-codec-x86_64-unknown-linux-gnu -lpjmedia-x86_64-unknown-linux-gnu -lpjmedia-videodev-x86_64-unknown-linux-gnu -lpjmedia-audiodev-x86_64-unknown-linux-gnu -lpjmedia-x86_64-unknown-linux-gnu -lpjnath-x86_64-unknown-linux-gnu -lpjlib-util-x86_64-unknown-linux-gnu  -lsrtp-x86_64-unknown-linux-gnu -lresample-x86_64-unknown-linux-gnu -lgsmcodec-x86_64-unknown-linux-gnu -lspeex-x86_64-unknown-linux-gnu -lilbccodec-x86_64-unknown-linux-gnu -lg7221codec-x86_64-unknown-linux-gnu -lportaudio-x86_64-unknown-linux-gnu  -lpj-x86_64-unknown-linux-gnu -lssl -lcrypto -ldl -lz -luuid -lm -lrt -lpthread  -lasound


CFLAGS += -I../pjlib/include/ -I../pjlib-util/include  -I../pjnath/include
CFLAGS += -L./ -lcurl 
CFLAGS += -Wl,-rpath=./ 

all: 
	gcc -o $(APP) verikdemo.c httpwrapper.c $(CFLAGS)  

clean:
	rm -f $(APP)
