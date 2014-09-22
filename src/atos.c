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

#define WAIT_TIME 2
#define LONG_WAIT_TIME 200 //1500

#define MIN_DARK_LEVEL 12000
#define WAIT_TIME_DARK 4

#define RMSE_THRESHOLD	1800

float* predictions;
int predictionsLength;
char** predictionsLabels;
int predictionsLabelsLength;
const char * networkFileName;
int layer;
const char * predictorFileName;
const char * imageFileName;


void * networkHandle;
void * predictor;

int release=0;
int exit_now=0;
sem_t stopped;
sem_t running;


void unload_module(char * s) {
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
}

void load_module(char *s ) {
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
}

void reload_uvc() {
        fprintf(stderr,"remogin\n");
        unload_module("uvcvideo");
        unload_module("videobuf2_core");
        unload_module("videobuf2_vmalloc");
        unload_module("videodev");
        load_module("videodev");
        load_module("videobuf2_core");
        load_module("videobuf2_vmalloc");
        load_module("uvcvideo");
        fprintf(stderr,"Loaded UVC\n");
        return;
}

float dark_level(char * fn) {
	//fswebcam here
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
	return darkness;
}

float rmse_pictures(char * fn1,char * fn2) {
	//fswebcam here
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
	return rmse;
}



int take_picture(char * fn, char * fn_small) {
	//fswebcam here
	unlink(fn); //remove the file if it exists
	int i=0;
	while ( access( fn, F_OK ) == -1 ) {
		if (i>0) {
			sleep(1); //give it a little rest if we cant get picture
			if (i%4==0) {
				reload_uvc();			
			}
		} 
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
			char * args[] = { "/usr/bin/fswebcam","-r","640x480","--skip","5",
				"--no-info","--no-banner","--no-timestamp","--quiet",fn, "--scale", "320x240" ,fn_small, NULL };
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
		}
		//master
		waitpid(pid,NULL,0);
		i++;	
	}
	return 0;
}




int crop_picture(char * fn, char * fnout) {
	//fswebcam here
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
	return 0;
}

int blur_picture(char * fn, char * fnout) {
	//fswebcam here
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
	return 0;
}


void busy_wait(int s) {
	while (s>0) {
		sleep(1);
		if (release==1) {
			return;
		}
		s--;
	}
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
	
	float pred = jpcnn_predict(predictor, predictions, predictionsLength);
	fprintf(stdout,"Atos probability %f\n",pred);

	//next predict
	if (release==1) {
		return 0;
	}	

	//next send out the image if it passes
	if (pred>0.18) {
		char pred_s[1024];
		sprintf(pred_s,"%0.4f", pred);
		int pid=fork();
		if (pid==0) {
			//child
			char * args[] = { "/bin/bash","/home/pi/petbot-selfie/scripts/send_atos.sh",fn, pred_s, NULL };
			int r = execv(args[0],args);
			fprintf(stderr,"SHOULD NEVER REACH HERE %d\n",r);
			exit(1);
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
	fprintf(stderr,"Loading predictor %s\n",predictorFileName);
	predictor = jpcnn_load_predictor(predictorFileName);
	if (predictor==NULL) {
		fprintf(stderr,"Failed to load predictor\n");
		return NULL;
	}

	int i=0;


	//main loop
	while (1>0) {
		sem_wait(&running);
		
		
		//detect-and-work loop
		while (1>0) {
			
			if (release==1) {
				break;
			}
	
			//first picture
			take_picture(currentImageFileName,currentImageFileNameSmall);
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
				rename(currentImageFileName,previousImageFileName);
				rename(currentImageFileNameSmall,previousImageFileNameSmall);
		
				//get current picture
				take_picture(currentImageFileName,currentImageFileNameSmall);

				//check darkness level
				float darkness = dark_level(currentImageFileNameSmall);
			//	fprintf(stderr,"DARK %f\n", darkness);
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
				if (motion<=0) {
					busy_wait(WAIT_TIME);
				} else {
					motion--;
				}

				if (release==1) {
					break;
				}
			
				//compare
				float rmse = rmse_pictures(currentImageFileNameSmall,previousImageFileNameSmall);
				//fprintf(stderr,"RMSE is %f\n",rmse);

				//check for motion
				if (rmse>RMSE_THRESHOLD) {
					fprintf(stderr,"passed threshold moving on to detector...\n");;
					motion=10;
					//int check = check_for_dog(currentImageFileName,currentImageFileNameSmall);	
					int check=0;
					if (i%2==0) {
						check = check_for_dog(currentImageFileName,currentImageFileNameSmall);	
					} else {
						char cropped_filename[1024];
						sprintf(cropped_filename,"%s_cropped.jpg",currentImageFileNameSmall);
						crop_picture(currentImageFileNameSmall,cropped_filename);
						check = check_for_dog(currentImageFileName,cropped_filename);	
						unlink(cropped_filename);
					}
					if (check==1) {
						busy_wait(LONG_WAIT_TIME);
					}
				}
			}
		}
		if (exit_now==1) {
			break;
		}
	  	sem_post(&stopped);

  	}

	fprintf(stderr, "Cleaning up predictor...\n");
	jpcnn_destroy_predictor(predictor);
	fprintf(stderr,"Cleaning up network...\n");
	jpcnn_destroy_network(networkHandle);
	sem_post(&stopped);

	return NULL;
}

int main(int argc, const char * argv[]) {


  


  if (argc!=5) {
	fprintf(stderr,"%s network_filename layer svm_filename img_filename\n",argv[0]);
	exit(1);
  }

  networkFileName=argv[1];
  layer = atoi(argv[2]);
  predictorFileName=argv[3];
  imageFileName=argv[4];


        sem_init(&stopped,0,0);
        sem_init(&running,0,0);

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
	while (1>0) {
		fgets(buffer, 1024, stdin);
		if (strlen(buffer)>0 ) {
			buffer[strlen(buffer)-1]='\0';
		}
		if (strcmp(buffer,"STOP")==0) {
			if (release==1) {
				//hmmmm	
			} else {
				release=1;
				sem_wait(&stopped); //wait for other guy to stop
				//when here other guy is stopped
				fprintf(stdout,"STOPPED\n");	
			}
		} else if (strcmp(buffer,"GO")==0) {
			if (release==0) {

			} else {
				release=0;	
				sem_post(&running);
			}	
		} else if (strcmp(buffer,"EXIT")==0) {
			exit_now=1;
			if (release==1) {
				//hmmmm	
			} else {
				release=1;
				sem_wait(&stopped); //wait for other guy to stop
				//when here other guy is stopped
				fprintf(stdout,"STOPPED\n");	
			}
			return 0;
		}
	}	

  return 0;
}
