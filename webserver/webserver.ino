/*********
  Terry Lee
  完整說明請參閱 http://honeststore.com.tw  
*********/

// 載入Wi-Fi資料庫
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <SimpleDHT.h> 
#include <HX711.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>
#include <TimeLib.h>
#include <TridentTD_LineNotify.h>   //匯入TridentTD_LineNotify程式庫

#define LINE_TOKEN "kWPDzA8A7SYJ3RFdWs2msWeDrdkC3SloP43bCDArR1Y"  //複製你的LINE權杖


// 將下列ssid及pass替換為您的網絡憑據
const char* ssid     = "pcdm";
const char* password = "pcdm3401";

// NTP 設定
const long utcOffsetInSeconds = 8 * 3600;  // GMT+8，台灣時區
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

const String uploadUrl = "http://api.thingspeak.com/update?api_key=2TQA2UWO2A0RXSE8";
// 設定web server端口為80 port
WiFiServer wifiserver(80);
ESP8266WebServer server(80);

const int pinDHT11 = 12;//修改腳位

//relayPin D5,
const int relayPin = 14 ;
SimpleDHT11 dht11(pinDHT11);//宣告SimpleDHT11物件
HX711 scale;
float calibration_factor = -906600; //-106600 worked for my 40Kg max scale setup
//90088424/906600
// 設定變數header為HTTP請求值
String header;
String content;

String fanState="OFF";
String foodTime = "";
int statusCode;

// 當前時間
unsigned long currentTime = millis();
// 前次時間
unsigned long previousTime = 0; 
// 定義暫時時間以毫秒為單位 (example: 2000ms = 2s)
const long timeoutTime = 2000;

//relay Low = 0,打開繼電器 ; High = 1,關閉繼電器
int state=HIGH;

//servoPin D4
const int servoPin = 2;    // 伺服馬達控制腳位
Servo servo;

void setup() {
  Serial.begin(115200);
  
  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // 列出得到的區域端IP及啟動web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  server.begin();

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);

  //  秤重感測器pin腳位
  //  scale.begin(D8(白色clk),D7(灰色dt))
  scale.begin(15, 13);
  scale.set_scale();
  scale.tare(); //Reset the scale to 0

  long zero_factor = scale.read_average(); //Get a baseline reading
  Serial.print("Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
  Serial.println(zero_factor);

  // 初始化時間
  timeClient.begin();
  timeClient.update();
  servo.attach(servoPin);
  servo.writeMicroseconds(1500);  // 將伺服馬達設定為中立位置

  LINE.setToken(LINE_TOKEN);  //設定要傳遞的權杖

}

void loop(){
  Serial.println("=============kg=========");
  scale.set_scale(calibration_factor); //Adjust to this calibration factor

//  float ca = (scale.get_units(), 3)-2.667;
  
  Serial.print("Reading: ");
  Serial.print(scale.get_units(), 3);
//  Serial.print("ca:"+ String(ca));
  Serial.print(" kg"); //Change this to kg and re-adjust the calibration factor if you follow SI units like a sane person
  Serial.print(" calibration_factor: ");
  Serial.print(calibration_factor);
  Serial.println();

  if(Serial.available())
  {
    char temp = Serial.read();
    if(temp == '+' || temp == 'a')
      calibration_factor += 10;
    else if(temp == '-' || temp == 'z')
      calibration_factor -= 10;
    else if(temp == 's')
      calibration_factor += 100;  
    else if(temp == 'x')
      calibration_factor -= 100;  
    else if(temp == 'd')
      calibration_factor += 1000;  
    else if(temp == 'c')
      calibration_factor -= 1000;
    else if(temp == 'f')
      calibration_factor += 10000;  
    else if(temp == 'v')
      calibration_factor -= 10000;  
    else if(temp == 't')
      scale.tare();  //Reset the scale to zero
  }
  Serial.println("=============");
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("溫度計讀取失敗，錯誤碼="); Serial.println(err);delay(3000);
//    return;
  }
  //讀取成功，將溫濕度顯示在序列視窗
  Serial.print("溫度計讀取成功: ");
  Serial.print((int)temperature); Serial.print(" *C, ");
  Serial.print((int)humidity); Serial.println(" H");
  String temperatureStr = String((int)temperature);
  String humidityStr = String((int)humidity);

  int fanState = 1;

  if((int)temperature>=25){
    LINE.notify("檢測環境發生異常，請協助儘速派人查看處理，目前環境狀態：\n 溫度:"+ String((int)temperature) + " ；濕度:" + String((int)humidity));
    delay(500);
    digitalWrite(relayPin, LOW);
    fanState = 0;
    Serial.println("風扇轉起來");

  }else{
    digitalWrite(relayPin, HIGH);
    fanState = 1;
    Serial.println("關閉風扇囉");
  }

  createWebServer();
  server.handleClient();
  Serial.println("web sever test"+temperatureStr+" "+humidityStr+" "+fanState);
  
  WiFiClient client = wifiserver.available();   // 傾聽clients傳入的值
  
  HTTPClient http;
  //上傳資料
  if((int)temperature !=0 &&(int)humidity !=0){
    String url1 = uploadUrl + "&amp;field1=" + (int)temperature + "&amp;field2=" + (int)humidity + "&amp;field3=" + String(scale.get_units(), 3) +"&amp;field4=" +fanState;
//    Serial.println(url1);
    http.begin(client,url1);
    int httpUploadCode = http.GET();
    String uploadResult="";
    Serial.println(httpUploadCode);
    if (httpUploadCode == HTTP_CODE_OK) {
    //讀取網頁內容到payload
    uploadResult = http.getString();
      //將內容顯示出來
      Serial.print("網頁內容=");
      Serial.println(uploadResult);
    } else {
      //讀取失敗
      Serial.println("網路傳送失敗");
    }
    http.end();
  }

  time_t now = timeClient.getEpochTime();
  struct tm *timeinfo;
  timeinfo = localtime(&now);

  // 格式化時間
  char formattedTime[32];
  strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S", timeinfo);

  // 顯示格式化後的時間
  String strDateTime = String(formattedTime);

  Serial.println("Current time: " + strDateTime);
  String strTime = getValue(strDateTime,' ',1);
  Serial.println("Current time: " + strTime);
  String strH = getValue(strTime,':',0);
  Serial.println("Current strH: " + strH);
  String strM = getValue(strTime,':',1);
  Serial.println("Current strM: " + strM);

  Serial.println("foodTime"+foodTime);
  if(foodTime.length()>0){
    String eatH = getValue(foodTime,':',0);
    String eatM = getValue(foodTime,':',1);
    boolean eat =  ((strH == eatH) && (strM == eatM))? 1:0 ;
    Serial.println(eat);
    if(eat){
      LINE.notify("甲奔囉");
      // 透過增加脈衝寬度實現連續順時針旋轉
      for (int i = 1500; i <= 2400; i += 50) {
        servo.writeMicroseconds(i);
        delay(20);
      }
      // 停止連續旋轉，將伺服馬達設定為中立位置
      servo.writeMicroseconds(1500);
    
      // 等待一段時間，或根據需要進行其他操作
      delay(60000);
      yield();
    }
    
    
  }

  
  delay(3000);
}
void createWebServer()
{

  server.on("/test", []() {
    content ="<!DOCTYPE html>";
    content +="<html lang=\"en\">";
    content +="<head>";
    content +="    <meta charset=\"UTF-8\">";
    content +="    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    content +="    <title>CAT</title>";
    content +="    <style>";
    content +="        body {";
    content +="            margin: 0;";
    content +="            padding: 0;";
    content +="            display: flex;";
    content +="            height: 100vh;";
    content +="        }";
    content +="";
    content +="        .left {";
    content +="            flex: 0.45;";
    content +="            background-color: #f0f0f0;";
    content +="            border: 1px solid #ccc;";
    content +="            padding: 20px;";
    content +="            display: flex;";
    content +="            flex-direction: column;";
    content +="        }";
    content +="        .left-top,";
    content +="        .left-bottom {";
    content +="            flex: 1;";
    content +="            background-color: #ddd;";
    content +="            margin-bottom: 10px;";
    content +="            padding: 10px;";
    content +="        }";
    content +="";
    content +="        .right {";
    content +="            flex: 1;";
    content +="            display: flex;";
    content +="            flex-direction: column;";
    content +="        }";
    content +="";
    content +="        .right-top {";
    content +="            flex: 1;";
    content +="            display: flex;";
    content +="            flex-direction: row;";
    content +="        }";
    content +="";
    content +="        .right-top-left,";
    content +="        .right-top-right {";
    content +="            flex: 1;";
    content +="            background-color: #f0f0f0;";
    content +="            border: 1px solid #ccc;";
    content +="            padding: 10px;";
    content +="            margin: 5px;";
    content +="        }";
    content +="";
    content +="        .right-bottom {";
    content +="            flex: 1;";
    content +="            background-color: #f0f0f0;";
    content +="            border: 1px solid #ccc;";
    content +="            padding: 20px;";
    content +="            margin: 10px;";
    content +="        }";
    content +="        .current_data {";
    content +="            flex: 1;";
    content +="            background-color: #f0f0f0;";
    content +="            border: 1px solid #ccc;";
    content +="            padding: 5px;";
    content +="            margin: 5px;";
    content +="            text-align: center;";
    content +="        }";
    content +="        .chart_borad {";
    content +="            flex: 1;";
    content +="            background-color: #f0f0f0;";
    content +="            border: 1px solid #ccc;";
    content +="            padding: 5px;";
    content +="            margin: 5px;";
    content +="            text-align: center;";
    content +="        }";
    content +="        .chart{";
    content +="            flex: 1;";
    content +="            background-color: #f0f0f0;";
    content +="            border: 1px solid #ccc;";
    content +="            padding: 5px;";
    content +="            margin: 5px;";
    content +="            text-align: center;";
    content +="        }";
    content +="    </style>";
    content +="    <script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>";
    content +="</head>";
    content +="<body>";
    content +="    ";
    content +="    <div class=\"left\">";
    content +="        <div class=\"left-top\">";
    content +="            <div class=\"current_data\">";
    content +="                <h2>目前溫度:</h2><iframe width=\"150\" height=\"150\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/2324980/widgets/776617\"></iframe>";
//    content +="                <h2>目前溫度:555°C</h2>";
    content +="            </div>";
    content +="            <div class=\"chart_borad\">";
    content +="                <iframe width=\"550\" height=\"300\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/2324980/charts/1?bgcolor=%23ffffff&color=%23d62020&days=1&results=10\"></iframe>";
    content +="            </div>";
    content +="        </div>";
    content +="        <div class=\"left-bottom\">";
    content +="            <div class=\"current_data\">";
    content +="                <h2>目前濕度:</h2><iframe width=\"150\" height=\"150\" style=\" solid #cccccc;\" src=\"https://thingspeak.com/channels/2324980/widgets/776618\"></iframe>";
//    content +="                <h2>目前濕度:665%</h2>";
    content +="            </div>";
    content +="            <div class=\"chart_borad\">";
    content +="                <iframe width=\"500\" height=\"300\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/2324980/charts/2?bgcolor=%23ffffff&color=%23d62020&days=1&dynamic=true&results=10&type=line\"></iframe>";
    content +="            </div>";
    content +="        </div>";
    content +="    </div>";
    content +="";
    content +="    <div class=\"right\">";
    content +="        <div class=\"right-top\">";
    content +="            <div class=\"right-top-left\">";
    content +="                <div class=\"left-top\">";
    content +="                    <div class=\"current_data\">";
    content +="                        <h2>剩餘飼料量:"+String(scale.get_units(), 3)+"kg</h2>";
    content +="                    </div>";
    content +="                    <div class=\"chart_borad\">";
    content +="                        <iframe width=\"500\" height=\"300\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/2324980/charts/3?bgcolor=%23ffffff&color=%23d62020&results=10\"></iframe>";
    content +="                    </div>";
    content +="                </div>";
    content +="            </div>";
    content +="";
    content +="            <div class=\"right-top-right\">";
    content +="                <div class=\"fan\">";
    content +="                    <h2>風扇狀態:</h2><iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/2324980/widgets/776620\"></iframe>";
    content +="                </div>";
    content +="                <div class=\"fan\">";
    content +="                    <h2>餵食時間:"+foodTime+"</h2>";
    content +="                    <form method='get' action='setuid'><label>餵食器時間:</label><input name='foodTime' type='time' length=64><input type='submit'></form>";
    content +="                </div>";
    content +="                <!-- 右半邊上半部分的右側內容 -->";
    content +="                <h2>Right Top Right Content</h2>";
    content +="            </div>";
    content +="        </div>";
    content +="";
    content +="        <div class=\"right-bottom\">";
    content +="            <!-- 右半邊下半部分的內容 -->";
    content +="            <iframe width=\"1920\" height=\"1080\" style=\"border: 1px solid #cccccc;\" src=\"http://192.168.50.180:5000\"></iframe>";
    content +="            <p >http://192.168.50.180:5000/</p>";
    content +="        </div>";
    content +="    </div>";
    content +="    ";
    content +="</body>";
    content +="</html>";
    server.send(200, "text/html", content);
  });
 
    server.on("/setuid", []() {
        foodTime = server.arg("foodTime");    
        if (foodTime.length() > 0) {
          Serial.println("html uid:");
          Serial.println(foodTime);
          content = "設定成功!請回上一頁。";
//          content =  "<!DOCTYPE html>";
//          content += "<html lang=\"en\">";
//          content += "<head></head>";
//          content += "<body> <form method='get' action='test'><label>設定成功!</label><input type='submit'></form></body></html>";
          statusCode = 200;
        } else {
          content = "{\"Error\":\"404 not found\"}";
          statusCode = 404;
          Serial.println("Sending 404");
        }
        server.send(statusCode, "application/json", content);
    });
  
}

String getValue(String data, char separator, int index){
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;
    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
