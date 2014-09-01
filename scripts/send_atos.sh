#!/bin/bash

selfie_clip=/dev/shm/petselfie.mov
selfie_trigger=$1
selfie_marked=/dev/shm/petbot_selfie.jpg

rm -f ${selfie_clip}
rm -f ${selfie_marked}

/bin/sh /home/pi/petbot/play_sound.sh "https://petbot.ca/static/sounds/mpu.mp3"
/home/pi/petbot-selfie/scripts/gst-record.sh 320x240 500000 10 ${selfie_clip} &
sleep 0.5 
sudo /home/pi/petbot/single_cookie/single_cookie 10
wait # wait for clip to finish recording
#export EMAIL=selfie@petbot.ca # recieving mail servers do not like this....
/usr/bin/composite -gravity southeast -quality 100 /home/pi/petbot-selfie/scripts/tiny_logo.png $1 ${selfie_marked}
/usr/bin/mutt -s "Atos - This is me? $2"  -a ${selfie_marked} -a ${selfie_clip} -- mouse9911@gmail.com < /dev/null

