/*
Copyright (c) 2014, Michael (Misko) Dzamba.
All rights reserved.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

//  Originally forked from main.c from Peter Warden on 4/28/14.
//  Copyright (c) 2014 Jetpac, Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <libjpcnn.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <string.h>
       #include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>
#include <time.h>

#define WAIT_TIME 5
#define LONG_WAIT_TIME 15000

#define MIN_DARK_LEVEL 14000
#define WAIT_TIME_DARK 60

#define RMSE_THRESHOLD	1800

int atos_pid=0;

float* predictions;
int predictionsLength;
char** predictionsLabels;
int predictionsLabelsLength;
const char * networkFileName;
int layer;
const char * predictorFileName;
const char * imageFileName;

void * networkHandle;
//void * predictor;


struct timespec now, tmstart;

int time_to_wait=0;

int release=0;
int exit_now=0;
sem_t stopped;
sem_t running;

double seconds;

int downsample_picture(char * fn, char * fndown) ;

void start_time(char * s) {
	clock_gettime(CLOCK_REALTIME, &tmstart);
	fprintf(stderr,"%s\n",s);
	seconds = (double)((now.tv_sec+now.tv_nsec*1e-9) - (double)(tmstart.tv_sec+tmstart.tv_nsec*1e-9));
}
void stop_time(char * s) {
	clock_gettime(CLOCK_REALTIME, &now);
	double seconds_now = (double)((now.tv_sec+now.tv_nsec*1e-9) - (double)(tmstart.tv_sec+tmstart.tv_nsec*1e-9));
	fprintf(stderr,"%s - %lf\n",s,seconds_now-seconds);
}

void unload_module(char * s) {
	start_time("Unload modules");
        pid_t pid=fork();
        if (pid==0) {
                //child
                char * args[] = { "/usr/bin/sudo","/sbin/rmmod", "-f", s, NULL };
                int r = execv(args[0],args);
                fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
		exit(1);
        }
        //master
        waitpid(pid,NULL,0);
	stop_time("Unload modules - Done");
}

void load_module(char *s ) {
	start_time("Load modules");
        pid_t pid=fork();
        if (pid==0) {
                //child
                char * args[] = { "/usr/bin/sudo", "/sbin/modprobe", s, NULL };
                int r = execv(args[0],args);
                fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
		exit(1);
        }
        //master
        waitpid(pid,NULL, 0);
	stop_time("Load modules - Done");
}

void reload_uvc() {
        fprintf(stderr,"Reload UVC modules\n");
        unload_module("uvcvideo");
        unload_module("videobuf2_core");
        unload_module("videobuf2_vmalloc");
        unload_module("videodev");
        load_module("videodev");
        load_module("videobuf2_core");
        load_module("videobuf2_vmalloc");
        load_module("uvcvideo");
        fprintf(stderr,"Reloaded UVC\n");
        return;
}

float dark_level(char * fn) {
	//fswebcam here
	start_time("Dark level");
	int pipefd[2];
	if (pipe(pipefd)==-1) {
		fprintf(stderr,"ERROR IN PIPE CREATE\n");
		exit(1);
	}


	pid_t pid=fork();
	if (pid==0) {
		close(pipefd[0]);
		//child
		dup2(pipefd[1],1);
		char * args[] = { "/usr/bin/convert" , fn, "-format" , "\%[mean]" , "info:", NULL};
		int r = execv(args[0],args);
		fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
		exit(1);
	}
	close(pipefd[1]);
	//get the RMSE
	char buffer[1024];
	int r = read(pipefd[0], buffer, 1024);
	buffer[r]='\0';
	float darkness=0.0;
	if (r<=0) {
		fprintf(stderr,"ERROR IN READING\n");
	} else {
		sscanf(buffer, "%f",&darkness);
	}
	//master
	waitpid(pid,NULL,0);	
	close(pipefd[0]);
	stop_time("Dark level - done");
	return darkness;
}

float rmse_pictures(char * fn1,char * fn2) {
	//fswebcam here
	start_time("RMSE start");
	int pipefd[2];
	if (pipe(pipefd)==-1) {
		fprintf(stderr,"ERROR IN PIPE CREATE\n");
		exit(1);
	}


	pid_t pid=fork();
	if (pid==0) {
		close(pipefd[0]);
		//child
		dup2(pipefd[1],2);
		char * args[] = { "/usr/bin/compare", "-fuzz",  "5", "-metric",  "RMSE" , fn1,  fn2,  "/dev/null", NULL};
		int r = execv(args[0],args);
		fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
		exit(1);
	}
	close(pipefd[1]);
	//get the RMSE
	char buffer[1024];
	int r = read(pipefd[0], buffer, 1024);
	buffer[r]='\0';
	float rmse=0.0;
	if (r<=0) {
		fprintf(stderr,"ERROR IN READING\n");
	} else {
		sscanf(buffer, "%f",&rmse);
	}
	//master
	waitpid(pid,NULL,0);	
	close(pipefd[0]);
	fprintf(stderr,"RMSE IS %f\n",rmse);
	stop_time("RMSE - Done");
	return rmse;
}



int take_picture(char * fn, char * fn_small) {
	start_time("Take picture");
	//fswebcam here
	unlink(fn); //remove the file if it exists
	int i=0;
	while ( release!=1 && access( fn, F_OK ) == -1 ) {
		fprintf(stderr,"LOOP %d\n", i);
		if (i>0) {
			sleep(1); //give it a little rest if we cant get picture
			if (i%4==0) {
				reload_uvc();			
			}
		} 
		if (release==1) {
			break;
		}
		int filedes[2];
		if (pipe(filedes) == -1) {
		  perror("pipe");
		  continue;
		}
		pid_t pid=fork();
		if (pid==0) {
			//child
			int devNull = open("/dev/null", O_WRONLY);
			dup2(devNull,2);
			dup2(filedes[1],1); //send stdout to parent
			close(filedes[0]);//close reading end of parent pipe
			//TODO make sure acquired image file
			/*char * args[] = { "/usr/bin/fswebcam","-r","640x480","--skip","5",
				"--no-info","--no-banner","--no-timestamp","--quiet",fn, "--scale", "320x240" ,fn_small, NULL };*/
			char * args[] = { "/home/pi/petbot-selfie/src/v4l2grab/v4l2grab","-o",fn,NULL};
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
		}
		close(filedes[1]); //close the writting part of the pipe
		fd_set          set;
		struct          timeval timeout;
		FD_ZERO(&set);
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		FD_SET(filedes[0], &set);
		if (select(filedes[0]+1, &set, NULL,  NULL, &timeout)==0) {
			//timeout occured
			fprintf(stderr,"TIMEOUT OCCURED\n");
			kill(pid, SIGKILL);
			unlink(fn); //remove the file if it exists
		} else {
			//fprintf(stderr,"NO TIMEOUT\n");
		}
		close(filedes[0]);
		//master
		waitpid(pid,NULL,0);
		i++;
	}
	downsample_picture(fn, fn_small);
	/*while ( release!=1 && access( fn_small, F_OK ) == -1 ) {
		pid_t pid=fork();
		if (pid==0) {
			//child
			int devNull = open("/dev/null", O_WRONLY);
			dup2(devNull,2);
			dup2(devNull,1);
			//TODO make sure acquired image file
			//char * args[] = { "/usr/bin/fswebcam","-r","640x480","--skip","5",
			//	"--no-info","--no-banner","--no-timestamp","--quiet",fn, "--scale", "320x240" ,fn_small, NULL };
			char * args[] = { "/home/pi/petbot-selfie/src/v4l2grab/v4l2grab","-W","320","-H","240","-o",fn_small,NULL};
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
		}
		//master
		waitpid(pid,NULL,0);
		i++;	
	}*/
	stop_time("Take picture done\n");
	return 0;
}




int crop_picture(char * fn, char * fnout) {
	//fswebcam here
	start_time("crop picture");
	unlink(fnout); //remove the file if it exists
	int i=0;
	while ( access( fnout, F_OK ) == -1 ) {
		if (release==1) {
			break;
		}
		pid_t pid=fork();
		if (pid==0) {
			//child
			int devNull = open("/dev/null", O_WRONLY);
			dup2(devNull,2);
			dup2(devNull,1);
			//TODO make sure acquired image file
			//char * args[] = { "/usr/bin/convert",fn,"-despeckle", "-gravity", "Center", "-crop", "80%", fnout, NULL };
			char * args[] = { "/usr/bin/convert",fn,"-gravity", "Center", "-crop", "80%", fnout, NULL };
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
		}
		//master
		waitpid(pid,NULL,0);
		i++;	
	}
	stop_time("Crop picture - done");
	return 0;
}

int blur_picture(char * fn, char * fnout) {
	//fswebcam here
	start_time("Blur picture");
	unlink(fnout); //remove the file if it exists
	int i=0;
	while ( access( fnout, F_OK ) == -1 ) {
		if (release==1) {
			break;
		}
		pid_t pid=fork();
		if (pid==0) {
			//child
			int devNull = open("/dev/null", O_WRONLY);
			dup2(devNull,2);
			dup2(devNull,1);
			//TODO make sure acquired image file
			//char * args[] = { "/usr/bin/convert",fn,"-despeckle", "-gravity", "Center", "-crop", "80%", fnout, NULL };
			char * args[] = { "/usr/bin/convert",fn,"-despeckle", fnout, NULL };
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
		}
		//master
		waitpid(pid,NULL,0);
		i++;	
	}
	stop_time("Blur picture - done");
	return 0;
}

int blur_picture_inplace(char *fn) {
	char * temp_file="/dev/shm/temp.jpg";
	blur_picture(fn,temp_file);
	rename(temp_file,fn);
	return 0;
}

int downsample_picture(char * fn, char * fndown) {
	//fswebcam here
	start_time("Downsample picture");
	unlink(fndown); //remove the file if it exists
	int i=0;
	while ( access( fndown, F_OK ) == -1 ) {
		if (release==1) {
			break;
		}
		pid_t pid=fork();
		if (pid==0) {
			//child
			int devNull = open("/dev/null", O_WRONLY);
			dup2(devNull,2);
			dup2(devNull,1);
			//TODO make sure acquired image file
			char * args[] = { "/usr/bin/convert",fn,"-resize","50%", fndown, NULL };
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
		}
		//master
		waitpid(pid,NULL,0);
		i++;	
	}
	stop_time("Down sample picture - done");
	return 0;
}



int busy_wait(int s) {
	int x=0;
	while (s>0) {
		sleep(1);
		x++;
		if (release==1) {
			return x;
		}
		s--;
	}
	return x;
}

void long_wait(int s) {
	time_to_wait+=s;
	fprintf(stderr,"Need to wait %d more seconds\n", time_to_wait);
	time_to_wait-=busy_wait(time_to_wait);	
	//fprintf(stderr,"Need to wait %d more seconds 2\n", time_to_wait);
}


int check_for_dog(char * fn , char * fndown) {
	if (release==1) {
		return 0;
	}	
	//downsample the image
	//char fndown[1024];
	//sprintf(fndown,"%s_down.jpg",fn);
	//downsample_picture(fn,fndown);

	//if (release==1) {
	//	return 0;
	//}	

	void * imageHandle = jpcnn_create_image_buffer_from_file(fndown);
	//unlink(fndown); //remove the file if it exists TODO
	if (imageHandle == NULL) {
		fprintf(stderr, "DeepBeliefSDK: Couldn't load image file '%s'\n", fn);
		return 0;
	}

	//next classify
	if (release==1) {
		return 0;
		jpcnn_destroy_image_buffer(imageHandle);
	}
	

	jpcnn_classify_image(networkHandle, imageHandle, 0, layer, &predictions, &predictionsLength, &predictionsLabels, &predictionsLabelsLength);
	jpcnn_destroy_image_buffer(imageHandle);


	//next predict
	if (release==1) {
		return 0;
	}	
	
	//float pred = jpcnn_predict(predictor, predictions, predictionsLength);
	float pred = predictions[0];
	fprintf(stderr,"prediction %f %s\n",predictions[0],predictionsLabels[0]);
	//fprintf(stdout,"Atos probability %f\n",pred);

	//next predict
	if (release==1) {
		return 0;
	}	


	//read in sensitivity
	double sensitivity=0.3;
	char * sensitivity_fn="/home/pi/sensitivity";
	if ( access( sensitivity_fn, F_OK ) != -1 ) {
		//have a sensitivity file lets use that
		char buffer[128];
		FILE * fptr=fopen(sensitivity_fn,"r");
		if (fptr==NULL) {
			fprintf(stderr,"error opening sensitivity file %s\n", sensitivity_fn);
		} else {
			fread(buffer, 1, 128, fptr);
			double x =atof(buffer);
			if (x<=1.0 && x>0.0) {
				sensitivity=x;
			}
		}
	}
	fprintf(stderr,"Sensitivity threshold is %lf\n",sensitivity);
	

	//next send out the image if it passes
	if (pred>sensitivity) {
		char pred_s[1024];
		sprintf(pred_s,"%0.4f", pred);
		int pid=fork();
		if (pid==0) {
			//child
			//char * args[] = { "/bin/bash","/home/pi/petbot-selfie/scripts/send_atos.sh",fn, pred_s, NULL };
			char * args[] = { "/usr/bin/python","/home/pi/petbot-selfie/scripts/capture-selfie.py",fn, pred_s, NULL };
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
		}
		atos_pid=pid;
		while (pid>0 && waitpid(pid,NULL,WNOHANG)<=0) {
			//next predict
			if (release==1) {
				fprintf(stderr,"Sending kill to atos process\n");
				kill(atos_pid, SIGTERM);
				return 0;
			}	
			//lets wait for the kid
			sleep(1);
		}
		
		return 1;
	}
	return 0;
}





void * analyze() {
	char currentImageFileName[1024];
	char currentImageFileNameSmall[1024];
	char previousImageFileName[1024];
	char previousImageFileNameSmall[1024];
	sprintf(currentImageFileName,"%s_current.jpg",imageFileName);
	sprintf(currentImageFileNameSmall,"%s_current_small.jpg",imageFileName);
	sprintf(previousImageFileName,"%s_previous.jpg",imageFileName);
	sprintf(previousImageFileNameSmall,"%s_previous_small.jpg",imageFileName);


	fprintf(stderr,"Loading network %s\n",networkFileName);
	networkHandle = jpcnn_create_network(networkFileName);
	if (networkHandle == NULL) {
		fprintf(stderr, "DeepBeliefSDK: Couldn't load network file '%s'\n", networkFileName);
		return NULL;
	}
	/*fprintf(stderr,"Loading predictor %s\n",predictorFileName);
	predictor = jpcnn_load_predictor(predictorFileName);
	if (predictor==NULL) {
		fprintf(stderr,"Failed to load predictor\n");
		return NULL;
	}*/

	int i=0;

	//fprintf(stderr,"START MAIN LOOP\n");
	//main loop
	while (1>0) {
		sem_wait(&running);
		while (time_to_wait>0) {
			long_wait(0);
			if (release==1) {
				break;
			}
		}
		
		
		//fprintf(stderr,"START SECOND LOOP\n");
		//detect-and-work loop
		while (1>0) {
			
			if (release==1) {
				break;
			}
			//fprintf(stderr,"START THIRD\n");
	
			//first picture
			take_picture(currentImageFileName,currentImageFileNameSmall);
			fprintf(stderr, "TAKE FIRST PIC\n");
			//blur_picture_inplace(currentImageFileNameSmall);
			//blur_picture_inplace(currentImageFileName);


			busy_wait(WAIT_TIME);
			if (release==1) {
				break;
			}	
			
			//motion loop
			int diff=0;
			int motion=3;
			while (diff<2500) {
				i++;
				if (release==1) {
					break;
				}	
				//move current to previous
				//fprintf(stderr, "RENAME PIC\n");
				rename(currentImageFileName,previousImageFileName);
				rename(currentImageFileNameSmall,previousImageFileNameSmall);
		
				//get current picture
				//fprintf(stderr, "TAKE PIC\n");
				take_picture(currentImageFileName,currentImageFileNameSmall);

				//check darkness level
				float darkness = dark_level(currentImageFileNameSmall);
				fprintf(stderr,"DARK %f\n", darkness);
				busy_wait(WAIT_TIME);
				if (release==1) {
					break;
				}	
				if (darkness<MIN_DARK_LEVEL) {	
					if (i%100==0) {
						time_t rawtime;
						struct tm * timeinfo;

						time ( &rawtime );
						timeinfo = localtime ( &rawtime );	

						fprintf(stdout, "Its dark.... %s \n", asctime (timeinfo));
					}
					busy_wait(WAIT_TIME_DARK);
					continue;
				}
	
					
				//blur_picture_inplace(currentImageFileNameSmall);
				//blur_picture_inplace(currentImageFileName);
				//if no motion the wait a bit
				//fprintf(stderr, "WAIT PIC\n");
				if (motion<=0) {
					busy_wait(WAIT_TIME);
				} else {
					motion--;
				}

				if (release==1) {
					break;
				}
			
				//compare
				//fprintf(stderr, "RMSE PIC\n");
				float rmse = rmse_pictures(currentImageFileNameSmall,previousImageFileNameSmall);
				//fprintf(stderr,"RMSE is %f\n",rmse);

				//check for motion
				if (rmse>RMSE_THRESHOLD) {
					fprintf(stderr,"passed threshold moving on to detector...\n");;
					motion=10;
					//int check = check_for_dog(currentImageFileName,currentImageFileNameSmall);	
					int check=0;
					if (i%2==0) {
						//fprintf(stderr, "CHECK1 \n");
						check = check_for_dog(currentImageFileName,currentImageFileNameSmall);	
					} else {
						//fprintf(stderr, "CHECK2 \n");
						char cropped_filename[1024];
						sprintf(cropped_filename,"%s_cropped.jpg",currentImageFileNameSmall);
						//fprintf(stderr, "CHECK3 \n");
						crop_picture(currentImageFileNameSmall,cropped_filename);
						check = check_for_dog(currentImageFileName,cropped_filename);	
						unlink(cropped_filename);
					}
					//fprintf(stderr, "DONE RMSE\n");
					if (check==1) {
						//fprintf(stderr, "CHECK WAIT DONE \n");
						int long_wait_time=LONG_WAIT_TIME;
						char * long_wait_time_fn="/home/pi/long_wait_time";
						if ( access( long_wait_time_fn, F_OK ) != -1 ) {
							//have a sensitivity file lets use that
							char buffer[128];
							FILE * fptr=fopen(long_wait_time_fn,"r");
							if (fptr==NULL) {
								fprintf(stderr,"error opening long wait time file %s\n", long_wait_time_fn);
							} else {
								fread(buffer, 1, 128, fptr);
								int x =atoi(buffer);
								if (x<=1.0 && x>0.0) {
									long_wait_time=x;
								}
							}
						}
						//busy_wait(LONG_WAIT_TIME);
						fprintf(stderr,"waiting for %d\n", long_wait_time);
						long_wait(long_wait_time);
					}
				}
			}
			fprintf(stderr,"LOOPA\n");
		}
		fprintf(stderr,"LOOPB\n");
		if (exit_now==1) {
			fprintf(stderr,"TRYING TO EXIT\n");
			break;
		}
		fprintf(stderr,"POST TO STOPPED\n");
	  	int r = sem_post(&stopped);
		if (r<0) {
			fprintf(stderr,"Failed to post to semaphore\n");
			exit(1);
		}

  	}

	//fprintf(stderr, "Cleaning up predictor...\n");
	//jpcnn_destroy_predictor(predictor);
	fprintf(stderr,"Cleaning up network...\n");
	jpcnn_destroy_network(networkHandle);
	sem_post(&stopped);

	return NULL;
}

int main(int argc, const char * argv[]) {


  


  if (argc!=4) {
	fprintf(stderr,"%s network_filename layer img_filename\n",argv[0]);
	exit(1);
  }

  networkFileName=argv[1];
  layer = atoi(argv[2]);
  //predictorFileName=argv[3];
  imageFileName=argv[3];


        int r = sem_init(&stopped,0,0);
	if (r<0) {
		fprintf(stderr,"SEMAPHORE ERROR!\n");
		exit(1);
	}
        r=  sem_init(&running,0,0);
	if (r<0) {
		fprintf(stderr,"SEMAPHORE ERROR2!\n");
		exit(1);
	}
        pthread_t analyze_thread;
        int  iret1;


	release=1;

        //start the tcp_client
        iret1 = pthread_create( &analyze_thread, NULL, analyze, NULL);
        if(iret1) {
                fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
                exit(EXIT_FAILURE);
        }


	//monitor STDIN/ STDOUT
	char buffer[1024];
	int i=0;
	while (1>0) {
		fgets(buffer, 1024, stdin);
		if (strlen(buffer)>0 ) {
			buffer[strlen(buffer)-1]='\0';
		}
		if (strcmp(buffer,"STOP")==0) {
			if (release==1) {
				//hmmmm	
				fprintf(stderr,"WAITING ON STOP - OOPS\n");	
			} else {
				release=1;
				fprintf(stderr,"WAITING ON STOP\n");	
				sem_wait(&stopped); //wait for other guy to stop
				//when here other guy is stopped
			}
			fprintf(stderr,"STOPPED\n");	
			fprintf(stdout,"STOPPED\n");
			fflush(stdout);	
		} else if (strcmp(buffer,"GO")==0) {
			if (release==0) {
				//fprintf(stderr,"NO GO, ALREADY GOING\n");	
			} else {
				release=0;	
				//fprintf(stderr,"STARTING GO\n");
				sem_post(&running);
			}	
		} else if (i>10 || strcmp(buffer,"EXIT")==0) {
			exit_now=1;
			release=1;
			/*if (release==1) {
				//hmmmm	
				fprintf(stdout,"STOPPED?\n");
			} else {
				release=1;
				fprintf(stdout,"WAIT STOPPED\n");	
				//when here other guy is stopped
			}*/
			//fprintf(stderr,"WAITING TO EXIT\n");
			sem_post(&running);
			sem_wait(&stopped); //wait for other guy to stop
			fprintf(stdout,"EXITING\n");
			fflush(stdout);	
			return 0;
		} else {
			fprintf(stderr,"NO MATCH!!\n");
			i++;
		}
	}	

  return 0;
}
