/*
NOTE: In order to compile this sketch, you need to install the following libraries from the library manager (Tools -> Manage Libraries...):
1. MPU6050_VibrationRMS by Iftahul Yusro
2. SimpleKalmanFilter by Denys Sene
3. arduinoFFT by Enrique Condes (install version 1.5.6)
4. Adafruit MPU6050 by Adafruit
5. Adafruit Unified Sensor by Adafruit
6. Adafruit Bus IO by Adafruit
*/
#include <esp_now.h>
#include <WiFi.h>

// These are for the MPU
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
// This library is used to calculate the velocity vibration RMS
#include <MPU6050_VibrationRMS.h>

Adafruit_MPU6050 MPU;
// This object below is used to measure the velocity vibration RMS
MPU6050_VibrationRMS Sensor;

// These are used to calculate the distance of a detected car from the camera
#define FOCAL_LENGTH 14.3
#define CAR_WIDTH    67
#define DISTANCE     5.0

// This is just a placeholder value
#define ANGULAR_VELOCITY_MAGNITUDE 2.5

// ---------------------------------------------------- Transmitter Code ----------------------------------------------------
// MAC address of ESP32-CAM
uint8_t broadcastAddress_1[] = {0xC0, 0xCD, 0xD6, 0xCF, 0x14, 0x94};
// MAC address of the other ESP32-CAM
uint8_t broadcastAddress_2[] = {0xC0, 0xCD, 0xD6, 0x8D, 0xFC, 0x88};

// Structure the data that we'll send to the ESP32-CAM
typedef struct DEVKIT_Message
{
  bool takePicture;
  bool stopBuffer;
} DEVKIT_Message;

// Create an object of type DEVKIT_Message called 'Control_Signal'
DEVKIT_Message Control_Signal;

// The DevKitC V4 will keep track of a peer list
// We want to add the ESP32-CAMs into the development board's peer list
esp_now_peer_info_t peerInfo_1;
esp_now_peer_info_t peerInfo_2;

// Callback function
// This will be called when data is sent
void OnDataSent(const esp_now_send_info_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Failure");
}
// ---------------------------------------------------- End of Transmitter Code ---------------------------------------------

// ---------------------------------------------------- Receiver Code -------------------------------------------------------
// Structure the data that we'll recieve from the ESP32-CAM
typedef struct CAM_Message
{
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} CAM_Message;

// Create an object of type CAM_Message called 'FOMO_Coordinates'
CAM_Message FOMO_Coordinates;

// Callback function
// This will be called when data is received
void OnDataRecv(const esp_now_recv_info_t *mac_addr, const uint8_t *incomingData, int length)
{
  memcpy(&FOMO_Coordinates, incomingData, sizeof(FOMO_Coordinates));
  Serial.print("Data received of length: ");
  Serial.println(length);
  Serial.print("x: ");
  Serial.println(FOMO_Coordinates.x);
  Serial.print("y: ");
  Serial.println(FOMO_Coordinates.y);
  Serial.print("width: ");
  Serial.println(FOMO_Coordinates.width);
  Serial.print("height: ");
  Serial.println(FOMO_Coordinates.height);
  bool carClose = 0;
  float distance = ((FOCAL_LENGTH * CAR_WIDTH) / FOMO_Coordinates.width) / 12; // Expressed in feet
  if(distance <= DISTANCE)
  {
    Serial.println("Detected car too close! Take a photo now...");
    carClose = 1;
    Control_Signal.takePicture = carClose;
    esp_err_t result = esp_now_send(broadcastAddress_1, (uint8_t*) &Control_Signal, sizeof(Control_Signal));
    if(result == ESP_OK)
    {
      Serial.println("(C0:CD:D6:CF:14:94): Sending Confirmed");
    }
    else
    {
      Serial.println("(C0:CD:D6:CF:14:94): Sending Error");
    }
    }
}
// ---------------------------------------------------- End of Receiver Code -------------------------------------------------

void setup() {
  // put your setup code here, to run once:

  // Set up the Serial Monitor
  Serial.begin(115200);

  // Set the ESP32-WROOM-32E as a Wi-Fi station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if(esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the receiving callback function
  esp_now_register_recv_cb(OnDataRecv);

  // Register the sending callback function
  esp_now_register_send_cb(OnDataSent);

  // Register both ESP32-CAMs as peers
  memcpy(peerInfo_1.peer_addr, broadcastAddress_1, 6);
  peerInfo_1.channel = 0;
  peerInfo_1.encrypt = false;

  memcpy(peerInfo_2.peer_addr, broadcastAddress_2, 6);
  peerInfo_2.channel = 0;
  peerInfo_2.encrypt = false;

  // Now add the ESP32-CAMs as peers
  if(esp_now_add_peer(&peerInfo_1) != ESP_OK)
  {
    Serial.println("Failed to add ESP32-CAM with MAC address C0:CD:D6:CF:14:94");
  }

  if(esp_now_add_peer(&peerInfo_2) != ESP_OK)
  {
    Serial.println("Failed to add ESP32-CAM with MAC address C0:CD:D6:8D:FC:88");
  }

  // Initialize the MPU
  if(!MPU.begin())
  {
    Serial.println("Failed to find MPU6050 chip.");
  }
  else
  {
    Serial.println("MPU6050 found!");
  }

  // Activate the accelerometer
  MPU.setAccelerometerRange(MPU6050_RANGE_8_G);
  Serial.print("Acceleromter range set to: ");
  switch(MPU.getAccelerometerRange())
  {
    case MPU6050_RANGE_2_G:
      Serial.println("+-2G");
      break;
    case MPU6050_RANGE_4_G:
      Serial.println("+-4G");
      break;
    case MPU6050_RANGE_8_G:
      Serial.println("+-8G");
      break;
    case MPU6050_RANGE_16_G:
      Serial.println("+-16G");
      break;
  }

  // Activate the gyroscope
  MPU.setGyroRange(MPU6050_RANGE_500_DEG);
  Serial.print("Gyroscope range set to: ");
  switch(MPU.getGyroRange())
  {
    case MPU6050_RANGE_250_DEG:
      Serial.println("+- 250 deg/s");
      break;
    case MPU6050_RANGE_500_DEG:
      Serial.println("+- 500 deg/s");
      break;
    case MPU6050_RANGE_1000_DEG:
      Serial.println("+- 1000 deg/s");
      break;
    case MPU6050_RANGE_2000_DEG:
      Serial.println("+- 2000 deg/s");
      break;
  }

  // Activate the frequency filter
  Serial.print("Filter bandwidth set to: ");
  switch(MPU.getFilterBandwidth())
  {
    case MPU6050_BAND_260_HZ:
      Serial.println("260 Hz");
      break;
    case MPU6050_BAND_184_HZ:
      Serial.println("184 Hz");
      break;
    case MPU6050_BAND_94_HZ:
      Serial.println("94 Hz");
      break;
    case MPU6050_BAND_44_HZ:
      Serial.println("44 Hz");
      break;
    case MPU6050_BAND_21_HZ:
      Serial.println("21 Hz");
      break;
    case MPU6050_BAND_10_HZ:
      Serial.println("10 Hz");
      break;
    case MPU6050_BAND_5_HZ:
      Serial.println("5 Hz");
      break;
  }

  Sensor.begin();
}

void loop() {
  // put your main code here, to run repeatedly:

  // Get new sensor events with the readings
  sensors_event_t accel, gyro, temp;
  MPU.getEvent(&accel, &gyro, &temp);

  // Print out values of the accelerometer
  Serial.print("Acceleration X: "); Serial.print(accel.acceleration.x);
  Serial.print(", Y: "); Serial.print(accel.acceleration.y);
  Serial.print(", Z: "); Serial.print(accel.acceleration.z);
  Serial.println(" m/s^2");

  // Print out values of the gyroscope
  Serial.print("Rotation X: "); Serial.print(gyro.gyro.x);
  Serial.print(", Y: "); Serial.print(gyro.gyro.y);
  Serial.print(", Z: "); Serial.print(gyro.gyro.z);
  Serial.println(" rad/s");

  // Print out the temperature
  //Serial.print("Temperature: "); Serial.print(temp.temperature);
  //Serial.println(" degC");

  // Print out the velocity vibration RMS
  float Vrms = Sensor.readVRMS();
  Serial.print("Vibration RMS: ");
  Serial.print(Vrms, 2);
  Serial.println(" mm/s");
  
  float angular_velocity_magnitude_empirical = sqrt(gyro.gyro.x * gyro.gyro.x + gyro.gyro.y * gyro.gyro.y + gyro.gyro.z * gyro.gyro.z);
  // If the magnitude of the angular velocity exceeds our pre-defined value, then take a photo
  if(angular_velocity_magnitude_empirical >= ANGULAR_VELOCITY_MAGNITUDE)
  {
    Serial.println("Angular velocity threshold passed! Take a photo now...");
    // Send the message to the ESP32-CAM
    Control_Signal.takePicture = 1;
    //esp_err_t result_1 = esp_now_send(broadcastAddress_1, (uint8_t*) &Control_Signal, sizeof(Control_Signal));
    esp_err_t result_2 = esp_now_send(broadcastAddress_2, (uint8_t*) &Control_Signal, sizeof(Control_Signal));
    
    /*
    if(result_1 == ESP_OK)
    {
      Serial.println("(C0:CD:D6:CF:14:94): Sending Confirmed");
    }
    else
    {
      Serial.println("(C0:CD:D6:CF:14:94): Sending Error");
    }
    */
    if(result_2 == ESP_OK)
    {
      Serial.println("(C0:CD:D6:8D:FC:88): Sending Confirmed");
    }
    else
    {
      Serial.println("(C0:CD:D6:8D:FC:88): Sending Error");
    }
    Control_Signal.takePicture = 0;
  }

  delay(1000);
}
