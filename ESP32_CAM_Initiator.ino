#include <esp_now.h>
#include <WiFi.h>

// These are for the camera
#include <esp_camera.h>
#include <driver/rtc_io.h>
#include <FS.h>
#include <SD_MMC.h>
#include <EEPROM.h>
#include <soc/rtc_cntl_reg.h>

// These are for the object detection model
#include <FOMO_Model_ESP32_CAM_V2_inferencing.h>
#include <edge-impulse-sdk/dsp/image/image.hpp>


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
// ---------------------------------------------------- Object Detection Code -----------------------------------------------
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS 320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS 240
#define EI_CAMERA_FRAME_BYTE_SIZE       3

// Set this to true to see e.g. features generated from the raw signal
static bool debug_nn = false;

// This is used to check if the camera driver has been initialized
static bool is_initialised = false;

// Points to the output of the capture
static uint8_t *snapshot_buf = nullptr;

// Forward declare these functions
void ei_camera_deinit(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t* out_buf);
// ---------------------------------------------------- End of Object Detection Code ----------------------------------------

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
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    } else 
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    // These paramters are for the object deteection model
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    // sensor_t* s = esp_camera_sensor_get();

    is_initialised = true;

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

// Initialize the microSD card
void initMicroSDCard()
{
  // 
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

  /* TEST CODE */
  if (psramFound()) {
        snapshot_buf = (uint8_t *)ps_malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);
    } else {
        Serial.println("Error: PSRAM not found! FOMO requires PSRAM.");
        while(1);
    }
  /* END OF TEST CODE */

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

// ---------------------------------------------------- Object Detection Code -----------------------------------------------
  if(ei_sleep(5) != EI_IMPULSE_OK)
  {
    return;
  }

  //snapshot_buf = (uint8_t*)malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);

  // Check if allocation was successful
  if(snapshot_buf == nullptr)
  {
    ei_printf("ERR: Failed to allocate snapshot buffer!\n");
    return;
  }

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &ei_camera_get_data;

  if(ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf) == false)
  {
    ei_printf("Failed to capture image\r\n");
    return;
  }

  // Run the classifier
  ei_impulse_result_t result_2 = { 0 };

  EI_IMPULSE_ERROR err = run_classifier(&signal, &result_2, debug_nn);
  if(err != EI_IMPULSE_OK)
  {
    ei_printf("ERR: Failed to run classifier (%d)\n", err);
    return;
  }

  // Print the predictions
  ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
              result_2.timing.dsp, result_2.timing.classification, result_2.timing.anomaly);

  #if EI_CLASSIFIER_OBJECT_DETECTION == 1
    ei_printf("Object detection bounding boxes:\r\n");
    for(uint32_t i = 0; i < result_2.bounding_boxes_count; i++)
    {
      ei_impulse_result_bounding_box_t bb = result_2.bounding_boxes[i];
      if(bb.value == 0)
      {
        continue;
      }

      ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                bb.label,
                bb.value,
                bb.x,
                bb.y,
                bb.width,
                bb.height);
    }
  #endif

    //free(snapshot_buf);
// ---------------------------------------------------- End of Object Detection Code ----------------------------------------
}
// ---------------------------------------------------- Object Detection Code -----------------------------------------------
// Stop streaming of sensor data by de-initializing the camera
void ei_camera_deinit(void)
{
  esp_err_t err = esp_camera_deinit();

  if(err != ESP_OK)
  {
    ei_printf("Camera de-initialization failed.\n");
    return;

    is_initialised = false;
    return;
  }
}

// Capture, rescale, and crop image
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t* out_buf)
{

  bool do_resize = false;

  if(!is_initialised || out_buf == nullptr)
  {
    ei_printf("ERR: Camera is not initialized\r\n");
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) 
  {
    Serial.println("Camera capture failed!");
    return false;
  }

  bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);

  esp_camera_fb_return(fb);

  if(!converted)
  {
    ei_printf("Conversion failed\n");
    return false;
  }

  if((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS) || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS))
  {
    do_resize = true;
  }

  if(do_resize)
  {
    ei::image::processing::crop_and_interpolate_rgb888(
      out_buf,
      EI_CAMERA_RAW_FRAME_BUFFER_COLS,
      EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
      out_buf,
      img_width,
      img_height
    );
  }

  return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float* out_ptr)
{
  // We already have a RGB888 buffer, so recalculate offset into pixel index
  size_t pixel_ix = offset * 3;
  size_t pixels_left = length;
  size_t out_ptr_ix = 0;

  while(pixels_left != 0)
  {
    // Swap BGR to RGB here
    // due to https://github.com/espressif/esp32-camera/issues/379
    out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix+ 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];

    //go to the next pixel
    out_ptr_ix++;
    pixel_ix += 3;
    pixels_left--;
  
  }
  // and done!
  return 0;
}
#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif
// ---------------------------------------------------- End of Object Detection Code ----------------------------------------