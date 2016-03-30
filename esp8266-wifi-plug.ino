#include <ESP8266WiFi.h>

//needed for WifiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

//for WifiManager LED status
#include <Ticker.h>

#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <FS.h>

//PINs on the Arduino
#define LED_0 0 // LED PIN
#define RELAY 2 // Relay PIN

#define USE_SERIAL Serial

Ticker ticker;

// multicast DNS responder
MDNSResponder mdns;

// TCP server at port 80 will respond to HTTP requests
ESP8266WebServer server(80);

//WiFiManager
//Local intialization. Once its business is done, there is no need to keep it around
WiFiManager wifiManager;

const String HTTP_404 = "<!DOCTYPE html><html><head><meta charset=\"utf-8\" /><title>Wifi Plug Control</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body class=\"error\"><section><h1>404</h1><h2>Oops! The page you are looking for does not exist.</h2></section><section class=\"button\"><h3><a href=\"/\">&#xf015;</a></section></body></html>";
const String openingHtml = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>";
const String closingHtml = "</html>\r\n\r\n";

void tick()
{
  //toggle state
  int state = digitalRead(LED_0);  // get the current state of GPIO1 pin
  digitalWrite(LED_0, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  USE_SERIAL.println("Entered config mode");
  USE_SERIAL.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  USE_SERIAL.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  else if(filename.endsWith(".eot")) return "application/vnd.ms-fontobject";
  else if(filename.endsWith(".svg")) return "image/svg+xml";
  else if(filename.endsWith(".ttf")) return "application/octet-stream";
  else if(filename.endsWith(".woff")) return "application/x-font-woff";
  return "text/plain";
}

bool handleFileRead(String path){
  USE_SERIAL.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    USE_SERIAL.println("File exists: " + path);
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleOn() {
  digitalWrite(RELAY, LOW);
  digitalWrite(LED_0, HIGH);
  USE_SERIAL.println("Sending 200");
  handleFileRead("/");
}

void handleOff() {
  digitalWrite(RELAY, HIGH);
  digitalWrite(LED_0, LOW);
  USE_SERIAL.println("Sending 200");
  handleFileRead("/");
}

void handleClear() {
  String s = openingHtml;
  s += "Hello from ESP8266";
  s += "<p>Clearing the settings.<p>";
  s += closingHtml;
  USE_SERIAL.println("Sending 200");
  server.send(200, "text/plain", s);

  wifiManager.resetSettings();

  WiFi.disconnect();      //after EEPROM is cleared, disconnect from the current wifi access
  ESP.reset();
}

void setup() {
  USE_SERIAL.begin(115200);

  //Setup LED Diagnostic
  pinMode(LED_0, OUTPUT);

  //Relay pin
  pinMode(RELAY, OUTPUT);

  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  //WiFiManager wifiManager;
  //reset settings - for testing
  //wifiManager.resetSettings();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    USE_SERIAL.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  //if you get here you have connected to the WiFi
  USE_SERIAL.println("Connected...");
  ticker.detach();

  //keep LED on
  digitalWrite(LED_0, LOW);

  SPIFFS.begin();
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      USE_SERIAL.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    USE_SERIAL.printf("\n");
  }

  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp8266.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  if (!mdns.begin("esp8266", WiFi.localIP())) {
    USE_SERIAL.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  USE_SERIAL.println("mDNS responder started");

  //Server Init
  server.on("/on", HTTP_GET, handleOn);
  server.on("/off", HTTP_GET, handleOff);
  server.on("/clear",HTTP_GET, handleClear);

  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/html", HTTP_404);
  });

  // Start TCP (HTTP) server
  server.begin();
  USE_SERIAL.println("TCP server started");
}

void loop() {

  // Check for any mDNS queries and send responses
  mdns.update();
  server.handleClient();

}
