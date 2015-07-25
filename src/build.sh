gcc -o atos atos.c  -L/usr/lib -ljpcnn -lpthread -Wall -O3 -lrt -fopenmp
gcc -o train train.c  -L/usr/lib -ljpcnn -lpthread -Wall -O3 -fopenmp
gcc -o load load.c  -L/usr/lib -ljpcnn -lpthread -Wall -O3 -fopenmp
cd v4l2grab
sh build.sh
cd ..
