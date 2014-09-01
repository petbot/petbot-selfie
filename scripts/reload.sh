#!/bin/bash

#this is tricky but needed..... 

unload_modules="uvcvideo videobuf2_core videobuf2_vmalloc videodev"
load_modules="videodev videobuf2_core videobuf2_vmalloc uvcvideo"

for module in $unload_modules; do
	sudo rmmod -f $module
done

for module in $load_modules; do
	sudo modprobe $module
done

