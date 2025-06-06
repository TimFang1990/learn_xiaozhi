#include "wake_word_detect.h"
#include "application.h"

#include <esp_log.h>
#include <model_path.h>
#include <arpa/inet.h>
#include <sstream>

#define DETECTION_RUNNING_EVENT 1
#define WAKE_NET_TO_PROCESS_VALID_DATA 2

static const char* TAG = "WakeWordDetect";

WakeWordDetect::WakeWordDetect()
    : afe_data_(nullptr),
      wake_word_pcm_(),
      wake_word_opus_() {

    event_group_ = xEventGroupCreate();
}

WakeWordDetect::~WakeWordDetect() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    vEventGroupDelete(event_group_);
}

void WakeWordDetect::Initialize(AudioCodec* codec) {
    codec_ = codec;
    int ref_num = codec_->input_reference() ? 1 : 0;

    srmodel_list_t *models = esp_srmodel_init("model");
    for (int i = 0; i < models->num; i++) {
        ESP_LOGI(TAG, "Model %d: %s", i, models->model_name[i]);
        if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL) {
            wakenet_model_ = models->model_name[i];
            auto words = esp_srmodel_get_wake_words(models, wakenet_model_);
            // split by ";" to get all wake words
            std::stringstream ss(words);
            std::string word;
            while (std::getline(ss, word, ';')) {
                wake_words_.push_back(word);
            }
        }
    }

#if CONFIG_USE_WAKENET_DIRECT_IF
    //Only one model is supported
    afe_iface_ = (esp_wn_iface_t*)esp_wn_handle_from_name(models->model_name[models->num-1]);
    afe_data_ = afe_iface_->create(models->model_name[models->num-1], DET_MODE_95);
#else
    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_config->aec_init = codec_->input_reference();
    afe_config->aec_mode = AEC_MODE_SR_HIGH_PERF;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
#endif

    xTaskCreate([](void* arg) {
        auto this_ = (WakeWordDetect*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 4096, this, 3, nullptr);
}

void WakeWordDetect::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void WakeWordDetect::StartDetection() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void WakeWordDetect::StopDetection() {
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
#ifndef CONFIG_USE_WAKENET_DIRECT_IF
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
#endif
}

bool WakeWordDetect::IsDetectionRunning() {
    return xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT;
}

void WakeWordDetect::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr) {
        return;
    }
#if CONFIG_USE_WAKENET_DIRECT_IF
    data_to_det_.insert(data_to_det_.begin(), data.begin(), data.end());
    xEventGroupSetBits(event_group_, WAKE_NET_TO_PROCESS_VALID_DATA);
#else
    afe_iface_->feed(afe_data_, data.data());
#endif
}

size_t WakeWordDetect::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
#if CONFIG_USE_WAKENET_DIRECT_IF
    return afe_iface_->get_samp_chunksize(afe_data_) * codec_->input_channels();
#else
    return afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
#endif
}

void WakeWordDetect::AudioDetectionTask() {
#if CONFIG_USE_WAKENET_DIRECT_IF
    auto feed_size = afe_iface_->get_samp_chunksize(afe_data_) * sizeof(int16_t);
    auto audio_channels = codec_->input_channels();
    ESP_LOGI(TAG, "Audio detection task started, feed size per channel: %d, audio channels: %d",
        feed_size, audio_channels);
#else
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio detection task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);
#endif

    while (true) {
    #if CONFIG_USE_WAKENET_DIRECT_IF
        xEventGroupWaitBits(event_group_, WAKE_NET_TO_PROCESS_VALID_DATA, pdFALSE, pdTRUE, portMAX_DELAY);
        if(data_to_det_.size()){
            wakenet_state_t state = afe_iface_->detect(afe_data_, data_to_det_.data());
            data_to_det_.clear();
            if(state == WAKENET_DETECTED){
                StopDetection();
                if (wake_word_detected_callback_) {
                    wake_word_detected_callback_(wake_words_[wake_words_.size()-1]);
                }
            }
        }    
        xEventGroupClearBits(event_group_, WAKE_NET_TO_PROCESS_VALID_DATA);
    #else
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);
        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;;
        }

        // Store the wake word data for voice recognition, like who is speaking
        StoreWakeWordData((uint16_t*)res->data, res->data_size / sizeof(uint16_t));

        if (res->wakeup_state == WAKENET_DETECTED) {
            StopDetection();
            last_detected_wake_word_ = wake_words_[res->wake_word_index - 1];

            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(last_detected_wake_word_);
            }
        }
    #endif
    }
}

void WakeWordDetect::StoreWakeWordData(uint16_t* data, size_t samples) {
    // store audio data to wake_word_pcm_
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    // keep about 2 seconds of data, detect duration is 32ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 32) {
        wake_word_pcm_.pop_front();
    }
}

// void WakeWordDetect::EncodeWakeWordData() {
//     wake_word_opus_.clear();
//     if (wake_word_encode_task_stack_ == nullptr) {
//         wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(4096 * 8, MALLOC_CAP_SPIRAM);
//     }
//     wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
//         auto this_ = (WakeWordDetect*)arg;
//         {
//             auto start_time = esp_timer_get_time();
//             auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
//             encoder->SetComplexity(0); // 0 is the fastest

//             for (auto& pcm: this_->wake_word_pcm_) {
//                 encoder->Encode(std::move(pcm), [this_](std::vector<uint8_t>&& opus) {
//                     std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
//                     this_->wake_word_opus_.emplace_back(std::move(opus));
//                     this_->wake_word_cv_.notify_all();
//                 });
//             }
//             this_->wake_word_pcm_.clear();

//             auto end_time = esp_timer_get_time();
//             ESP_LOGI(TAG, "Encode wake word opus %zu packets in %lld ms",
//                 this_->wake_word_opus_.size(), (end_time - start_time) / 1000);

//             std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
//             this_->wake_word_opus_.push_back(std::vector<uint8_t>());
//             this_->wake_word_cv_.notify_all();
//         }
//         vTaskDelete(NULL);
//     }, "encode_detect_packets", 4096 * 8, this, 2, wake_word_encode_task_stack_, &wake_word_encode_task_buffer_);
// }

bool WakeWordDetect::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
