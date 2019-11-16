#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define DHT_DEBUG
#include <DHT.h>

// Generic ESP 8266
// 3V power
// To program lock GPIO0 to GND !
// 115200 serial debug output!

#include "user_interface.h"

#define STASSID "WiFiSirenevaya8V"
//#define STASSID "WifiSirenevaya8VL2"
#define STAPSK  "34567890"

const char * SSID = STASSID;
const char * PSK = STAPSK;

ESP8266WebServer server(80);

// Digital pin connected to the DHT sensor
#define DHTPIN 2
#define DHTTYPE DHT11   // DHT 11

DHT dht(DHTPIN, DHTTYPE);

float s_humid;
float s_temp;
float s_hidx;
int   s_errs = 0;

//int led = 0; // TXD!!!

//#define SERDEBUG 1

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PSK);
  Serial.println("connecting");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println(WiFi.localIP());
  Serial.print(
    "WL_IDLE_STATUS      = 0\n"
    "WL_NO_SSID_AVAIL    = 1\n"
    "WL_SCAN_COMPLETED   = 2\n"
    "WL_CONNECTED        = 3\n"
    "WL_CONNECT_FAILED   = 4\n"
    "WL_CONNECTION_LOST  = 5\n"
    "WL_DISCONNECTED     = 6\n"
  );

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

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  dht.begin();

}

void dhtloop() {
  // Wait a few seconds between measurements.
  delay(100);

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
    s_errs++;
    return;
  }

  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);

  s_temp = t;
  s_humid = h;
  s_hidx = hic;
  s_errs = 0;

#if SERDEBUG
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.print(F("°C "));
  Serial.print(F("  Heat index: "));
  Serial.print(hic);
  Serial.print(F("°C "));
  Serial.println("");
#endif
}

void handleRoot() {
  auto node = WiFi.localIP().toString();
  String message = "{ node: '"+node+"', temp: "+s_temp+", humid: "+s_humid+", hindex: "+s_hidx+ ", err: "+ s_errs + " }";
  server.send(200, "text/plain", message);
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
  server.handleClient();
  MDNS.update();

  dhtloop();
}
