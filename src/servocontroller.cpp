/*
 * Jarvis Schultz
 * July 1, 2011
 * Servo animation code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <float.h>
#include "kbhit.h"
#include <assert.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
using namespace std;

/*******************************************************************************
 * GLOBAL VARIABLES ************************************************************
 ******************************************************************************/ 

#define		BAUDRATE	B115200
#define		MODEMDEVICE	"/dev/rfcomm0"
// #define		MODEMDEVICE	"/dev/ttyUSB0"
#define 	_POSIX_SOURCE	1 /* POSIX compliant source */
#define         PACKET_SIZE	4
#define		NUM_SERVOS	8
#define		MIN_PULSE	992
#define		MAX_PULSE	2000
#define		MAX_CHANNELS	18
#define		MAX_SPEED	255

int fd;
struct termios oldtio,newtio;
unsigned int NUM_FRAMES = 0;
int offset_array[NUM_SERVOS] = {-60,0,160,120,30,70,-20,-160};

/*******************************************************************************
 * CLASS DECLARATIONS ** *******************************************************
 ******************************************************************************/
typedef struct
{
    unsigned short int chan;
    unsigned int ramp;
    unsigned int range;
} Servo;

typedef struct
{
    unsigned int pause;
    Servo servo_array[NUM_SERVOS];
} Frame;

typedef struct
{
    int dummy;
    Frame frame_array[];
} Show;

/*******************************************************************************
 * FUNCTION DECLARATIONS *******************************************************
 ******************************************************************************/
void init_comm(void);
void send_data(Servo *servo);
void send_frame(Frame *frame);
void send_animation(Show *animation);
void send_calibrate_frame(void);
Show *ReadControls(std::string filename);

/*******************************************************************************
 * FUNCTIONS TO CALL************************************************************
 ******************************************************************************/
// The following function is used for opening a com port to
// communicate with the mobile robot.
void init_comm(void)
{
    /* 
       Open modem device for reading and writing and not as controlling tty
       because we don't want to get killed if linenoise sends CTRL-C.
    */
    fd = open(MODEMDEVICE, O_RDWR | O_NOCTTY ); 
    if (fd <0) {perror(MODEMDEVICE); exit(-1); }

    tcgetattr(fd,&oldtio); /* save current serial port settings */
    bzero(&newtio, sizeof(newtio)); /* clear struct for new port settings */
 
    /* 
       BAUDRATE: Set bps rate. You could also use cfsetispeed and cfsetospeed.
       CRTSCTS : output hardware flow control (only used if the cable has
       all necessary lines. See sect. 7 of Serial-HOWTO)
       CS8     : 8n1 (8bit,no parity)
       CSTOPB  : enable 2 stop bits
       CLOCAL  : local connection, no modem contol
       CREAD   : enable receiving characters
    */
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	 
    /*
      IGNPAR  : ignore bytes with parity errors
      ICRNL   : map CR to NL (otherwise a CR input on the other computer
      will not terminate input)
      otherwise make device raw (no other input processing)
    */
    newtio.c_iflag = IGNPAR;

    /*
      Raw output.
    */
    newtio.c_oflag &= ~OPOST;

    /*
     * Disable canonical input so that we can read byte-by-byte 
     */
    newtio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    /* Now we need to set the number of bytes that we want to read in,
     * and the timeout length */
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 0;
	
    /* 
       now clean the modem line and activate the settings for the port
    */
    tcflush(fd, TCIOFLUSH);
    tcsetattr(fd,TCSANOW,&newtio);
}

void send_data(Servo *servo)
{
    char dest[PACKET_SIZE];
    int i = 0;
    memset(dest,0,sizeof(dest));

    // Set the position command byte:
    dest[0] = 0x84;

    // set the channel value:
    if (servo->chan >=0 && servo->chan <= MAX_CHANNELS)
	dest[1] = servo->chan;
    else
    {
	puts("ERROR: Channel out of range");
	return;
    }

    // set position value:
    printf("Position = %d\n\r", servo->range);
    if (servo->range >= (MIN_PULSE+offset_array[servo->chan])*4 &&
	servo->range <= (MAX_PULSE+offset_array[servo->chan])*4)
    {
	dest[2] = servo->range & 0x7F;
	dest[3] = ((servo->range) >> 7) & 0x7F;
    }
    else
    {
	puts("ERROR: Position out of range");
	return;
    }
        
    // Now we can send the data:
    write(fd, dest, PACKET_SIZE);
    fsync(fd);

    // Now repeat for the speed packet:
    dest[0] = 0x87;

    if (servo->ramp >=0 && servo->ramp <= MAX_SPEED)
    {
	dest[2] = servo->ramp & 0x7F;
	dest[3] = ((servo->ramp) >> 7) & 0x7F;
    }
    else
    {
	puts("ERROR: Speed out of range");
	return;
    }
}

void send_frame(Frame *frame)
{
    int i;
    for(i=0; i<NUM_SERVOS; i++)
    {
	send_data(&frame->servo_array[i]);
    }
}

void send_animation(Show *animation)
{
    struct timespec delaytime = {0, 100000};
    struct timespec delayrem;
    static int i = 0;
    int pause_time;
    printf("FRAME NUMBER %d\n\r",i);
    send_frame(&animation->frame_array[i]);
    // Now pause:
    pause_time = (&animation->frame_array[i])->pause;
    if (pause_time < 1000)
	delaytime.tv_nsec = pause_time*1000000;
    else
    {
	double big_time, small_time;
	small_time = modf((double) pause_time/1000.0, &big_time);
	delaytime.tv_sec = (int) big_time;
	delaytime.tv_nsec = ((int) small_time)*1000000;
    }
    
    if(nanosleep(&delaytime, &delayrem)) printf("Nanosleep Error\n");

    i++;
    if(i>3) i = 0;
 }

void send_calibrate_frame(void)
{
    // Create a Show:
    Servo temp_servo;
    Frame temp_frame;
    Show *cal;
    unsigned int i,j;

    // Now, we need to define the size of the show struct:
    size_t alloc;
    alloc = sizeof(*cal) + sizeof(cal->frame_array[0]);
    cal = (Show*) malloc(alloc);

    // Now, let's fill in the arrays:
    cal->dummy = 0;
    for(i=0; i<NUM_FRAMES; i++)
    {
	for(j=0; j<NUM_SERVOS; j++)
	{
	    temp_servo.chan = j;
	    temp_servo.ramp = 220;
	    temp_servo.range = (1500+offset_array[j])*4;
	    temp_frame.servo_array[j] = temp_servo;
	}
	temp_frame.pause = 0;
	cal->frame_array[i] = temp_frame;
    }

    // Send out the show:
    printf("Calibration Frame - Hit key to continue\n\r");
    send_frame(&cal->frame_array[0]);
    while(!kbhit());
}

Show *ReadControls(std::string filename)
{
    unsigned int i,j, temp_int;
    std::string line, temp;
    Show *animation;
    Frame temp_frame;
    Servo temp_servo;
    ifstream file;
    file.open(filename.c_str(), fstream::in);
    // Read line that tells us the number of frames:
    getline(file, line);
    // Get number of frames:
    std::stringstream ss(line);
    ss >> temp >> NUM_FRAMES;

    // Read and ignore headers:
    getline(file, line);

    // Now we can read in the show:
    unsigned int pause_array[NUM_FRAMES];
    unsigned int big_array_length = NUM_FRAMES*NUM_SERVOS;
    unsigned int range_array[big_array_length], ramp_array[big_array_length];

    for(i=0; i<NUM_FRAMES; i++)
    {
	// Get rid of frame number:
	getline(file, line, ',');
	// Get pause value:
	getline(file, line, ',');
	std::stringstream ss(line);
	ss >> temp_int;
	printf("%d, ",temp_int);
	pause_array[i] = temp_int;

	// Get ramp values:
	for(j=0; j<NUM_SERVOS; j++)
	{
	    getline(file, line, ',');
	    std::stringstream ss(line);
	    ss >> temp_int;
	    printf("%d, ",temp_int);
	    ramp_array[i*NUM_SERVOS+j] = temp_int;
	}

	// Get range values:
	for(j=0; j<NUM_SERVOS; j++)
	{
	    getline(file, line, ',');
	    std::stringstream ss(line);
	    ss >> temp_int;
	    printf("%d, ",temp_int);
	    range_array[i*NUM_SERVOS+j] = temp_int;
	}
	printf("\n\r");
    }
	    
    // Now, we need to define the size of the show struct:
    size_t alloc;
    alloc = sizeof(*animation) + sizeof(animation->frame_array[0])*NUM_FRAMES;
    animation = (Show*) malloc(alloc);

    // Now, let's fill in the arrays:
    animation->dummy = 0;
    for(i=0; i<NUM_FRAMES; i++)
    {
	for(j=0; j<NUM_SERVOS; j++)
	{
	    temp_servo.chan = j;
	    temp_servo.ramp = (1-ramp_array[i*NUM_SERVOS+j]/63)*MAX_SPEED;
	    temp_servo.range = (range_array[i*NUM_SERVOS+j]+offset_array[j])*4;
	    temp_frame.servo_array[j] = temp_servo;
	}
	temp_frame.pause = pause_array[i];
	animation->frame_array[i] = temp_frame;
    }
    return(animation);  
}
	

int main(int argc, char** argv)
{
    Show *animation;
    char path[256];
 
    // Get filenames:
    getcwd(path, sizeof(path));
    std::string working_dir(path);
    std::size_t found = working_dir.find("src");  
    working_dir = working_dir.substr(0, found);  
    std::string file_dir = "data/"; 
    std::string filename = working_dir + file_dir + "WalkingTable.csv";
    
    animation = ReadControls(filename);

    // Initialize communication:
    init_comm();

    // send_calibrate_frame();
   
    // First, let's sit and wait for a keyboard hit before doing anything:
    send_frame(&animation->frame_array[0]);
	
    puts("Press Button to begin execution:");
    while(!kbhit());
    puts("Beginning Execution");

    while(1)
    {
	send_animation(animation);
    	if (kbhit())
    	{
    	    int c = fgetc(stdin);
    	    if (c == 'q') break;
    	}
    }
    
    close(fd);
    return (EXIT_SUCCESS);
}