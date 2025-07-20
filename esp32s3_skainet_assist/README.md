# ESP32S3 Skainet Voice Assistant - Setup Guide

## 1. ESP-IDF Setup
- Install ESP-IDF (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
- Clone this project and open a terminal in the project root.

## 2. Partition Table
- This project uses a custom partition table (`partitions.csv`) with a 3MB SPIFFS partition.
- In your project root, ensure `partitions.csv` exists (already provided).
- In your project, run:
  ```sh
  idf.py menuconfig
  ```
- Go to: `Partition Table` → `Custom partition table CSV` → set to `partitions.csv`
- Save and exit menuconfig.

## 3. sdkconfig
- Use `idf.py menuconfig` to adjust settings as needed.
- Defaults are fine for most users.
- Ensure SPIFFS is enabled and max files is at least 5.

## 4. Build and Flash
- Connect your ESP32S3 board.
- Run:
  ```sh
  idf.py build
  idf.py -p /dev/tty.usbmodemXXXX flash monitor
  ```
  (Replace `/dev/tty.usbmodemXXXX` with your serial port.)

## 5. Python Flask Server
- Ensure Python 3.8+ is installed.
- Install dependencies:
  ```sh
  pip install flask flask-cors gtts pillow requests openai-whisper torch
  ```
- Start Ollama in a terminal:
  ```sh
  ollama run llava
  ```
- Start the Flask server in your project directory:
  ```sh
  python server.py
  ```
- The server will listen on port 5000 (or as configured).

## 6. Usage
- The ESP32S3 will connect to WiFi, stream images, and send audio on wake word.
- The server will keep the latest 10 images per device and use the most recent for LLM queries.
- The ESP32S3 will play back the audio response from the server.

## 7. Troubleshooting
- If SPIFFS mount fails, check partition table size.
- If you get Python errors, ensure all dependencies are installed.
- If you have issues with Ollama, ensure it is running and the model is pulled.

---
For further help, see the ESP-IDF and Flask documentation or ask your AI assistant! 