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

//  Originally forked from Peter Warden on 4/28/14.
//  Copyright (c) 2014 Jetpac, Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <libjpcnn.h>

#include <sys/stat.h>
#define NETWORK_FILE_NAME "jetpac.ntwk"


char ** read_lines(const char * f) {
		FILE *fp = fopen(f, "r");
		fseek(fp, 0, SEEK_END);
		long fsize = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		char *string = (char*)malloc(fsize + 1);
		fread(string, fsize, 1, fp);
		fclose(fp);

		string[fsize] = '\0';

		int lines=0;
		int i;
		for (i=0; i<fsize; i++) {
			if (string[i]=='\n') {
				lines++;
			}
		}

		char ** files = (char**)malloc(sizeof(char*)*(lines+1));
		int line=0;
		files[line]=string;
		for (i=1; i<fsize; i++) {
			if (string[i]=='\n') {
				string[i]='\0';
				line++;
				files[line]=string+i+1;
			}
		}
		files[line]=NULL;

		return files;
}


int main(int argc, const char * argv[]) {


  float* predictions;
  int predictionsLength;
  char** predictionsLabels;
  int predictionsLabelsLength;


  if (argc != 5) {
	fprintf(stdout, "%s network_filename layer positive_list negative_list\n", argv[0]);
	return 1;
  }

  const char * networkFileName=argv[1];
  int layer = atoi(argv[2]);
  const char * positives_fn = argv[3];
  const char * negatives_fn = argv[4];

  void * networkHandle = jpcnn_create_network(networkFileName);
  if (networkHandle == NULL) {
    fprintf(stderr, "DeepBeliefSDK: Couldn't load network file '%s'\n", networkFileName);
    return 1;
  }

  //lets try to make a trainer
  void * trainer = jpcnn_create_trainer();
  if (trainer==NULL) {
	fprintf(stderr, "Failed to create trainer\n");
	return 1;
  }

  //lets read in the positive and negative examples
  char ** positives_filenames = read_lines(positives_fn);
  char ** negatives_filenames = read_lines(negatives_fn);

  char ** examples[2] = {negatives_filenames, positives_filenames};
  int j;
  for (j=0; j<2; j++ ){  
  char ** train_examples=examples[1-j];
  int i;
  for (i=0; train_examples[i]!=NULL; i++) {
		  if (i%4==0) {
			continue;
		  }
		  char * imageFileName = train_examples[i];
		  struct stat st;
		  stat(imageFileName, &st);
	      if (st.st_size<100) {
			continue;
		  }
		
		  void * imageHandle = jpcnn_create_image_buffer_from_file(imageFileName);
		  if (imageHandle == NULL) {
			//fprintf(stdout, "DeepBeliefSDK: Couldn't load image file '%s'\n", imageFileName);
			continue;
		  }
		  jpcnn_classify_image(networkHandle, imageHandle, 0, layer, &predictions, &predictionsLength, &predictionsLabels, &predictionsLabelsLength);
		  //jpcnn_train(trainer, (float)j, predictions, predictionsLength);
		  if (1-j==0) {
		  	jpcnn_train(trainer, 0.0f, predictions, predictionsLength);
		  } else {
		  	jpcnn_train(trainer, 1.0f, predictions, predictionsLength);
		  }
		  jpcnn_destroy_image_buffer(imageHandle);
  }
  }


  void * predictor = jpcnn_create_predictor_from_trainer(trainer);
  jpcnn_print_predictor(predictor);


  /*for (j=0; j<2; j++ ){  
  char ** train_examples=examples[j];
  int i;
  for (i=0; train_examples[i]!=NULL; i++) {
		  char * imageFileName = train_examples[i];
		  struct stat st;
		  stat(imageFileName, &st);
	      if (st.st_size<100) {
			continue;
		  }
		
		  imageHandle = jpcnn_create_image_buffer_from_file(imageFileName);
		  if (imageHandle == NULL) {
			continue;
		  }
		  jpcnn_classify_image(networkHandle, imageHandle, 0, -1, &predictions, &predictionsLength, &predictionsLabels, &predictionsLabelsLength);
		  float pred = jpcnn_predict(predictor, predictions, predictionsLength);
		  //fprintf(stdout, "%d -> %f  , %d\n",j,pred,i%4);
		  jpcnn_destroy_image_buffer(imageHandle);
  }
  }*/

  jpcnn_destroy_network(networkHandle);

  return 0;
}
