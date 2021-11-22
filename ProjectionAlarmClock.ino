#include <Arduino.h>
//https://github.com/olikraus/u8g2/wiki/u8g2reference#drawstr
#include <U8g2lib.h>
//https://github.com/olikraus/u8g2
// V 2.31.2
#include <ArduinoJson.h>
//https://arduinojson.org/
//6.18.4
#include <PubSubClient.h>
//https://pubsubclient.knolleary.net/
//V 2.8.0
#include "uptime.h"
//https://github.com/YiannisBourkelis/Uptime-Library
// V 1.0.0
#include <NTPClient.h>
// https://github.com/arduino-libraries/NTPClient
// v 3.2.0
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <WiFi.h>
//-----------
#include "config.h"
#include "version.h"

char *day_from_number[] = {"Monday", "Tuesday", "Wednesday",
                     "Thursday", "Firday", "Saturday", "Sunday"
                    };

void callback(char *topic, byte *payload, unsigned int length) {
  //Should figure out max payload size and do memory allocation correctly.
  StaticJsonDocument<1024> r_doc;
  deserializeJson(r_doc, payload, length);
  timeClient.update();
  int mqttRxTime = timeClient.getEpochTime();
  // start a mqtt message buffer
  char mqtt_log_message[256];
  sprintf(mqtt_log_message, "RX at %d ", mqttRxTime);
  if (r_doc["set_polling"]) {
    if (r_doc["set_polling"].as<int>() != 0){
        polling_rate = r_doc["set_polling"].as<int>()*1000;
        size_t offset = strlen(mqtt_log_message);
        sprintf(&(mqtt_log_message[offset]), " Setting the polling rate at %d seconds" , polling_rate/1000); //38+10
      }
    }
  if (r_doc["set_info"]) {
    if (r_doc["set_info"].as<int>() != 0){
      send_info_rate = r_doc["set_info"].as<int>()*1000;
      size_t offset = strlen(mqtt_log_message);
      sprintf(&(mqtt_log_message[offset]), " Setting the info send rate at %d seconds" , send_info_rate/1000);
    }
  }
  mqttLog(mqtt_log_message);
}
void mqttLog(const char* str) {
  if (mqtt_client.connected()){
    DynamicJsonDocument doc(256);
    timeClient.update();
    int mqtt_log_time = timeClient.getEpochTime();
    doc["time"] = mqtt_log_time;
    doc["msg"] = str;
    char buffer[256];
    size_t n = serializeJson(doc, buffer);
    mqtt_client.publish(mqtt_log_topic, buffer, n);
  }else{
    // print to serial
    // also figure out a way to store.
  }
}
void mqttConnect() {
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqtt_client.connect(mqttClientName, mqttUsername, mqttPassword, willTopic, willQoS, willRetain, willMessage)) {
      Serial.println("Connection Complete");
      Serial.println(willTopic);
      delay(1000);
      mqtt_client.publish(willTopic, "online", true);
      // ...
      // Subcribe here.
      mqtt_client.subscribe(mqtt_command_topic);
      } else {
      Serial.print("failed, rc = ");
      Serial.print(mqtt_client.state());
      Serial.println(" Trying again in 5 seconds");
      delay(5000);
    }
  }
}
void send_info(){
  timeClient.update();
  int epochDate = timeClient.getEpochTime();
  DynamicJsonDocument doc(512);
  doc["t"] = epochDate; //1+10
  doc["Wifi IP"] = ip_char; //7+16
  doc["Code Version"] = _VERSION; //12+21
  doc["polling_rate"] = polling_rate/1000; //12+8 1000 days is 8 digits in seconds
  doc["info_rate"] = send_info_rate/1000; //9+8
  // doc["info_rate_seconds"] = polling_rate*send_info_rate; //17+16 max # digits of product of two 8 digit
  uptime::calculateUptime();
  char readable_time[12];
  sprintf(readable_time, "%02d:%02d:%02d:%02d", uptime::getDays(),uptime::getHours(),uptime::getMinutes(),uptime::getSeconds());
  doc["Uptime"] = readable_time; //6+12
  //155
  char buffer[256];
  size_t n = serializeJson(doc, buffer);
  mqtt_client.publish(mqtt_info_topic, buffer, n);
  mqtt_client.loop();
}

struct data_frame {
  float AHTt;
  float AHTh;
};

#include <Adafruit_AHTX0.h>
//https://github.com/adafruit/Adafruit_AHTX0
// V 2.0.1
Adafruit_AHTX0 aht;
bool aht_found;
bool aht_started;

float aht_h_storage = 0;
float aht_t_storage = 0;

int start_aht(){
  aht_found = aht.begin();
  if (!aht_found) {
    Serial.println("Could not find a valid AHTX0 sensor, check wiring!");
    return 0;
  }else{
    Serial.println("AHTX0 started.");
    return 1;
  }
}

void read_AHT(struct data_frame &dataframe){
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  dataframe.AHTt = temp.temperature;
  if (dataframe.AHTt == 0){
    dataframe.AHTt = aht_t_storage;
  }
  dataframe.AHTh = humidity.relative_humidity;
  if (dataframe.AHTh == 0){
    dataframe.AHTh = aht_h_storage;
  }
  aht_t_storage = dataframe.AHTt;
  aht_h_storage = dataframe.AHTh;


}

void send_data(struct data_frame d_frame){
  timeClient.update();
  int epochDate = timeClient.getEpochTime();
  DynamicJsonDocument doc(1024);
  doc["t"] = epochDate;

  char AHTt[15];
  char AHTh[15];
  sprintf(AHTt, "%.1f", d_frame.AHTt);
  sprintf(AHTh, "%.1f", d_frame.AHTh);
  doc["aht_t"] = AHTt;
  doc["aht_h"] = AHTh;

  char buffer[256];
  size_t n = serializeJson(doc, buffer);
  mqtt_client.publish(json_data_topic, buffer, n);
  mqtt_client.loop();
}

void setup(void){
  Serial.begin(115200);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
  timeClient.begin();
  Serial.println("Time Client Started");
  Serial.println("WiFi connected:");
  IPAddress ip = WiFi.localIP();
  Serial.print(ip);
  Serial.println("/");

  // Generate some info once for info topic
  sprintf(ip_char, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  //BMP280
  aht_started = start_aht();
  /* u8g2.begin() is required and will sent the setup/init sequence to the display */
  u8g2.begin();

  timeClient.begin();

  mqtt_client.setServer(mqttServerIp, mqttServerPort);
  mqtt_client.setCallback(callback);
  delay(500);
}


void loop(void) {
  if (!mqtt_client.connected()) {
   mqttConnect();
  }
  mqtt_client.loop();
  currentMillis = millis();
  timeClient.update();
  String current_time = timeClient.getFormattedTime();
  int str_len = current_time.length() + 1;
  char char_array[str_len];
  current_time.toCharArray(char_array, str_len);
  int temp_day = timeClient.getDay();
  if (temp_day == 0){
    temp_day = 7;
  }
  struct data_frame dframe;
  if (currentMillis - last_poll_Millis >= send_info_rate){

    if (aht_started){
      read_AHT(dframe);
    }else{
    }
    send_data(dframe);
  }
  if (currentMillis - last_info_Millis >= send_info_rate){
    last_info_Millis = currentMillis;
    send_info();
  }

  u8g2.firstPage();
  do {
   u8g2.setFont(u8g2_font_ncenB10_tr);
   u8g2.drawStr(ALIGN_CENTER(char_array),15,char_array);
   u8g2.drawStr(ALIGN_CENTER(day_from_number[temp_day-1]),30,day_from_number[temp_day-1]);

   char aht_data[20];
   char aht_h[5];
   sprintf(aht_data, "%.1f", dframe.AHTt);
   strcat (aht_data,"C ");
   sprintf(aht_h, "%.1f", dframe.AHTh);
   strcat (aht_data,aht_h);
   strcat (aht_data,"%");

   u8g2.drawStr(ALIGN_CENTER(aht_data),45,aht_data);

     } while ( u8g2.nextPage() );
  delay(1000);
}
