#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <InfluxDb.h>

#define DHT_DEBUG
#include <DHT.h>

// Generic ESP 8266
// 3V power
// To program lock GPIO0 to GND !
// 115200 serial debug output!

/* 
 * 7500mah / 36 hrs = 200 ma
 * 0.07 => 0.01
 * 
 */

#include "user_interface.h"

#define STASSID "WiFiSirenevaya8V"
//#define STASSID "WifiSirenevaya8VL2"
#define STAPSK  "34567890"

#define INFLUXDB_HOST "us-west-2-1.aws.cloud2.influxdata.com"
#define INFLUXDB_FINGER "9B 62 0A 63 8B B1 D2 CA 5E DF 42 6E A3 EE 1F 19 36 48 71 1F"

Influxdb influx(INFLUXDB_HOST);

const char * SSID = STASSID;
const char * PSK = STAPSK;

ESP8266WebServer server(80);

// Digital pin connected to the DHT sensor
#define DHTPIN  2
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

float s_humid;
float s_temp;
float s_hidx;
int   s_errs = 0;
uint32 s_tick = 0;

int s_sleep = 5*60*1000;

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
  #define SERIAL_PRINT(...) { Serial.print(__VA_ARGS__); }
  #define SERIAL_PRINTLN(...) { Serial.println(__VA_ARGS__); }
#else
  #define SERIAL_PRINT(...) {}
  #define SERIAL_PRINTLN(...) {}
#endif


void connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PSK);
  Serial.println("connecting");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    SERIAL_PRINT(".");
  }
  
  Serial.println();
  Serial.println(WiFi.localIP());
/*  SERIAL_PRINT(
    "WL_IDLE_STATUS      = 0\n"
    "WL_NO_SSID_AVAIL    = 1\n"
    "WL_SCAN_COMPLETED   = 2\n"
    "WL_CONNECTED        = 3\n"
    "WL_CONNECT_FAILED   = 4\n"
    "WL_CONNECTION_LOST  = 5\n"
    "WL_DISCONNECTED     = 6\n"
  );
*/
#define TEST(name, var, varinit, func) \
  static decltype(func) var = (varinit); \
  if ((var) != (func)) { var = (func); Serial.printf("**** %s: ", name); Serial.println(var); }

  TEST("Free Heap", freeHeap, 0, ESP.getFreeHeap());
  TEST("WiFiStatus", status, WL_IDLE_STATUS, WiFi.status());
  TEST("STA-IP", localIp, (uint32_t)0, WiFi.localIP());
  TEST("AP-IP", apIp, (uint32_t)0, WiFi.softAPIP());

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
  influx.setToken("FOOBAR==");
  influx.setVersion(2);
  influx.setFingerPrint(INFLUXDB_FINGER);
}

void dhtloop() {

  bool is_read = true;
  
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
    is_read = false;
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

  if (is_read) {
    influx.setVersion(2);  
    InfluxData row("temperature");
    row.addTag("device", WiFi.macAddress());
    row.addValue("value", t);
    influx.write(row);
  
    influx.setVersion(2);
    InfluxData row1("humidity");
    row1.addTag("device", WiFi.macAddress());
    row1.addValue("value", h);
    influx.write(row1);
  }

  influx.setVersion(2);
  InfluxData row2("stats");
  row2.addTag("device", WiFi.macAddress());
  row2.addValue("uptime", s_tick*s_sleep/1000);
  row2.addValue("err", s_errs);
  row2.addValue("vcc", ESP.getVcc());
  influx.write(row2);

}

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


void loop(void) {

  bool toReconnect = false;
  if (WiFi.status() != WL_CONNECTED) {
    SERIAL_PRINTLN("Нет соединения WiFi");
    toReconnect = true;
  }
  if (toReconnect) {
    connect();
  }
  
  //server.handleClient();
  //MDNS.update();
  dhtloop();

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  //WiFi.disconnect(true);
  //WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 1000);
  //WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
  delay(s_sleep);
  
  //ESP.deepSleep(10000000);
  //ESP.deepSleep(10*1000*1000, WAKE_RF_DISABLED);
  //setup();
  
  s_tick += 1;
}
