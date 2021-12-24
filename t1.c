#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <string.h> 
#include <errno.h> 
#include <pthread.h>
#include <getopt.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>


#define BUFFER_MAX 3
#define DIRECTION_MAX 35
#define VALUE_MAX 30

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define PWM 0

#define POUT 23
#define PIN 24

#define PIRPIN 22

#define MAX_TIME 83
#define DHT11PIN 7

#define POUT2 21
#define PIN2 20

#define CS_MCP3208 11 //GPIO 8 
#define SPI_CHANNEL 0 
#define SPI_SPEED 1000000 //1Mhz

#define ARRAY_SIZE(array) sizeof(array)/ sizeof(array[0])

static const char *DEVICE = "/dev/spidev0.0";
static uint8_t MODE = SPI_MODE_0;
static uint8_t BITS = 8;
static uint32_t CLOCK = 1000000;
static uint16_t DELAY = 5;

int dht11_val[5] = {0,0,0,0,0};

double distance =0;
int sock = -1;
int turn = 0;

static int GPIOExport(int pin){
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;
    
    fd= open("/sys/class/gpio/export", O_WRONLY);
    if(-1 == fd){
        fprintf(stderr,"Fail 1");
        return (-1);
        }
    
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write (fd, buffer, bytes_written);
    close(fd);
    return (0);
}
static int GPIOUnexport(int pin) {
   char buffer[BUFFER_MAX];
   ssize_t bytes_written;
   int fd;

   fd = open("/sys/class/gpio/unexport", O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open unexport for writing!\n");
      return(-1);
   }

   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
   write(fd, buffer, bytes_written);
   close(fd);
   return(0);
}

static int GPIODirection(int pin, int dir) {
   static const char s_directions_str[]  = "in\0out";


   char path[DIRECTION_MAX]="/sys/class/gpio/gpio%d/direction";
   int fd;

   snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
   
   fd = open(path, O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open gpio direction for writing!\n");
      return(-1);
   }

   if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
      fprintf(stderr, "Failed to set direction!\n");
      return(-1);
   }

   close(fd);
   return(0);
}

static int GPIORead(int pin) {

   char path[VALUE_MAX];
   char value_str[3];
   int fd;

   snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
   fd = open(path, O_RDONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open gpio value for reading!\n");
      return(-1);
   }

   if (-1 == read(fd, value_str, 3)) {
      fprintf(stderr, "Failed to read value!\n");
      return(-1);
   }

   close(fd);

   return(atoi(value_str));
}

static int GPIOWrite(int pin, int value) {
   static const char s_values_str[] = "01";

   char path[VALUE_MAX];
   int fd;
   

   snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
   fd = open(path, O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open gpio value for writing!\n");
      return(-1);
   }

   if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
      fprintf(stderr, "Failed to write value!\n");
      return(-1);

   close(fd);
    }
   return(0);
}

static int prepare(int fd)
{
    if(ioctl(fd, SPI_IOC_WR_MODE, &MODE) == -1){
        perror("Can't set MODE\n");
        return -1;
    }    

    if(ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &BITS) == -1){
        perror("Can't set number of BITS\n");
        return -1;
    }
    if(ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &CLOCK) == -1){
        perror("Can't set write CLOCK\n");
        return -1;
    }
    if(ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &CLOCK) == -1){
        perror("Can't set read CLOCK\n");
        return -1;
    }

    return 0;
}

uint8_t control_bits_differential(uint8_t channel)
{
    return (channel & 7) << 4;
}

uint8_t control_bits(uint8_t channel)
{
    return 0x8 | control_bits_differential(channel);
}

int readadc(int fd, uint8_t channel)
{
    uint8_t tx[] = {1, control_bits(channel), 0};
    uint8_t rx[3];

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = ARRAY_SIZE(tx),
        .delay_usecs = DELAY,
        .speed_hz = CLOCK,
        .bits_per_word = BITS,
    };

    if(ioctl(fd, SPI_IOC_MESSAGE(1), &tr) == 1){
        perror("IO Error\n");
        abort();
    }
    return ((rx[1] << 8) & 0x300) | (rx[2] & 0xFF);
}

void error_handling( char *message){
    fputs(message,stderr);
    fputc( '\n',stderr);
    exit( 1);
}

void* ultrawave_thread(){
    clock_t start_t, end_t;
    double time;

    if(-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN)){
        printf("gpio export err\n");
        exit(-1);
    }

    usleep(1000);

    if(-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN)){
        printf("gpio direciton err\n");
        exit(-1);
    }

    GPIOWrite(POUT, 0);
    usleep(1000);
    
    int valid = 0;
    while(1){
        if(-1 == GPIOWrite(POUT, 1)){
            printf("gpio write/trigger err\n");
            exit(-1);
        }
        usleep(100);
        GPIOWrite(POUT, 0);
    
        while(GPIORead(PIN) == 0){
            start_t = clock();
        }
        while(GPIORead(PIN) == 1){
            end_t = clock();
        }

        time = (double)(end_t - start_t)/CLOCKS_PER_SEC;
        distance = time/2 * 34000;

        if(distance > 900){
            distance = 900;
        }
        
        if(distance < 7){
            valid++;
        }   
        else
            valid = 0;
            
        if(valid >= 3){
            //printf("send packet!\n");
            write(sock, "S0", sizeof("S0"));
            turn = 0;
            valid = 0;
            usleep(1000);
        } 

        //printf("distance : %.2lfcm\n", distance);
        
        usleep(80000);
    }
    GPIOUnexport(PIN);
}

void* motion_thread(){
     if(-1 == GPIOExport(PIRPIN)){
        printf("gpio export err\n");
        exit(-1);
    }

    usleep(1000);

    if(-1 == GPIODirection(PIRPIN, IN)){
        printf("gpio direciton err\n");
        exit(-1);
    }
    usleep(1000);
    
    int pre_state = 0;
    int value = 0;
    while(1){
        value = GPIORead(PIRPIN);
        printf("value : %d\n", value);
        if (value == 1){
            write(sock, "L1", sizeof("L1"));
        }
        else{
            write(sock, "0", sizeof("0"));
            usleep(5000);
        }
        usleep(6000);
    }
    GPIOUnexport(PIRPIN);
}

void dht11_read_val()
{

   uint8_t laststate = HIGH;
   uint8_t counter =0;
   uint8_t j=0, i;

   pinMode(DHT11PIN, OUTPUT);
   digitalWrite(DHT11PIN, 0);
   delay(18);
   digitalWrite(DHT11PIN, 1);
   delayMicroseconds(40);
   pinMode(DHT11PIN, INPUT);
   
   for( i=0 ; i<MAX_TIME ; i++) {
      counter =0;
      while(digitalRead(DHT11PIN) == laststate) {
         counter++;
         delayMicroseconds(1);
         if(counter == 200)
            break;
      }
       laststate = digitalRead(DHT11PIN);
       if(counter == 200)
          break;
       delayMicroseconds(5.0);

        if((i>=4) && (i%2 ==0)) {
          dht11_val[j/8] <<= 1;                     
          if(counter > 20) {
             dht11_val[j/8] |= 1;
          }
          j++;
       }
    }   
        
    if (j >= 40 &&(dht11_val[4] == ((dht11_val[0] + dht11_val[1] + dht11_val[2] + dht11_val[3]) & 0xFF))) {
        //printf("humidity = %d.%d %% Temperature = %d.%d *C \n", dht11_val[0], dht11_val[1], dht11_val[2], dht11_val[3]) ;
        if(dht11_val[0] > 50){
            write(sock, "F1", sizeof("F1"));
        }
        else{
            write(sock, "F0", sizeof("F0"));
        }
    }

    for(int idx = 0; idx < 5; idx++)
        dht11_val[idx] = 0;
}


void* dht11_thread() {

   if(wiringPiSetup() == -1 ) {
      printf("return -1 error");   
      exit(-1);
   }

   while(1) {
       dht11_read_val();
       usleep(7000);
   }
   
   return 0;
}


 
void* FSR_thread() {
 
    int value =0;
    int fd = open(DEVICE, O_RDWR);
    if(fd <= 0 ){
        printf("Device %s not found\n", DEVICE);
        exit(-1);
    }
    if (prepare(fd)==-1){
        exit(-1);
        }
    
    int valid = 0;
    while (1){
        value = readadc(fd,0);
        //printf("valid: %d\n",valid);
        if(value >= 700)
            valid++;
        else
            valid = 0;
            
        if(valid > 3){
            //printf("send\n");
            write(sock, "I1", sizeof("I1"));
            valid = 0;
        }
        else
            write(sock, "00", sizeof("00"));
        usleep(8000);
        
    }
        
    close (fd);
        
    return 0;
}

void* button_thread()
{
    int state = 1;
    int pre_state = 1;
    
    if(-1 == GPIOExport(POUT2) || -1 == GPIOExport(PIN2))
        exit(-1);
    if(-1 == GPIODirection(POUT2, OUT) || -1 == GPIODirection(PIN2, IN))
        exit(-1);
    if(-1 == GPIOWrite(POUT2, 1))
            exit(0);
                
    while(1){
        state = GPIORead(PIN2);
        if(state == 0 && state != pre_state && turn == 0){
            //printf("on\n");
            write(sock, "S1", sizeof("S1"));
            turn = 1;
        }
        else if (state == 0 && state != pre_state && turn == 1){
            //printf("off\n");
            write(sock, "S0", sizeof("S0"));
            turn = 0;
        }
        pre_state = state;
        usleep(9000);
    }
    if(-1 == GPIOUnexport(POUT2) || -1 == GPIOUnexport(PIN2))
        exit(-1);
}    

int main(int argc, char* argv[])
{
    int serv = -1;
    struct sockaddr_in serv_addr;
    int str_len;
    int light = 0;

    if(argc!=3){
        printf("Usage : %s <IP> <port>\n",argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if(serv < 0){
        serv = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        if(serv == -1)
            error_handling("connect() error");
    }
    
    pthread_t p_thread[5];
    int thr_id;
    int status;

    thr_id = pthread_create(&p_thread[0], NULL, ultrawave_thread, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }

    thr_id = pthread_create(&p_thread[1], NULL, motion_thread, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }
    
    thr_id = pthread_create(&p_thread[2], NULL, dht11_thread, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }
    
    thr_id = pthread_create(&p_thread[3], NULL, FSR_thread, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }
    
    thr_id = pthread_create(&p_thread[4], NULL, button_thread, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }
    
    pthread_join(p_thread[0], (void**)&status);
    pthread_join(p_thread[1], (void**)&status);
    pthread_join(p_thread[2], (void**)&status);
    pthread_join(p_thread[3], (void**)&status);
    pthread_join(p_thread[4], (void**)&status);

    close(sock);
    return(0);

}
