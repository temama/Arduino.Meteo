#include <LiquidCrystal.h>
#include <DHT.h>
#include <Wire.h>
#include <SparkFunBME280.h>

//code version
#define VERSION 0.2

// states (screens)
#define TMP_HUM_NOW 0
#define PRESS_NOW 1
#define TMP_HUM_AVG 10
#define PRESS_AVG 11
#define OPT_GEN 50
#define OPT_BACKLIGHT_TIME 51
#define OPT_AVG_TIME 52

// LCD
#define LCD_LIGHT_PIN A3
#define LCD_LIGHT_TIME 180000 // 3 min
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);
bool isLcdLightOn = true;
bool screenChanged = false;

// DHT
#define DHTPIN 6
#define DHTTYPE DHT22
#define DHT_CHECK_INTERVAL 2000 // 2 sec
DHT dht(DHTPIN, DHTTYPE);
float hum; //humidity
float tmp; //temperature 
float hic; //health index
bool dhtOk = true;
unsigned long lastDhtCheck = 0;

// BME
#define BME_CHECK_INTERVAL 2000 // 2 sec
BME280 bme;
float prs; //pressure
float alt; //altitude
bool bmeOk = true;
unsigned long lastBmeCheck = 0;

// Button
#define BTNPIN 2
#define BTN_BOUNCE_TIME 200 // 100 ms 
#define BTN_DOUBLECLICK_MIN 201
#define BTN_DOUBLECLICK_MAX 600
volatile unsigned long lastBtnClick = 0;
volatile bool btnHandled = true;
volatile bool btnDblHandled = true; // Double click

// Common
int state = TMP_HUM_NOW;
bool viewChanged = true;

// Avarages
#define PUBLISH_INTERVAL 60000
unsigned long lastPublish = 0;

//testing
//#define RFRSH 1000
//char text[16];

void setup() {
  delay(100);
  
  initLCD();
  initDHT();
  initBME();
  initButton();
  
  Serial.begin(9600);
  delay(2000);

  //initial read sensors data
  readDht(0);
  readBme(0);
  
  screenChanged = true;
}

void initLCD()
{
  // Set the lcd display backlight anode pin as an output
  pinMode(LCD_LIGHT_PIN, OUTPUT);
  // Set the lcd display backlight anode pin to high - lcd light on
  digitalWrite(LCD_LIGHT_PIN, HIGH);
  isLcdLightOn = true;
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // init message
  lcd.setCursor(0, 0);
  lcd.print("Firmware v");
  lcd.print(VERSION);
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  lastBtnClick = millis(); // to control backlight
}

void initDHT()
{
  dht.begin();  
}

void initBME()
{
  bme.settings.commInterface = I2C_MODE;
  bme.settings.I2CAddress = 0x76;
  //renMode can be:
  //  0, Sleep mode
  //  1 or 2, Forced mode
  //  3, Normal mode
  bme.settings.runMode = 2; //Forced mode
  //tStandby can be:
  //  0, 0.5ms
  //  1, 62.5ms
  //  2, 125ms
  //  3, 250ms
  //  4, 500ms
  //  5, 1000ms
  //  6, 10ms
  //  7, 20ms
  bme.settings.tStandby = 5;
  //filter can be off or number of FIR coefficients to use:
  //  0, filter off
  //  1, coefficients = 2
  //  2, coefficients = 4
  //  3, coefficients = 8
  //  4, coefficients = 16
  bme.settings.filter = 0;
  
  //tempOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.tempOverSample = 1;

  //pressOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.pressOverSample = 1;

  //humidOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.humidOverSample = 1;

  delay(10);
  bmeOk = bme.begin();
}

void initButton()
{
  // Button interrupt
  pinMode(BTNPIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTNPIN), btnClick, RISING);
}

void loop() {
  unsigned long timeStamp = millis();
  
  // handle button click
  handleBtn(timeStamp);
  
  // read sensors data
  readDht(timeStamp);
  readBme(timeStamp);
  
  // lcd processing
  processLcd(timeStamp);
  
  // computations
  computeAvarages(timeStamp);

  // publish data to serial port
  publishToSerial(timeStamp);
  
  delay(20);
}

void handleBtn(unsigned long timeStamp)
{
  if (!btnDblHandled)
  {
    if (!isLcdLightOn)
    {
      digitalWrite(LCD_LIGHT_PIN, HIGH);
      isLcdLightOn = true;
    }
    else
    {
      processClick(timeStamp, true);
      screenChanged = true;
    }
    
    btnDblHandled = true;
    btnHandled = true;    
    return;
  }
  else if (!btnHandled and 
      timeStamp - lastBtnClick > BTN_DOUBLECLICK_MAX) //waiting for double-click
  {    
    if (!isLcdLightOn)
    {
      digitalWrite(LCD_LIGHT_PIN, HIGH);
      isLcdLightOn = true;
    }
    else
    {
      processClick(timeStamp, false);
      screenChanged = true;
    }
    
    btnHandled = true;
  }
}

void processClick(unsigned long timeStamp, bool dbl)
{
  if (state == TMP_HUM_NOW)
  {
    if (dbl) state = TMP_HUM_AVG;
    else state = PRESS_NOW;
    viewChanged = true; 
  }
  else if (state == PRESS_NOW)
  {
    if (dbl) state = PRESS_AVG;
    else state = TMP_HUM_NOW;
    viewChanged = true;
  }
  else if (state == TMP_HUM_AVG)
  {
    if (dbl) state = OPT_GEN;
    else state = PRESS_AVG;
    viewChanged = true;
  }
  else if (state == PRESS_AVG)
  {
    if (dbl) state = OPT_GEN;
    else state = TMP_HUM_AVG;
    viewChanged = true; 
  }
  else if (state == OPT_GEN)
  {
    if (dbl) 
    {
      state = TMP_HUM_NOW;
      viewChanged = true;
    }
    //else state = TMP_HUM_AVG; 
  }
}

void processLcd(unsigned long timeStamp)
{
  if (timeStamp - lastBtnClick > LCD_LIGHT_TIME)
  {
    digitalWrite(LCD_LIGHT_PIN, LOW);
    isLcdLightOn = false;
  }
  
  if (!screenChanged)
    return;

  if (viewChanged == true)
  {
    lcd.clear();
    viewChanged = false;
  }
  
  if (state == TMP_HUM_NOW)
  {
    displayTempHum(true);
  }
  else if (state == PRESS_NOW)
  {
    displayPressure(true);
  }
  else if (state == TMP_HUM_AVG)
  {
    displayTempHum(false);
  }
  else if (state == PRESS_AVG)
  {
    displayPressure(false);
  }
  else if (state == OPT_GEN)
  {
    displayOptions();
  }
    
  screenChanged = false;
}

void displayTempHum(bool now)
{  
   float t, h, i;
   if (now)
   {
    t=tmp;
    h=hum;
    i=hic;
   }
   else
   {
    t=0;
    h=0;
    i=0;
   }
   
    if (dhtOk)
    {
      lcd.setCursor(0, 0);
      lcd.print("T:");
      if (t>=0) lcd.print("+");
      lcd.print(t);
      lcd.print(" ~");
      if (i>=0) lcd.print("+");
      lcd.print(i);

      lcd.setCursor(0, 1);
      lcd.print("H:");
      lcd.print(h);
      lcd.print("%    ");
    }
    else
    {
      lcd.setCursor(0, 0);
      lcd.print("Temp&Hum sensor");
      lcd.print("!ERROR!");
    }
    lcd.setCursor(13, 1);
    if (now) lcd.print("NOW");
    else lcd.print("AVG");
}

void displayPressure(bool now)
{
  float p, a;
  if (now)
  {
    p=prs;
    a=alt;
  }
  else
  {
    p=0;
    a=0;
  }

  lcd.setCursor(0, 0);
  lcd.print("P:");
  lcd.print(p/100);
  lcd.print("Pa   ");

  lcd.setCursor(0, 1);
  lcd.print("A:");
  lcd.print(a);
  lcd.print("m    ");
  
  lcd.setCursor(13, 1);
  if (now) lcd.print("NOW");
  else lcd.print("AVG");
}

void displayOptions()
{
  lcd.setCursor(0, 0);
  lcd.print("OPTIONS");
}

void readDht(unsigned long timeStamp)
{
  if (timeStamp - lastDhtCheck < DHT_CHECK_INTERVAL and timeStamp != 0)
    return;
     
  hum = dht.readHumidity();
  tmp = dht.readTemperature();
  dhtOk = !(isnan(hum) || isnan(tmp));
  if (dhtOk)
    hic = dht.computeHeatIndex(tmp, hum);

  if (state == TMP_HUM_NOW)
    screenChanged = true;

  lastDhtCheck = timeStamp;
}

void readBme(unsigned long timeStamp)
{
  if (!bmeOk)
    return;
    
  if (timeStamp - lastBmeCheck < BME_CHECK_INTERVAL and timeStamp != 0)
    return;

  prs = bme.readFloatPressure();
  alt = bme.readFloatAltitudeMeters();

  if (state == PRESS_NOW)
    screenChanged = true;
  
  lastBmeCheck = timeStamp;
}

void computeAvarages(unsigned long timeStamp)
{

}

void publishToSerial(unsigned long timeStamp)
{
  if (timeStamp - lastPublish < PUBLISH_INTERVAL and lastPublish != 0)
    return;
    
  Serial.print("t=");
  Serial.println(tmp);
  Serial.print("h=");
  Serial.println(hum);
  Serial.print("i=");
  Serial.println(hic);
  Serial.print("p=");
  Serial.println(prs);
  Serial.print("a=");
  Serial.println(alt);  

  lastPublish = timeStamp;
}

void btnClick()
{
  unsigned long timeStamp = millis();
  if (timeStamp - lastBtnClick < BTN_BOUNCE_TIME or
      !btnDblHandled)
    return;
  
  if (timeStamp - lastBtnClick > BTN_DOUBLECLICK_MIN and
      timeStamp - lastBtnClick < BTN_DOUBLECLICK_MAX)
      {
        btnDblHandled = false;
        btnHandled = true;
        lastBtnClick = timeStamp;
        return;
      }
      
  btnHandled = false;
  lastBtnClick = timeStamp;
}
