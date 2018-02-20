#!/bin/bash
# scpipt to go back to the pink-ap mode with raspap
#
# gpio 21 is the input from the run/stop button


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
         # shutdown 
                 echo "########################"
                 echo "# SWITCHING TO AP MODE #"
                 echo "########################" 
               #  uncomment the next line if you want to log when switching occurs
	       #  echo "switching to AP mode" >> /home/pi/pink/src_raffi/workmode.log 
                 WPA_PID=$(ps -ef | grep wpa_supplicant | grep -v grep | cut -c10-14)
		 sudo kill $WPA_PID
		 sudo /etc/init.d/hostapd stop
                 sudo dhclient -r wlan0 
                 sudo ip route flush table main  
                 sudo /etc/init.d/pink stop 
                 sudo ip route flush table main
         # startup  
                 sleep 1
                 /etc/init.d/hostapd start
		 sleep 1
                 sudo iwlist wlan0 scan 
		 sudo iwlist wlan0 scan
		 ionice -c 1 -n 0 nice -n -20 /etc/init.d/pink start 
                 amixer set PCM 100%
		 counter=1
	       #  uncomment the next line if you want to log when switching occurs 
               #  echo "switched to AP mode" >> /home/pi/pink/src_raffi/workmode.log
              fi 
        else
              counter=1 
        fi
        sleep 0.7
done    

