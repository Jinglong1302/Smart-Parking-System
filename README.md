# Smart Parking System using ESP32-CAM and AWS Serverless

## 📖 Project Description
This project implements an intelligent parking management system leveraging the Internet of Things (IoT) and Cloud Computing. The system uses an ESP32-CAM to visually detect vehicles at entry and exit points. It utilizes **AWS Serverless architecture** (Lambda, API Gateway) and **Amazon Rekognition** to perform Optical Character Recognition (OCR) on license plates.

The system automates gate control using a servo motor, tracks parking duration, manages capacity in real-time, and provides a dashboard for monitoring occupancy and revenue.

## 🏗 System Architecture
![Architecture Diagram](diagram/architecture_diagram.jpg)

### Hardware Components
* **Controller:** ESP32-CAM (AI-Thinker Model)
    * *Note: Ensure your Micro-USB cable supports **Data Transfer**. Charge-only cables will not work.*
* **Sensors:** 2x HC-SR04 Ultrasonic Sensors (Shared Trigger, Independent Echo)
* **Actuator:** SG90 Servo Motor (Gate Barrier)
* **Indicators:** Red and Green LEDs
* **Resistors:** 2x 220Ω Resistors (for LEDs)
* **Prototyping:** 1x Breadboard
* **Power:** 5V External Supply / Breadboard Power Rail
* **Wires:** 8x Female-to-male jumper wires & 13x Male-to-male jumper wires

### Software & Cloud Services
* **Firmware:** C++ (Arduino Framework)
* **Middleware:** AWS Lambda (Python 3.9)
* **AI/ML:** Amazon Rekognition (License Plate OCR)
* **Database:** Amazon DynamoDB (State management & Logging)
* **Storage:** Amazon S3 (Image Debugging)
* **Visualization:** Amazon CloudWatch (Metrics & Dashboard)

---

## ⚙️ Dependencies and Requirements

### Arduino (Firmware) Requirements
You must install the following libraries in the Arduino IDE:
1.  **ESP32 Board Manager** by Espressif Systems (Version 2.0.x or later)
2.  **ESP32Servo** by Kevin Harrington (for gate control)
3.  **WiFi** & **WiFiClientSecure** (Built-in with ESP32 board)

### Python (Backend) Requirements
The AWS Lambda function runs on **Python 3.9**.
* `boto3` (AWS SDK for Python)
* `base64` (Standard library)
* `json` (Standard library)
* `decimal` (Standard library)

---

## 🚀 Setup and Installation Instructions

### Step 1: Hardware Assembly
Connect the components according to the pin mapping below.
* **Servo Signal:** GPIO 14
* **Entry Sensor Echo:** GPIO 13
* **Exit Sensor Echo:** GPIO 15
* **Shared Trigger:** GPIO 12
* **Green LED:** GPIO 2
* **Red LED:** GPIO 3

> 📄 **Detailed Wiring:** For a complete pin-to-pin connection table, please refer to the file `diagrams/Pin Connection.pdf`.

### Step 2: AWS Cloud Configuration
1.  **DynamoDB:** Create two tables:
    * `ParkingLot` (Partition Key: `lot_id`)
    * `ParkingLogs` (Partition Key: `log_id`)
2.  **S3 Bucket:** Create a bucket named `parking-lot-images-cpc357`.
3.  **Lambda:** Create a function (Python 3.9) and paste the code from `backend/lambda_function.py`.
    * *Permission:* Grant the Lambda role access to S3, Rekognition, CloudWatch, and DynamoDB.
4.  **API Gateway:**
    * Create a REST API.
    * Create a **Resource** named `/upload`.
    * Create a **POST Method** under `/upload` that triggers your Lambda function.
    * Deploy the API and note the **Invoke URL** (e.g., `https://xyz.amazonaws.com/dev`).

### Step 3: Firmware Upload
1.  Open `firmware/SmartParking_ESP32/SmartParking_ESP32.ino` in Arduino IDE.
2.  Update the following lines with your credentials:
    ```cpp
    const char* ssid = "YOUR_WIFI_NAME";
    const char* password = "YOUR_WIFI_PASSWORD";
    // Ensure you include the /upload path in your AWS endpoint variable if required by logic
    const char* aws_endpoint = "YOUR_API_GATEWAY_URL"; //excldue the https://
    ```
3.  Select board **"AI Thinker ESP32-CAM"**.

#### ⚠️ Upload Troubleshooting (Flashing Mode)
If the upload fails with a "Connecting..." timeout, you need to manually put the board into **Boot Mode**:
1.  Unplug the USB cable.
2.  Press and **HOLD** the `FLASH` (or `IO0`) button on the ESP32-CAM.
3.  While holding the button, plug the USB cable back into your computer.
4.  Click **Upload** in the Arduino IDE.
5.  Once you see the message `Hard resetting via RTS pin...` in the console, the upload is complete.
6.  Press the `RST` (Reset) button on the board once to start the code.

## 📊 Usage
1.  Power on the system. The Red LED will light up (Gate Closed).
2.  Drive a car (or hold a printed license plate) in front of the Entry Sensor.
3.  The system will capture an image, analyze the plate, and open the gate if valid.
4.  Real-time stats will appear on the CloudWatch Dashboard.
