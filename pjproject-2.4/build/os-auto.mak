# build/os-auto.mak.  Generated from os-auto.mak.in by configure.

export OS_CFLAGS   := $(CC_DEF)PJ_AUTOCONF=1  -fPIC -DPJ_IS_BIG_ENDIAN=0 -DPJ_IS_LITTLE_ENDIAN=1 -DOPENSSL_NO_SSL2=1

export OS_CXXFLAGS := $(CC_DEF)PJ_AUTOCONF=1  -fPIC 

export OS_LDFLAGS  :=  -lssl -lcrypto -ldl -lz -luuid -lm -lrt -lpthread  -lasound  

export OS_SOURCES  := 


