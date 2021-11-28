#include <NTPClient.h>

#include <DHT.h>
#include <DHT_U.h>

#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>


#ifndef STASSID
#define STASSID "XoxloMiMiMix"
//#define STASSID "Xoxolhouse"
#define STAPSK  "14881488"

#define DHTTYPE DHT11
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

String JAVA_SERVER = "192.168.137.142:8080";

WiFiClient wifiClient;

ESP8266WebServer server(80);


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
  server.send(200, "text/plain", "hello from esp8266!\r\n");
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


    boolean digitalToBoolean(uint8_t pin){
      boolean result = false;
      if (digitalRead(pin) == HIGH) result = true;
      if (digitalRead(pin) == LOW) result = false;
      return result;
    }
    
void setup(void) {


  
  pinMode(DHTPin, INPUT);
  dht.begin();

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

  server.on("/status",[](){
        server.send(200, "application/json", getJsonFromSensors());
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
  delay(60000);
  checkSensors();
  getTime();
  postEspDataOnServer();
  
  server.handleClient();
  MDNS.update();  

}

String getTime(){  
  String dateTime ;
  timeClient.update();
  return timeClient.getFormattedTime();
}

void postEspDataOnServer(){

  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    HTTPClient http;    //Declare object of class HTTPClient
    String url = "http://" + JAVA_SERVER + "/espdata/save";
    http.begin(wifiClient,url);     //Specify request destination
    http.addHeader("Content-Type", "application/json");  //Specify content-type header
 
    int httpCode = http.POST(getJsonFromSensors());   //Send the request
    String payload = http.getString();                  //Get the response payload
 
    Serial.println(httpCode);   //Print HTTP return code
    Serial.println(payload);    //Print request response payload
 
    http.end();  //Close connection
  }else {
 
    Serial.println("Error in WiFi connection");
 
  }
}

String getJsonFromSensors(){
  DynamicJsonDocument doc(1024);        
        doc["temperature"] = dht.readTemperature();
        doc["humidity"] = dht.readHumidity();
        doc["heater"] = digitalToBoolean(heaterPin);
        doc["light"] = digitalToBoolean(lightPin);
        doc["vape"] = digitalToBoolean(vapePin);
        doc["fan"] = digitalToBoolean(fanPin);
        doc["time"] = timeClient.getFormattedTime();

        String json;
        serializeJson(doc, json);
        return json;
}
