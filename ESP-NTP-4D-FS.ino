#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

// Wifi Manager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager

// OTA upport (OTA needs ESP module with large enough flash)
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h> 

// TM1637 stuff
#include <TM1637.h>

TM1637 tm1637_0(D1,D0);
TM1637 tm1637_1(D3,D2);
TM1637 tm1637_2(D5,D4);
TM1637 tm1637_3(D7,D6);

int8_t TimeDisp[] = {0x00,0x00,0x00,0x00};

// NTP Stuff
#include <time.h>
#include <EEPROM.h>

// FS
#include "FS.h"

time_t startupTime = 0;
String gatewayName = "ESP-NTP-"; 
String NTPserver = "pool.ntp.org";
boolean configUpdated = false;
int resync = 0;
int brightness = 7;
int lastHS = 0;
int dots = 0x01;
struct settings {
  int tz = -4;
  int tzid = 1;
  boolean _24hours = 0;
  boolean daylightTime = 1;
};

settings d_0;
settings d_1;
settings d_2;
settings d_3;

ESP8266WebServer webServer(80);
WiFiManager wifiManager;

void setup() {
      Serial.begin(115200);
      Serial.println();
      Serial.println("ESP - NTP - TM1637, compiled: "  __DATE__  ", " __TIME__ );
      SPIFFS.begin();

      loadConfig(d_0,0);
      loadConfig(d_1,128);
      loadConfig(d_2,256);
      loadConfig(d_3,384);

      tm1637_0.set(brightness);
      tm1637_0.init();
      tm1637_0.point(1);

      tm1637_1.set(brightness);
      tm1637_1.init();
      tm1637_1.point(1);

      tm1637_2.set(brightness);
      tm1637_2.init();
      tm1637_2.point(1);

      tm1637_3.set(brightness);
      tm1637_3.init();
      tm1637_3.point(1);
       
      gatewayName += String(ESP.getChipId(), HEX);
      initWifiManager();
      initOTA();
      initWebServer();
      configTime(0, 0, NTPserver.c_str() );
      delay(1000);
      time(&startupTime);
      Serial.print("UTC Time at startup: "); 
      Serial.println(ctime(&startupTime));
}

void loop () {
  ArduinoOTA.handle();
  webServer.handleClient();
  time_t t = time(NULL);
  int hs = t % 30 ;
  
  if (lastHS != hs){
    dots = ~dots;
    displayTime(t, tm1637_0, d_0);
    displayTime(t, tm1637_1, d_1);
    displayTime(t, tm1637_2, d_2);
    displayTime(t, tm1637_3, d_3);
    lastHS = hs;
    if(++resync == 3600){
      configTime(0, 0, NTPserver.c_str() );
      resync = 0;
    }
  } 
}

void displayTime(time_t t, TM1637 tm1637, settings &d_set){
  t += (d_set.tz * 3600);
  int h = (t / 3600) % 24;
  if (!d_set._24hours)
    if (h == 0)
      h = 12;
    else if (h > 12) 
      h -= 12; 
  int m = (t / 60) % 60;
  
  if (!(h /10) && !d_set._24hours) 
    TimeDisp[0] = 0x7f;
  else 
    TimeDisp[0] = h / 10;
    
  TimeDisp[1] = h % 10;
  TimeDisp[2] = m / 10;
  TimeDisp[3] = m % 10;
  tm1637.point(dots);
  tm1637.display(TimeDisp);
}
  

void initWifiManager() {
  wifiManager.setTimeout(180);
 
  if( ! wifiManager.autoConnect(gatewayName.c_str())) {
    Serial.println("Timeout connecting. Restarting...");
    delay(1000);
    ESP.reset();
  } 

  Serial.print("WiFi connected to "); Serial.print(WiFi.SSID());
  Serial.print(", IP address: "); Serial.println(WiFi.localIP());
}

void initOTA() {
  ArduinoOTA.setHostname(gatewayName.c_str());

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  Serial.println("OTA Ready and waiting for updates...");
}

void initWebServer() {
  webServer.on("/", []() {
    webServer.send(200, "text/html", getHtmlPage());
  });
  webServer.on("/reset", []() {
    Serial.println("Forced configuration..");
    wifiManager.resetSettings();
    webServer.sendHeader("Location", String("/"), true);
    webServer.send ( 302, "text/plain", "");
    delay(1000);
    ESP.reset();
  });
  webServer.on("/setconfig", []() {
    updateNTPconf();
  });
  webServer.begin();
  Serial.println("WebServer started on port 80");
}

void updateNTPconf()
{
  String message = "";
  for (int i = 0; i < webServer.args(); i++) {
    message += "Arg "+ (String)i + " > ";
    message += webServer.argName(i) + ": ";
    message += webServer.arg(i) + "\r\n";
  } 
  Serial.print(message);

  String web_NTPserver = webServer.arg("NTPs"); if (NTPserver != web_NTPserver){NTPserver = web_NTPserver; configUpdated = true;}
  int web_brightness = webServer.arg("brightness").toInt(); if (brightness != web_brightness){brightness = web_brightness; configUpdated = true;} 

  ntpConf(webServer.arg("tz_0"),webServer.arg("_24hours_0"),d_0);
  ntpConf(webServer.arg("tz_1"),webServer.arg("_24hours_1"),d_1);
  ntpConf(webServer.arg("tz_2"),webServer.arg("_24hours_2"),d_2);
  ntpConf(webServer.arg("tz_3"),webServer.arg("_24hours_3"),d_3);
   
   if (configUpdated) {
      persistConfig(d_0,0);
      persistConfig(d_1,128);
      persistConfig(d_2,256);
      persistConfig(d_3,384);
      delay(1000);
      ESP.reset();    
   }
  
   webServer.sendHeader("Location", String("/"), true);
   webServer.send ( 302, "text/plain", "");
}

void ntpConf(String str, String _24hours, settings &d){
  char testr[15];
  str.toCharArray(testr,15);  
  char *tstr = testr; 
  char *tmpstr[5];
  int i = 0;
  while ((tmpstr[i++] = strtok_r(tstr,"|",&tstr)));
      
  int web_daylightTime = atoi(tmpstr[0]); if (d.daylightTime != web_daylightTime){d.daylightTime = web_daylightTime; configUpdated = true;}
  int web_tz = atoi(tmpstr[1]); if (d.tz != web_tz){d.tz = web_tz; configUpdated = true;}
  int web_tzid = atoi(tmpstr[2]); if (d.tzid != web_tzid){d.tzid = web_tzid; configUpdated = true;}
  boolean web_24hours = _24hours.toInt(); if (d._24hours != web_24hours){d._24hours = web_24hours; configUpdated = true;}
}

String getHtmlPage() {

      String menu = "";
      File menu_file = SPIFFS.open("/menu.txt", "r");
      if (!menu_file) {
        Serial.println("file open failed");
      }
      for (int i=0; i < menu_file.size(); i++)
        menu += (char)menu_file.read();
      menu_file.close();
      
  // TODO store all these literals in flash?
  String response = 
    "<!DOCTYPE HTML>\r\n"
    "<HTML>\r\n<HEAD>\r\n"
      "<TITLE>" + gatewayName + "</TITLE>\r\n"
    "</HEAD>\r\n"
    "<BODY>\r\n"
    "<div style=\"text-align:center; font-family:verdana;\">\r\n"
    "<h1>ESP + NTP + TM1637: " + gatewayName + "</h1>\r\n";

  time_t t = time(NULL);
  response += "Current time is: <b>" + String(ctime(&t)) + "</b>"; 
  response += ", up since: <b>" + String(ctime(&startupTime)) + "</b>"; 
  response += "<br>\r\n";

  response +="WiFi is "; 
  if (WiFi.status() == WL_CONNECTED) {
    response+="connected to: <b>"; response += WiFi.SSID();
    response += "</b>, IP address: <b>"; response += WiFi.localIP().toString();
    response += "</b>, WiFi connection RSSI: <b>"; response += WiFi.RSSI();
    response += "</b>";
  } else {
    response+="DISCONNECTED";
  }
response +="<br><hr>\r\n";
//-- Begin
 response +=
    "<h2>Settings</h2>\r\n"
    "<form action=\"setconfig\">\r\n"
      "Brightness level:"
      "<input name=\"brightness\" type=\"range\" min=\"0\" max=\"7\" value=\"" + String(brightness) + "\">\r\n"
      "<br><br>\r\n"
      "NTP Server:\r\n"
      "<input type=\"text\" name=\"NTPs\" value=\"" + String(NTPserver) + "\">\r\n"
      "<br>\r\n"
      "Time Zone:\r\n"
      "<table style=\"width:100%\">\r\n<tr><td>\r\n"
      "<select id =\"tz-select_0\" name = \"tz_0\"></select>\r\n"
      "<input type=\"checkbox\" name=\"_24hours_0\" value=\"1\" " + ( d_0._24hours ? "checked" : "") + ">24 Hours?\r\n" 
      "</td><td>\r\n"
      "<select id =\"tz-select_1\" name = \"tz_1\"></select>\r\n"
      "<input type=\"checkbox\" name=\"_24hours_1\" value=\"1\" " + ( d_1._24hours ? "checked" : "") + ">24 Hours?\r\n" 
      "</td></tr>\r\n<tr><td>\r\n"
      "<select id =\"tz-select_2\" name = \"tz_2\"></select>"
      "<input type=\"checkbox\" name=\"_24hours_2\" value=\"1\" " + ( d_2._24hours ? "checked" : "") + ">24 Hours?\r\n" 
      "</td><td>\r\n"
      "<select id =\"tz-select_3\" name = \"tz_3\"></select>\r\n"
      "<input type=\"checkbox\" name=\"_24hours_3\" value=\"1\" " + ( d_3._24hours ? "checked" : "") + ">24 Hours?\r\n"
      "</td></tr>\r\n</table>\r\n"
      "<br><br>\r\n"
      "<input type=\"submit\" value=\"Update\">\r\n"
      + (configUpdated ? "<b>Updated</b>" : "") +
    "</form>\r\n"
    "</div>\r\n";
 //-- End
 response +="<script>\r\n"
 "for(i=0; i<=3; i++) \r\n";
 response += menu;
 response +=
 "\r\ndocument.getElementById(\"tz-select_0\").selectedIndex = \""+ String(d_0.tzid) + "\";"
 "\r\ndocument.getElementById(\"tz-select_1\").selectedIndex = \""+ String(d_1.tzid) + "\";"
 "\r\ndocument.getElementById(\"tz-select_2\").selectedIndex = \""+ String(d_2.tzid) + "\";"
 "\r\ndocument.getElementById(\"tz-select_3\").selectedIndex = \""+ String(d_3.tzid) + "\";"
 "\r\n</script>\r\n</BODY>\r\n</HTML>\r\n";
 configUpdated = false;
 return response;
}

#define EEPROM_SAVED_MARKER 69

void persistConfig(settings &tmp, int a) {
  EEPROM.begin(512);
  EEPROM.write(0, EEPROM_SAVED_MARKER); // flag to indicate EEPROM contains a config
  int addr = 1;
  addr = eepromWriteString(addr, NTPserver);
  EEPROM.put(addr, brightness);             addr += sizeof(brightness);
  addr+=a;  
  EEPROM.put(addr, tmp.tz);                 addr += sizeof(tmp.tz);
  EEPROM.put(addr, tmp.tzid);               addr += sizeof(tmp.tzid);
  EEPROM.put(addr, tmp._24hours);           addr += sizeof(tmp._24hours);
  EEPROM.put(addr, tmp.daylightTime);       addr += sizeof(tmp.daylightTime);
      
  EEPROM.commit();
  Serial.print("Saved ");
}

void loadConfig(settings &tmp, int a) {
  EEPROM.begin(512);

  if (EEPROM.read(0) != EEPROM_SAVED_MARKER) {
    Serial.println("Using default config");
    return; 
  }

  int addr = 1;
  NTPserver = eepromReadString(addr);   addr += NTPserver.length() + 1;
  EEPROM.get(addr, brightness);         addr += sizeof(brightness);
  addr+=a;  
  EEPROM.get(addr, tmp.tz);             addr += sizeof(tmp.tz);
  EEPROM.get(addr, tmp.tzid);           addr += sizeof(tmp.tzid);
  EEPROM.get(addr, tmp._24hours);       addr += sizeof(tmp._24hours);
  EEPROM.get(addr, tmp.daylightTime);   addr += sizeof(tmp.daylightTime);
}


int eepromWriteString(int addr, String s) {
  int l = s.length();
  for (int i=0; i<l; i++) {
     EEPROM.write(addr++, s.charAt(i));
  }
  EEPROM.write(addr++, 0x00);
  return addr;  
}

String eepromReadString(int addr) {
  String s;
  char c;
  while ((c = EEPROM.read(addr++)) != 0x00) {
     s += c;
  }
  return s;
}

