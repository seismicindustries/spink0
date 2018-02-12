#!/bin/bash
# script to enter client mode with the credentials in the wpa_supplicant.conf file
# raffi 23.11.2017 
#
# gpio 21 is from the button
# gpio 22 is from the encoder

# sequence description
#
#  1.  stop pink
#  2.  kill wpa_supplicant via PID
#  3.  flush routing table
#  4.  stop hostapd
#  5.  stop networking
#  6.  release dhcp info wlan0
#  7.  shut wlan0
#  8.  start wlan0
#  9.  start networking
# 10.  start wpa_supplicant
# 11.  wait
# 12.  dhcp release/renew


setup ()
{
  echo Setup
  counter=1
#  gpio mode 21 in ;
}
setup
while :
do
        # read the encoder button 
        result2=`gpio read 22`
        if [ $result2 -eq 0 ]; then
         echo $counter
	 ((counter++))	 
              if [ $counter -gt 7 ]; then
                 /etc/init.d/pink stop
	         WPA_PID=$(ps -ef | grep wpa_supplicant | grep -v grep | cut -c10-14)
                 sudo kill $WPA_PID 
                 sleep 1
                 sudo ip route flush table main
                 /etc/init.d/hostapd stop
#		 /etc/init.d/dnsmasq stop
		 /etc/init.d/networking stop 
                 dhclient -r wlan0
                 sudo ifconfig wlan0 down
                 sudo ifconfig wlan0 up
                 /etc/init.d/networking start
                 sudo wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf
                 sleep 5
                 sudo dhclient -r wlan0
                 sudo dhclient -v wlan0  
                 /etc/init.d/pink start 
                 counter=1 
              fi 
        else
              counter=1 
        fi
        sleep 0.7
done    

