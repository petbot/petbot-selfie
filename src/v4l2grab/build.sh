gcc -DHAVE_CONFIG_H -I.     -g -O2 -MT v4l2grab.o -MD -MP -MF v4l2grab.Tpo -c -o v4l2grab.o v4l2grab.c
gcc -DHAVE_CONFIG_H -I.     -g -O2 -MT yuv.o -MD -MP -MF yuv.Tpo -c -o yuv.o yuv.c
gcc  -g -O2   -o v4l2grab v4l2grab.o yuv.o  -lv4l2 -ljpeg -Wl,-rpath,/usr/local/lib
