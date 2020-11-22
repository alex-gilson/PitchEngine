#ifndef MAIN_H
#define MAIN_H

#include "wavio.h"
#include "fft.h"
#include <math.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#define NFREQ      (1 + FFTLEN/2)
#define OVERSAMP   4
#define FRAMEINC   (FFTLEN/OVERSAMP)    // Hopcount or number of samples between frames.
#define CIRCBUF    (FFTLEN + FRAMEINC)
#define DTA        (FFTLEN/FSAMP)				// Delta t_a. Time between frames.
#define MAXVAL16   32768/1.5
#define WINCONST   0.85185
#define FFT        0                    // FFT = 1: Compute the FFT and IFFT of input audio
#define DISTORTION 0                    // DISTORTION = 1: Apply a polynomial function to the input audio

// Global variables declaration
float *inbuffer, *outbuffer;  	 // Input/output circular buffers
float *inframe, *outframe; 		 	 // Input and output frames
float *inwin, *outwin;				   // Input and output windows
float *in_audio, *out_audio;	 	 // Complete audio data from wav file for input and output
float *mag, *phase;						 	 // Frame magnitude and phase
float *phi_s;                    // Phase adjusted for synthesis stage
float *coeffs = NULL;            // Coefficients from the distortion polynomial
size_t coeff_size;							 // Number of coefficients pointed at by coeffs
int16_t *audio16;                // 16 bit integer representation of the audio
complex *cpx;						         // Complex variable for FFT 
volatile int io_ptr = 0;			   // Input/output pointer for circular buffers
volatile int frame_ptr = 0;		 	 // Frame pointer 
unsigned long int audio_ptr = 0; // Wav file sample pointer
unsigned long NUM_SAMP;				   // Total number of samples in wave file
struct sigaction sa;				     // Set interrupt timer for input/output simulation
struct itimerval timer;          // Set interrupt timer for input/output simulation
float avg_time = 0;              // Average time taken to compute a frame
float elapsed_time = 0;          // Time spent computing the current frame
float N = 1;                     // Number of samples processed

// Function declaration
void buffer_interrupt(int sig);
void sigalrm_handler(int sig);
void process_buffer();
void process_frame();
float* load_distortion_coefficients(size_t* coeff_size);

#endif // MAIN_H
