#include <NTPClient.h>

#include <DHT.h>
#include <DHT_U.h>

#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#ifndef STASSID
#define STASSID "Xoxolhouse"
#define STAPSK  "14881488"

#define DHTTYPE DHT11
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

ESP8266WebServer server(80);

const int led = 2;

// датчик DHT
uint8_t DHTPin = D5; 

uint8_t heaterPin = D1;
uint8_t lightPin = D2;
uint8_t vapePin = D3;
uint8_t fanPin = D4;

float minTemp = 25;
float maxTemp = 27;

float minHym = 30;
float maxHym  = 50;

               
// инициализация датчика DHT.
DHT dht(DHTPin, DHTTYPE); 

// Define NTP Client to get time
const long utcOffsetInSeconds = 10800;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

void checkSensors(){
    if (dht.readTemperature()<minTemp){
      digitalWrite(heaterPin,HIGH);
    }
    if (dht.readTemperature()>maxTemp){
      digitalWrite(heaterPin,LOW);
    }
    
    if (dht.readHumidity()<minHym){
      digitalWrite(vapePin,HIGH);
    }
    if (dht.readHumidity()>maxHym){
      digitalWrite(vapePin,LOW);
    }
  
}

void handleRoot() {
  digitalWrite(led, 1);
  server.send(200, "text/plain", "hello from esp8266!\r\n");
  digitalWrite(led, 0);
}

void handleNotFound() {
  digitalWrite(led, 1);
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
  digitalWrite(led, 0);
}


    boolean digitalToBoolean(uint8_t pin){
      boolean result = false;
      if (digitalRead(pin) == HIGH) result = true;
      if (digitalRead(pin) == LOW) result = false;
      return result;
    }
    
void setup(void) {


  
  pinMode(DHTPin, INPUT);
  dht.begin();

  
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.on("/status",[](){

        DynamicJsonDocument doc(1024);        
        doc["temperature"] = dht.readTemperature();
        doc["hymidity"] = dht.readHumidity();
        doc["heater"] = digitalToBoolean(heaterPin);
        doc["light"] = digitalToBoolean(lightPin);
        doc["vape"] = digitalToBoolean(vapePin);
        doc["fan"] = digitalToBoolean(fanPin);

        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });


  server.on("/getSettings",[](){

        DynamicJsonDocument doc(1024);
        doc["minTemp"] = minTemp;
        doc["maxTemp"] = maxTemp;

        doc["minHym"] = minHym;
        doc["maxHym "] = maxHym ;

        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });


  server.onNotFound(handleNotFound);

  /////////////////////////////////////////////////////////
  // Hook examples

  server.addHook([](const String & method, const String & url, WiFiClient * client, ESP8266WebServer::ContentTypeFunction contentType) {
    (void)method;      // GET, PUT, ...
    (void)url;         // example: /root/myfile.html
    (void)client;      // the webserver tcp client connection
    (void)contentType; // contentType(".html") => "text/html"
    Serial.printf("A useless web hook has passed\n");
    Serial.printf("(this hook is in 0x%08x area (401x=IRAM 402x=FLASH))\n", esp_get_program_counter());
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });

  server.addHook([](const String&, const String & url, WiFiClient*, ESP8266WebServer::ContentTypeFunction) {
    if (url.startsWith("/fail")) {
      Serial.printf("An always failing web hook has been triggered\n");
      return ESP8266WebServer::CLIENT_MUST_STOP;
    }
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });

  server.addHook([](const String&, const String & url, WiFiClient * client, ESP8266WebServer::ContentTypeFunction) {
    if (url.startsWith("/dump")) {
      Serial.printf("The dumper web hook is on the run\n");

      // Here the request is not interpreted, so we cannot for sure
      // swallow the exact amount matching the full request+content,
      // hence the tcp connection cannot be handled anymore by the
      // webserver.
#ifdef STREAMSEND_API
      // we are lucky
      client->sendAll(Serial, 500);
#else
      auto last = millis();
      while ((millis() - last) < 500) {
        char buf[32];
        size_t len = client->read((uint8_t*)buf, sizeof(buf));
        if (len > 0) {
          Serial.printf("(<%d> chars)", (int)len);
          Serial.write(buf, len);
          last = millis();
        }
      }
#endif
      // Two choices: return MUST STOP and webserver will close it
      //                       (we already have the example with '/fail' hook)
      // or                  IS GIVEN and webserver will forget it
      // trying with IS GIVEN and storing it on a dumb WiFiClient.
      // check the client connection: it should not immediately be closed
      // (make another '/dump' one to close the first)
      Serial.printf("\nTelling server to forget this connection\n");
      static WiFiClient forgetme = *client; // stop previous one if present and transfer client refcounter
      return ESP8266WebServer::CLIENT_IS_GIVEN;
    }
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });

  // Hook examples
  /////////////////////////////////////////////////////////

  server.begin();
  Serial.println("HTTP server started");
  timeClient.begin();
  timeClient.update();
}

void loop(void) {
  Serial.print("Temperature = ");
  Serial.println(dht.readTemperature());
  Serial.print("Humidity = ");
  Serial.println(dht.readHumidity());
  delay(1000);
  checkSensors();
  getTime();
  
  server.handleClient();
  MDNS.update();  

}

void getTime(){

  timeClient.update();
  Serial.print(timeClient.getHours());
  Serial.print(":");
  Serial.print(timeClient.getMinutes());
  Serial.print(":");
  Serial.println(timeClient.getSeconds());
  //Serial.println(timeClient.getFormattedTime());
}
