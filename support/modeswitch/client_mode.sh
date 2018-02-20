#!/bin/bash
# script to enter client mode with the credentials in the wpa_supplicant.conf file
# raffi 23.11.2017 
#
# gpio 21 is from the button
# gpio 22 is from the encoder


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
       # shutdown
                 echo "############################"
                 echo "# SWITCHING TO CLIENT MODE #"
                 echo "############################"
		#  uncomment next line if you want to log events
		#  echo "switching to CLIENT mode" >> /home/pi/pink/src_raffi/workmode.log
	         WPA_PID=$(ps -ef | grep wpa_supplicant | grep -v grep | cut -c10-14)
                 kill $WPA_PID 
                 sudo /etc/init.d/hostapd stop
                 sudo dhclient -4 -r wlan0
                 sudo ip route flush table main
                 sudo /etc/init.d/pink stop
      # start   
                 sleep 2
		 sudo wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf
                 sleep 5
                 ionice -c 1 -n 0 nice -n -20 /etc/init.d/pink start
                 sleep 1
                 sudo dhclient -4 -r wlan0
                 sleep 2
                 sudo dhclient -4 -v wlan0  
                 sleep 1
                 sudo dhclient -4 -v wlan0 
                 amixer set PCM 100%
		 counter=1 
               #  uncomment next line if you want to log events
	       #  echo "switched to CLIENT mode" >> /home/pi/pink/src_raffi/workmode.log
              fi 
        else
              counter=1 
        fi
        sleep 0.7
done    

