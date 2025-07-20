// Seeed Studio XIAO ESP32S3 Sense - Voice Assistant Firmware
// Features: Wake word detection, voice recording, camera capture, HTTP POST to Flask server
// Libraries required: WiFi, HTTPClient, SPIFFS, esp_camera, Porcupine (wake word), I2S, ESP32-AudioI2S (for TTS/audio playback)

#include <WiFi.h>
#include <HTTPClient.h>
#include "FS.h"
#include "SPIFFS.h"
#include "esp_camera.h"
#include <I2S.h> // For microphone input
#include <esp32cam.h> // Add this for esp32cam library
#include <Porcupine.h> // Uncomment and configure for wake word
#include <Audio.h> // Uncomment and configure for TTS/audio playback
Audio audio;

// WiFi credentials
const char* ssid = "Gokul";
const char* password = "12345678";

// Flask server endpoint
const char* serverUrl = "http://192.168.1.100:5000/query"; // Replace with your computer's IP

// Audio recording settings
#define SAMPLE_RATE 16000
#define RECORD_TIME_SEC 5
#define WAV_FILE "/voice.wav"

// Camera resolution settings
static auto loRes = esp32cam::Resolution::find(320, 240);
static auto midRes = esp32cam::Resolution::find(350, 530);
static auto hiRes = esp32cam::Resolution::find(800, 600);

// Placeholder for Porcupine wake word engine
Porcupine *porcupine;
int16_t pcm_buffer[512];

const char* device_id = "esp32s3_1";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // WiFi setup
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  // SPIFFS setup
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    while (1);
  }

  // Camera setup using esp32cam
  using namespace esp32cam;
  Config cfg;
  cfg.setPins({
    .d0 = 9,
    .d1 = 11,
    .d2 = 10,
    .d3 = 8,
    .d4 = 13,
    .d5 = 14,
    .d6 = 12,
    .d7 = 18,
    .xclk = 15,
    .pclk = 17,
    .vsync = 6,
    .href = 7,
    .sccb_sda = 4,
    .sccb_scl = 5,
    .pwdn = 16,
    .reset = -1
  });
  cfg.setResolution(loRes); // Start with low resolution
  cfg.setBufferCount(2);
  cfg.setJpeg(80);
  bool ok = Camera.begin(cfg);
  Serial.println(ok ? "CAMERA OK" : "CAMERA FAIL");
  if (!ok) {
    while (1);
  }

  // I2S microphone setup (adjust pins for your hardware)
  I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE, 16);

  // Wake word engine setup (Porcupine)
  // TODO: Replace with your actual access key and .ppn model path
  porcupine = new Porcupine("YOUR_ACCESS_KEY", {"/spiffs/hey-assist.ppn"});
  if (!porcupine) {
    Serial.println("Porcupine init failed");
    while (1);
  }
  Serial.println("Porcupine wake word engine initialized");

  // TTS/audio playback setup (if needed)
}

// Wake word detection using Porcupine
bool detectWakeWord() {
  int num_samples = porcupine->getFrameLength();
  int16_t pcm[num_samples];
  Serial.println("Listening for wake word...");
  while (true) {
    I2S.read((char *)pcm, num_samples * 2);
    int keyword_index = porcupine->process(pcm);
    if (keyword_index >= 0) {
      Serial.println("Wake word detected!");
      return true;
    }
    delay(10);
  }
}

// Write a simple WAV header for 16-bit mono PCM
typedef struct {
  char riff[4] = {'R','I','F','F'};
  uint32_t overall_size;
  char wave[4] = {'W','A','V','E'};
  char fmt_chunk_marker[4] = {'f','m','t',' '};
  uint32_t length_of_fmt = 16;
  uint16_t format_type = 1;
  uint16_t channels = 1;
  uint32_t sample_rate = SAMPLE_RATE;
  uint32_t byterate;
  uint16_t block_align;
  uint16_t bits_per_sample = 16;
  char data_chunk_header[4] = {'d','a','t','a'};
  uint32_t data_size;
} wav_header_t;

void writeWavHeader(File &file, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize) {
  wav_header_t header;
  header.overall_size = dataSize + 36;
  header.byterate = sampleRate * channels * bitsPerSample / 8;
  header.block_align = channels * bitsPerSample / 8;
  header.data_size = dataSize;
  file.write((uint8_t*)&header, sizeof(header));
}

void recordVoice() {
  File file = SPIFFS.open(WAV_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  uint32_t dataSize = SAMPLE_RATE * RECORD_TIME_SEC * 2; // 16-bit mono
  writeWavHeader(file, SAMPLE_RATE, 16, 1, dataSize);
  int16_t sample;
  for (int i = 0; i < SAMPLE_RATE * RECORD_TIME_SEC; i++) {
    I2S.read((char *)&sample, 2);
    file.write((uint8_t *)&sample, 2);
  }
  file.close();
  Serial.println("Voice recorded to SPIFFS");
}

void captureImages() {
  for (int i = 0; i < 3; i++) {
    auto frame = esp32cam::capture();
    if (frame == nullptr) {
      Serial.println("Camera capture failed");
      continue;
    }
    String filename = "/img" + String(i) + ".jpg";
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open image file for writing");
      continue;
    }
    file.write(frame->data(), frame->size());
    file.close();
    Serial.printf("Image %d captured and saved\n", i);
    delay(500); // Small delay between captures
  }
}

void playAudioResponse() {
  // Play the response.mp3 file from SPIFFS using ESP32-AudioI2S
  // Set your I2S output pins here (example: 26, 25, 22)
  audio.setPinout(26, 25, 22); // BCLK, LRC, DOUT (adjust for your hardware)
  audio.connecttoFS(SPIFFS, "/response.mp3");
  while (audio.isRunning()) {
    audio.loop();
  }
}

void streamImage() {
  // Capture image to SPIFFS as "/img_stream.jpg"
  auto frame = esp32cam::capture();
  if (frame == nullptr) return;
  File file = SPIFFS.open("/img_stream.jpg", FILE_WRITE);
  if (!file) return;
  file.write(frame->data(), frame->size());
  file.close();

  HTTPClient http;
  String url = String("http://192.168.1.100:5000/image_stream");
  http.begin(url);
  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  String contentType = "multipart/form-data; boundary=" + boundary;
  http.addHeader("Content-Type", contentType);
  String body = "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n";
  body += device_id;
  body += "\r\n--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"image\"; filename=\"img_stream.jpg\"\r\n";
  body += "Content-Type: image/jpeg\r\n\r\n";
  File img = SPIFFS.open("/img_stream.jpg", FILE_READ);
  while (img.available()) body += (char)img.read();
  img.close();
  body += "\r\n--" + boundary + "--\r\n";
  http.POST((uint8_t*)body.c_str(), body.length());
  http.end();
}

void playResponse(String response) {
  // If response is text, use TTS library to synthesize and play
  // If response is audio, save to SPIFFS and play using Audio library
  // Placeholder: print response
  Serial.println("Assistant says: " + response);
}

void loop() {
  static unsigned long lastImageStream = 0;
  if (millis() - lastImageStream > 1000) {
    streamImage();
    lastImageStream = millis();
  }
  if (detectWakeWord()) {
    Serial.println("Wake word detected!");
    recordVoice();
    sendToServer();
  }
  delay(100);
} 