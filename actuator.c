#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <pthread.h>


#define BUFFER_MAX 3
#define DIRECTION_MAX 45
#define VALUE_MAX 256

#define IN  0
#define OUT 1
#define LOW  0
#define HIGH 1

#define POUT 4
#define POUT2 5
#define POUTA 17
#define POUTB 27

#define SERVO_PWM 0
#define FAN_PWM 1

#define SERVO_PERIOD 20000000
#define ANGLE_180 2500000
#define ANGLE_N180 200000
#define ANGLE_90 1500000

int clnt_sock = -1;

void error_handling( char *message){
 fputs(message,stderr);
 fputc( '\n',stderr);
 exit( 1);
}

static int PWMExport(int pwmnum) {
	char buffer[BUFFER_MAX];
	ssize_t bytes_written;
	int fd;

	fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in export!\n");
		return(-1);
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
	write(fd, buffer, bytes_written);
	close(fd);
    sleep(1);
    return(0);
}

static int PWMUnxport(int pwmnum) {
	char buffer[BUFFER_MAX];
	ssize_t bytes_written;
	int fd;

	fd = open("/sys/class/pwm/pwmchip0/unexport", O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in unexport!\n");
		return(-1);
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
	write(fd, buffer, bytes_written);
	close(fd);
    sleep(1);
    return(0);
}

static int PWMEnable(int pwmnum){
    static const char s_unable_str[] = "0";
    static const char s_enable_str[] = "1";

    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);

    fd = open(path, O_WRONLY);
    if(-1 == fd) {
        fprintf(stderr, "Failed to open in enable!");
        return -1;
    }

    write(fd, s_unable_str, strlen(s_unable_str));
    close(fd);

    fd = open(path, O_WRONLY);
    if(-1 == fd){
        fprintf(stderr, "Failed to open in enable!");
        return -1;
    }

    write(fd, s_enable_str, strlen(s_enable_str));
    close(fd);
    return(0);
}

static int PWMUnable(int pwmnum){
    static const char s_unable_str[] = "0";

    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);

    fd = open(path, O_WRONLY);
    if(-1 == fd) {
        fprintf(stderr, "Failed to open in unable!");
        return -1;
    }

    write(fd, s_unable_str, strlen(s_unable_str));
    close(fd);

    return(0);
}

static int PWMWritePeriod(int pwmnum, int value) {
	char s_values_str[VALUE_MAX];
	char path[VALUE_MAX];
	int fd, byte;
	

	snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/period", pwmnum);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in period!\n");
		return(-1);
	}

    byte = snprintf(s_values_str, 10, "%d", value);
	if (-1 == write(fd, s_values_str, byte)) {
		fprintf(stderr, "Failed to value! in period!\n");
        close(fd);
		return(-1);
    }
	close(fd);
	return(0);
}

static int PWMWriteDutyCycle(int pwmnum, int value) {
	char s_values_str[VALUE_MAX];
	char path[VALUE_MAX];
	int fd, byte;
	

	snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/duty_cycle", pwmnum);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open in duty_cylce!\n");
		return(-1);
	}

    byte = snprintf(s_values_str, 10, "%d", value);
	if (-1 == write(fd, s_values_str, byte)) {
		fprintf(stderr, "Failed to write value! in duty_cycle!\n");
        close(fd);
		return(-1);
    }
	close(fd); 
	return(0);
}

static int GPIOExport(int pin) {
	char buffer[BUFFER_MAX];
	ssize_t bytes_written;
	int fd;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open export for writing!\n");
		return(-1);
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return(0);
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
	return(0);
	}
}

void* led_thd()
{
	if (-1 == GPIOExport(POUT))
		exit(-1);
	if (-1 == GPIODirection(POUT, OUT))
		exit(-1);
	if(-1 == GPIOWrite(POUT, LOW))
		exit(-1);

	int str_len;
	char msg[3];
	int state = 0;
	clock_t start_t, end_t;
    double time = 0;

	while(1) {
		str_len = read(clnt_sock, msg, sizeof(msg)); //msg format : "<TYPE><VALUE>" L and I is LED, S is Servo_moter, F is Fan_moter
		if(str_len != -1 && msg[0] == 'L'){
			state = msg[1] - '0';
			//printf("state: %d\n", state);
			if(state == 1){
				start_t = clock();
				if(-1 == GPIOWrite(POUT, HIGH))
				{
					printf("LED on Error\n");
					exit(-1);
				}
			}
		}

		end_t = clock();
		time = (double)(end_t - start_t) / CLOCKS_PER_SEC;
		printf("time : %lf\n", time);
        if(time > 0.1){
			state = 0;
			if(-1 == GPIOWrite(POUT, LOW))
			{
				printf("time out L off Error\n");
				exit(-1);
			}
		}
		usleep(1000);
    }
	printf("end!!\n");
	if (-1 == GPIOUnexport(POUT))
		exit(-1);
	exit(0);
}

void* infraredRayLed_thd()
{
	if (-1 == GPIOExport(POUT2))
      exit(-1);
   if (-1 == GPIODirection(POUT2, OUT))
      exit(-1);
   if(-1 == GPIOWrite(POUT2, LOW))
      exit(-1);

   int str_len;
   char msg[3];
   int state = 0;
   clock_t start_t = 0, end_t;
   	double time = 0;

   while(1) {
        str_len = read(clnt_sock, msg, sizeof(msg)); //msg format : "<TYPE><VALUE>" L and I is LED, S is Servo_moter, F is Fan_moter
    	if(str_len != -1 && msg[0] == 'I'){
        	state = msg[1] - '0';
        	if(state == 1){
        		start_t = clock();
            	if(-1 == GPIOWrite(POUT2, HIGH)){
               		printf("I on Error\n");
               		exit(-1);
            	}
         	}         
		}
		end_t = clock();
      	time = (double)(end_t - start_t) / CLOCKS_PER_SEC;

      	printf("time : %lf\n", time);
        if(time > 0.2){
        	state = 0;
         	if(-1 == GPIOWrite(POUT2, LOW)){
            	printf("time out I off error");
            	exit(-1);
         	}
      	}
      	usleep(1000);   
    }
   if (-1 == GPIOUnexport(POUT2))
      exit(-1);
   exit(0);
}
	
void* servo_thd(){
	if(-1 == PWMExport(SERVO_PWM))
		exit(-1);
    if(-1 == PWMWritePeriod(SERVO_PWM, SERVO_PERIOD))
		exit(-1);
    if(-1 == PWMWriteDutyCycle(SERVO_PWM, ANGLE_90))
		exit(-1);
    if(-1 == PWMEnable(SERVO_PWM))
		exit(-1);

    int str_len;
	char msg[3];
	int state = 0, pre_state = 0;
	
	while(1) {
        str_len = read(clnt_sock, msg, sizeof(msg)); //msg format : "<TYPE><VALUE>" L and I is LED, S is Servo_moter, F is eFan_moter
		if(str_len == -1 || msg[0] != 'S'){
			usleep(1000);
			continue;
		}
       	
		state = msg[1] - '0';
		//printf("state : %d\n", state);
		if(state == 1 && state != pre_state){
			printf("on\n");
			if(-1 == PWMWriteDutyCycle(SERVO_PWM, ANGLE_180)){
				printf("Servo on error\n");
				exit(-1);
			}
		}
		else if(state == 0 && state != pre_state){
			printf("off\n");
			if(-1 == PWMWriteDutyCycle(SERVO_PWM, ANGLE_90)){
				printf("Servo off error\n");
				exit(-1);
			}
		}
		pre_state = state;
        usleep(1000);
    }
    exit(0);
}

void* fan_thd()
{
	if (-1 == GPIOExport(POUTA) || -1 == GPIOExport(POUTB))
		exit(-1);
	if (-1 == GPIODirection(POUTA, OUT) || -1 == GPIODirection(POUTB, OUT))
		exit(-1);
	if(-1 == GPIOWrite(POUTA, LOW) || -1 == GPIOWrite(POUTB, LOW))
		exit(-1);

	int str_len;
	char msg[3];
	int state = 0, pre_state = 0;
	
	while(1) {
        str_len = read(clnt_sock, msg, sizeof(msg)); //msg format : "<TYPE><VALUE>" L and I is LED, S is Servo_moter, F is Fan_moter
		if(str_len == -1 || msg[0] != 'F'){
			usleep(1000);
			continue;
		}
		state = msg[1] - '0';
		//printf("state : %d\n", state);
		if(state == 1 && pre_state != state){
			if(-1 == GPIOWrite(POUTA, HIGH) || -1 == GPIOWrite(POUTB, LOW)){
				printf("Fan on error\n");
				exit(-1);
			}
			pre_state = state;
		}
		else if(state == 0 && pre_state != state){
			if(-1 == GPIOWrite(POUTA, LOW) || -1 == GPIOWrite(POUTB, LOW)){
				printf("Fan off error\n");
				exit(-1);
			}	
			pre_state = state;
		}
        usleep(1000);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
	//server socket init
	int serv_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;

    if(argc!=2){
        printf("Usage : %s <port>\n", argv[0]);
    }

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(serv_sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0 , sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if(listen(serv_sock,5) == -1)
        error_handling("listen() error");

    if(clnt_sock < 0){
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if(clnt_sock == -1)
                error_handling("accept() error");
    }

	//thread init
	pthread_t p_thread[4];
    int thr_id;
    int status;

    thr_id = pthread_create(&p_thread[0], NULL, servo_thd, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    
	}
	
    thr_id = pthread_create(&p_thread[1], NULL, fan_thd, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }

	thr_id = pthread_create(&p_thread[2], NULL, led_thd, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }
	
	thr_id = pthread_create(&p_thread[3], NULL, infraredRayLed_thd, NULL);
    if(thr_id < 0){
        perror("thread create error : ");
        exit(0);
    }
	
	pthread_join(p_thread[0], (void**)&status);
    pthread_join(p_thread[1], (void**)&status);
	pthread_join(p_thread[2], (void**)&status);
	pthread_join(p_thread[3], (void**)&status);

	close(clnt_sock);
    close(serv_sock);

    PWMUnxport(SERVO_PWM);
    
    if(-1 == GPIOUnexport(POUTA) || -1 == GPIOUnexport(POUTB)){
        return(-1);
    }
    return 0;
}
