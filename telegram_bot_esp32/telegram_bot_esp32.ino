/*  BEFORE USING THIS CODE MAKE SURE YOU HAVE ESP-32 AI Thinker cam AND
 *  PIR SENSOR CONECTED TO 13 PIN SUPPLIED WITH ENOUGH POWER 
 *  
 *  YOU MAY HAVE THIS PROBLEM
 *  https://stackoverflow.com/questions/60896587/ai-thinker-esp32-cam-problems-using-pin-13-as-input-pulldown-after-setting-pin
 *  
 *  INTERNAL TEMPERATURE SENSOR DEFINING BELOW
 */
#ifdef __cplusplus
  extern "C" {
 #endif

  uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include "UniversalTelegramBotRZO.h"
#include "soc/soc.h"           
#include "soc/rtc_cntl_reg.h" 

#define CAMERA_MODEL_AI_THINKER

// Initialize Wifi connection to the router
const char* ssid = "wifi name";
const char* password = "wifi password";
String chat_id;
String first_name;

// Initialize Telegram BOT
#define BOTtoken "Your bot token"  // your Bot Token (Get from Botfather)
String token = BOTtoken;

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

int Bot_mtbs = 3000; //mean time between scan messages
long Bot_lasttime;   //last time messages' scan has been done

int periodTrig = 2000; //period while PIR sensor triggered
long previousMillisTrig = 0; //last time from trigger

int periodTemp = 10000; //period while temperature is measured
long previousMillisTemp = 0; //last time from measure

float temp;

camera_fb_t * fb;
uint8_t* fb_buffer;
size_t fb_length;
int currentByte;

bool armed = false;
bool switchLight = false;
bool sleeping = false;
bool overheated = false;

int pictureNumber;

// SET YOUR CHAT ID FIRST TO GET PHOTO NOTIFICATION
const String allowed_chat_id[5] = {"x", "x", "x", "x", "x"};

// 1 byte = 256 pictures
#define EEPROM_SIZE 1

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define TRIGGER_PIN       13

#define LED_BUILTIN        4

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT);
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;  // Trying to reduce memory use
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();

  s->set_framesize(s, FRAMESIZE_XGA);
  
  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

bool isMoreDataAvailable() {
  return (fb_length - currentByte);
}

uint8_t photoNextByte() {
  currentByte++;
  return (fb_buffer[currentByte - 1]);
}

void take_send_photo(String chat_id) {
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  currentByte = 0;
  fb_length = fb->len;
  fb_buffer = fb->buf;
  bot.sendPhotoByBinary(chat_id, "image/jpeg", fb->len, isMoreDataAvailable, photoNextByte, nullptr, nullptr);
  esp_camera_fb_return(fb);
  fb_length = NULL;
  fb_buffer = NULL;
}

String arm() {
  armed = true;
  return "Armed.\nTrigger period: " + String(periodTrig / 1000);
}

String disarm() {
  armed = false;
  return "Disarmed.";
}

String switch_light() {
  switchLight = switchLight ? false : true;
  if (switchLight) {
    digitalWrite(LED_BUILTIN, HIGH);
    return "Light on";
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    return "Light off";
  }
}

String info(String chat_id) {
  String divider = "\n--------------------\n";
  String infoArmed = String("Armed: " + String(armed ? "Yes" : "No"));
  String infoLight = String("Light: " + String(switchLight ? "On" : "Off"));
  String infoPeriod = String("Period: " + String(periodTrig / 1000) + " seconds\nTo set new trigger period use /timer xx.\n(xx - number)\nPlease use integer positive numbers in range (0; 20]");
  String infoStatus = String("Your status: " + String(isAllowed(chat_id) ? "Admin" : "User"));
  String infoSleeping = String("Sleeping: " + String(sleeping ? "Yes" : "No"));
  String infoTemp = String("Temperature: " + String(temp) + " C");
  return String(infoArmed 
                + divider 
                + infoLight 
                + divider 
                + infoPeriod 
                + divider 
                + infoSleeping 
                + divider 
                + infoTemp 
                + divider
                + infoStatus); 
}

// USE THIS FUNCTION IF YOU WANT TO SEND PHOTOS TO SEVERAL CHATS
void detect() {
  /*for (byte i = 0; i < 5; i++) {
    if (chat_id != "x") {
      //bot.sendMessage(chat_id, "Detection!", "");
      take_send_photo(allowed_chat_id[i]);
    }
  }*/
}

bool isAllowed(String chat_id) {
  for (byte i = 0; i < 5; i++) {
    if (chat_id == allowed_chat_id[i] && chat_id != "x") {
      return true;
    }
  }
  return false;
}

boolean isValidNumber(String str) {
  int checkBefore = str.length();
  int checkAfter = 0; 
  for(byte i=0;i<str.length();i++) {
      if(isDigit(str.charAt(i))) {
        checkAfter++;
      }
  }
  if (checkBefore == checkAfter) {
    return true;
  }
  return false;
} 

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillisTrig > periodTrig) {
    
    if (armed && digitalRead(TRIGGER_PIN) == HIGH) {
      take_send_photo(allowed_chat_id[0]);
    }
    
    previousMillisTrig = currentMillis;
  }

  if (currentMillis - previousMillisTemp > periodTemp) {
    temp = (temprature_sens_read() - 32) / 1.8;
    if (temp > 80) {
      bot.sendMessage(allowed_chat_id[0], "Temperature is high: " + String(temp) + "\nEnter sleeping mode", "");
      bot.sendMessage(allowed_chat_id[0], "Sleeping... 😴", "");
      Bot_mtbs = 30000;
      sleeping = true;
      overheated = true;
    } else if (temp < 60 && overheated) {
      bot.sendMessage(allowed_chat_id[0], "My esp-32 brains is now cooled. I can cam again ;)", "");
      Bot_mtbs = 3000;
      sleeping = false;
      overheated = false;
    }
  }
  
  if (millis() > Bot_lasttime + Bot_mtbs)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  
    while (numNewMessages) {
      chat_id = bot.messages[0].chat_id;
      first_name = bot.messages[0].from_name;
      Serial.println(chat_id);
      
      for (int i = 0; i < numNewMessages; i++) {
        String command = bot.messages[i].text;

        if (command.substring(0, 1) != "/") {
          bot.sendMessage(chat_id, "Unfortunately, I don't understand human language. Please text me with command starting with \'/\' symbol", "");
          break;
        }

        if (command == "/start") {
          if (isAllowed(chat_id)) {
            bot.sendMessage(chat_id, "Hello, " + first_name , "");
            break;
          }
          bot.sendMessage(chat_id, "Hello, I am spying bot hosted on esp-32 module. \nBefore using me contact @crinitus_vulpi to give you an admin access.\nAt this moment you can use only /info command." , "");
          break;
        }

        if (command == "/info") {
          bot.sendMessage(chat_id, info(chat_id), "");
          break;
        }

        if (isAllowed(chat_id)) {
          if (command == "/photo") {
            take_send_photo(chat_id);
            break;
          }
       
          if (command == "/light") {
            bot.sendMessage(chat_id, switch_light(), "");
            break;
          }
        
          if (command == "/arm" && !armed) {
            bot.sendMessage(chat_id, arm(), "");
            break;
          } else if (command == "/arm") {
            bot.sendMessage(chat_id, "Already armed", "");
            break; 
          }
        
          if (command == "/disarm" && armed) {
            bot.sendMessage(chat_id, disarm(), "");
            break;
          } else if (command == "/disarm") {
            bot.sendMessage(chat_id, "Already disarmed", "");
            break; 
          }

          if (command == "/sleep") {
              bot.sendMessage(chat_id, "Sleeping... 😴", "");
              Bot_mtbs = 30000;
              sleeping = true;
              break;
          }

          if (command == "/wakeup") {
              bot.sendMessage(chat_id, "Ok, I am here. \nWhat happened while I've been sleeping? \nWhat I have to do?", "");
              Bot_mtbs = 3000;
              sleeping = false;
              break;
          }
          
          if (command.substring(0, 6) == "/timer") {
            Serial.println(command.length());
            if (command.length() <= 5) {
              bot.sendMessage(chat_id, "I know how the number looks like. Don't try to fool me", "");
              break;
            }
            String newTimeString = command.substring(7, command.length());
            if (isValidNumber(newTimeString)) {
              int newTime = newTimeString.toInt();
            if (newTime == 0) {
                bot.sendMessage(chat_id, "Seriously? I am gonna to burn with 0 timeout. \nSend /timer and a positive number greater than 0 and lesser than 20.\nExample: /timer 10", "");
                break;
              } else if (newTime > 20) {
                bot.sendMessage(chat_id, "It's too big period. \nSend /timer and a positive number greater than 0 and lesser than 20.\nExample: /timer 10", "");
                break;
              } else {
                periodTrig = newTime * 1000;
                bot.sendMessage(chat_id, "Done!", "");
                break;
              }
            } else {
              bot.sendMessage(chat_id, "I know how the positive integer numbers in range of (0; 20] looks like. Don't try to fool me!", "");
              break;
            }
          }

          bot.sendMessage(chat_id, "Unknown command, type / to see commands list", "");
          break;
          
        } else {
          bot.sendMessage(chat_id, "You don't have permission to use this command!\nContact @crinitus_vulpi to fix this", "");
        }
     }
      
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    Bot_lasttime = millis();
  }
}
