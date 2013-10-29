#include "SPI.h" // SPI library (stock)
#include "Wire.h" // TwoWire library (stock) sda-scl
#include "RTClib.h" // Real time clock library (original library, slightly rewritten to fit the Due)
#include "SPI.h"
#include "MI0283QT9Due.h" // LCD library (quite much had to be rewritten). Pins were changed, usage of Software SPI... quite a mess but it works great
#include "ADS7846Due.h" // Touch controller (same story as at the lcd library)
#include "Adafruit_TSL2561.h" // light sensor library (had to be rewritten in some parts to fit the Due) functions were added to correct the lux-output that wasn't always correct with the stock library
#include "OneWire.h" // OneWire (I don't know, but I guess it's the stock library)
#include "avr/dtostrf.h" // optional library to workaround some compatibility problems with the Due
#include "math.h" // standard C-maths library
#include "SD.h" // SD reader (stock)
#include "WiFi.h" // (stock)
#include "time.h" // standard C-time library
#include "math.h"

#define ledW	(11)
#define ledR	(12)
#define ledG	(13)
#define Poti	(A0)
#define Button	(A1)
#define Temp	(A2)
#define Wat		(A3)

#define shift8to32(a,b,c,d) (d << 24) | (c << 16) | (b << 8) | a
#define shift8to16(a,b) (b << 8) | a

MI0283QT9 lcd;
ADS7846 tp;
RTC_DS1307 rtc;
OneWire tempsen(Temp);
Adafruit_TSL2561 lightsen(TSL2561_ADDR_FLOAT,12345);
File bitmap;
WiFiClient client(0);

float valueWater=0, valueTemp=0, valueLight=0, valueLightIR=0; // sensor values (avaredged)
uint32_t valueTime=0,valueCounter[3]={0,0,0};
uint16_t counter=1,potValue=0; // vlauecounter 0:Water 1:Temp 2:Light/IR 
uint8_t cursorY=1,cursorX=0;
bool buttonValue=0,logging=0,wifiConnection,mossstatus=0,firstreceive=1;

int waitForTap(){
	tp.service();
	while(tp.getPressure()==0) // Idle until the touchscreen is pressed to continue
	{
		delay(20);
		tp.service();
		if(digitalRead(Button)==0)
			return 1;
	}
	return 0;
}

void continueText(String drawtext, int cursY=cursorY-1, int cursX=cursorX,int maxlen=38){
	maxlen=maxlen-cursX;

	lcd.drawText(15+cursX*8,cursY*15,drawtext,1,COLOR_WHITE,COLOR_BLACK);

	cursorX=drawtext.length()+1;
}

void drawText(String drawtext="", int cursY=cursorY,int maxlen=38){
	continueText(drawtext,cursY,0);
	cursorY=cursY+1;
}

void OpenBMPFile(char *file, int16_t x, int16_t y)
{
  uint8_t pad;
  uint8_t buf[54]; //read buf (min. size = sizeof(BMP_DIPHeader))
  int16_t width, height, w, h;

  if(!SD.exists(file))
  {
    Serial.println(String(file)+"not found");
    return;
  }
  else
	  bitmap = SD.open(file, FILE_READ);
  if(!bitmap.available())
  {
    Serial.println("not reading image file");
	return;
  }
  else
	  Serial.println("reading image file");

  //BMP Header
  for(int i=0;i<14;i++)
  {
	  buf[i]=bitmap.read();
  }

  if((buf[0] == 'B') && (buf[1] == 'M') && (buf[10] == 54))
  {
	  Serial.println("Bitmap recognized");
    //BMP DIP-Header
	for(int i=14;i<54;i++)
		buf[i]=bitmap.read();
	if(
	Serial.println(shift8to32(buf[14],buf[15],buf[16],buf[17]))&& // size
	Serial.println(shift8to16(buf[28],buf[29]))&& // bitspp
	Serial.println(shift8to32(buf[30],buf[31],buf[32],buf[33]))) // compress
	{
		Serial.println("DIP recognized");
      //BMP data (1. pixel = bottom left)
      width  = shift8to32(buf[18],buf[19],buf[20],buf[21]); // width
      height = shift8to32(buf[22],buf[23],buf[24],buf[25]); // height
      pad    = width % 4; //padding (line is multiply of 4)
	  Serial.println("printing");
      if((x+width) <= lcd.getWidth() && (y+height) <= lcd.getHeight())
      {
        lcd.setArea(x, y, x+width-1, y+height-1);
        for(h=(y+height-1); h >= y; h--) //for every line
        {
			uint8_t pix[320][3];
		bitmap.read(pix,width*3);
		for(w=0; w < width; w++) //for every pixel in line
          {
            lcd.drawPixel(x+w, h, RGB(pix[w][2],pix[w][1],pix[w][0]));
          }
          if(pad)
          {
            bitmap.read(pix, pad);
          }
        }
      }
      else
      {
        lcd.drawTextPGM(x, y, PSTR("Pic out of screen!"), 1, RGB(0,0,0), RGB(255,255,255));
      }
    }
  }
  bitmap.close();
}

void initDisplay()
{
	lcd.init();
	lcd.setOrientation(180);
	lcd.led(100);
	lcd.clear(COLOR_BLACK);
}

void initSD(){
  if (!SD.begin(4)) {
    continueText("failed");
	return;
  } else {
   continueText("succeeded");; 
  }
}

void initLightSen() // initialize the light sensor
{
	lightsen.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);
	lightsen.enableAutoGain(true);	
	lightsen.begin();
}

void initWiFi()
{
	lcd.clear(COLOR_BLACK);
	cursorY=1;
	WiFi.disconnect();
	drawText("number of networks:");
	uint8_t nnet=WiFi.scanNetworks();
	continueText(String(nnet));
	for(int i=0;i<nnet;i++)
		drawText(WiFi.SSID(i));
	drawText();
	drawText("connecting to YOURWIFI..");
	for(int i=0;WL_CONNECTED!=WiFi.begin("YOURWIFI","yourpassword")&&i<3;i++)
	{
		WiFi.scanNetworks();
		drawText(String("connection failed ")+String(2-i)+String(" left.."));
	}
	if(WiFi.status()==WL_CONNECTED)
	{
		continueText(" succeeded");
		drawText("local IP address: "+String(WiFi.localIP()[0])+"."+String(WiFi.localIP()[1])+"."+String(WiFi.localIP()[2])+"."+String(WiFi.localIP()[3]));
	}
	else
		drawText("connection failed");
	logging=0;
	drawText();
	drawText("continue to data display & logging ..");
	waitForTap();
	lcd.clear(COLOR_BLACK);
	drawText("button: off",6);
	continueText("logging: off",1,25);
	String s="potentiometer: "+String((((float)analogRead(Poti))*0.0977517106549))+"   ";
	drawText(s,7);
}

void updateTP(){
	tp.service();
	drawText("x: "+String(tp.getX())+" y: "+String(tp.getY())+"     ",2);
}

void updateButtonAndPoti(){
	bool butv=(!digitalRead(Button));
	if(buttonValue!=butv)
	{
		if(wifiConnection==0)
		{
			initWiFi();
			counter=1;
			return;
		}
		if(butv)
		{
			logging=!logging;
			if(logging)
			{
				continueText("logging: on ",1,25);
				WiFiSend();
				counter=1;
			}
			else
			{
				continueText("logging: off",1,25);
				WiFiReceive();
				counter=1025;
			}
		}
		if(!butv)
			drawText("button: off",6);
		else
			drawText("button: on ",6);
		buttonValue=!buttonValue;
	}
	uint16_t potv=analogRead(Poti);
	if(potValue!=potv)
	{
		String s="potentiometer: "+String((((float)potv)*0.0977517106549))+"   ";
		drawText(s,7);
		potValue=potv;
	}

}

void updateRTC() // update the time from the clock
{
	DateTime tm=rtc.now();
	if(valueTime != tm.unixtime()) // Only refresh the values on the display if they changed (when the seconds value has changed since the last call)
	{
		char s[30];
		sprintf(s,"%02i:%02i:%02i %02i.%02i:%04i",tm.hour(),tm.minute(),tm.second(),tm.day(),tm.month(),tm.year());
		drawText(s,1);
	}
	valueTime = tm.unixtime();
}

void updateSenLight() // get current light values
{
	uint16_t lsbroadband,lsinfrared; // definition
	float flsbroadband,flsinfrared;
	lightsen.getLuminosity(&lsbroadband,&lsinfrared); // the full function is implemented in the library
	flsbroadband=((float)lsbroadband*lightsen.getMultiplier());
	flsinfrared=((float)lsinfrared*lightsen.getMultiplier()); // getMultipier is a function I added. I believe the developers just missed something out in their lib
	valueLight=(((float)valueCounter[2]*valueLight)+(float)flsbroadband)/(float)(valueCounter[2]+1.0); // Calculate the avaredged values
	valueLightIR=(((float)valueCounter[2]*valueLightIR)+(float)flsinfrared)/(float)(valueCounter[2]+1.0);
	valueCounter[2]++;
	drawText(String("visible  light: ")+String(flsbroadband,1)+"    ",4);
	drawText(String("infrared light: ")+String(flsinfrared,1)+"   ",5);
	float lratio=flsinfrared/flsbroadband;
	continueText(String("ratio: ")+String(lratio,4),5,25);
	uint8_t ledvw=-0.0000307574013*pow((double)log(flsbroadband)*25.5,3.0)+0.0117647055*pow((double)log(flsbroadband)*25.5,2.0);
	analogWrite(ledW,ledvw);
	uint8_t ledvr=-0.0000307574013*pow((lratio-0.2)*425.0,3.0)+0.0117647055*pow((lratio-0.2)*425.0,2.0);
	analogWrite(ledR,ledvr);
	uint8_t ledvg=-0.0000307574013*pow(255.0-((lratio-0.2)*425.0),3.0)+0.0117647055*pow(255.0-((lratio-0.2)*425.0),2.0);
	analogWrite(ledG,ledvg);
	bool alive;
	if(flsinfrared<30.0&&flsbroadband>50)
		alive=1;
	else
		alive=0;
	mossstatus=alive;
	}

}

void updateSenWater() // get current water values
{
	float water = (0.0977517106549*(float)analogRead(Wat)); // The water sensor doesn't really work lineary. Here is my approximation for correction.
	valueWater=((valueCounter[0]*valueWater)+water)/(valueCounter[0]+1); // avaredged value
	valueCounter[0]++;
	String s="water level: "+String(water)+"%   ";
	drawText(s,6);
}

void updateSenTemp(){ // get current temperature data
  byte data[12]; // array the incoming values should be stored to
  byte addr[8] = {0x28,0x1D,0xF1,0xB0,0x04,0x00,0x00,0x6B}; // OneWire temperature sensor address
  
	tempsen.reset(); // build up a connection and request data
	tempsen.select(addr);
	tempsen.write(0x44,1);
	tempsen.depower();
	tempsen.reset();
	tempsen.select(addr);
	tempsen.write(0xBE);
	for(byte z = 0; z < 9; z++) data[z] = tempsen.read(); // read out the return and store it byte per byte to the array
	unsigned int raw = (data[1] << 8) | data[0];
	valueTemp=(float)((valueCounter[1]*valueTemp)+((float)raw / 16.0)-1.5)/(float)(valueCounter[1]+1); // Take the current temperature into account for the avaredged value
	valueCounter[1]++;
	String s="temperature: "+String(((float)raw / 16.0)-1.5)+"    ";
	drawText(s,3);
}

void updateWiFiConnection(){
	uint8_t wstat;
	if(!client.connected())
		client.stop();
	if(WiFi.status()==WL_CONNECTED)
		wstat=1;
	else
		wstat=0;
	if(wstat!=wifiConnection)
	{
		logging=0;
		continueText("logging: off",1,25);
		wifiConnection=wstat;
		if(wstat)
		{
			continueText("WiFi:  con",2,25);
			continueText("             ",3,25);
		}
		else
		{
			drawText("                              ",10);
			drawText("                              ",11);
			drawText("                              ",12);
			drawText("                              ",13);
			drawText("                              ",14);
			continueText("WiFi:  dis",2,25);
			continueText("button: recn",3,25);

		}
	}
}

void WiFiSend()
{
	if(!client.connected())
		client.stop();
	drawText("                              ",10);
	drawText("                              ",11);
	drawText("                              ",12);
	drawText("                              ",13);
	drawText("                              ",14);
	drawText("client connecting..           ",10);
	cursorX-=11;
	if(!wifiConnection)
	{
		updateWiFiConnection();
		return;
	}
	if(client.connect("your.website.here",80))
	{
		continueText("connected");
		drawText("sending data..",11);

		client.print("GET http://your.website.here/save.php?key=yourkey");
		client.print(String("&water=")+String(valueWater,3) + "&temp="+String(valueTemp,3) + "&light1="+String(valueLight,3) + "&light2="+String(valueLightIR,3) +"&time="+String(valueTime));
		client.println(/*" HTTP/1.1"*/);
		client.println("Host: your.website.here");
		client.println("User-Agent: arduino-ethernet");
		client.println("Connection: close");
		client.println();

		continueText("done");
		for(int i=0;i<3;i++)
			valueCounter[i]=0; // Reset all counters and values for the avaredging process
		valueWater=0.0;
		valueTemp=0.0;
		valueLight=0.0;
		valueLightIR=0.0;
		firstreceive=1;
	}
	else
	{
		client.stop();
		continueText("failed");
		firstreceive=0;
		return;
	}
}

void WiFiReceive()
{
	int i=0;
	if(firstreceive)
	{
		drawText("                              ",10);
		drawText("                              ",11);
		drawText("                              ",12);
		drawText("                              ",13);
		drawText("                              ",14);
		drawText("listening..",10);
		firstreceive=0;
	}
	String response = "";
	while(client.connected() && i++<200)
	{
		if(client.available())
		{
			while(client.available())
			{
				response.concat((char)client.read());	
			}
			i=200;
			client.flush();
		}
		else
			delay(1);
	}
	if(i==200)
		Serial.println();
	Serial.print(response);
	drawText(String(response),10);
}

void setup()
{
	initDisplay();
	Serial.begin(9600);
	Serial.println("serial ready");
	drawText("WiFi initialized");
	drawText("serial ready");
	drawText("init SD card reader.. ");
	initSD();
	drawText("init some pins");
	analogReadResolution(10);
	pinMode(Poti,INPUT);
	pinMode(Button,INPUT);
	pinMode(Temp,INPUT);
	pinMode(Wat,INPUT);
	pinMode(ledW,OUTPUT);
	pinMode(ledR,OUTPUT);
	pinMode(ledG,OUTPUT);
	digitalWrite(Poti,HIGH);
	digitalWrite(Button,HIGH);
	analogWrite(ledW,255);
	analogWrite(ledR,255);
	analogWrite(ledG,255);
	drawText("init touchpad");
	tp.init();
	tp.setOrientation(180);
	drawText("init real time clock");
	rtc.begin();
	drawText("init light sensor");
	initLightSen();
	drawText("done");
	drawText();
	drawText("Tap for WiFi search and connection\n..");
	drawText("Press button to proceed without WiFi..");
	if(waitForTap()==0)
	{
		initWiFi();
	}
	else
	{
		lcd.clear(COLOR_BLACK);
		drawText("button: off",6);
		continueText("logging: off",1,25);
		String s="potentiometer: "+String((((float)analogRead(Poti))*0.0977517106549))+"   ";
		drawText(s,7);
		wifiConnection=0;
		continueText("WiFi:   dis",2,25);
		continueText("button: recn",3,25);
	}
}

void loop()
{
	if(counter%8==1)				updateButtonAndPoti();
	if(counter%8==5)				updateTP();
	if(counter%64==19)				updateRTC();
	if(counter%64==37)				updateSenWater();
	if(counter%128==11)				updateSenLight();
	if(counter%128==73)				updateSenTemp();
	if(counter%256==7)				updateWiFiConnection();
	if(counter%4096==0&&logging)	WiFiSend();
	if(counter%512==320&&logging)	WiFiReceive();
	counter++;
	delay(1);
}
