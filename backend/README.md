# Seeed Studio XIAO ESP32S3 Sense Multimodal Voice Assistant

This project turns your XIAO ESP32S3 Sense into a privacy-friendly, hands-free voice assistant with camera, using only free and local tools (except for TTS, which uses gTTS cloud API).

---

## Features
- Wake word detection (Porcupine, hands-free)
- Voice recording (I2S mic, WAV)
- Camera capture (3 images)
- HTTP POST to Flask server (audio + images)
- Local speech-to-text (Whisper)
- Local LLM (Ollama Llava for vision+text)
- TTS (gTTS, cloud, free)
- ESP32S3 plays back the answer via I2S speaker

---

## Hardware Required
- Seeed Studio XIAO ESP32S3 Sense
- I2S microphone (e.g., INMP441)
- Speaker (for I2S output)
- Jumper wires

### Example I2S Wiring (adjust for your hardware):
- **BCLK**: GPIO 26
- **LRC**: GPIO 25
- **DOUT**: GPIO 22
- **Speaker**: Connect to I2S amplifier or module

---

## Software Setup

### 1. Arduino IDE & ESP32 Board Support
- Install [Arduino IDE](https://www.arduino.cc/en/software)
- Add ESP32 board support ([instructions](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html))
- Install these libraries via Library Manager:
  - `esp32cam`
  - `ESP32-AudioI2S`
  - `Porcupine` (from Picovoice)
  - `HTTPClient`
  - `SPIFFS`

### 2. Porcupine Wake Word Model
- Sign up at [Picovoice Console](https://console.picovoice.ai/)
- Create a project, add "Hey Assist" wake word
- Download the `.ppn` model for ESP32
- Place the `.ppn` file in SPIFFS (see below)
- Get your Porcupine Access Key

#### Upload .ppn to SPIFFS
- In Arduino IDE, use **Tools > ESP32 Sketch Data Upload** (install the plugin if needed)
- Place your `.ppn` file in the `data/` folder of your Arduino project
- Upload to SPIFFS

### 3. ESP32S3 Firmware
- Open `esp32s3_assist.ino`
- Set your WiFi credentials and Flask server IP
- Set your Porcupine Access Key and .ppn path in the code
- Set your I2S output pins in `audio.setPinout(...)`
- Upload the sketch to your ESP32S3

---

## Flask Server & AI Backend

### 1. Python Environment
- Install Python 3.8+
- Install dependencies:
  ```sh
  pip install flask gtts pillow requests openai-whisper torch
  pip install pyttsx3  # (optional, for offline TTS)
  ```

### 2. Ollama (Llava for vision+text)
- Download and install from [https://ollama.com/download](https://ollama.com/download)
- In a terminal:
  ```sh
  ollama pull llava
  ollama run llava
  ```
- Ollama must be running for the Flask server to use it

### 3. Whisper (local STT)
- Already installed above (`openai-whisper`)
- No API key needed

### 4. Run the Flask Server
- In your project directory:
  ```sh
  python server.py
  ```
- The server will listen on port 5000

---

## Usage
1. Power up your ESP32S3 and open Serial Monitor (115200 baud)
2. Say "Hey Assist" (or your chosen wake word)
3. Ask your question and show an object to the camera
4. ESP32S3 records, captures images, sends to Flask
5. Flask transcribes, queries LLM, synthesizes TTS, returns MP3
6. ESP32S3 plays the answer via speaker

---

## Troubleshooting
- **Ollama not found:** Install from [https://ollama.com/download](https://ollama.com/download) and restart your terminal
- **No audio playback:** Check I2S wiring and pin numbers in code
- **Wake word not detected:** Ensure .ppn is in SPIFFS and access key is correct
- **Flask server errors:** Check Python dependencies and that Ollama is running
- **Slow transcription:** Use a smaller Whisper model ("base" or "small")
- **gTTS errors:** Requires internet connection

---

## Optional: Fully Offline TTS
- Use `pyttsx3` instead of `gTTS` in `server.py` for robotic, offline speech

---

## Credits
- [Picovoice Porcupine](https://github.com/Picovoice/porcupine)
- [Ollama](https://ollama.com/)
- [OpenAI Whisper](https://github.com/openai/whisper)
- [gTTS](https://pypi.org/project/gTTS/)
- [ESP32-AudioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)
- [Seeed Studio XIAO ESP32S3 Sense](https://wiki.seeedstudio.com/XIAO_ESP32S3_Sense/) 