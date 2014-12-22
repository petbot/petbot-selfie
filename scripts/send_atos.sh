#!/bin/bash
trap 'kill $(jobs -p)' SIGINT SIGTERM EXIT

selfie_clip=/dev/shm/petselfie.mov
selfie_trigger=$1
selfie_marked=/dev/shm/petbot_selfie.jpg

rm -f ${selfie_clip}
rm -f ${selfie_marked}

/home/pi/petbot-selfie/scripts/gst-record.sh 320x240 500000 30 ${selfie_clip} &
#/bin/sh /home/pi/petbot/play_sound.sh "https://petbot.ca/static/sounds/mpu.mp3"
sleep 0.5 
#sudo /home/pi/petbot/single_cookie/single_cookie 10
wait # wait for clip to finish recording
/usr/bin/composite -gravity southeast -quality 100 /home/pi/petbot-selfie/scripts/tiny_logo.png $1 ${selfie_marked}
deviceid=`cat /proc/cpuinfo  | tail -n 1 | awk '{print $NF}'`
curl -i -X POST -F selfie=@${selfie_clip} "https://petbot.ca/petselfie/$deviceid" 
