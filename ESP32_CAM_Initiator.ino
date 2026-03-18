#include <esp_now.h>
#include <WiFi.h>

// These are for the camera
#include <esp_camera.h>
#include <driver/rtc_io.h>
#include <FS.h>
#include <SD_MMC.h>
#include <EEPROM.h>
#include <soc/rtc_cntl_reg.h>


// GPIO for the push button
#define BUTTON_PIN 13
// Allocate one byte to use for naming the images that stored to the microSD card
#define EEPROM_SIZE 1

// Pin configuration for ESP32-CAM
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

// ---------------------------------------------------- Camera Code ---------------------------------------------------------
// Camera initialization function
bool initCamera() 
{
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

    if (psramFound()) 
    {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else 
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    return esp_camera_init(&config) == ESP_OK;
}

// Function to capture and save an image
bool takePhoto() 
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed!");
        return false;
    }

    // Generate a unique filename
    String path = "/photo_" + String(millis()) + ".jpg";

    // Save image to SD card
    File file = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing!");
        esp_camera_fb_return(fb);
        return false;
    }

    file.write(fb->buf, fb->len);
    file.close();

    esp_camera_fb_return(fb);
    Serial.println("Saved file to: " + path);
    return true;
}

void initMicroSDCard()
{
  if(!SD_MMC.begin("/sdcard", true))
  {
    Serial.println("Failed to found microSD card.");
  }
}
// ---------------------------------------------------- End of Camera Code --------------------------------------------------

// ---------------------------------------------------- Transmitter Code ----------------------------------------------------
// MAC address of responder (ESP32-WROOM-32E)
uint8_t broadcastAddress[] = {0xE0, 0x8C, 0xFE, 0xC2, 0x10, 0x88};

// Structure the data that we'll send to the DevKitC V4
typedef struct CAM_Message
{
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} CAM_Message;

// Create an object of type CAM_Message called 'FOMO_Coordinates'
CAM_Message FOMO_Coordinates;

// The ESP32-CAM will keep track of a peer list
// We want to add the ESP32-WROOM-32E into the camera's peer list
esp_now_peer_info_t peerInfo;

// Callback function
// This will be called when data is sent
void OnDataSent(const esp_now_send_info_t *mac_addr, esp_now_send_status_t status) 
{
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Failure");
}
// ---------------------------------------------------- End of Transmitter Code ---------------------------------------------

// ---------------------------------------------------- Receiver Code -------------------------------------------------------
// Structure the data that we'll receive from the DevKitC V4
typedef struct DEVKIT_Message
{
  bool takePicture;
} DEVKIT_Message;

// Create an object of type DEVKIT_Message called 'Control_Signal'
DEVKIT_Message Control_Signal;

// Callback function
// This will be called when data is received
void OnDataRecv(const esp_now_recv_info_t *mac_addr, const uint8_t *incomingData, int length)
{
  memcpy(&Control_Signal, incomingData, sizeof(Control_Signal));
  Serial.print("Data received of length: ");
  Serial.println(length);
  Serial.print("takePicture: ");
  Serial.println(Control_Signal.takePicture);
}
// ---------------------------------------------------- End of Receiver Code -------------------------------------------------
bool requestPhoto = 0;
void ARDUINO_ISR_ATTR isr()
{
  requestPhoto = 1;
}
// ---------------------------------------------------- Interrupt Code -------------------------------------------------------
volatile bool takePhotoFlag = false;
unsigned long lastInterruptTime = 0;

void IRAM_ATTR buttonPressedISR()
{
  unsigned long interruptTime = millis();
  if(interruptTime - lastInterruptTime > 500)
  {
    takePhotoFlag = true;
    lastInterruptTime = interruptTime;
  }
}
// ---------------------------------------------------- End of Interrupt Code ------------------------------------------------
void setup() {
  // put your setup code here, to run once:

  // Set up the Serial Monitor
  Serial.begin(115200);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Initialize the push button with a pull-up resistor so that it's HIGH when the button isn't pressed
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Register 'buttonPressedISR' as an interrupt service routine that's triggered on the falling edge of GPIO 13
  // (i.e., when the push button is pressed)
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPressedISR, FALLING);

    // Initialize the camera
  if (!initCamera()) 
  {
    Serial.println("Camera init failed!");
    while (1);
  }
  Serial.println("Camera initialized successfully.");

  // Initialize SD card
  initMicroSDCard();
  EEPROM.begin(EEPROM_SIZE);

  /*
  if (!SD_MMC.begin()) 
  {
    Serial.println("SD Card init failed!");
    while (1);
  }
  Serial.println("SD Card initialized successfully.");
  */

  // Set the ESP32-CAM as a Wi-Fi station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if(esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register 'OnDataSend' as a send callback function
  esp_now_register_send_cb(OnDataSent);

  // Register 'OnDataRecv; as a receiving callback function
  esp_now_register_recv_cb(OnDataRecv);

  // Register the ESP32-WROOM-32E as a peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Now add the ESP32-WROOM-32E as a peer
  if(esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  if(takePhotoFlag)
  {
    takePhoto();
    takePhotoFlag = false;
  }

  // Create test data
  uint16_t x_test, y_test, width_test, height_test;

  x_test = 50;
  y_test = 94;
  width_test = 7;
  height_test = 43;

  FOMO_Coordinates.x = random(1, 50);
  FOMO_Coordinates.y = random(1, 50);
  FOMO_Coordinates.width = random(1, 50);
  FOMO_Coordinates.height = random(1, 50);

  // Send the message through ESP-NOW
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*) &FOMO_Coordinates, sizeof(FOMO_Coordinates));

  if(result == ESP_OK)
  {
    Serial.println("Sending Confirmed");
  }
  else
  {
    Serial.println("Sending Error");
  }
  delay(2000);
}
