from flask import Flask, request, send_file
from gtts import gTTS
import whisper
import requests
from PIL import Image
import os
from flask_cors import CORS
import base64
from collections import defaultdict, deque
import time

app = Flask(__name__)
CORS(app)

# Load Whisper model once
whisper_model = whisper.load_model("base")  # or "small", "medium", "large"

# Rolling buffer: device_id -> deque of (timestamp, image_path)
image_buffers = defaultdict(lambda: deque(maxlen=10))

def transcribe_audio(audio_path):
    result = whisper_model.transcribe(audio_path)
    return result["text"]

def ask_llava(question, image_path):
    # Read image as base64
    with open(image_path, "rb") as f:
        image_b64 = base64.b64encode(f.read()).decode("utf-8")
    payload = {
        "model": "llava",
        "prompt": question,
        "images": [image_b64],
        "stream": False
    }
    response = requests.post(
        "http://localhost:11434/api/generate",
        json=payload
    )
    print("Ollama raw response:", response.text)
    data = response.json()
    if "response" in data:
        return data["response"]
    elif "error" in data:
        raise RuntimeError(f"Ollama error: {data['error']}")
    else:
        raise RuntimeError(f"Unexpected Ollama response: {data}")

@app.route('/image_stream', methods=['POST'])
def image_stream():
    device_id = request.form.get('device_id', request.remote_addr)
    image = request.files.get('image')
    if image:
        ts = int(time.time())
        img_path = f"images/{device_id}_{ts}.jpg"
        os.makedirs("images", exist_ok=True)
        image.save(img_path)
        image_buffers[device_id].append((ts, img_path))
        # Remove old files if needed
        while len(image_buffers[device_id]) > 10:
            _, old_path = image_buffers[device_id].popleft()
            if os.path.exists(old_path):
                os.remove(old_path)
        return "OK", 200
    return "No image", 400

@app.route('/query', methods=['POST'])
def handle_query():
    device_id = request.form.get('device_id', request.remote_addr)
    audio = request.files.get('audio')
    # Get latest image for this device
    if image_buffers[device_id]:
        _, latest_img_path = image_buffers[device_id][-1]
    else:
        latest_img_path = None  # Or a default image
    if audio:
        audio.save('received_voice.wav')
    if latest_img_path:
        image_path = latest_img_path
    else:
        image_path = None
    # 1. Transcribe audio
    question = transcribe_audio('received_voice.wav')
    # 2. Ask local LLM with question and image
    response_text = ask_llava(question, image_path)
    # 3. Synthesize response
    tts = gTTS(response_text)
    tts.save("response.mp3")
    return send_file("response.mp3", mimetype="audio/mpeg")

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5080) 