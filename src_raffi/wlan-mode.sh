#!/bin/bash

# Switch spink WLAN configuration (AP Mode / Client Mode)

# GPIO 22 is from the button --> press starts AP Mode
# GPIO 21 is from the encoder ---> press starts Client mode

# Global variables
wlan_mode="none"

ap_mode(){
  echo "Starting $wlan_mode mode"

  WPA_PID=$(ps -ef | grep wpa_supplicant | grep -v grep | cut -c10-14)
  kill $WPA_PID
  /etc/init.d/hostapd stop
  sudo dhclient -4 -r wlan0
  ip route flush table main
  /etc/init.d/pink stop
  ip route flush table main

  sleep 1
  iwlist wlan0 scan
  sleep 1
  /etc/init.d/hostapd start
  sleep 1
#  iwlist wlan0 scan
#  sudo iw dev wlan0 scan ap-force
  wla_cli scan_results
  ionice -c 1 -n 0 nice -n 20 /etc/init.d/pink start
  amixer set PCM 100%
	
}

client_mode(){
  echo "Starting $wlan_mode mode"

  WPA_PID=$(ps -ef | grep wpa_supplicant | grep -v grep | cut -c10-14)
  kill $WPA_PID
  /etc/init.d/hostapd stop
  sudo dhclient -4 -r wlan0
  ip route flush table main
  /etc/init.d/pink stop
  ip route flush table main 

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
}

#switch_mode(){
#
# TODO:
# Merge ap_mode and client_mode into switch_mode only 
# to differnciate the startup (ap and client)
# currrently problem with conditional not working
#}

setup (){
  echo "Setup"
  counter=1
#  gpio mode 21 in ;
}

setup

# gpio 22 / AP Mode
# gpio 21 / Client Mode
while :
do
        # read the run/stop button state
        ap_mode_btn=$(gpio read 22)
        client_mode_btn=$(gpio read 21)

        if [ $ap_mode_btn -eq 0 ]; then
          echo $counter
	  ((counter++))
          if [ $counter -gt 7 ]; then
	    wlan_mode="ap"
            ap_mode
	    #switch_mode
            counter=1
          fi
        elif [ $client_mode_btn -eq 0 ]; then
            echo $counter
            ((counter++))
            if [ $counter -gt 7 ]; then
	      wlan_mode="client"
              client_mode
	      #switch_mode
              counter=1
            fi
        else
              counter=1
        fi
        sleep 0.7
done
