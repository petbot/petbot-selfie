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

//  Originally forked from  Peter Warden on 4/28/14.
//  Copyright (c) 2014 Jetpac, Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <libjpcnn.h>
#include <string.h>

#define NETWORK_FILE_NAME "jetpac.ntwk"

int main(int argc, const char * argv[]) {


  float* predictions;
  int predictionsLength;
  char** predictionsLabels;
  int predictionsLabelsLength;

  if (argc!=5) {
	fprintf(stderr,"%s network_filename layer svm_filename img_filename\n",argv[0]);
	exit(1);
  }

  const char * networkFileName=argv[1];
  int layer = atoi(argv[2]);
  const char * predictorFileName=argv[3];
  const char * imageFileName=argv[4];


  void * networkHandle = jpcnn_create_network(networkFileName);
  if (networkHandle == NULL) {
    fprintf(stderr, "DeepBeliefSDK: Couldn't load network file '%s'\n", networkFileName);
    return 1;
  }

  void * predictor=NULL;
  if (strcmp(predictorFileName,"-")!=0) {
	  predictor = jpcnn_load_predictor(predictorFileName);
	  if (predictor==NULL) {
		fprintf(stderr,"Failed to load predictor\n");
		return 1;
	  }
  }


  int i=0;
  for (i=0; i<1; i++) {
  void * imageHandle = jpcnn_create_image_buffer_from_file(imageFileName);
  if (imageHandle == NULL) {
    fprintf(stderr, "DeepBeliefSDK: Couldn't load image file '%s'\n", imageFileName);
    return 1;
  }



  jpcnn_classify_image(networkHandle, imageHandle, 0, layer, &predictions, &predictionsLength, &predictionsLabels, &predictionsLabelsLength);
  if (strcmp(predictorFileName,"-")!=0) {
  	float pred = jpcnn_predict(predictor, predictions, predictionsLength);
  	fprintf(stdout,"%f\n",pred);
  } else {
	int index;
	  for (index = 0; index < predictionsLength; index += 1) {
    float predictionValue;
    char* label = predictionsLabels[index];
    predictionValue = predictions[index];
    fprintf(stdout, "%d\t%f\t%s\n", index, predictionValue, label);
  }
  }
  jpcnn_destroy_image_buffer(imageHandle);
  }
  jpcnn_destroy_predictor(predictor);

  /*for (index = 0; index < predictionsLength; index += 1) {
    float predictionValue;
    char* label = predictionsLabels[index];
    predictionValue = predictions[index];
    fprintf(stdout, "%d\t%f\t%s\n", index, predictionValue, label);
  }*/
  

  jpcnn_destroy_network(networkHandle);

  return 0;
}
