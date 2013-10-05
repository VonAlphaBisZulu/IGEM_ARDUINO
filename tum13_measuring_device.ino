#include "SPI.h" // SPI library (stock)
#include "Wire.h" // TwoWire library (stock) sda-scl
#include "DS1307.h" // Real time clock library (original library, slightly rewritten to fit the Due)
#include "MI0283QT9.h" // LCD library (quite much had to be rewritten). Pins were changed, usage of Software SPI... quite a mess but it works great
#include "ADS7846.h" // Touch controller (same story as at the lcd library)
#include "Adafruit_TSL2561.h" // light sensor library (had to be rewritten in some parts to fit the Due) functions were added to correct the lux-output that wasn't always correct with the stock library
#include "OneWire.h" // OneWire (I don't know, but I guess it's the stock library)
#include "avr/dtostrf.h" // optional library to workaround some compatibility problems with the Due
#include "math.h" // standard C-maths library
#include "SD.h" // SD reader (stock)
#include "WiFi.h" // (stock)
#include "time.h" // standard C-time library

DS1307 *rtc; // real time clock
MI0283QT9 *lcd;
ADS7846 *tscr; // the "touch" of the touchscreen
OneWire *tempsen;
Adafruit_TSL2561 *lightsen;
WiFiClient *client;
boolean lastConnected, firstrun=1;
char c;

uint8_t sea; // current value of seconds
uint16_t counter=0,valueCounter[3]; // vlauecounter 0:Water 1:Temp 2:Light/IR
float valueWater=0, valueTemp=0, valueLight=0, valueLightIR=0; // these are the sensor values that are avaredged over each time interval between the sending processes
uint32_t valueTime=0;

void initDisplay() // initialize the lcd display
{
	lcd->led(100); // lights on 100%
	lcd->setOrientation(180); // flip screen orientation to 180° (optional)
	lcd->clear(COLOR_WHITE);
	tscr->service();
	if(tscr->getPressure()>5) // Calibration on pressure
	{
		tscr->doCalibration(lcd);
	}
	lcd->printClear();
}

void initLightSen() // initialize the light sensor
{
	lightsen->setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);
	lightsen->enableAutoGain(true);	
	lightsen->begin();
}

void initWiFi() // initialize the WIFI
{
	char buf[64];
	uint8_t nonet;
	IPAddress ip;
	nonet=WiFi.scanNetworks();
	sprintf(buf,"Number of Networks: %2i",nonet); // Output of up to 6 networks in your range + display
	lcd->drawText(10,15,buf,1,COLOR_BLACK,COLOR_WHITE); // All display outputs need to be written into a String or char-array before they can be passed to the lcd controller
	sprintf(buf,WiFi.SSID(0));
	lcd->drawText(10,30,buf,1,COLOR_BLACK,COLOR_WHITE);
	sprintf(buf,WiFi.SSID(1));
	lcd->drawText(10,45,buf,1,COLOR_BLACK,COLOR_WHITE);
	sprintf(buf,WiFi.SSID(2));
	lcd->drawText(10,60,buf,1,COLOR_BLACK,COLOR_WHITE);
	sprintf(buf,WiFi.SSID(3));
	lcd->drawText(10,75,buf,1,COLOR_BLACK,COLOR_WHITE);
	sprintf(buf,WiFi.SSID(4));
	lcd->drawText(10,90,buf,1,COLOR_BLACK,COLOR_WHITE);

	WiFi.begin("YOURWIFI","YOURPASSWORD"); // Connection to a WIFI (feel free to change these settings)
	strcpy(buf,WiFi.SSID());
	lcd->drawText(10,120,buf,1,COLOR_BLACK,COLOR_WHITE); // Output of the SSID (WIFI name) if the WiFi Shield was connected successfully
	ip=WiFi.localIP();
	sprintf(buf,"%3i.%3i.%3i.%3i",ip[0],ip[1],ip[2],ip[3]); // Show the Arduino's IP address
	lcd->drawText(10,135,buf,1,COLOR_BLACK,COLOR_WHITE);
	while(tscr->getPressure()==0) // Idle until the touchscreen is pressed to continue
	{
		delay(20);
		tscr->service();
	}
	lcd->clear(COLOR_WHITE);
}

void initPhoto() // Initialize the photodiodes
{
	analogReadResolution(12); // Set the reading resolution and the pinmode
	pinMode(A0,INPUT);
	pinMode(A1,INPUT);
	pinMode(A1,INPUT);
}

void updateSenTemp(){ // get current temperature data
  byte data[12]; // array the incoming values should be stored to
  byte addr[8] = {0x28,0x1D,0xF1,0xB0,0x04,0x00,0x00,0x6B}; // OneWire temperature sensor address
  
	tempsen->reset(); // build up a connection and request data
	tempsen->select(addr);
	tempsen->write(0x44,1);
	tempsen->depower();
	tempsen->reset();
	tempsen->select(addr);
	tempsen->write(0xBE);
 
	for(byte z = 0; z < 9; z++) data[z] = tempsen->read(); // read out the return and store it byte per byte to the array
 
	unsigned int raw = (data[1] << 8) | data[0];
	char buf[20],temp[8];
	dtostrf(((float)raw / 16.0)-1.5,4,2,temp);// conversion of the raw data into degrees Celsius
	valueTemp=(float)((valueCounter[1]*valueTemp)+((float)raw / 16.0)-1.5)/(float)(valueCounter[1]+1); // Take the current temperature into account for the avaredged value
	valueCounter[1]++;
	strcat(temp,"C  ");
	sprintf(buf, "temperature: "); // Display output
	strcat(buf,temp);
	lcd->drawText(10, 90, buf, 1, COLOR_BLACK, COLOR_WHITE);
}

void updateSenLight() // get current light values
{
	char buf[32];
	uint16_t lsbroadband,lsinfrared; // definition
	lightsen->getLuminosity(&lsbroadband,&lsinfrared); // the full function is implemented in the library
	lsbroadband=((float)lsbroadband*lightsen->getMultiplier());
	lsinfrared=((float)lsinfrared*lightsen->getMultiplier()); // getMultipier is a function I added. I believe the developers just missed something out in their lib
	sprintf(buf, "complete:  %5i lux ",lsbroadband);
	lcd->drawText(10, 60, buf, 1, COLOR_BLACK, COLOR_WHITE); // Display output
	sprintf(buf, "infrared:  %5i lux ",lsinfrared);
	lcd->drawText(10, 75, buf, 1, COLOR_BLACK, COLOR_WHITE);
	valueLight=(((float)valueCounter[2]*valueLight)+(float)lsbroadband)/(float)(valueCounter[2]+1.0); // Calculate the avaredged values
	valueLightIR=(((float)valueCounter[2]*valueLightIR)+(float)lsinfrared)/(float)(valueCounter[2]+1.0);
	valueCounter[2]++;
}

void updateSenWater() // get current water values
{
	char buf[20],wtr[7];
	float water = (0.0009765625*(float)analogRead(A5)); // The water sensor doesn't really work lineary. Here is my approximation for correction.
	water = 1.0-water;
	water = -pow(water,7.0);
	water = pow(2.0,water);
	water = 200.0*(1-water);
	if(water>0)
		dtostrf(water,4,2,wtr);
	else
	{
		sprintf(wtr,"0.0");
		water=0.0;
	}
	valueWater=((valueCounter[0]*valueWater)+water)/(valueCounter[0]+1); // avaredged value
	valueCounter[0]++;
	sprintf(buf,"Wasser: "); // Display output
	strcat(buf,wtr);
	strcat(buf,"  ");
	lcd->drawText(10, 105, buf, 1, COLOR_BLACK, COLOR_WHITE);
}

void updateTouch() // Register if someone touched the display
{
	uint16_t pos;
	char buf[7];
	tscr->service(); // some kind of flush-function. Gets the touchpads current state.
	pos=tscr->getX();
	sprintf(buf,"X=%04i",pos);
	lcd->drawText(10,30,buf,1,COLOR_BLACK,COLOR_WHITE); // Display the x and y touch position
	pos=tscr->getY();
	sprintf(buf,"Y=%04i",pos);
	lcd->drawText(10,45,buf,1,COLOR_BLACK,COLOR_WHITE);
	tscr->service();
}

void updateRTC() // update the time from the clock
{
	uint8_t se,mi,ho,da,mo;
	uint16_t yr;
	uint32_t unixtime;
	rtc->get(&se,&mi,&ho,&da,&mo,&yr);
	if(sea!=se) // Only refresh the values on the display if they changed (when the seconds value has changed since the last call)
	{
		char buf[24];
		sprintf(buf,"%02i:%02i:%02i %02i.%02i:%04i",ho,mi,se,da,mo,yr);
		lcd->drawText(10,15,buf,1,COLOR_BLACK,COLOR_WHITE);
		sea=se;
	}
	struct tm t; // Calculate the unixtime from year, month and so on
	time_t t_of_day;
	t.tm_year=yr-1900;
	t.tm_mon=mo-1;
	t.tm_mday=da;
	t.tm_hour=ho;
	t.tm_min=mi;
	t.tm_sec=se;
	t_of_day = mktime(&t); 
	valueTime=t_of_day; // pass the unixtime to the global variable
}

void updatePhoto(){ // update the photosensors (just an analogue readout)
	char buf[5];
	sprintf(buf,"%04i",analogRead(A0));
	lcd->drawText(10,120,buf,1,COLOR_BLACK,COLOR_WHITE); // Display output
	sprintf(buf,"%04i",analogRead(A1));
	lcd->drawText(10,135,buf,1,COLOR_BLACK,COLOR_WHITE);
	sprintf(buf,"%04i",analogRead(A2));
	lcd->drawText(10,150,buf,1,COLOR_BLACK,COLOR_WHITE);
}

void WifiDatasend(){
	char key[] = "YOURKEY",url[] = "http://igem.yourwebsite.com/save.php"; // key and url to your php-save-script (feel free to change)
	if(firstrun)
	{
		delay(1000); // fix, because there are no good values on the first run
		firstrun=0;
		return;
	}
	if (client->connect("igem.yourwebsite.com", 80)) // build up a conntection to the server on port 80 (feel free to change)
	{
		Serial.println("Connected, Sending Data...");
		client->println("GET " + String(url) + "?key=" + String(key) + "&water="+String(valueWater,3) + "&temp="+String(valueTemp,3) + "&light1="+String(valueLight,3) + "&light2="+String(valueLightIR,3) +"&time="+String(valueTime));
		Serial.println("GET " + String(url) + "?key=" + String(key) + "&water="+String(valueWater,3) + "&temp="+String(valueTemp,3) + "&light1="+String(valueLight,3) + "&light2="+String(valueLightIR,3) +"&time="+String(valueTime));
		client->println("Connection: close");
		Serial.println("Connection: close");
	}
	else
	{
		Serial.println("***** CONNECTION FAILED *****");
	}
	for(int i=0;i<3;i++)
		valueCounter[i]=0; // Reset all counters and values for the avaredging process
	valueWater=0.0;
	valueTemp=0.0;
	valueLight=0.0;
	valueLightIR=0.0;
}

void WifiDataReceive()
{
	int i=0;
	while (client->connected() && i++<200)              // Read out the servers response and pass it to the Serial output (needs connection to a PC)
	{
		if(client->available())
		{
			while (client->available())
			{
				char response = client->read();	
				Serial.print(response);
			}
			i=200;
		}
		else
			delay(1);
	}
	if(i==200)
		Serial.println();
}

// Here are the main functions that are called by the arduino.
// All devices had to be defined as pointers and initialized in the setup function due to compiler issues with the Arduino Due.
// The usage of the Arduino Due isn't quite established and the arduino software has still some room for improvement here.
// Setup is called once at the startup and at each reset
// All called functions are commented and explained in detail above ;)

void setup()
{
	lightsen = new Adafruit_TSL2561(TSL2561_ADDR_FLOAT,12345);
	lcd = new MI0283QT9();
	rtc = new DS1307();
	tscr = new ADS7846();
	tempsen = new OneWire(14);
	client = new WiFiClient();
	pinMode(A5,INPUT); // Pin A5 is an analogue input

	lastConnected = false;
	Serial.begin(9600);

	SD.begin(4);
	rtc->start();
	lcd->init();
	tscr->init();
	initDisplay();
	initLightSen();
	initPhoto();
	initWiFi();
}


// The if(counter%xx==0) code functions as some kind of process handler. Some functions should be called more often than others.
// at each run there is a delay of 1ms and a value of '1' is added to the counter. Most of the functions hardly take any time.
// But some are quite time-consuming.
// The % operator is the modulo. At counter%7==0 the function is called every 7th run.
// For example the functions WifiDatasend and -Receive, which don't have to be called that often. They are executed every 1000th/5000th time.

void loop()
{
	if(counter%29==0)	updateRTC();
	if(counter%7==0)	updateTouch();
	if(counter%503==0)	updateSenTemp();
	if(counter%197==0)  {updateSenLight();updatePhoto();};
	if(counter%457==0)	updateSenWater();
	if(counter%5000==0)	WifiDatasend();
	if(counter%1000==1)	WifiDataReceive();
	if(counter%5000==4300) // Around 700 ms before the data will be sent the Arduino breaks up the old connection to the server
	{
		client->stop();
		Serial.println("Done.");
		client->flush();
	}
	counter++;
	delay(1);
}
