#!/usr/bin/python

import subprocess
import sys
import os
import threading
import atexit
import signal
from time import sleep 
if len(sys.argv)!=3:
	print "%s selfie_trigger trigger_value" % sys.argv[0]
	sys.exit(1)


selfie_trigger=sys.argv[1]
selfie_clip="/dev/shm/petselfie.mov"

try:
	os.unlink(selfie_clip)
except:
	pass


def sigterm_handler(_signo, _stack_frame):
	try:
		capture.run=False
		capture.p.kill()
	except:
		pass
	print "HANDLE!"
	sys.exit(1)

def deviceID():
	cpu_file = open('/proc/cpuinfo','r')
	for line in cpu_file:
		info = line.split(':')
		if info and  info[0].strip() == 'Serial':
			return info[1].strip()
	return "None"


def capture():
	fps=30
	width=640
	height=480
	bitrate=500000
	seconds=11
	fn=selfie_clip
	max_tries=5
	tries=0
	while capture.run and (os.path.isfile(fn)==False or os.stat(fn).st_size<20) and tries<max_tries:
		cmd ="/usr/bin/gst-launch-1.0 v4l2src do-timestamp=true num-buffers=%d io-mode=1 ! queue ! videorate ! video/x-raw, width=%d, height=%d, framerate=%d/1 ! gdkpixbufoverlay location=/home/pi/petbot-selfie/scripts/petbot_video_watermark.png offset-x=180 offset-y=180 ! omxh264enc target-bitrate=%d control-rate=variable ! h264parse ! qtmux dts-method=asc presentation-time=1 ! filesink location=%s" % (seconds*fps,width,height,fps,bitrate,fn)
		capture.p = subprocess.Popen(cmd.split())
		capture.p.wait()
		tries+=1
	if capture.run==False or tries==max_tries:
		#failed
		pass
	else:
		#lets send the clip!
		upload_p=subprocess.Popen(["/usr/bin/curl", "-i", "-X", "POST", "-F" ,"selfie=@%s" % (fn), "https://petbot.ca/petselfie/%s" % deviceID() ])
		upload_p.wait()

capture.run=True

def cleanup():
	capture.run=False
	try:
		capture.p.kill()
	except:
		pass
	print "AT EXIT!"


def sound_and_treat():
	#wait while not recording
	fn=selfie_clip
	while (os.path.isfile(fn)==False or os.stat(fn).st_size<20) and capture.run:
		sleep(0.1)
	if not capture.run:
		return
	#play a sound
	sound_p=subprocess.Popen(["/bin/sh", "/home/pi/petbot/play_sound_local.sh", "/home/pi/petbot-selfie/mpu.mp3"])
	sound_p.wait()
	#drop a treat
	treat_p=subprocess.Popen(["/usr/bin/sudo","/home/pi/petbot/single_cookie/single_cookie","10"])
	treat_p.wait()
	
atexit.register(cleanup)

signal.signal(signal.SIGTERM, sigterm_handler)

#start capture thread that exits with us
t=threading.Thread(target=sound_and_treat)
t.daemon=True
t.start()

capture()



