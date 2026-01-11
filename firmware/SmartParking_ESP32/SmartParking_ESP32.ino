#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h> 

// =================================================================
// 1. NETWORK & AWS CONFIGURATION
// =================================================================
const char* ssid = "YOUR_WIFI_SSID";         // Wi-Fi SSID
const char* password = "YOUR_WIFI_PASSWORD"; // Wi-Fi Password
const char* aws_endpoint = "YOUR_AWS_ENDPOINT"; 
const String aws_path = "/upload"; 

// =================================================================
// 2. PIN DEFINITIONS
// =================================================================
// Sensors & Actuators
#define TRIG_PIN 12          // Shared Trigger for both ultrasonic sensors
#define ENTRY_ECHO_PIN 13    // Echo pin for Entry Sensor
#define EXIT_ECHO_PIN 15     // Echo pin for Exit Sensor
#define SERVO_PIN 14         // Servo motor control pin
#define FLASHLIGHT_PIN 4     // On-board high-power LED
#define GREEN_LED_PIN 2      // Status LED (Success/Open)
#define RED_LED_PIN 3        // Status LED (Stop/Closed)

// System Constants
#define DETECT_DIST 15       // Distance threshold in cm

// AI-Thinker Camera Pin Map
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

// Globals
Servo gateServo;

// Function Prototypes
long getDistance(int echoPin);
String sendImageToAWS(String actionType);
void blinkWarning();

// =================================================================
// 3. SETUP ROUTINE
// =================================================================
void setup() {
  // A. Initialize Sensors & LEDs
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ENTRY_ECHO_PIN, INPUT);
  pinMode(EXIT_ECHO_PIN, INPUT);
  
  pinMode(FLASHLIGHT_PIN, OUTPUT);
  digitalWrite(FLASHLIGHT_PIN, LOW); // Ensure Flash is OFF initially

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  
  // Set Initial State (Gate Closed, Red Light ON)
  digitalWrite(RED_LED_PIN, HIGH);   
  digitalWrite(GREEN_LED_PIN, LOW); 

  // B. Initialize Servo
  gateServo.setPeriodHertz(50); 
  gateServo.attach(SERVO_PIN, 500, 2400); 
  gateServo.write(0); // Position 0 (Closed)

  // C. Connect to Wi-Fi
  // Blinks Green LED while connecting
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(GREEN_LED_PIN, HIGH); delay(100);
    digitalWrite(GREEN_LED_PIN, LOW); delay(100);
  }
  
  // Connection Success Signal
  digitalWrite(GREEN_LED_PIN, HIGH); delay(1000);
  digitalWrite(GREEN_LED_PIN, LOW); digitalWrite(RED_LED_PIN, HIGH);

  // D. Initialize Camera
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 
  
  // Use lower quality for faster transmission if PSRAM is not available
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA; 
    config.jpeg_quality = 12;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  esp_camera_init(&config);
}

// =================================================================
// 4. MAIN LOOP
// =================================================================
void loop() {
  // Read both sensors (small delay to prevent interference)
  long exitDist = getDistance(EXIT_ECHO_PIN);
  delay(20);
  long entryDist = getDistance(ENTRY_ECHO_PIN);

  // ---------------- CASE A: VEHICLE EXITING ----------------
  // Priority given to exit to ensure cars don't get stuck
  if (exitDist > 0 && exitDist < DETECT_DIST) {
    
    digitalWrite(FLASHLIGHT_PIN, LOW); 

    // Send Image with "EXIT" context
    String result = sendImageToAWS("EXIT");

    // Open Gate Immediately (No validation required for exit)
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, HIGH);
    
    gateServo.write(90); // Open
    delay(3000);         // Wait for car to pass
    gateServo.write(0);  // Close
    
    // Reset LEDs
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
    delay(2000); // Debounce delay
  }

  // ---------------- CASE B: VEHICLE ENTERING ----------------
  else if (entryDist > 0 && entryDist < DETECT_DIST) {
    
    digitalWrite(FLASHLIGHT_PIN, LOW); 
    
    // Send Image with "ENTRY" context & Wait for Server Response
    String result = sendImageToAWS("ENTRY");
    
    // Check if Cloud authorized the entry
    if (result.indexOf("OPEN_GATE") >= 0) {
      // Access Granted
      digitalWrite(RED_LED_PIN, LOW);    
      digitalWrite(GREEN_LED_PIN, HIGH); 
      
      gateServo.write(90); // Open
      delay(5000);         // Allow more time for entry
      gateServo.write(0);  // Close
      
      digitalWrite(GREEN_LED_PIN, LOW); 
      digitalWrite(RED_LED_PIN, HIGH);   
    } else {
      // Access Denied
      blinkWarning();
    }
    delay(2000); // Debounce delay
  }
  
  delay(100); // Idle loop delay
}

// =================================================================
// 5. HELPER FUNCTIONS
// =================================================================

/**
 * Calculates distance using the HC-SR04 ultrasonic sensor.
 * Returns: Distance in cm, or 999 if no echo received.
 */
long getDistance(int echoPin) {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 25000); // 25ms timeout
  if (duration == 0) return 999; 
  return duration * 0.034 / 2;
}

/**
 * Blinks the RED LED to indicate access denied or error.
 */
void blinkWarning() {
  for(int i=0; i<5; i++) {
    digitalWrite(RED_LED_PIN, LOW); delay(100);
    digitalWrite(RED_LED_PIN, HIGH); delay(100);
  }
  digitalWrite(RED_LED_PIN, HIGH);
}

/**
 * Captures a photo and uploads it to AWS via API Gateway.
 * actionType: "ENTRY" or "EXIT" (Sent as a custom HTTP header)
 * Returns: Server response string (e.g., "OPEN_GATE")
 */
String sendImageToAWS(String actionType) {
  camera_fb_t * fb = NULL;

  // 1. FLUSH BUFFER: Capture and discard old frame to ensure 
  //    we get a fresh image from the sensor.
  fb = esp_camera_fb_get(); 
  if(fb){
      esp_camera_fb_return(fb); 
      fb = NULL; 
  }

  delay(200); // Allow sensor to adjust to light

  // 2. CAPTURE FRESH IMAGE
  fb = esp_camera_fb_get(); 
  if(!fb) return "Camera Capture Failed";
  
  // 3. UPLOAD TO AWS
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate validation for speed/simplicity
  String serverResponse = ""; 

  if (client.connect(aws_endpoint, 443)) {
    // Construct HTTP POST Headers
    client.println("POST " + aws_path + " HTTP/1.1");
    client.println("Host: " + String(aws_endpoint));
    client.println("Content-Type: image/jpeg");
    client.println("Content-Length: " + String(fb->len));
    
    // Custom Header for Logic (Entry vs Exit)
    client.println("x-parking-action: " + actionType);
    
    client.println(); // End of headers

    // Send Image Data in Chunks
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0; n<fbLen; n=n+1024) {
      if (n+1024 < fbLen) {
        client.write(fbBuf, 1024); fbBuf += 1024;
      } else if (fbLen%1024>0) {
        client.write(fbBuf, fbLen%1024);
      }
    }
    
    // Read Response
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
        serverResponse = client.readStringUntil('\n');
        break;
      }
    }
    client.stop();
  } else {
     serverResponse = "ERROR_CONNECT"; 
  }
  
  // Free memory
  esp_camera_fb_return(fb);
  return serverResponse;
}