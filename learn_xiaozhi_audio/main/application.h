#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <list>
#include <vector>

#include "background_task.h"

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatalError
};

#define SCHEDULE_EVENT (1 << 0)

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Start();
    DeviceState GetDeviceState() const { return device_state_; }
    bool IsVoiceDetected() const { return voice_detected_; }
    void Schedule(std::function<void()> callback);
    void SetDeviceState(DeviceState state);
    void Alert(const char* status, const char* message, const char* emotion = "");
    void DismissAlert();
    void ToggleChatState();
    void Reboot();
    bool CanEnterSleepMode();

    //Redefine the functions for demo
    void StartListening();
    void StopListening();

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::list<std::function<void()>> main_tasks_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    bool keep_listening_ = false;
    bool aborted_ = false;
    bool busy_decoding_audio_ = false;
    bool voice_detected_ = false;
    int clock_ticks_ = 0;

    // Audio encode / decode
    TaskHandle_t audio_loop_task_handle_ = nullptr;
    BackgroundTask* background_task_ = nullptr;
    std::vector<int16_t> demo_audio_raw_data_;

    void MainEventLoop();
    void OnAudioInput();
    void OnAudioOutput();
    void ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples);
    void ResetDecoder();
    void OnClockTimer();
    void AudioLoop();
};


#endif // _APPLICATION_H_