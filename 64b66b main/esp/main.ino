#include "esp_camera.h"
#include <WiFi.h>
#include "camera_pins.h"
#include "encode_64b66b.h"

const char* ssid = "XXXXXXXX";
const char* password = "xxxxxxxxxxxx";
const char* serverIP = "XXX.XXX.XXX.XXX";
const int serverPort = 5000;

WiFiServer server(80);

void sendPhotoToVPS(camera_fb_t *fb) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  WiFiClient client;
  if (!client.connect(serverIP, serverPort)) {
    Serial.println("Connection failed");
    return;
  }

  // Calculate encoded size (9 bytes per 8-byte block)
  const size_t input_len = fb->len;
  const size_t num_blocks = (input_len + 7) / 8;  // Round up
  const size_t encoded_size = num_blocks * 9;
  
  uint8_t* encoded_data = (uint8_t*)malloc(encoded_size);
  if (!encoded_data) {
    Serial.println("Memory allocation failed");
    client.stop();
    return;
  }

  uint64_t scrambler_state = (1ULL << 58) - 1;  // Fresh state per photo

  // Process in 8-byte chunks
  for (size_t block_idx = 0; block_idx < num_blocks; block_idx++) {
    const size_t offset = block_idx * 8;
    const size_t bytes_left = input_len - offset;
    const size_t valid_bytes = (bytes_left > 8) ? 8 : bytes_left;

    // Extract 64-bit chunk (big-endian format)
    uint64_t chunk = 0;
    for (size_t i = 0; i < 8; i++) {
      const uint8_t byte = (i < valid_bytes) ? fb->buf[offset + i] : 0x00;
      chunk = (chunk << 8) | byte;
    }

    // Encode and store result
    uint8_t encoded_block[9];
    encode_64b66b(chunk, encoded_block, &scrambler_state);
    memcpy(&encoded_data[block_idx * 9], encoded_block, 9);
  }

  // Send encoded data
  String boundary = "ESP32Boundary";
  String header = "POST /upload HTTP/1.1\r\n";
  header += "Host: " + String(serverIP) + "\r\n";
  header += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
  header += "Content-Length: " + String(encoded_size + boundary.length() * 2 + 8) + "\r\n\r\n";
  
  client.print(header);
  client.print("--" + boundary + "\r\n");
  client.print("Content-Disposition: form-data; name=\"image\"; filename=\"encoded.img\"\r\n");
  client.print("Content-Type: application/octet-stream\r\n\r\n");
  
  client.write(encoded_data, encoded_size);
  client.print("\r\n--" + boundary + "--\r\n");

  free(encoded_data);
  client.stop();
}


void startCameraServer() {
  server.begin();
  Serial.println("Camera Ready! Connect to http://your-esp32-cam-ip");
}

void handleClientStream(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println();

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      client.stop();
      return;
    }

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.println("\r\n");

    esp_camera_fb_return(fb);

    if (!client.connected()) break;
  }
}

void handleSerialCommand(void *parameter) {
  while (true) {
    if (Serial.available()) {
      char command = Serial.read();
      if (command == 'p') {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
          sendPhotoToVPS(fb);
          esp_camera_fb_return(fb);
          Serial.println("Photo sent to VPS");
        } else {
          Serial.println("Failed to capture photo");
        }
      }
    }
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

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

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  startCameraServer();
  xTaskCreate(handleSerialCommand, "SerialCommandTask", 4096, NULL, 1, NULL);
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    handleClientStream(client);
  }
}