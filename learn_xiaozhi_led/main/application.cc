#include "application.h"
#include <cstring>
#include <esp_log.h>

#define TAG "Application"

Application::Application() {
    //event_group_ = xEventGroupCreate();
    //background_task_ = new BackgroundTask(4096 * 8);

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

}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    // if (background_task_ != nullptr) {
    //     delete background_task_;
    // }
    //vEventGroupDelete(event_group_);
}

void Application::Start() {
    ESP_LOGI(TAG, "Start Init ... ");
    /* Start the main loop */
    xTaskCreate([](void* arg) {
        Application* app = (Application*)arg;
        app->MainLoop();
        vTaskDelete(NULL);
    }, "main_loop", 4096 * 2, this, 4, nullptr);
    ESP_LOGI(TAG, "Start Init Done, Entering MainLoop() ... ");
}

// The Main Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainLoop() {
    while (true) {
        if(++state_count_ > 8) state_count_ = 1;
        device_state_ = DeviceState(state_count_);
        GetLed()->OnStateChanged();
        ESP_LOGI(TAG, "State change to %s (%d).", stateName(device_state_), state_count_);
        //Software delay just for test
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

SingleLed* Application::GetLed(){
    static SingleLed led(GPIO_NUM_48);
    return &led;
}

const char * Application::stateName(DeviceState state){
    switch(state){
        case kDeviceStateUnknown:
            return "kDeviceStateUnknown";
        case kDeviceStateStarting:
            return "kDeviceStateStarting";
        case kDeviceStateWifiConfiguring:
            return "kDeviceStateWifiConfiguring";
        case kDeviceStateIdle:
            return "kDeviceStateIdle";
        case kDeviceStateConnecting:
            return "kDeviceStateConnecting";
        case kDeviceStateListening:
            return "kDeviceStateListening";
        case kDeviceStateSpeaking:
            return "kDeviceStateSpeaking";
        case kDeviceStateUpgrading:
            return "kDeviceStateUpgrading";
        case kDeviceStateActivating:
            return "kDeviceStateActivating";
        case kDeviceStateFatalError:
            return "kDeviceStateFatalError";
        default:
            return "Invalid_kDeviceState";
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
    }
}
