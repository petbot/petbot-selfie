#PetBot selfie

An open source repository for [PetBot](http://petbot.ca) dog recognition software.

Here we store code to recognize dogs from a USB camera from the raspberry pi.

Included in this repository is:

* **DeepBeliefSDK** - The JetPac SDK that is needed to run the underlying neural-network
* **src/atos.c** - The main binary that runs recognition when needed and controls execution
* **src/load.c** - Load a network, model and evaluate an image file
* **src/train.c** - Train a model given lists of files for positives and negatives
* **model/** - Contains the current petbot-selfie SVM model
* **scripts/** - Scripts to capture video and send an email

Run by using:

```sudo ./atos ccv2012.ntwk -2 model_2012_l2_p5p50_n4 /dev/shm/out```

## License
All content here is copyright Michael (Misko) Dzamba 2014. Unless otherwise stated in the headers. Please feel free to use any of my code or trained models for any personal projects. If you would like to package parts of this software with your product please contact me for further details.
