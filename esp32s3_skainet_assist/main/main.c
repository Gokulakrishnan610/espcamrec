#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_skainet.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_spiffs.h"
#include "esp_camera.h"
#include "esp_http_client.h"
#include "driver/i2s.h"

#define WAKE_WORD_GPIO GPIO_NUM_2 // Onboard LED for feedback

// WiFi credentials
#define WIFI_SSID      "Gokul"
#define WIFI_PASS      "12345678"

// Flask server endpoint
#define SERVER_URL     "http://192.168.1.100:5000/query"

// Audio recording settings
#define SAMPLE_RATE    16000
#define RECORD_TIME_SEC 5
#define WAV_FILE_PATH  "/spiffs/voice.wav"
#define IMG_FILE_PATH  "/spiffs/img0.jpg"
#define MP3_FILE_PATH  "/spiffs/response.mp3"

// Camera pin config for XIAO ESP32S3 Sense (OV2640)
#define CAM_PIN_PWDN    16
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    5
#define CAM_PIN_D7      18
#define CAM_PIN_D6      12
#define CAM_PIN_D5      14
#define CAM_PIN_D4      13
#define CAM_PIN_D3      8
#define CAM_PIN_D2      10
#define CAM_PIN_D1      11
#define CAM_PIN_D0      9
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK    17

// I2S mic pins (INMP441 example, adjust as needed)
#define I2S_MIC_BCK     1
#define I2S_MIC_WS      3
#define I2S_MIC_DATA    2

// I2S speaker pins (example, adjust as needed)
#define I2S_SPK_BCK     26
#define I2S_SPK_WS      25
#define I2S_SPK_DATA    22

static const char *TAG = "SKAINET_ASSIST";

// WAV header struct
typedef struct {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
    char fmt_chunk_marker[4];
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_chunk_header[4];
    uint32_t data_size;
} wav_header_t;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying WiFi connection...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected!");
    }
}

// WiFi init
void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// SPIFFS init
void spiffs_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    ESP_LOGI(TAG, "SPIFFS initialized");
}

// Camera init
void camera_init(void) {
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 1
    };
    ESP_ERROR_CHECK(esp_camera_init(&config));
    ESP_LOGI(TAG, "Camera initialized");
}

// Write WAV header
void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels, uint32_t data_size) {
    wav_header_t header = {
        .riff = {'R','I','F','F'},
        .overall_size = data_size + 36,
        .wave = {'W','A','V','E'},
        .fmt_chunk_marker = {'f','m','t',' '},
        .length_of_fmt = 16,
        .format_type = 1,
        .channels = channels,
        .sample_rate = sample_rate,
        .byterate = sample_rate * channels * bits_per_sample / 8,
        .block_align = channels * bits_per_sample / 8,
        .bits_per_sample = bits_per_sample,
        .data_chunk_header = {'d','a','t','a'},
        .data_size = data_size
    };
    fwrite(&header, sizeof(header), 1, file);
}

// Audio recording (I2S, WAV)
void record_audio(const char *path) {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_MIC_BCK,
        .ws_io_num = I2S_MIC_WS,
        .data_out_num = -1,
        .data_in_num = I2S_MIC_DATA
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open WAV file for writing");
        return;
    }
    uint32_t data_size = SAMPLE_RATE * RECORD_TIME_SEC * 2; // 16-bit mono
    write_wav_header(file, SAMPLE_RATE, 16, 1, data_size);

    size_t bytes_read;
    int16_t buffer[1024];
    int total_samples = SAMPLE_RATE * RECORD_TIME_SEC;
    int samples_written = 0;
    while (samples_written < total_samples) {
        i2s_read(I2S_NUM_0, (void*)buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);
        int samples = bytes_read / 2;
        if (samples > (total_samples - samples_written)) {
            samples = total_samples - samples_written;
        }
        fwrite(buffer, 2, samples, file);
        samples_written += samples;
    }
    fclose(file);
    i2s_driver_uninstall(I2S_NUM_0);
    ESP_LOGI(TAG, "Audio recorded to %s", path);
}

// Camera capture
void capture_image(const char *path) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open image file for writing");
        esp_camera_fb_return(fb);
        return;
    }
    fwrite(fb->buf, 1, fb->len, file);
    fclose(file);
    esp_camera_fb_return(fb);
    ESP_LOGI(TAG, "Image captured to %s", path);
}

// HTTP POST to Flask server
void send_to_server(const char *audio_path, const char *img_path) {
    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Prepare multipart body
    char boundary[] = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);

    // Read files
    FILE *audio = fopen(audio_path, "rb");
    FILE *img = fopen(img_path, "rb");
    if (!audio || !img) {
        ESP_LOGE(TAG, "Failed to open files for HTTP POST");
        if (audio) fclose(audio);
        if (img) fclose(img);
        return;
    }
    fseek(audio, 0, SEEK_END);
    size_t audio_size = ftell(audio);
    fseek(audio, 0, SEEK_SET);
    fseek(img, 0, SEEK_END);
    size_t img_size = ftell(img);
    fseek(img, 0, SEEK_SET);

    // Calculate total body size
    char header1[256], header2[256], footer[64];
    int header1_len = snprintf(header1, sizeof(header1),
        "--%s\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"voice.wav\"\r\nContent-Type: audio/wav\r\n\r\n", boundary);
    int header2_len = snprintf(header2, sizeof(header2),
        "\r\n--%s\r\nContent-Disposition: form-data; name=\"image0\"; filename=\"img0.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n", boundary);
    int footer_len = snprintf(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);

    size_t total_len = header1_len + audio_size + header2_len + img_size + footer_len;
    esp_http_client_set_post_field(client, NULL, total_len); // We'll stream the body

    // Open connection
    esp_err_t err = esp_http_client_open(client, total_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        fclose(audio);
        fclose(img);
        esp_http_client_cleanup(client);
        return;
    }

    // Write multipart body
    esp_http_client_write(client, header1, header1_len);
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), audio)) > 0) {
        esp_http_client_write(client, buf, n);
    }
    esp_http_client_write(client, header2, header2_len);
    while ((n = fread(buf, 1, sizeof(buf), img)) > 0) {
        esp_http_client_write(client, buf, n);
    }
    esp_http_client_write(client, footer, footer_len);

    fclose(audio);
    fclose(img);

    // Read response (MP3)
    FILE *mp3 = fopen(MP3_FILE_PATH, "wb");
    if (!mp3) {
        ESP_LOGE(TAG, "Failed to open MP3 file for writing");
        esp_http_client_cleanup(client);
        return;
    }
    int content_length = esp_http_client_fetch_headers(client);
    while (1) {
        int read = esp_http_client_read(client, buf, sizeof(buf));
        if (read <= 0) break;
        fwrite(buf, 1, read, mp3);
    }
    fclose(mp3);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Audio response saved to %s", MP3_FILE_PATH);
}

// Audio playback (WAV via I2S)
void play_audio(const char *path) {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SPK_BCK,
        .ws_io_num = I2S_SPK_WS,
        .data_out_num = I2S_SPK_DATA,
        .data_in_num = -1
    };
    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &pin_config);

    FILE *file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open audio file for playback");
        return;
    }
    // Skip WAV header
    fseek(file, 44, SEEK_SET);
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) {
        size_t bytes_written = 0;
        i2s_write(I2S_NUM_1, buf, n, &bytes_written, portMAX_DELAY);
    }
    fclose(file);
    i2s_driver_uninstall(I2S_NUM_1);
    ESP_LOGI(TAG, "Audio playback finished");
}

void stream_image_task(void *pvParameter) {
    while (1) {
        // Capture image to SPIFFS as "/img_stream.jpg"
        capture_image("/img_stream.jpg");
        // Send to server
        esp_http_client_config_t config = {
            .url = "http://192.168.1.100:5000/image_stream", // Replace with your server IP
            .method = HTTP_METHOD_POST,
            .timeout_ms = 30000
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);

        char boundary[] = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
        char content_type[128];
        snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
        esp_http_client_set_header(client, "Content-Type", content_type);

        FILE *img = fopen("/img_stream.jpg", "rb");
        if (!img) {
            ESP_LOGE(TAG, "Failed to open image file for streaming");
            esp_http_client_cleanup(client);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        fseek(img, 0, SEEK_END);
        size_t img_size = ftell(img);
        fseek(img, 0, SEEK_SET);

        char header[256], footer[64];
        int header_len = snprintf(header, sizeof(header),
            "--%s\r\nContent-Disposition: form-data; name=\"image\"; filename=\"img_stream.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n", boundary);
        int footer_len = snprintf(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);

        size_t total_len = header_len + img_size + footer_len;
        esp_http_client_set_post_field(client, NULL, total_len);

        esp_err_t err = esp_http_client_open(client, total_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection for streaming: %s", esp_err_to_name(err));
            fclose(img);
            esp_http_client_cleanup(client);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        esp_http_client_write(client, header, header_len);
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), img)) > 0) {
            esp_http_client_write(client, buf, n);
        }
        esp_http_client_write(client, footer, footer_len);

        fclose(img);

        esp_http_client_cleanup(client);
        ESP_LOGI(TAG, "Image streamed to server");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "ESP-Skainet Voice Assistant");

    // 1. WiFi
    wifi_init_sta();

    // 2. SPIFFS
    spiffs_init();

    // 3. Camera
    camera_init();

    // 4. Skainet (wake word)
    esp_skainet_handle_t *skainet = esp_skainet_create(NULL);
    esp_skainet_model_t *model = esp_skainet_model_create("wn9"); // "Hi, Lexin"
    esp_skainet_set_model(skainet, model);

    // 5. Feedback LED
    gpio_set_direction(WAKE_WORD_GPIO, GPIO_MODE_OUTPUT);

    // 6. Start image streaming task
    xTaskCreate(stream_image_task, "stream_image_task", 4096, NULL, 5, NULL);

    while (1) {
        int ret = esp_skainet_detect(skainet, NULL); // NULL: use default I2S mic
        if (ret == 1) {
            ESP_LOGI(TAG, "Wake word detected!");
            gpio_set_level(WAKE_WORD_GPIO, 1); // LED on

            // --- Assistant Workflow ---
            record_audio(WAV_FILE_PATH);
            // capture_image(IMG_FILE_PATH); // No longer needed for streaming
            send_to_server(WAV_FILE_PATH, IMG_FILE_PATH);
            play_audio(MP3_FILE_PATH);

            gpio_set_level(WAKE_WORD_GPIO, 0); // LED off
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
} 