#!/bin/bash
# scpipt to go back to the pink-ap mode with raspap
#
# gpio 21 is the input from the run/stop button

#  sequence description
#######################
#  1.	pink stop
#  2.   networking stop
#  3.   check wpa_supplicant PID and kill it
#  4.   release IP from dev wlan0
#  5.   flush routing table
#  6.   shutdown wlan0
#  7.   start wlan0
#  8.   start networking
#  9.   stop hostapd
# 10.   start hostapd
# 11.   start pink
#######################



setup ()
{
  echo Setup
  counter=1
#  gpio mode 21 in ;
}
setup
while :
do
        # read the run/stop button state  
        result=`gpio read 21` 
        if [ $result -eq 0 ]; then
         echo $counter
	 ((counter++))	 
              if [ $counter -gt 7 ]; then
                 /etc/init.d/pink stop
                 /etc/init.d/networking stop
                 WPA_PID=$(ps -ef | grep wpa_supplicant | grep -v grep | cut -c10-14)
		 kill $WPA_PID
		 dhclient -r wlan0 
                 ip route flush table main  
                 ifconfig wlan0 down
                 ifconfig wlan0 up
                 /etc/init.d/networking start 
#                 dhclient -r wlan0
                 /etc/init.d/hostapd stop
                 sleep 1
                 /etc/init.d/hostapd start
		 sleep 1
                 /etc/init.d/pink start 
                 counter=1 
              fi 
        else
              counter=1 
        fi
        sleep 0.7
done    

