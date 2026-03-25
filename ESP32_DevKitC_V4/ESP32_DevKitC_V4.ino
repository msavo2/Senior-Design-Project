#include <esp_now.h>
#include <WiFi.h>

// ---------------------------------------------------- Transmitter Code ----------------------------------------------------
// MAC address of ESP32-CAM
uint8_t broadcastAddress_1[] = {0xC0, 0xCD, 0xD6, 0xCF, 0x14, 0x94};
// MAC address of the other ESP32-CAM
uint8_t broadcastAddress_2[] = {0xC0, 0xCD, 0xD6, 0x8D, 0xFC, 0x88};

// Structure the data that we'll send to the ESP32-CAM
typedef struct DEVKIT_Message
{
  bool takePicture;
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
}

void loop() {
  // put your main code here, to run repeatedly:

  // Create test data

  bool takePicture_test = random(0, 2);

  Control_Signal.takePicture = takePicture_test;

  // Send the message to both ESP32-CAMs through ESP-NOW
  esp_err_t result_1 = esp_now_send(broadcastAddress_1, (uint8_t*) &Control_Signal, sizeof(Control_Signal));
  esp_err_t result_2 = esp_now_send(broadcastAddress_2, (uint8_t*) &Control_Signal, sizeof(Control_Signal));

  if(result_1 == ESP_OK)
  {
    Serial.println("(C0:CD:D6:CF:14:94): Sending Confirmed");
  }
  else
  {
    Serial.println("(C0:CD:D6:CF:14:94): Sending Error");
  }

  if(result_2 == ESP_OK)
  {
    Serial.println("(C0:CD:D6:8D:FC:88): Sending Confirmed");
  }
  else
  {
    Serial.println("(C0:CD:D6:8D:FC:88): Sending Error");
  }
  delay(2000);
}
