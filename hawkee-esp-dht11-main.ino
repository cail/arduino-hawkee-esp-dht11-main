#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <InfluxDb.h>
#include <DHT.h>
#include "user_interface.h"
#include <OneWire.h>
#include <DallasTemperature.h>


// Generic ESP 8266
// 3V power
// To program lock GPIO0 to GND !
// 115200 serial debug output!

/* 
 * 7500mah / 36 hrs = 200 ma
 * 0.07 => 0.01
 * 
 * 7500 / 5 / 24 = 40mah
 * 
 * spec: 15ma with wifi
 * 
 * 25-04: 6 days 3*2700mah
 * 
 * esp-12f: 66ma wifi, 600ua deepsleep - HOORAAY!
 * 
 */

#define STASSID { "WiFiSirenevaya8V", "IOT", "HOME", }

#define STAPSK  { "34567890", "56789012", "12345678", "" }

#define INFLUXDB_HOST "us-west-2-1.aws.cloud2.influxdata.com"
#define INFLUXDB_FINGER "9B 62 0A 63 8B B1 D2 CA 5E DF 42 6E A3 EE 1F 19 36 48 71 1F"

// Digital pin connected to the DHT sensor
#define DHTPIN  2
#define DHTTYPE DHT11

#define ONE_WIRE_BUS 0

#define READ_DALLAS 0

#define POWER_SAVE 0

#define POWER_SAVE_DEEP 1

/*
#define USESERIAL 1
#define SERDEBUG 1
#define SERVER 1
/*/
#define USESERIAL 0
#define SERDEBUG 0
#define SERVER 0
//*/

#if USESERIAL
  #define SERIAL_PRINT(...) do { Serial.print(__VA_ARGS__); } while(0)
  #define SERIAL_PRINTLN(...) do { Serial.println(__VA_ARGS__); } while(0)
#else
  #define SERIAL_PRINT(...) do {} while(0)
  #define SERIAL_PRINTLN(...) do {} while(0)
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

Influxdb influx(INFLUXDB_HOST);

#if SERVER
ESP8266WebServer server(80);
#endif

DHT dht(DHTPIN, DHTTYPE);

bool  s_is_read_dht;
float s_humid;
float s_temp;
float s_hidx;
int   s_errs = 0;
uint32 s_tick = 0;

int s_sleep = 5*60*1000;
//int s_sleep = 10*1000;

OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

// function to print a device address
void getAddress(DeviceAddress deviceAddress, char *dname)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    sprintf(dname, "%02x", deviceAddress[i]);
    dname +=2;
    if (i < 7) {
      *dname = ':';
      dname++;
    }
  }
  *dname = 0;
}

// found wifi address
int ssid_idx_stored = -1;
int psk_idx_stored = -1;

void connect()
{
  int ssid_idx = 0;
  int psk_idx = 0;
  const char* SSIDS[] = STASSID;
  const char* PSKS[] = STAPSK;
  
  do{    
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSIDS[ssid_idx], PSKS[psk_idx]);
    Serial.println("connecting");
  
    int tmo = 10;
    while (WiFi.status() != WL_CONNECTED && tmo--) {
      delay(500);
      SERIAL_PRINT(".");
    }
    SERIAL_PRINT("WiFi Status: ");
    SERIAL_PRINTLN(WiFi.status());
    
    ssid_idx = (ssid_idx+1) % ARRAY_SIZE(SSIDS);
    psk_idx = (psk_idx+1) % ARRAY_SIZE(PSKS);
  } while (WiFi.status() != WL_CONNECTED);    

  SERIAL_PRINT("Free Heap ");
  SERIAL_PRINTLN(ESP.getFreeHeap());
  SERIAL_PRINT("WiFiStatus ");
  SERIAL_PRINTLN(WiFi.status());
  SERIAL_PRINT("STA-IP ");
  SERIAL_PRINTLN(WiFi.localIP());
  SERIAL_PRINT("AP-IP ");
  SERIAL_PRINTLN(WiFi.softAPIP());

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }
  
#if SERVER
  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
#endif

}

void setup() {

#if USESERIAL
  Serial.begin(115200);
  //Serial.setDebugOutput(true);
#endif

  connect();

  SERIAL_PRINTLN("DHT Init");
  dht.begin();

  SERIAL_PRINTLN("Influx Init");
  influx.setBucket("69740d3d80cb3f3e");
  influx.setOrg("04ee1a142e69eece");
  influx.setPort(443);
  influx.setToken("zX949yFx6PX_H5AlfmM0Ijrvbi7dlP1SevnxKdzQcH5JwAqyg0VW5tniTe09ZvMS0Cy2WmQwwU3xvvSZ7vzYiw==");
  influx.setVersion(2);
  influx.setFingerPrint(INFLUXDB_FINGER);
}

void read_sensors() {
  
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
#if SERDEBUG
    Serial.println(F("Failed to read from DHT sensor!"));
#endif
    s_is_read_dht = false;
    s_errs++;
    dht.begin();
  } else {

    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(t, h, false);
  
    s_temp = t;
    s_humid = h;
    s_hidx = hic;
    s_errs = 0;
  
  #if SERDEBUG
    SERIAL_PRINT(F("Humidity: "));
    SERIAL_PRINT(h);
    SERIAL_PRINT(F("%  Temperature: "));
    SERIAL_PRINT(t);
    SERIAL_PRINT(F("°C "));
    SERIAL_PRINT(F("  Heat index: "));
    SERIAL_PRINT(hic);
    SERIAL_PRINT(F("°C "));
    SERIAL_PRINTLN("");
  #endif
  }

}

void report_sensors()
{
  if (s_is_read_dht) {
    InfluxData row("temperature");
    row.addTag("device", WiFi.macAddress());
    row.addValue("value", s_temp);
    influx.prepare(row);
  
    InfluxData row1("humidity");
    row1.addTag("device", WiFi.macAddress());
    row1.addValue("value", s_humid);
    influx.prepare(row1);
  }

#if READ_DALLAS
  sensors.begin();
  int sensorsCount = sensors.getDeviceCount();
  
  SERIAL_PRINT("Found ");
  SERIAL_PRINT(sensorsCount, DEC);
  SERIAL_PRINTLN(" devices.");

  // report parasite power requirements
  SERIAL_PRINT("Parasite power is: "); 
  if (sensors.isParasitePowerMode())
    SERIAL_PRINTLN("ON");
  else
    SERIAL_PRINTLN("OFF");

  sensors.requestTemperatures();
  for(int i = 0; i < sensorsCount; i++)
  {
     DeviceAddress t;
     char dname[32];
     
     if (!sensors.getAddress(t, i))
         Serial.println("Unable to find address for Device" + i); 
    
    // set the resolution to 9 bit (Each Dallas/Maxim device is capable of several different resolutions)
    //sensors.setResolution(t, 9);
        
    getAddress(t, dname);
    float tempC = sensors.getTempC(t);
    
    // show the addresses we found on the bus
    SERIAL_PRINT("Device: ");
    SERIAL_PRINT(dname);
    SERIAL_PRINT(" = ");
    SERIAL_PRINT(tempC);
    SERIAL_PRINTLN();

    InfluxData rowt("temperature");
    //rowt.addTag("device", WiFi.macAddress());
    rowt.addTag("device", dname);
    rowt.addValue("value", tempC);
    influx.prepare(rowt);
  }

#endif

  InfluxData row2("stats");
  row2.addTag("device", WiFi.macAddress());
  row2.addValue("uptime", s_tick*s_sleep/1000);
  row2.addValue("err", s_errs);
  row2.addValue("vcc", ESP.getVcc());
  influx.prepare(row2);
  
  influx.write();

}

#if SERVER
void handleRoot() {
  //auto node = WiFi.localIP().toString();
  auto node = WiFi.macAddress();
  String message = "{ node: '"+node+"', temp: "+s_temp+", humid: "+s_humid+", hindex: "+s_hidx+ ", err: "+ s_errs + ", live:"+ s_tick + "` }";
  server.send(200, "text/html", message);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}
#endif

void loop(void) {

  bool toReconnect = false;
  if (WiFi.status() != WL_CONNECTED) {
    SERIAL_PRINTLN("Нет соединения WiFi");
    toReconnect = true;
  }
  if (toReconnect) {
    connect();
  }

#if SERVER
  server.handleClient();
  MDNS.update();
#endif

  read_sensors();
  report_sensors();

  //ESP.wdtEnable(5000);

#if POWER_SAVE
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  WiFi.disconnect(true);
  //WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 1000);
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
#endif

#if POWER_SAVE_DEEP
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  ESP.deepSleep(s_sleep*1000);
  // will not return
  //ESP.deepSleep(10*1000*1000, WAKE_RF_DISABLED);
#endif

  delay(s_sleep);
    
  s_tick += 1;
}
