#!/bin/sh

./configure --enable-shared \
        --enable-static \
        --disable-thread \
        --enable-cookies \
        --enable-crypto-auth \
        --enable-nonblocking \
        --enable-file \
        --enable-ftp \
        --enable-http \
        --disable-ares \
        --disable-debug \
        --disable-dict \
        --disable-gopher \
        --disable-ldap \
        --disable-manual \
        --disable-sspi \
        --disable-telnet \
        --enable-tftp \
        --disable-verbose \
        --without-ca-bundle \
        --without-gnutls \
        --without-krb4 \
        --without-libidn \
        --without-nss \
        --without-libssh2 
