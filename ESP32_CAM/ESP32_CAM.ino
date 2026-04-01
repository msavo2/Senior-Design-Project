// These are for ESP-NOW functionality
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

// These are for the webserver, which serves photos stored on the microSD card
#include <ESP32WebServer.h>
#include "CSS.h"
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>

// GPIO for the push button
#define BUTTON_PIN 13
// Allocate one byte to use for naming the images that are stored on the microSD card
#define EEPROM_SIZE 1
// Network name that we'll connect to via a smartphone
#define NETWORKNAME "ESP32_CAM_NETWORK"
// Server name that we'll enter into a web browser (e.g., 'ESP32_CAM_SERVER.local') after we connect to the network
#define SERVERNAME "ESP32_CAM_SERVER"
// The size of our rolling buffer
#define BUFFERCOUNT 50
// The index of the buffer
short bufferIndex = 0;

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

// ---------------------------------------------------- Web Server Code -----------------------------------------------------
// Create an ESP32WebServer object called 'server' and initialize it to port 80 (port 80 is for unencrypted HTTP traffic)
ESP32WebServer server(80);

// We'll use this Boolean object to check is the microSD has been initialized in the setup
bool SD_present = false;

// Initial page of the web server, lists the directory, and gives users a chance to download photos
void SD_dir();

// Upload a file to the microSD card
void File_Upload();

// Prints the directory
void printDirectory(const char * dirname, uint8_t levels);

// Download a file from the microSD card
void SD_file_download(String filename);

// Upload a new file to the file system
void handleFileUpload();

// Delete a file from the microSD card
void SD_file_delete(String filename);

// Send the HTML header tag
void SendHTML_Header();

// Send the HTML client that has been written thus far in 'webpage' to the client
void sendHTML_Content();

// Disconnect the client from the server
void SendHTML_Stop();

// Is the microSD card plugged into the ESP32-CAM?
void ReportSDNotPresent();

// Is the file we want access to present on the server?
void ReportFileNotPresent(String target);

// Can we upload a file?
void ReportCouldNotCreateFile(String target);

// Return size of file
String file_size(int bytes);

String getNextBufferPath();
// ---------------------------------------------------- End of Web Server Code ----------------------------------------------

// ---------------------------------------------------- Object Detection Code -----------------------------------------------
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS 320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS 240
#define EI_CAMERA_FRAME_BYTE_SIZE       3

// Set this to true to see (e.g., features generated from the raw signal)
static bool debug_nn = false;

// This is used to check if the camera driver has been initialized
static bool is_initialised = false;

// Points to the output of the capture
static uint8_t *snapshot_buf = nullptr;

// Stop streaming of sensor data by de-initializing the camera
void ei_camera_deinit(void);

// Capture, rescale, and crop image
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t* out_buf);
// ---------------------------------------------------- End of Object Detection Code ----------------------------------------

// ---------------------------------------------------- Camera Code ---------------------------------------------------------
// Camera initialization function
bool initCamera();

// Function to capture and save an image in the 'Photographs' directory
bool takePhoto();

// Function to capture and save an image in the 'Buffer' directory
bool takePhotoBuffer();

void saveRawFrameToSD(uint8_t* buf, size_t w, size_t h);

// Initialize the microSD card
void initMicroSDCard();
// ---------------------------------------------------- End of Camera Code --------------------------------------------------

// ---------------------------------------------------- Interrupt Code -------------------------------------------------------
// The volatile attribute tells the compiler NOT to perform any optimizations on 'takePhotoFlag'
// This attribute is important for variables involved in intterrupts
volatile bool takePhotoFlag = false;
volatile bool stopBufferFlag = false;
unsigned long lastInterruptTime = 0;

// The 'IRAM_ATTR' places this function in the interntal RAM
// This is a recommended attribute for interrupt handlers
void IRAM_ATTR buttonPressedISR()
{
  // Debounce the push button by ignoring the first 0.5 seconds of input
  unsigned long interruptTime = millis();
  if(interruptTime - lastInterruptTime > 500)
  {
    takePhotoFlag = true;
    lastInterruptTime = interruptTime;
  }
}
// ---------------------------------------------------- End of Interrupt Code ------------------------------------------------

// ---------------------------------------------------- Transmitter Code ----------------------------------------------------
// MAC address of responder (ESP32-WROOM-32E)
uint8_t broadcastAddress[] = {0xE0, 0x8C, 0xFE, 0xC2, 0x10, 0x88};

// Structure the data that we'll send to the DevKitC V4 (i.e., ESP32-WROOM-32E)
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
  bool stopBuffer;
} DEVKIT_Message;

// Create an object of type DEVKIT_Message called 'Control_Signal'
DEVKIT_Message Control_Signal;

// Callback function
// This will be called when data is received
void OnDataRecv(const esp_now_recv_info_t *mac_addr, const uint8_t *incomingData, int length)
{
  memcpy(&Control_Signal, incomingData, sizeof(Control_Signal));
  // Serial.print("Data received of length: ");
  // Serial.println(length);
  // Serial.print("takePicture: ");
  // Serial.println(Control_Signal.takePicture);
  takePhotoFlag  = Control_Signal.takePicture;
  stopBufferFlag = Control_Signal.stopBuffer;
}
// ---------------------------------------------------- End of Receiver Code -------------------------------------------------

void setup() {
  // put your setup code here, to run once:

  // Set up the Serial Monitor
  Serial.begin(115200);

  // Set up an access point identified by 'NETWORKNAME' on the ESP32-CAM
  WiFi.softAP(NETWORKNAME);

  if(!MDNS.begin(SERVERNAME))
  {
    Serial.println(F("Error setting up MDNS responser!"));
    ESP.restart();
  }

  if (psramFound()) 
  {
    snapshot_buf = (uint8_t *)ps_malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);
  } 
  else 
  {
    Serial.println("Error: PSRAM not found! FOMO requires PSRAM.");
    while(1);
  }

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

  // Initialize the microSD card
  initMicroSDCard();
  EEPROM.begin(EEPROM_SIZE);

  // Set the ESP32-CAM as a Wi-Fi station and retain its status as an access point
  WiFi.mode(WIFI_AP_STA);

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

  server.on("/", SD_dir);
  server.on("/upload", File_Upload);
  server.on("fupload", HTTP_POST,[](){ server.send(200);}, handleFileUpload);

  server.begin();
}

void loop() {
  // put your main code here, to run repeatedly:

  // This functino is used to connect clients to the server
  server.handleClient();

  // Did an interrupt occur?
  if(takePhotoFlag)
  {
    Serial.println("Taking a photo...");
    takePhoto();
    takePhotoFlag = false;
  }

  // Do we want to stop taking photos and storing them into the buffer?
  if(~stopBufferFlag)
  {
    takePhotoBuffer();
  }
// ---------------------------------------------------- Object Detection Code -----------------------------------------------
// NOTE: This code was taken nearly verbatim from the example code provided by the library
  if(ei_sleep(5) != EI_IMPULSE_OK)
  {
    return;
  }

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
  // ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
              // result_2.timing.dsp, result_2.timing.classification, result_2.timing.anomaly);

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
      
      // Send these metrics to the DevKitC V4
      FOMO_Coordinates.x = bb.x;
      FOMO_Coordinates.y = bb.y;
      FOMO_Coordinates.width = bb.width;
      FOMO_Coordinates.height = bb.height;

      esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*) &FOMO_Coordinates, sizeof(FOMO_Coordinates));

      // Did we successfully send our message?
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
  #endif
// ---------------------------------------------------- End of Object Detection Code ----------------------------------------
}

// ---------------------------------------------------- Web Server Code -----------------------------------------------------
// Initial page of the web server, lists the directory, and gives users a chance to download photos
void SD_dir()
{
  if (SD_present) 
  {
    String currentDir = "/";
    if(server.hasArg("dir"))
    {
      currentDir = "/" + server.arg("dir");
    }

    // An argument was received, so perform its corresponding action
    if (server.args() > 0 && !server.hasArg("dir"))
    { 
      Serial.println(server.arg(0));
  
      String Order = server.arg(0);
      Serial.println(Order);
      
      // Are we attempting to download a photo?
      if (Order.indexOf("download_")>=0)
      {
        Order.remove(0,9);
        SD_file_download(Order);
        Serial.println(Order);
      }
  
      // Are we attempting to delete a photo?
      if ((server.arg(0)).indexOf("delete_")>=0)
      {
        Order.remove(0,7);
        SD_file_delete(Order);
        Serial.println(Order);
      }
    }
    
    SendHTML_Header();
    
    // If we are at the root, show the "Photographs" and "Buffer" options
    if (currentDir == "/") {
      webpage += F("<h3>Select a Directory:</h3>");
      webpage += F("<ul>");
      webpage += F("<li><a href='/?dir=Photographs'>[ Photographs ]</a></li>");
      webpage += F("<li><a href='/?dir=Buffer'>[ Buffer ]</a></li>");
      webpage += F("</ul><br><br>");
    } 
    else {
      // If we are inside a directory, show a back button and the files
      webpage += "<h3>Folder: " + currentDir + "</h3>";
      webpage += F("<a href='/'>[ Back to Root ]</a><br><br>");
      webpage += F("<table align='center'>");
      webpage += F("<tr><th>Name</th><th style='width:20%'>Type</th><th>Size</th><th>Action</th></tr>");
      
      // Call printDirectory for the specific folder selected
      printDirectory(currentDir.c_str(), 0);
      
      webpage += F("</table>");
    }

    append_page_footer();
    SendHTML_Content();
    SendHTML_Stop();
  } 
  else {
    ReportSDNotPresent();
  }
    /*
    // Open the root directory of the microSD card
    File root = SD_MMC.open("/");

    if (root) {
      root.rewindDirectory();
      SendHTML_Header();    
      webpage += F("<table align='center'>");
      webpage += F("<tr><th>Name/Type</th><th style='width:20%'>Type File/Dir</th><th>File Size</th></tr>");
      printDirectory("/",0);
      webpage += F("</table>");
      SendHTML_Content();
      root.close();
    }
    else 
    {
      SendHTML_Header();
      webpage += F("<h3>No Files Found</h3>");
    }
    append_page_footer();
    SendHTML_Content();
    // Stop is needed because no content length was sent
    SendHTML_Stop();
  } 
  else 
    ReportSDNotPresent();
  */
}

// Upload a file to the microSD card
void File_Upload()
{
  append_page_header();
  webpage += F("<h3>Select File to Upload</h3>"); 
  webpage += F("<FORM action='/fupload' method='post' enctype='multipart/form-data'>");
  webpage += F("<input class='buttons' style='width:25%' type='file' name='fupload' id = 'fupload' value=''>");
  webpage += F("<button class='buttons' style='width:10%' type='submit'>Upload File</button><br><br>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  server.send(200, "text/html",webpage);
}

// Prints the directory
void printDirectory(const char * dirname, uint8_t levels)
{
  
  File root = SD_MMC.open(dirname);

  if(!root){
    return;
  }
  if(!root.isDirectory()){
    return;
  }
  File file = root.openNextFile();

  int i = 0;
  while(file){

    String fullPath = String(dirname) + "/" + String(file.name());
    fullPath.replace("//", "/");

    if (webpage.length() > 1000) {
      SendHTML_Content();
    }
    if(file.isDirectory()){
      webpage += "<tr><td>"+String(file.isDirectory()?"Dir":"File")+"</td><td>"+String(file.name())+"</td><td></td></tr>";
      printDirectory(file.name(), levels-1);
    }
    else
    {
      webpage += "<tr><td>"+String(file.name())+"</td>";
      webpage += "<td>"+String(file.isDirectory()?"Dir":"File")+"</td>";
      int bytes = file.size();
      String fsize = "";
      if (bytes < 1024)                     fsize = String(bytes)+" B";
      else if(bytes < (1024 * 1024))        fsize = String(bytes/1024.0,3)+" KB";
      else if(bytes < (1024 * 1024 * 1024)) fsize = String(bytes/1024.0/1024.0,3)+" MB";
      else                                  fsize = String(bytes/1024.0/1024.0/1024.0,3)+" GB";
      webpage += "<td>"+fsize+"</td>";
      webpage += "<td>";
      webpage += "<form action='/' method='post' style='display:inline;'>";
      webpage += "<button type='submit' name='download' value='download_" + fullPath + "'>Download</button>";
      webpage += "</form> "; // Space between buttons

      // 2. Correct Delete Button
      webpage += "<form action='/' method='post' style='display:inline;'>";
      webpage += "<button type='submit' name='delete' value='delete_" + fullPath + "'>Delete</button>";
      webpage += "</form>";

      webpage += "</td>";
      webpage += "</tr>";
      /*
      webpage += F("<FORM action='/' method='post'>"); 
      webpage += F("<button type='submit' name='download'"); 
      webpage += F("' value='"); webpage +="download_"+String(file.name()); webpage +=F("'>Download</button>");
      //webpage += "</td>";
      // webpage += "<td>";
      webpage += F("<FORM action='/' method='post'>"); 
      webpage += F("<button type='submit' name='delete'"); 
      webpage += F("' value='"); webpage +="delete_"+String(file.name()); webpage +=F("'>Delete</button>");
      webpage += F("<button type='submit' name='download' value='download_"); 
      webpage += fullPath; // Use full path for the action
      webpage += F("'>Download</button></form></td>");
      webpage += "</td>";
      webpage += "</tr>";
      */
    }
    file = root.openNextFile();
    i++;
  }
  file.close();

 
}

// Download a file from the microSD card
void SD_file_download(String filename)
{
  if (SD_present) 
  { 
    File download = SD_MMC.open("/"+filename);
    if (download) 
    {
      server.sendHeader("Content-Type", "text/text");
      server.sendHeader("Content-Disposition", "attachment; filename="+filename);
      server.sendHeader("Connection", "close");
      server.streamFile(download, "application/octet-stream");
      download.close();
    } else ReportFileNotPresent("download"); 
  } else ReportSDNotPresent();
}

// Handles the file upload a file to the microSD card
File UploadFile;

// Upload a new file to the file system
void handleFileUpload()
{ 
  HTTPUpload& uploadfile = server.upload(); 
  
  // See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/srcv
  // For further information on 'status' structure, there are other reasons such as a failed transfer that could be used
  if(uploadfile.status == UPLOAD_FILE_START)
  {
    String filename = uploadfile.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("Upload File Name: "); Serial.println(filename);
    // Remove a previous version, otherwise data is appended the file again
    SD_MMC.remove(filename);
    // Open the file for writing in SD (create it, if doesn't exist)                         
    UploadFile = SD_MMC.open(filename, FILE_WRITE);
    filename = String();
  }
  else if (uploadfile.status == UPLOAD_FILE_WRITE)
  {
    // Write the received bytes to the file
    if(UploadFile) UploadFile.write(uploadfile.buf, uploadfile.currentSize);
  } 
  else if (uploadfile.status == UPLOAD_FILE_END)
  {
    // Has the file been successfully created?
    if(UploadFile)      
    {
      // Close the file                                    
      UploadFile.close();
      Serial.print("Upload Size: "); Serial.println(uploadfile.totalSize);
      webpage = "";
      append_page_header();
      webpage += F("<h3>File was successfully uploaded</h3>"); 
      webpage += F("<h2>Uploaded File Name: "); webpage += uploadfile.filename+"</h2>";
      webpage += F("<h2>File Size: "); webpage += file_size(uploadfile.totalSize) + "</h2><br><br>"; 
      webpage += F("<a href='/'>[Back]</a><br><br>");
      append_page_footer();
      server.send(200,"text/html",webpage);
    } 
    else
    {
      ReportCouldNotCreateFile("upload");
    }
  }
}

// Delete a file from the microSD card
void SD_file_delete(String filename) 
{ 
  if (SD_present) { 
    SendHTML_Header();
    // Read data from the microSD card
    File dataFile = SD_MMC.open("/"+filename, FILE_READ);
    if (dataFile)
    {
      if (SD_MMC.remove("/"+filename)) {
        Serial.println(F("File deleted successfully"));
        webpage += "<h3>File '"+filename+"' has been erased</h3>"; 
        webpage += F("<a href='/'>[Back]</a><br><br>");
      }
      else
      { 
        webpage += F("<h3>File was not deleted - error</h3>");
        webpage += F("<a href='/'>[Back]</a><br><br>");
      }
    } else ReportFileNotPresent("delete");
    append_page_footer(); 
    SendHTML_Content();
    SendHTML_Stop();
  } else ReportSDNotPresent();
} 

// Send the HTML header tag
void SendHTML_Header()
{
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate"); 
  server.sendHeader("Pragma", "no-cache"); 
  server.sendHeader("Expires", "-1"); 
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); 
  // Empty content inhibits the 'Content-length' header, so we have to close the socket ourselves
  server.send(200, "text/html", "");
  append_page_header();
  server.sendContent(webpage);
  webpage = "";
}

// Send the HTML client that has been written thus far in 'webpage' to the client
void SendHTML_Content()
{
  server.sendContent(webpage);
  webpage = "";
}

// Disconnect the client from the server
void SendHTML_Stop()
{
  server.sendContent("");
  server.client().stop();
}

// Is the microSD card plugged into the ESP32-CAM?
void ReportSDNotPresent()
{
  SendHTML_Header();
  webpage += F("<h3>No SD Card present</h3>"); 
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

// Is the file we want access to present on the server?
void ReportFileNotPresent(String target)
{
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

// Can we upload a file?
void ReportCouldNotCreateFile(String target)
{
  SendHTML_Header();
  webpage += F("<h3>Could Not Create Uploaded File (write-protected?)</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

// Return size of file
String file_size(int bytes)
{
  String fsize = "";
  if (bytes < 1024)                 fsize = String(bytes)+" B";
  else if(bytes < (1024*1024))      fsize = String(bytes/1024.0,3)+" KB";
  else if(bytes < (1024*1024*1024)) fsize = String(bytes/1024.0/1024.0,3)+" MB";
  else                              fsize = String(bytes/1024.0/1024.0/1024.0,3)+" GB";
  return fsize;
}

String getNextBufferPath()
{
  String path = "/Buffer/photo_" + String(bufferIndex) + ".jpg";

  if(SD_MMC.exists(path))
  {
    Serial.println("Buffer full, deleting oldest: " + path);
    SD_MMC.remove(path);
  }

  bufferIndex++;
  if(bufferIndex >= BUFFERCOUNT)
  {
    bufferIndex = 0;
  }

  return path;
}
// ---------------------------------------------------- End of Web Server Code ----------------------------------------------

// ---------------------------------------------------- Object Detection Code -----------------------------------------------
// NOTE: This code was taken nearly verbatim from the example code provided by the library

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

    // Go to the next pixel
    out_ptr_ix++;
    pixel_ix += 3;
    pixels_left--;
  
  }

  // Done!
  return 0;
}
#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif
// ---------------------------------------------------- End of Object Detection Code ----------------------------------------

// ---------------------------------------------------- Camera Code ---------------------------------------------------------
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
        Serial.println("RESOLUTION: 1280x720");
    } else 
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        Serial.println("RESOLUTION: 800x600");
    }

    // These paramters are for the object deteection model
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

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
    String path = "/Photographs/photo_" + String(millis()) + ".jpg";

    // Save image to microSD card
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

// Function to capture and save an image in the 'Buffer' directory
bool takePhotoBuffer() 
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed!");
        return false;
    }

    // Generate a unique filename
    // String path = "/Photographs/photo_" + String(millis()) + ".jpg";

    // Get the next pathname for the rolling buffer
    String path = getNextBufferPath();

    // Save image to microSD card
    File file = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing! Path: " + path);
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
  // The second argument passed to 'begin' initialize the microSD card in 1-bit mode
  // In 4-bit mode, which is the default, the microSD card uses GPIOs 12, 13, 15, 14, 2, and 4
  // We need to initialize the microSD card in 1-bit mode so as to free GPIO 13 for our interrupt
  if(!SD_MMC.begin("/sdcard", true))
  {
    Serial.println("Failed to found microSD card.");
  }

  SD_present = true;
}
// ---------------------------------------------------- End of Camera Code --------------------------------------------------