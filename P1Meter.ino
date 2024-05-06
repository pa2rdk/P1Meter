// *************************************************************************************
//  V1.0 OTA
//  By R.J. de Kok - (c) 2024
// *************************************************************************************

#include "EEPROM.h"
#include "WiFi.h"
#include <WifiMulti.h>
//#include "Wire.h"
#include <HTTPClient.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <RDKOTA.h>

#include "Free_Fonts.h" // Include the header file attached to this sketch
#include <TFT_eSPI.h>   // https://github.com/Bodmer/TFT_eSPI
#include "NTP_Time.h"

#define LED           14
#define BEEPER        32
#define beepOn        1
#define beepOff       0

#define offsetEEPROM  0x10
#define EEPROM_SIZE   250
#define TIMEZONE euCET
#define DEG2RAD       0.0174532925
#define ExampleCounter  60
#define ExampleScale    10
#define ExampleMax      18
#define MinutesCounter  60
#define MinutesScale    10
#define HoursCounter  24
#define HoursScale    6

#define OTAHOST      "https://www.rjdekok.nl/Updates/P1Meter"
#define VERSION       "v1.0"

float examples[ExampleCounter];
float minutes[MinutesCounter];
float hours[HoursCounter];
int majorVersion = 0;
int minorVersion = 1;  //Eerste uitlevering 20/11/2023
time_t local_time;
time_t boot_time;
int prevHour = -1;
int prevMinute = -1;
float usedPower = 0;
float usedGas = 0;
float usedWater = 0;
bool showDayGraph = 0;
bool getWater = true;

char receivedString[128];
char chkGS[3] = "GS";

struct StoreStruct {
  byte chkDigit;
  char ESP_SSID[25];
  char ESP_PASS[27];
  char energyIP[16];
  char waterIP[16];  
  byte beeperCnt;
  uint32_t maxFasePower;
  uint32_t maxPower;
  uint32_t dayPower;
  uint32_t dayGas;
  uint32_t dayWater;
  bool useYesterdayAsMax;
  byte dispScreen;
  int prefDay;
  float lastPower;
  float lastGas;  
  float lastWater; 
};

typedef struct {  // WiFi Access
  const char *SSID;
  const char *PASSWORD;
} wlanSSID;

//#include "RdK_Settings.h"
#include "All_Settings.h"

WiFiMulti wifiMulti;
TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
TFT_eSprite screen = TFT_eSprite(&tft);
WiFiClient net;
HTTPClient http;
RDKOTA rdkOTA(OTAHOST);

void setup() {
  pinMode(BEEPER, OUTPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, 0);
    
  digitalWrite(BEEPER, beepOff);
  if (storage.beeperCnt > 0) SingleBeep(2);

  Serial.begin(115200);
  Serial.printf("HomeBridge P1 Meter display v%d.%d\r\n",majorVersion, minorVersion);
  Serial.println(F("beginning boot procedure...."));
  Serial.println(F("Start tft"));
  tft.begin();
  tft.setRotation(screenRotation);
  uint16_t calData[5] = { 304, 3493, 345, 3499, 4   };
  tft.setTouch(calData);

  tft.fillScreen(TFT_BLACK);

  if (!EEPROM.begin(EEPROM_SIZE)) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.println(F("failed to initialise EEPROM"));
    tft.println(F("failed to initialise EEPROM"));
    while (1)
      ;
  }
  if (EEPROM.read(offsetEEPROM) != storage.chkDigit) {
    Serial.println(F("Writing defaults...."));
    saveConfig();
  }

  loadConfig();
  printConfig();

  Serial.println(F("Type GS to enter setup:"));
  tft.println(F("Wait for setup"));
  delay(5000);
  if (Serial.available()) {
    Serial.println(F("Check for setup"));
    if (Serial.find(chkGS)) {
      tft.println(F("Setup entered"));
      Serial.println(F("Setup entered..."));
      setSettings(1);
    }
  }

  delay(1000);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println(F("Starting"));

  Serial.println(F("Start WiFi"));
  tft.println(F("Start WiFi"));

  int maxNetworks = (sizeof(wifiNetworks) / sizeof(wlanSSID));
  for (int i = 0; i < maxNetworks; i++ )
    wifiMulti.addAP(wifiNetworks[i].SSID, wifiNetworks[i].PASSWORD);
  wifiMulti.addAP(storage.ESP_SSID,storage.ESP_PASS);

  if (check_connection()){
    if (rdkOTA.checkForUpdate(VERSION)){
      if (questionBox("Installeer update", TFT_WHITE, TFT_NAVY, 5, 240, 230, 48)){
        messageBox("Installing update", TFT_YELLOW, TFT_NAVY, 5, 240, 230, 48);
        rdkOTA.installUpdate();
      } 
    }

    getNTPData();
    boot_time = local_time;
  }
  tft.fillScreen(TFT_BLACK);
  screen.setColorDepth(8);
  printSettings();
}

void loop() {
  uint16_t touchX = 0, touchY = 0;
  bool pressed = tft.getTouch(&touchX, &touchY);
  bool doMenu=false;
  if (pressed){
    Serial.printf("Position x:%d, y:%d\r\n",touchX, touchY);
    if (touchX>200 && touchY<30) esp_restart();
    showDayGraph = !showDayGraph;
  } 
  screen.createSprite(tft.width(), tft.height());
  bool longDelay = false;
  screen.fillSprite(TFT_BLACK);

  usedWater = 0;
  int httpCode;
  String getData;
  DynamicJsonDocument jsonDocument(2048);
  if (getWater){
    for (int i = 0;i<8;i++) if (storage.waterIP[i]!=storage.energyIP[i]) getWater = false;
    if (storage.waterIP[0]=='0') getWater = false;
  }

  if (getWater){
    getData = "http://" + String(storage.waterIP) + "/api/v1/data";
    Serial.println(getData);
    http.begin(net, getData); //Specify request destination
    httpCode = http.GET(); //Send the request
    Serial.printf("http Code = %d",httpCode);
    if (httpCode > 0) { //Check the returning code
      deserializeJson(jsonDocument, http.getStream());
      Serial.printf("total_liter_m3:%f\r\n", jsonDocument["total_liter_m3"].as<float>());
      usedWater = jsonDocument["total_liter_m3"].as<float>();
    } 
    http.end();
  }

  getData = "http://" + String(storage.energyIP) + "/api/v1/data";
  Serial.println(getData);
  http.begin(net, getData); //Specify request destination
  httpCode = http.GET(); //Send the request
  Serial.printf("http Code = %d",httpCode);
  if (httpCode > 0) { //Check the returning code
    deserializeJson(jsonDocument, http.getStream());
    Serial.printf("wifi_ssid:%s\r\n", jsonDocument["wifi_ssid"].as<const char*>());
    Serial.printf("wifi_strength:%d\r\n", jsonDocument["wifi_strength"].as<long>());

    Serial.printf("total_power_import_kwh:%f\r\n", jsonDocument["total_power_import_kwh"].as<float>());
    Serial.printf("total_power_export_kwh:%f\r\n", jsonDocument["total_power_export_kwh"].as<float>());
    Serial.printf("active_power_w:%f\r\n", jsonDocument["active_power_w"].as<float>());
    Serial.printf("active_power_l1_w:%f\r\n", jsonDocument["active_power_l1_w"].as<float>());
    Serial.printf("active_power_l2_w:%f\r\n", jsonDocument["active_power_l2_w"].as<float>());
    Serial.printf("active_power_l3_w:%f\r\n", jsonDocument["active_power_l3_w"].as<float>());            
    Serial.printf("active_voltage_l1_v:%f\r\n", jsonDocument["active_voltage_l1_v"].as<float>());
    Serial.printf("active_voltage_l2_v:%f\r\n", jsonDocument["active_voltage_l2_v"].as<float>());
    Serial.printf("active_voltage_l3_v:%f\r\n", jsonDocument["active_voltage_l3_v"].as<float>());
    Serial.printf("active_current_l1_a:%f\r\n", jsonDocument["active_current_l1_a"].as<float>());
    Serial.printf("active_current_l2_a:%f\r\n", jsonDocument["active_current_l2_a"].as<float>());
    Serial.printf("active_current_l3_a:%f\r\n", jsonDocument["active_current_l3_a"].as<float>());    
    Serial.printf("Total GAS usage:%f\r\n", jsonDocument["total_gas_m3"].as<float>());

    usedPower = jsonDocument["total_power_import_kwh"].as<float>() - jsonDocument["total_power_export_kwh"].as<float>();
    usedGas = jsonDocument["total_gas_m3"].as<float>();
    getNTPData();
    int tDay = day(local_time);
    int tHour = hour(local_time);
    int tMinute = minute(local_time);

    float totalPower = (usedPower - storage.lastPower);
    totalPower = round(totalPower * 100)/100;
    float totalGas = round((usedGas - storage.lastGas)*100)/100;
    float totalWater = round((usedWater - storage.lastWater)*1000);

    if (tDay != storage.prefDay){
      if (usedPower!=0) storage.lastPower = usedPower;
      if (usedGas>0) storage.lastGas = usedGas;
      if (usedWater>0) storage.lastWater = usedWater;
      storage.prefDay = tDay; 
      if (storage.useYesterdayAsMax && storage.prefDay!=-1){
        if (totalPower!=0) storage.dayPower = (int32_t)totalPower;
        if (totalGas>0) storage.dayGas = (uint32_t)totalGas;
        if (totalWater>0) storage.dayWater = (uint32_t)totalWater;
      }
      saveConfig();
    }
    if (tMinute != prevMinute){
      prevMinute = tMinute; 
      moveMinutes(jsonDocument["active_power_w"].as<float>());
    }
    if (tHour != prevHour){
      prevHour = tHour; 
      moveHours(jsonDocument["active_power_w"].as<float>());
    }

    if (storage.dayPower==0) storage.dayPower=25;
    if (storage.dayGas==0) storage.dayGas=5;
    if (storage.dayWater==0) storage.dayWater=500;

    int voltage = 0;
    int height = 0;
    int power1 = 0;
    int power2 = 0;
    int power3 = 0;    
    int heightDevider = storage.maxFasePower/123;
    uint32_t color = 0;
    screen.setTextDatum(MC_DATUM);
    screen.setTextColor(TFT_YELLOW, TFT_BLACK);
    screen.drawString("EnergyMeter", 120, 13, 4);
    screen.setTextColor(TFT_GREEN, TFT_BLACK);
    screen.setCursor(76, 30);
    screen.printf("%02d-%02d-%04d %02d:%02d", day(local_time), month(local_time), year(local_time), hour(local_time), minute(local_time));

    voltage = round(jsonDocument["active_voltage_l1_v"].as<float>());
    power1 = voltage * jsonDocument["active_current_l1_a"].as<float>();
    if (voltage==0) power1 = jsonDocument["active_power_l1_w"].as<float>();
    screen.setTextColor(TFT_YELLOW);
    screen.setCursor(0, 160);
    screen.println("L1");
    screen.setTextColor(TFT_WHITE);
    screen.setCursor(0, 170);
    if (voltage>0) screen.printf("%dV", voltage);
    screen.setCursor(0, 180);
    screen.printf("%dW", power1);
    screen.drawRect(0, 190, 30, 130, TFT_YELLOW);
    screen.fillRect(1, 191, 28, 128, TFT_BLACK);
    height = (voltage - 220)*3;
    color = TFT_GREEN;
    if (voltage>235) color = TFT_YELLOW;
    if (voltage>250) color = TFT_RED;
    if (voltage>0) screen.fillRect(2, 318-height, 12, height, color);
    
    height = power1/heightDevider;
    if (height>123) height = 123;
    color = TFT_BLUE;
    if (power1>storage.maxFasePower/2) color = TFT_RED;
    if (power1>0){
      screen.fillRect(voltage>0?16:2, 318-height, voltage>0?12:26, height, color);
      if (power1>storage.maxFasePower) screen.fillTriangle(17, 195, 22, 191, 27, 195, TFT_WHITE);
    } else {
      screen.fillRect(voltage>0?16:2, 192, voltage>0?12:26, height*-1, TFT_GREEN);
    }
    screen.setTextColor(TFT_WHITE);
    screen.setCursor(5, 310);
    if (voltage>0) screen.println("V");
    screen.setCursor(voltage>0?19:12, 310);
    screen.println("W");    

    voltage = round(jsonDocument["active_voltage_l2_v"].as<float>());
    power2 = voltage * jsonDocument["active_current_l2_a"].as<float>();
    if (voltage==0) power2 = jsonDocument["active_power_l2_w"].as<float>();
    screen.setTextColor(TFT_GREEN);
    screen.setCursor(35, 160);
    screen.println("L2");
    screen.setTextColor(TFT_WHITE);
    screen.setCursor(35, 170);
    if (voltage>0) screen.printf("%dV", voltage);
    screen.setCursor(35, 180);
    screen.printf("%dW", power2);
    screen.drawRect(35, 190, 30, 130, TFT_GREEN);
    screen.fillRect(36, 191, 28, 128, TFT_BLACK);
    height = (voltage - 220)*3;
    color = TFT_GREEN;
    if (voltage>235) color = TFT_YELLOW;
    if (voltage>250) color = TFT_RED;
    if (voltage>0) screen.fillRect(37, 318-height, 12, height, color);
    height = power2/heightDevider;
    if (height>123) height = 123;
    color = TFT_BLUE;
    if (power2>storage.maxFasePower/2) color = TFT_RED;
    if (power2>0) {
      screen.fillRect(voltage>0?51:37, 318-height, voltage>0?12:26, height, color);
      if (power2>storage.maxFasePower) screen.fillTriangle(52, 195, 57, 191, 62, 195, TFT_WHITE);
    } else {
      screen.fillRect(voltage>0?51:37, 192, voltage>0?12:26, height*-1, TFT_GREEN);
    } 
    screen.setTextColor(TFT_WHITE);
    screen.setCursor(40, 310);
    if (voltage>0) screen.println("V");
    screen.setCursor(voltage>0?54:47, 310);
    screen.println("W"); 

    voltage = round(jsonDocument["active_voltage_l3_v"].as<float>());
    power3 = voltage * jsonDocument["active_current_l3_a"].as<float>();
    if (voltage==0) power3 = jsonDocument["active_power_l3_w"].as<float>();
    screen.setTextColor(TFT_BLUE);
    screen.setCursor(70, 160);
    screen.println("L3");
    screen.setTextColor(TFT_WHITE);
    screen.setCursor(70, 170);
    if (voltage>0) screen.printf("%dV", voltage);
    screen.setCursor(70, 180);
    screen.printf("%dW", power3);
    screen.drawRect(70, 190, 30, 130, TFT_BLUE);
    screen.fillRect(71, 191, 28, 128, TFT_BLACK);
    height = (voltage - 220)*3;
    color = TFT_GREEN;
    if (voltage>235) color = TFT_YELLOW;
    if (voltage>250) color = TFT_RED;
    if (voltage>0) screen.fillRect(72, 318-height, 12, height, color);
    height = power3/heightDevider;
    if (height>123) height = 123;
    color = TFT_BLUE;
    if (power3>storage.maxFasePower/2) color = TFT_RED;
    if (power3>0){
      screen.fillRect(voltage>0?86:72, 318-height, voltage>0?12:26, height, color);
      if (power3>storage.maxFasePower) screen.fillTriangle(87, 195, 92, 191, 97, 195, TFT_WHITE);
    } else {
      screen.fillRect(voltage>0?86:72, 192, voltage>0?12:26, height*-1, TFT_GREEN);
    }
    screen.setTextColor(TFT_WHITE);
    screen.setCursor(75, 310);
    if (voltage>0) screen.println("V");
    screen.setCursor(voltage>0?89:82, 310);
    screen.println("W"); 


    int part1 = (abs(power1)*360)/(storage.maxPower>0?storage.maxPower:(power1+power2+power3!=0?power1+power2+power3:1));
    int part2 = (abs(power2)*360)/(storage.maxPower>0?storage.maxPower:(power1+power2+power3!=0?power1+power2+power3:1));
    int part3 = (abs(power3)*360)/(storage.maxPower>0?storage.maxPower:(power1+power2+power3!=0?power1+power2+power3:1)); 
    int nPart1 = 0;
    int nPart2 = 0;
    int nPart3 = 0;        
    if (power1<0) {
      nPart1 = part1;
      part1 = 0;
    }
    if (power2<0)
    {
      nPart2 = part2;
      part2 = 0;
    }
    if (power3<0){
      nPart3 = part3;
      part3 = 0;
    }
    int part4 = 360 - (part1 + part2 + part3);  
    int nPart4 = 360 - (nPart1 + nPart2 + nPart3); 
    Serial.printf("Power1:%d, Power2:%d, Power3:%d\r\n",power1,power2,power3); 
    Serial.printf("Part1:%d, Part2:%d, Part3:%d, Rest:%d\r\n",part1,part2,part3, part4);
    screen.drawCircle(170, 252, 66, TFT_WHITE);

    int totalPart = part1 + part2 + part3;
    int totalnPart = nPart1 + nPart2 + nPart3;

    fillSegment(170, 252, 0, part1, 64, TFT_YELLOW);
    fillSegment(170, 252, part1, part2, 64, TFT_GREEN);
    fillSegment(170, 252, part1 + part2, part3, 64, TFT_BLUE);
    fillSegment(170, 252, part1 + part2 + part3, part4, 64, TFT_BLACK);
    fillSegment(170, 252, 360 - totalnPart, totalnPart, 64, TFT_GREEN);
    fillSegment(170, 252, 0, 360, 58, TFT_BLACK);

    Serial.printf("Used Power today:(%f-%f) = %f KW\r\n", usedPower, storage.lastPower, totalPower);
    fillSegment(170, 252, 0, (totalPower)*(360/storage.dayPower), 56, TFT_MAGENTA);
    if (totalPower>storage.dayPower){
      float restPower = totalPower;
      while (restPower>storage.dayPower) restPower -= storage.dayPower; 
      fillSegment(170, 252, 0, (restPower)*(360/storage.dayPower), 54, TFT_RED);
    }

    fillSegment(170, 252, 0, 360, 50, TFT_BLACK);

    Serial.printf("Used GAS today:%f - %d\r\n", totalGas,storage.dayGas);
    fillSegment(170, 252, 0, ((totalGas*360)/storage.dayGas), 48, TFT_SKYBLUE);
    if (totalGas>storage.dayGas){
      float restGas = totalGas;
      while (restGas>storage.dayGas) restGas -= storage.dayGas; 
      fillSegment(170, 252, 0, (restGas)*(360/storage.dayGas), 46, TFT_RED); 
    }
    fillSegment(170, 252, 0, 360, 42, TFT_BLACK);

    if (getWater){
      Serial.printf("Used water today:%f - %d \r\n", totalWater,storage.dayWater);
      fillSegment(170, 252, 0, ((totalWater* 360)/storage.dayWater), 40, TFT_GREENYELLOW);
      Serial.println((totalWater* 360)/storage.dayWater);
      if (totalWater>storage.dayWater){
        float restWater = totalWater;
        while (restWater>storage.dayWater) restWater -= storage.dayWater; 
        fillSegment(170, 252, 0, (restWater)*(360/storage.dayWater), 38, TFT_RED);
      }
    }


    fillSegment(170, 252, 0, 360, 34, TFT_BLACK);

    screen.setTextDatum(MC_DATUM);
    screen.setTextPadding(screen.textWidth(String(power1+power2+power3) + "W"));
    if (totalPart>0)
      screen.setTextColor(TFT_RED, TFT_BLACK);
    else   
      screen.setTextColor(TFT_GREEN, TFT_BLACK);
    screen.drawString(String(power1+power2+power3)+"W", 170, 231, 2);

    screen.setTextPadding(screen.textWidth(String(totalPower) + "KW"));
    screen.setTextColor(TFT_MAGENTA, TFT_BLACK);
    screen.drawString(String(totalPower) + "KW", 170, 245, 2);

    screen.setTextPadding(screen.textWidth(String(totalGas) + "M3"));
    screen.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    screen.drawString(String(totalGas) + "M3", 170, 259, 2);

    if (getWater){
      screen.setTextPadding(screen.textWidth(String(totalWater,0) + "L"));
      screen.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
      screen.drawString(String(totalWater) + "L", 170, 273, 2);
    }
  } else {
    screen.fillSprite(TFT_BLACK);
    screen.setCursor(0, 0);
    screen.println(F("Unable to retrieve data"));
    longDelay = true;
  }

  // moveExamples();
  // printGraph(examples, ExampleCounter, ExampleScale, TFT_BLUE, "Examples");
  if (showDayGraph){
    printGraph(minutes, MinutesCounter, MinutesScale, TFT_WHITE, "Energy (M)");
  } else {
    printGraph(hours, HoursCounter, HoursScale, TFT_WHITE, "Energy (H)");
  }
  screen.pushSprite(0, 0);
  screen.deleteSprite();
  delay(2000);
  if (longDelay) delay(15000);
}

void moveExamples() {
  for (int i = 0; i < ExampleCounter -1 ; i++) {
    examples[(ExampleCounter-1) - i] = examples[(ExampleCounter-2) - i];
  }
  examples[0] = random(0, ExampleMax);
  examples[0] -= (ExampleMax/2);
}

void moveMinutes(float actualPower) {
  for (int i = 0; i < 59; i++) {
    minutes[59 - i] = minutes[58 - i];
  }
  Serial.printf("Total minute power is %d\r\n",actualPower);
  for (int i = 0; i < MinutesCounter -1 ; i++) {
    Serial.printf("%d = %d\r\n",i,minutes[i]);
  }
  minutes[0] = actualPower;
}

void moveHours(float actualPower) {
  for (int i = 0; i < 24; i++) {
    hours[24 - i] = hours[23 - i];
  }
  Serial.printf("Total hour power is %d\r\n",actualPower);
  for (int i = 0; i < HoursCounter -1 ; i++) {
    Serial.printf("%d = %d\r\n",i,hours[i]);
  }
  hours[0] = actualPower;
}

void printGraph(float graphArray[], int lenArray, int scale, uint32_t lColor, String gHeader){
  screen.fillRect(0,40,240,110,TFT_BLACK);
  screen.drawLine(25,50,25,130,lColor);
  screen.drawLine(25,130,215,130,lColor);
  screen.setTextSize(2);
  screen.setTextDatum(MC_DATUM);
  screen.setTextColor(lColor);
  screen.drawString(gHeader, 120, 60, GFXFF);

  screen.setTextSize(1);
  int hStart = 20;
  for (int i = 0;i<scale+1; i++){
    int xPos = 20 + (float)190/scale*i;
    int xTxt = lenArray - ((lenArray/scale)*i);
    screen.drawString(String(xTxt), xPos, 140, GFXFF);
  }

  float vMax = 1;
  float vMin = 0;
  for (int i = 0;i<lenArray; i++){
    if (graphArray[i]>vMax) vMax = graphArray[i];
    if (graphArray[i]<vMin) vMin = graphArray[i];
  }

  if (vMin==0){
    for (int i = 0;i<5; i++){
      int yPos = 50 + (i*20);
      float yTxt = vMin + ((((float)(vMax-vMin)/4)*(4-i)));
      char buff[5];
      if (yTxt <10 and yTxt > -10) sprintf(buff,"%.1f",yTxt);
      if (yTxt >9 or yTxt < -9) sprintf(buff,"%.0f",yTxt);
      screen.drawString(buff, 12, yPos, GFXFF);
    }
    float hScale = (float)190/(lenArray-1);
    float vScale = (float)80/vMax;

    int lastX, lastY;
    for (int i=0;i<lenArray;i++){
      int x = 215-(i*hScale);
      int y = 130-(graphArray[i]*vScale);
      //Serial.printf("Pixel %d with value  %d on %d,%d (max = %d)\r\n",i, graphArray[i], x, y, vMax);
      if (i==0) screen.drawPixel(x,y,TFT_RED);
      else screen.drawLine(lastX,lastY,x,y,TFT_RED);
      lastX = x;
      lastY = y;
    }
  } else {
    char buff[5];
    sprintf(buff,"%.0f",vMax);
    screen.drawString(buff, 12, 50, GFXFF);
    screen.drawString("0", 12, 90, GFXFF);
    sprintf(buff,"%.0f",vMin);
    screen.drawString(buff, 12, 130, GFXFF);
    
    float hScale = (float)190/(lenArray-1);
    float vScaleP = (float)40/vMax;
    float vScaleN = (float)40/(vMin*-1);

    float lastX, lastY;
    //for (int i=0;i<lenArray;i++){
    for (int i=lenArray-1;i>=0;i--){
      float x = 215-(i*hScale);
      float y = 90;
      if (graphArray[i]>=0) y = 90-(graphArray[i]*vScaleP);
      if (graphArray[i]<0) y = 90-(graphArray[i]*vScaleN);
      Serial.printf("Pixel %d with value  %f on %f,%f (max = %f)\r\n",i, graphArray[i], x, y, vMax);
      uint32_t color = (y>90 && (lastY>90 || y>lastY))?TFT_GREEN:TFT_RED;
      if (i==lenArray-1) screen.drawPixel(x,y,TFT_RED);
      else screen.drawLine(lastX,lastY,x,y,color);
      lastX = x;
      lastY = y;
    }
  }
}

void printSettings(){
  Serial.println("Storage:");
  Serial.printf("ESP_SSID:%s\r\n", storage.ESP_SSID);
  Serial.printf("ESP_PASS:%s\r\n", storage.ESP_PASS);
  Serial.printf("energyIP:%s\r\n", storage.energyIP);
  Serial.printf("waterIP:%s\r\n", storage.waterIP);  
  Serial.printf("beeperCnt:%d\r\n", storage.beeperCnt);
  Serial.printf("maxPhasePower:%d\r\n", storage.maxFasePower);
  Serial.printf("maxPower:%d\r\n", storage.maxPower);
  Serial.printf("dayPower:%d\r\n", storage.dayPower);
  Serial.printf("dayGas:%d\r\n", storage.dayGas);
  Serial.printf("dayWater:%d\r\n", storage.dayWater);
  Serial.printf("useYesterdayAsMax:%s\r\n", storage.useYesterdayAsMax?"Y":"N");
  Serial.printf("dispScreen:%d\r\n", storage.dispScreen);
  Serial.printf("prefDay:%d\r\n", storage.prefDay);
  Serial.printf("lastPower:%f\r\n", storage.lastPower);
  Serial.printf("lastGas:%f\r\n", storage.lastGas);  
  Serial.printf("lastWater:%f\r\n", storage.lastWater); 
}

int fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int color)
{
  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x1 = sx * r + x;
  uint16_t y1 = sy * r + y;

  // Draw color blocks every inc degrees
  for (int i = start_angle; i < start_angle + sub_angle; i++) {

    // Calculate pair of coordinates for segment end
    int x2 = cos((i + 1 - 90) * DEG2RAD) * r + x;
    int y2 = sin((i + 1 - 90) * DEG2RAD) * r + y;

    screen.fillTriangle(x1, y1, x2, y2, x, y, color);

    // Copy segment end to sgement start for next segment
    x1 = x2;
    y1 = y2;
  }
}

boolean check_connection() {
  if (WiFi.status() != WL_CONNECTED) {
    InitWiFiConnection();
  }
  return (WiFi.status() == WL_CONNECTED);
}

void InitWiFiConnection() {
  long startTime = millis();
  while (wifiMulti.run() != WL_CONNECTED && millis()-startTime<30000){
    delay(1000);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.print("Connected to: ");
    Serial.println(WiFi.SSID());
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
  }
}

void getNTPData() {
  if (check_connection()) {
    syncTime();
    local_time = TIMEZONE.toLocal(now(), &tz1_Code);   
  }
}

void SingleBeep(int cnt) {
  int tl = 200;
  for (int i = 0; i < cnt; i++) {
    digitalWrite(BEEPER, beepOn);
    delay(tl);
    digitalWrite(BEEPER, beepOff);
    delay(tl);
  }
}

bool saveConfig() {
  bool commitEeprom = false;
  for (unsigned int t = 0; t < sizeof(storage); t++) {
    if (*((char *)&storage + t) != EEPROM.read(offsetEEPROM + t)) {
      EEPROM.write(offsetEEPROM + t, *((char *)&storage + t));
      commitEeprom = true;
    }
  }
  if (commitEeprom) EEPROM.commit();
  return true;
}

void loadConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
    for (unsigned int t = 0; t < sizeof(storage); t++)
      *((char *)&storage + t) = EEPROM.read(offsetEEPROM + t);
}

void printConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit) {
    for (unsigned int t = 0; t < sizeof(storage); t++)
      Serial.write(EEPROM.read(offsetEEPROM + t));
    Serial.println();
    setSettings(0);
  }
}

void setSettings(bool doAsk) {
  int i = 0;
  Serial.print(F("SSID ("));
  Serial.print(storage.ESP_SSID);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(15);
    if (receivedString[0] != 0) {
      storage.ESP_SSID[0] = 0;
      strcat(storage.ESP_SSID, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("Password ("));
  Serial.print(storage.ESP_PASS);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(26);
    if (receivedString[0] != 0) {
      storage.ESP_PASS[0] = 0;
      strcat(storage.ESP_PASS, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("Energy IP ("));
  Serial.print(storage.energyIP);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(26);
    if (receivedString[0] != 0) {
      storage.energyIP[0] = 0;
      strcat(storage.energyIP, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("Water IP ("));
  Serial.print(storage.waterIP);
  Serial.print(F("):"));
  if (doAsk == 1) {
    getStringValue(26);
    if (receivedString[0] != 0) {
      storage.waterIP[0] = 0;
      strcat(storage.waterIP, receivedString);
    }
  }
  Serial.println();

  Serial.print(F("#Beeper ("));
  Serial.print(storage.beeperCnt);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.beeperCnt = i;
  }
  Serial.println();

  Serial.print(F("Max. current power per phase("));
  Serial.print(storage.maxFasePower);
  Serial.print(F(" W):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.maxFasePower = i;
  }
  Serial.println();

  Serial.print(F("Max. current power ("));
  Serial.print(storage.maxPower);
  Serial.print(F(" W):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.maxPower = i;
  }
  Serial.println();

  Serial.print(F("Max. daily power ("));
  Serial.print(storage.dayPower);
  Serial.print(F(" KW):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.dayPower = i;
  }
  Serial.println();

  Serial.print(F("Max. daily gas ("));
  Serial.print(storage.dayGas);
  Serial.print(F(" M3):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.dayGas = i;
  }
  Serial.println();

  Serial.print(F("Max. daily water ("));
  Serial.print(storage.dayWater);
  Serial.print(F(" L):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.dayWater = i;
  }
  Serial.println();

  Serial.print(F("Use Yesterday's usage as daily max (0 - 1) ("));
  Serial.print(storage.useYesterdayAsMax);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.useYesterdayAsMax = i;
  }
  Serial.println();

  // Serial.print(F("Screen 1 (0 - 5) ("));
  // Serial.print(storage.dispScreen);
  // Serial.print(F("):"));
  // if (doAsk == 1) {
  //   i = getNumericValue();
  //   if (receivedString[0] != 0) storage.dispScreen = i;
  // }
  // Serial.println();
  // Serial.println();

  if (doAsk == 1) {
    saveConfig();
    loadConfig();
  }
}

void getStringValue(int length) {
  serialFlush();
  receivedString[0] = 0;
  int i = 0;
  while (receivedString[i] != 13 && i < length) {
    if (Serial.available() > 0) {
      receivedString[i] = Serial.read();
      if (receivedString[i] == 13 || receivedString[i] == 10) {
        i--;
      } else {
        Serial.write(receivedString[i]);
      }
      i++;
    }
  }
  receivedString[i] = 0;
  serialFlush();
}

byte getCharValue() {
  serialFlush();
  receivedString[0] = 0;
  int i = 0;
  while (receivedString[i] != 13 && i < 2) {
    if (Serial.available() > 0) {
      receivedString[i] = Serial.read();
      if (receivedString[i] == 13 || receivedString[i] == 10) {
        i--;
      } else {
        Serial.write(receivedString[i]);
      }
      i++;
    }
  }
  receivedString[i] = 0;
  serialFlush();
  return receivedString[i - 1];
}

int getNumericValue() {
  serialFlush();
  int myInt = 0;
  byte inChar = 0;
  bool isNegative = false;
  receivedString[0] = 0;

  int i = 0;
  while (inChar != 13) {
    if (Serial.available() > 0) {
      inChar = Serial.read();
      if (inChar > 47 && inChar < 58) {
        receivedString[i] = inChar;
        i++;
        Serial.write(inChar);
        myInt = (myInt * 10) + (inChar - 48);
      }
      if (inChar == 45) {
        Serial.write(inChar);
        isNegative = true;
      }
    }
  }
  receivedString[i] = 0;
  if (isNegative == true) myInt = myInt * -1;
  serialFlush();
  return myInt;
}

void serialFlush() {
  for (int i = 0; i < 10; i++) {
    while (Serial.available() > 0) {
      Serial.read();
    }
  }
}

/***************************************************************************************
**                          Draw messagebox with message
***************************************************************************************/
void messageBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor) {
  messageBox(msg, fgcolor, bgcolor, 5, 240, 230, 24);
}

void messageBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor, int x, int y, int w, int h) {
  uint16_t current_textcolor = tft.textcolor;
  uint16_t current_textbgcolor = tft.textbgcolor;

  //tft.loadFont(AA_FONT_SMALL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fgcolor, bgcolor);
  tft.fillRoundRect(x, y, w, h, 5, fgcolor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 5, bgcolor);
  tft.setTextPadding(tft.textWidth(msg));
  tft.drawString(msg, w/2, y + (h / 2));
  tft.setTextColor(current_textcolor, current_textbgcolor);
  tft.unloadFont();
}

bool questionBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor, int x, int y, int w, int h) {
  uint16_t current_textcolor = tft.textcolor;
  uint16_t current_textbgcolor = tft.textbgcolor;

  //tft.loadFont(AA_FONT_SMALL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fgcolor, bgcolor);
  tft.fillRoundRect(x, y, w, h, 5, fgcolor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 5, bgcolor);
  tft.setTextPadding(tft.textWidth(msg));
  tft.drawString(msg, w/2, y + (h / 4));

  tft.fillRoundRect(x + 4, y + (h/2) - 2, (w - 12)/2, (h - 4)/2, 5, TFT_GREEN);
  tft.setTextColor(fgcolor, TFT_GREEN);
  tft.setTextPadding(tft.textWidth("Yes"));
  tft.drawString("Yes", x + 4 + ((w - 12)/4),y + (h/2) - 2 + (h/4));
  tft.fillRoundRect(x + (w/2) + 2, y + (h/2) - 2, (w - 12)/2, (h - 4)/2, 5, TFT_RED);
  tft.setTextColor(fgcolor, TFT_RED);
  tft.setTextPadding(tft.textWidth("No"));
  tft.drawString("No", x + (w/2) + 2 + ((w - 12)/4),y + (h/2) - 2 + (h/4));
  Serial.printf("Yes = x:%d,y:%d,w:%d,h:%d\r\n",x + 4, y + (h/2) - 2, (w - 12)/2, (h - 4)/2);
  Serial.printf("No  = x:%d,y:%d,w:%d,h:%d\r\n",x + (w/2) + 2, y + (h/2) - 2, (w - 12)/2, (h - 4)/2);
  tft.setTextColor(current_textcolor, current_textbgcolor);
  tft.unloadFont();

  uint16_t touchX = 0, touchY = 0;

  long startWhile = millis();
  while (millis()-startWhile<30000) {
    bool pressed = tft.getTouch(&touchX, &touchY);
    if (pressed){
      Serial.printf("Pressed = x:%d,y:%d\r\n",touchX,touchY);
      if (touchY>=y + (h/2) - 2 && touchY<=y + (h/2) - 2 + ((h - 4)/2)){
        if (touchX>=x + 4 && touchX<=x + 4 + ((w - 12)/2)) return true;
        if (touchX>=x + (w/2) + 2 && touchX<=x + (w/2) + 2 + ((w - 12)/2)) return false;
      }
    }
  }
  return false;
}
