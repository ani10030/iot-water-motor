#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266Ping.h>
// Libraries for Running Async Server for Web Serial Monitor
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
// Library for Time
#include <NTPClient.h>
#include <WiFiUdp.h>
// SSL Library
#include <WiFiClientSecureBearSSL.h>

bool internet_connected = false;
AsyncWebServer server(80);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// HTML web page to handle 3 input fields (input1, input2, input3)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>ESP01 - Home Automation Configuration</title><meta name="viewport" content="width=device-width, initial-scale=1"><style>table{font-family: arial, sans-serif; border-collapse: collapse; width: 500px;}td, th{border: 1px solid #dddddd; text-align: left; padding: 8px;}tr:nth-child(even){background-color: #dddddd;}</style></head><body><h1>ESP01 - Home Automation Configuration</h1><form action="/submit_config" method="post"><table> <tr><th>Configuration Variable</th><th>Value</th> </tr><tr><td>TriggerHour1</td><td><input type="text" name="TriggerHour1" required></td></tr><tr><td>TriggerMinute1</td><td><input type="text" name="TriggerMinute1" required></td></tr><tr><td>TriggerHour2</td><td><input type="text" name="TriggerHour2" required></td></tr><tr><td>TriggerMinute2</td><td><input type="text" name="TriggerMinute2" required></td></tr><tr><td>HTTPSWebhookURL</td><td><input type="text" name="HTTPSWebhookURL"></td></tr><tr><td>HTTPSFingerprint</td><td><input type="text" name="HTTPSFingerprint"></td></tr><tr><td>LEDPin2BlinkDelay</td><td><input type="text" name="LEDPin2BlinkDelay"></td></tr><tr><td>MaxTriesBeforeTurnOFF</td><td><input type="text" name="MaxTriesBeforeTurnOFF"></td></tr><tr><td>DelayAfterTurnOFF (Minutes)</td><td><input type="text" name="DelayAfterTurnOFF"></td></tr><tr><td>NormalLoopDelay (Minutes)</td><td><input type="text" name="NormalLoopDelay"></td></tr><tr><td></td><td><input type="submit" value="Submit"></td></tr></table></form></body></html>
)rawliteral";

bool initialization = 0;
int TriggerHour1;
int TriggerMinute1;
int TriggerHour2;
int TriggerMinute2;
String HTTPSWebhookURL = "https://www.your-webhoook-here.com";
String DeviceMAC;
String DeviceType = "204";
String HTTPSFingerprint = "";
int LEDPin2BlinkDelay = 1000;
int MaxTriesBeforeTurnOFF = 1800;
int DelayAfterTurnOFF = 6;
int NormalLoopDelay = 2;

void WebSerialPrint(String message)
{
  Serial.print(message);
  WebSerial.print(message);
}

void WebSerialPrintln(String message)
{
  Serial.println(message);
  WebSerial.println(message);
}

String LocalTime()
{
  timeClient.update();
  
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime); 
  int currentYear = ptm->tm_year+1900;
  int currentMonth = ptm->tm_mon+1;
  int monthDay = ptm->tm_mday;
  String timestamp = String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay) + " "+ String(timeClient.getFormattedTime());
  
  return timestamp;
}

int SwitchCommand(String mode){
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;
  if(https.begin(*client,HTTPSWebhookURL+"?operation_type="+mode)) {  // HTTPS
    WebSerialPrintln(LocalTime()+" -> HTTPS Initiated for TURN "+mode);
    
    int httpCode = https.GET();
    // httpCode will be negative on error
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        WebSerialPrintln(LocalTime()+" -> HTTP Code : "+String(httpCode));
        WebSerialPrintln(LocalTime()+" -> Command Send Successfully.");
      }
    } else {
      WebSerialPrintln(LocalTime()+" -> FAILED. Error Message : "+String(https.errorToString(httpCode).c_str()));
    }
    https.end();
    return httpCode;
  }
  else {
    WebSerialPrintln(LocalTime()+" -> HTTPS Connection FAILED.");
    return 0;
  }
}

void SwitchControl() {
  WebSerialPrintln(LocalTime()+" -> Sending TURN ON Command to Switch");
  int httpCode = SwitchCommand("ON");
  if(httpCode==200){
    WebSerialPrintln(LocalTime()+" -> Waiting until OFF command recieved."); 
    for (int i = 0; i <= MaxTriesBeforeTurnOFF; i++){
      // Look for External Interrupt
      if ( digitalRead(3) != 0 ) { 
        // Blink LED at Pin 2 until External Interrupt Detected
        delay (LEDPin2BlinkDelay);
        digitalWrite(2,0);
        WebSerialPrint("*");
        delay(LEDPin2BlinkDelay);
        digitalWrite(2,1);
        
        if(i == MaxTriesBeforeTurnOFF){ //Force Stop after Time Limit Reached
          WebSerialPrintln("");
          WebSerialPrintln(LocalTime()+" -> Switch Connection Limit Reached. Turning OFF.");
          SwitchCommand("OFF");
          digitalWrite(2,0);
          WebSerialPrintln(LocalTime()+" -> Putting a "+String(DelayAfterTurnOFF)+" minutes delay after TURN OFF.");
          delay(DelayAfterTurnOFF*60000); // 6 Minutes Delay
          break;
        }
      }
      else{
        WebSerialPrintln("");
        WebSerialPrintln(LocalTime()+" -> External Interrupt Detected. Sending Turn OFF command.");
        SwitchCommand("OFF");
        digitalWrite(2,0);
        WebSerialPrintln(LocalTime()+" -> Putting a "+String(DelayAfterTurnOFF)+" minutes delay after TURN OFF.");
        delay(DelayAfterTurnOFF*60000); // 6 Minutes Delay
        break;
      }
    }
  }
  else{
    WebSerialPrintln(LocalTime()+" -> Turn OFF Failed. HTTP Code != 200");
  }
}

void WiFi_Connect(String ssid, String password,String connect_type)
{
  Serial.println("");
  Serial.println(LocalTime()+" -> Disconnecting WiFi...");
  WiFi.disconnect();
  delay(3000);  
  
  if(WiFi.status() == 3){
    Serial.println(LocalTime()+" -> Current WiFi Status : CONNECTED");
  }
  else{
    Serial.println(LocalTime()+" -> Current WiFi Status : DISCONNECTED");
  }
  
  Serial.print(LocalTime()+" -> Attempting WiFi ");
  Serial.println(connect_type);
  Serial.println("");
  
  // Connect-Reconnect WiFi  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  // Wait for Connection
  for (int i = 0; i < 50; i++)
  {
    if ( WiFi.status() != WL_CONNECTED ) {
      // Blink LED at Pin 2 until Connected
      delay (250);
      digitalWrite(0,0);
      Serial.print(".");
      delay(250);
      digitalWrite(0,1);
    }
  }
  Serial.println("");
  if(WiFi.status() == 3){
    Serial.println(LocalTime()+" -> WiFi Status : CONNECTED");
  }
  else{
    Serial.println(LocalTime()+" -> WiFi Status : DISCONNECTED");
  }
  digitalWrite(0,0);  // LED at Pin 2 - OFF
  Serial.println("");
}

void setup() {
  // put your setup code here, to run once:
  pinMode(2,OUTPUT);
  pinMode(0,OUTPUT);
  pinMode(3,INPUT);
  
  Serial.begin(115200,SERIAL_8N1,SERIAL_TX_ONLY);
  Serial.println();
  Serial.println("....Initializing Setup....");

  WiFi_Connect("WiFi Name", "Password","CONNECT");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  DeviceMAC = String(WiFi.macAddress());

  // Initialize HTML Configuration Form
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  server.on("/submit_config", HTTP_POST, [] (AsyncWebServerRequest *request) {    
    if(initialization == 0){
      WebSerialPrintln("");
      initialization = 1;
      String PARAM_OUTPUT_1 = request->getParam("TriggerHour1",true)->value();
      sscanf(PARAM_OUTPUT_1.c_str(),"%d",&TriggerHour1);
      WebSerialPrintln("TriggerHour1 -> " + PARAM_OUTPUT_1);
      
      String PARAM_OUTPUT_2 = request->getParam("TriggerMinute1",true)->value();      
      sscanf(PARAM_OUTPUT_2.c_str(),"%d",&TriggerMinute1);
      WebSerialPrintln("TriggerMinute1 -> " + PARAM_OUTPUT_2);
      
      String PARAM_OUTPUT_3 = request->getParam("TriggerHour2",true)->value();
      sscanf(PARAM_OUTPUT_3.c_str(),"%d",&TriggerHour2);
      WebSerialPrintln("TriggerHour2 -> " + PARAM_OUTPUT_3);
      
      String PARAM_OUTPUT_4 = request->getParam("TriggerMinute2",true)->value();
      sscanf(PARAM_OUTPUT_4.c_str(),"%d",&TriggerMinute2);
      WebSerialPrintln("TriggerMinute2 -> " + PARAM_OUTPUT_4);
      
      String PARAM_OUTPUT_5 = request->getParam("HTTPSWebhookURL",true)->value();
      WebSerialPrintln("HTTPSWebhookURL -> " + PARAM_OUTPUT_5);
      
      String PARAM_OUTPUT_6 = request->getParam("HTTPSFingerprint",true)->value();
      WebSerialPrintln("HTTPSFingerprint -> " + PARAM_OUTPUT_6);
      
      String PARAM_OUTPUT_7 = request->getParam("LEDPin2BlinkDelay",true)->value();
      sscanf(PARAM_OUTPUT_7.c_str(),"%d",&LEDPin2BlinkDelay);
      WebSerialPrintln("LEDPin2BlinkDelay -> " + PARAM_OUTPUT_7);
      
      String PARAM_OUTPUT_8 = request->getParam("MaxTriesBeforeTurnOFF",true)->value();
      sscanf(PARAM_OUTPUT_8.c_str(),"%d",&MaxTriesBeforeTurnOFF);
      WebSerialPrintln("MaxTriesBeforeTurnOFF -> " + PARAM_OUTPUT_8);
      
      String PARAM_OUTPUT_9 = request->getParam("DelayAfterTurnOFF",true)->value();
      sscanf(PARAM_OUTPUT_9.c_str(),"%d",&DelayAfterTurnOFF);
      WebSerialPrintln("DelayAfterTurnOFF -> " + PARAM_OUTPUT_9);
      
      String PARAM_OUTPUT_10 = request->getParam("NormalLoopDelay",true)->value();
      sscanf(PARAM_OUTPUT_10.c_str(),"%d",&NormalLoopDelay);
      WebSerialPrintln("NormalLoopDelay -> " + PARAM_OUTPUT_10);
      
      request->send(200, "text/html", "Values Submitted Successfully.<br><br><a href=\"/\">Return to Home Page</a>");
    }
    else{
      request->send(200, "text/html", "Configuration already complete.");
    }
  });

  server.on("/reset", HTTP_GET, [] (AsyncWebServerRequest *request) {    
    request->send(200, "text/html", "Resetting ESP...");
    Serial.println("Resetting ESP...");
    ESP.restart();
  });
    
  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&server);
  server.begin();
  Serial.println("Web Serial Server in ON.");

  // Initialize a NTPClient to get time
  timeClient.begin();
  // GMT +5.5 = 19800
  timeClient.setTimeOffset(19800);
  WebSerialPrint("Time Sync Complete. Current Time : ");
  WebSerialPrintln(LocalTime());

  WebSerialPrintln("-- Waiting For Config Initialization --");
  int waitRetryNumber = 0;
  WebSerialPrintln("");
  while(initialization == 0 && waitRetryNumber <= 60){
    waitRetryNumber = waitRetryNumber+1;
    WebSerialPrint("-");
    delay(1000);
  }
  WebSerialPrintln("");
  if(waitRetryNumber >60){
    WebSerialPrintln("Resetting ESP...");
    ESP.restart();
  }

  WebSerialPrintln("");
  WebSerialPrintln("** Initialization Complete **");
}

void loop() {
  // Check If WiFi is connected, else attempt reconnect
  if (WiFi.status() != WL_CONNECTED) {
    internet_connected = false;
    Serial.println(LocalTime()+" -> [X] WiFi Disconnected [X]");
    WiFi_Connect("WiFi Name", "Password","RECONNECT");
  }
  else{
    WebSerialPrint(LocalTime()+" -> Pinging Host : ");
    WebSerialPrintln("www.google.co.in");
    
    if(Ping.ping("www.google.co.in")){
      internet_connected = true;
      digitalWrite(0,1); // Turn ON LED at Pin 0
      WebSerialPrintln(LocalTime()+" -> Ping Successful. Connected to Internet.");
      
      // Trigger Events if Time matches
      int currentHour = timeClient.getHours();
      int currentMinute = timeClient.getMinutes();
      if( (currentHour==TriggerHour1 && (currentMinute>=TriggerMinute1 && currentMinute<=TriggerMinute1+5) ) ||
          (currentHour==TriggerHour2 && (currentMinute>=TriggerMinute2 && currentMinute<=TriggerMinute2+5) )){
          
          WebSerialPrintln(LocalTime()+" -> Triggering Scheduled Event...");
          SwitchControl();
      }
      else if(digitalRead(3) == 0) //External Interrupt is still coming.. Just make a log
      {
        WebSerialPrintln(LocalTime()+" ->Another External Interrupt Detected. No Action Required.");
        delay(NormalLoopDelay * 60000); //Loop Repeat After 1 Minute
      }
      else
      {
        digitalWrite(2,0); // Turn OFF LED at Pin 2
        delay(NormalLoopDelay * 60000); //Loop Repeat After 1 Minute
      }
    }
    else{
      internet_connected = false;
      WebSerialPrintln(LocalTime()+" -> Ping Failed. Trigger WiFi Reconnect...");
      digitalWrite(0,0); // Turn OFF LED at Pin 0
      WiFi_Connect("WiFi Name", "Password","RECONNECT");
    }
    digitalWrite(2,0); // Turn OFF LED at Pin 2
  }
}
