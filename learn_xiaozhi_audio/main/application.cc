#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "font_awesome_symbols.h"
#include "assets/lang_config.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <esp_app_desc.h>

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(4096 * 8);

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
    esp_timer_start_periodic(clock_timer_handle_, 1000000);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (background_task_ != nullptr) {
        delete background_task_;
    }
    vEventGroupDelete(event_group_);
}

void Application::Alert(const char* status, const char* message, const char* emotion) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}


void Application::ToggleChatState() {
    if (device_state_ != kDeviceStateIdle) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            SetDeviceState(kDeviceStateListening);
        });
    }
}

void Application::StartListening(){
    Schedule([this]() {
        SetDeviceState(kDeviceStateListening);
    });
}
void Application::StopListening(){
    Schedule([this]() {
        SetDeviceState(kDeviceStateSpeaking);
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();

    codec->Start();
    ResetDecoder();

    #if CONFIG_USE_AUDIO_PROCESSOR
        xTaskCreatePinnedToCore([](void* arg) {
            Application* app = (Application*)arg;
            app->AudioLoop();
            vTaskDelete(NULL);
        }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_, 1);
    #else
        xTaskCreate([](void* arg) {
            Application* app = (Application*)arg;
            app->AudioLoop();
            vTaskDelete(NULL);
        }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_);
    #endif

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

#if CONFIG_USE_WAKE_WORD_DETECT
    wake_word_detect_.Initialize(codec);
    wake_word_detect_.OnWakeWordDetected([this](const std::string& wake_word) {
        Schedule([this, &wake_word]() {
            ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
        });
    });
    wake_word_detect_.StartDetection();
#endif

    SetDeviceState(kDeviceStateActivating);

    MainEventLoop();
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop() {
    auto codec = Board::GetInstance().GetAudioCodec();
    while (true) {
        OnAudioInput();
        if (codec->output_enabled()) {
            OnAudioOutput();
        }
    }
}

void Application::OnAudioInput() {
#if CONFIG_USE_WAKE_WORD_DETECT
    std::vector<int16_t> data;
    if (wake_word_detect_.IsDetectionRunning()) {  
        int samples = wake_word_detect_.GetFeedSize();
        if (samples > 0) {
            ReadAudio(data, 16000, samples);
            wake_word_detect_.Feed(data);
        }
    }
    if(device_state_ == kDeviceStateListening
    #if defined(CONFIG_IDF_TARGET_ESP32C6)
    && demo_audio_raw_data_.size() < 24*1024
    #endif
    ){
        demo_audio_raw_data_.insert(demo_audio_raw_data_.end(), data.begin(), data.end());
    }
    if(data.size()) return;
#else
    //When at the state of kDeviceStateListening, start sampling the data
    if (device_state_ == kDeviceStateListening) {
        int samples = 30 * Board::GetInstance().GetAudioCodec()->input_sample_rate() / 1000; //sample duration 30ms
        if (samples > 0) {
            std::vector<int16_t> audio_data_smp;
            ReadAudio(audio_data_smp, 16000, samples);
            demo_audio_raw_data_.insert(demo_audio_raw_data_.end(), audio_data_smp.begin(), audio_data_smp.end());
            return;
        }
    }
#endif    
    vTaskDelay(pdMS_TO_TICKS(30)); //Avoid to run while loop w/ dummy function
}

void Application::ResetDecoder() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec->input_sample_rate() != sample_rate) {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data)) {
            return;
        }
    } else {
        data.resize(samples);
        if (!codec->InputData(data)) {
            return;
        }
    }
}

void Application::OnAudioOutput() {
    if(!demo_audio_raw_data_.size()){
        return; 
    }
    if (device_state_ == kDeviceStateSpeaking) {
        Board::GetInstance().GetAudioCodec()->OutputData(demo_audio_raw_data_);
        demo_audio_raw_data_.clear();
    }
}

void Application::OnClockTimer() {
    clock_ticks_++;

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintRealTimeStats(pdMS_TO_TICKS(1000));
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);

        // If we have synchronized server time, set the status to clock "HH:MM" if the device is idle
        if (device_state_ == kDeviceStateIdle) {
            Schedule([this]() {
                // Set status to clock "HH:MM"
                time_t now = time(NULL);
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                Board::GetInstance().GetDisplay()->SetStatus(time_str);
            });
        }
    }
}

void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SCHEDULE_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            std::list<std::function<void()>> tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }
    }
}


void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);
    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "待命中...");
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
        #if CONFIG_USE_WAKE_WORD_DETECT
            if(!wake_word_detect_.IsDetectionRunning()){
                ESP_LOGI(TAG, "Restart wake up word detection.");
                wake_word_detect_.StartDetection();
            }
        #endif
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("loving");
            display->SetChatMessage("user", "Audio Demo: 聆听用户说话并同步播放...");
            if (previous_state == kDeviceStateSpeaking) {
                // FIXME: Wait for the speaker to empty the buffer
                vTaskDelay(pdMS_TO_TICKS(120));
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);
            display->SetEmotion("laughing");
            display->SetChatMessage("assistant", "Audio Demo: 等待用户按下speak按键...");
            break;
        default:
            // Do nothing
            break;
    }
}


void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }


    // Now it is safe to enter sleep mode
    return true;
}
