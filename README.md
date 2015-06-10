# 00_prj_ra

1. Introduction 

2. Build
$./myconfig.sh
$./make dep && make 
Will produce verikdemo at ./pjsip-apps/bin/samples/x86_64-unknown-linux-gnu/verikdemo

3. usage 
./verikdemo -s stun.l.google.com:19302 -S 116.100.11.109 -P 12345 -U usr4


[Todo]


1) Can send with more than 1 user simoutaniously. 

2) Integrate with Venus module 

3) Develop an app on Windows to control the device

4) Pretty and friendly command-line
+ Support config file. The program will set hard-code default value for options. If these options are available in configure file, it will override options. If these options are available from command-line, it should override options from config file
+ 
