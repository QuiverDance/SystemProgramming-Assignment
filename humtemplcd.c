#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wiringPiSPI.h>
#include <unistd.h>
#include <wiringPiI2C.h>

#define MAX_TIME 83
#define DHT11PIN 7

int dht11_val[5] = {0,0,0,0,0};


// Define some device parameters
#define I2C_ADDR   0x27 // I2C device address

// Define some device constants
#define LCD_CHR  1 // Mode - Sending data
#define LCD_CMD  0 // Mode - Sending command

#define LINE1  0x80 // 1st line
#define LINE2  0xC0 // 2nd line

#define LCD_BACKLIGHT   0x08  // On
// LCD_BACKLIGHT = 0x00  # Off

#define ENABLE  0b00000100 // Enable bit

void lcdInit(void);
void lcd_bytes(int bits, int mode);
void lcd_toggle_enable(int bits);
void ClrLcd(void); // clr LCD return home
void lcdLine(int line); //move cursor
void println(const char *s);
int fd;  // seen by all subroutines



void dht11_read_val()
{

   uint8_t laststate = HIGH;
   uint8_t counter =0;
   uint8_t j=0, i;
   char buffer[20];
   
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
       delayMicroseconds(5.1);

        if((i>=4) && (i%2 ==0)) {
          dht11_val[j/8] <<= 1;                     
          if(counter > 20) {
             dht11_val[j/8] |= 1;
          }
          j++;
       }
    }   

   if (j >= 40 &&(dht11_val[4] == ((dht11_val[0] + dht11_val[1] + dht11_val[2] + dht11_val[3]) & 0xFF))) {
      printf("humidity = %d.%d %% Temperature = %d.%d *C \n", dht11_val[0], dht11_val[1], dht11_val[2], dht11_val[3]) ;
      
      lcdLine(LINE1);
      println("hum =");
      sprintf(buffer, " %d.%d %% ", dht11_val[0], dht11_val[1]);
      println(buffer);


      lcdLine(LINE2);
      println("temp=");
      sprintf(buffer, " %d.%d *C", dht11_val[2], dht11_val[3]);
      println(buffer);
    }
   else printf("Data get failed\n") ;

   for(int idx = 0; idx < 5; idx++)
        dht11_val[idx] = 0;
}

int main(void) {
   if (wiringPiSetup () == -1) exit (1);

   fd = wiringPiI2CSetup(I2C_ADDR);

   lcdInit(); // setup LCD

   if(wiringPiSetup() == -1 ) {
      printf("return -1 error");   
      return -1;
   }

   printf("start dht11_read_val !!!\n");   
   while(1) {
       dht11_read_val();
       sleep(1);
   }
   
   return 0;
}


// clr lcd go home loc 0x80
void ClrLcd(void)   {
  lcd_bytes(0x01, LCD_CMD);
  lcd_bytes(0x02, LCD_CMD);
}

// go to location on LCD
void lcdLine(int line)   {
  lcd_bytes(line, LCD_CMD);
}


void println(const char *s)   {

  while ( *s ) lcd_bytes(*(s++), LCD_CHR);
}

void lcd_bytes(int bits, int mode)   {

  //Send byte to data pins
  // bits = the data
  // mode = 1 for data, 0 for command
  int bits_high;
  int bits_low;
  // uses the two half byte writes to LCD
  bits_high = mode | (bits & 0xF0) | LCD_BACKLIGHT ;
  bits_low = mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT ;

  // High bits
  wiringPiI2CReadReg8(fd, bits_high);
  lcd_toggle_enable(bits_high);

  // Low bits
  wiringPiI2CReadReg8(fd, bits_low);
  lcd_toggle_enable(bits_low);
}

void lcd_toggle_enable(int bits)   {
  // Toggle enable pin on LCD display
  delayMicroseconds(500);
  wiringPiI2CReadReg8(fd, (bits | ENABLE));
  delayMicroseconds(500);
  wiringPiI2CReadReg8(fd, (bits & ~ENABLE));
  delayMicroseconds(500);
}


void lcdInit()   {
  // Initialise LCD display
  lcd_bytes(0x33, LCD_CMD); // Initialise
  lcd_bytes(0x32, LCD_CMD); // Initialise
  lcd_bytes(0x06, LCD_CMD); // Cursor move direction
  lcd_bytes(0x0C, LCD_CMD); // 0x0F On, Blink Off
  lcd_bytes(0x28, LCD_CMD); // Data length, number of lines, font size
  lcd_bytes(0x01, LCD_CMD); // Clear display
  delayMicroseconds(500);
}