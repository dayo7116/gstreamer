//
// Created by dayong.li on 2023/7/27.
//

#include "VideoDecoder.h"

#include <vector>
#include <map>
#include <mutex>
#include <unordered_map>

#include "Logger.h"
// Media NDK
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <sstream>
typedef int64_t XrTime;

class PerformanceStatus {
public:
  PerformanceStatus() = default;

  using StartEnd = std::pair<uint64_t, uint64_t>;
  using StageMap=std::unordered_map<std::string, StartEnd>;

  void Start(XrTime frame_time, const std::string& stage_key);
  void Stop(XrTime frame_time, const std::string& stage_key);
  void Print(XrTime frame_time, const std::string& perf_tag, int width, int height);

protected:
  std::mutex m_lock;
  std::unordered_map<XrTime, StageMap> m_frame_decode_perf;
};
void PerformanceStatus::Start(XrTime frame_time, const std::string& stage_key) {
  std::lock_guard<std::mutex> auto_lock(m_lock);
  if (m_frame_decode_perf.find(frame_time) == m_frame_decode_perf.end()) {
    m_frame_decode_perf[frame_time] = StageMap();
  }
  StageMap& stages = m_frame_decode_perf[frame_time];
  auto current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  stages[stage_key] = std::make_pair<uint64_t, uint64_t>(current_ms, current_ms);
}
void PerformanceStatus::Stop(XrTime frame_time, const std::string& stage_key) {
  std::lock_guard<std::mutex> auto_lock(m_lock);
  auto frame_finder = m_frame_decode_perf.find(frame_time);
  if (frame_finder == m_frame_decode_perf.end()) {
    return;
  }
  StageMap& stages = frame_finder->second;
  auto stage_finder = stages.find(stage_key);
  if (stage_finder != stages.end()) {
    auto current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    stage_finder->second.second = current_ms;
  }
}
void PerformanceStatus:: Print(XrTime frame_time, const std::string& perf_tag, int width, int height) {
  StageMap stages;
  {
    std::lock_guard<std::mutex> auto_lock(m_lock);
    auto frame_finder = m_frame_decode_perf.find(frame_time);
    if (frame_finder == m_frame_decode_perf.end()) {
      return;
    }
    stages = frame_finder->second;
    m_frame_decode_perf.erase(frame_finder);
  }
  static int g_count = 0;
  static int g_total_cost = 0;
  std::ostringstream ss;
  ss << "XR-" << perf_tag << "-Perf frame:" << frame_time << "width:" << width << ",height:" << height;
  for(auto& item : stages) {
    const std::string& key = item.first;
    int duration = item.second.second - item.second.first;
    ss << " {" << key << ":" << duration << "}";
    g_total_cost += duration;
    g_count++;
  }
  ss << ", Average:" << g_total_cost * 1.0 / g_count;
  Log::Write(Log::Level::Error, ss.str());
}

PerformanceStatus g_decode_pef;

struct MTXR_FrameData {
  uint64_t id;
  std::vector<uint8_t> raw_data;

  // Decoded YUV
  std::vector<unsigned char> y_data;
  std::vector<unsigned char> u_data;
  std::vector<unsigned char> v_data;
  std::vector<unsigned char> data; // rgb

  int width;
  int height;

  uint64_t predictedDisplayTime;
};
typedef std::shared_ptr<MTXR_FrameData> MTXR_FrameDataPtr;

struct BufferReleaseGuard {
  BufferReleaseGuard(AMediaCodec *decoder, int buffer_index)
      : decoder(decoder), buffer_index(buffer_index)
  {
  }
  ~BufferReleaseGuard() {
    if (decoder && buffer_index >= 0) {
      AMediaCodec_releaseOutputBuffer(decoder, buffer_index, true);
    }
  }
  AMediaCodec *decoder;
  int buffer_index;
};

class VideoDecoder::Impl {
public:
  Impl(int width, int height) {
    this->width = width;
    this->height = height;

    this->decoder = AMediaCodec_createDecoderByType("video/avc");
    this->format = AMediaFormat_new();
    AMediaFormat_setString(this->format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(this->format, AMEDIAFORMAT_KEY_WIDTH, this->width);
    AMediaFormat_setInt32(this->format, AMEDIAFORMAT_KEY_HEIGHT, this->height);
    AMediaFormat_setInt32(this->format, AMEDIAFORMAT_KEY_FRAME_RATE, this->framerate);
    AMediaFormat_setInt32(this->format, AMEDIAFORMAT_KEY_LATENCY, 0);
#if __ANDROID_API__ >= 30
    AMediaFormat_setInt32(this->format, AMEDIAFORMAT_KEY_LOW_LATENCY, 1);
#endif

    AMediaCodec_configure(this->decoder, this->format, NULL, NULL, 0);

    AMediaCodec_start(this->decoder);
  }
  ~Impl() {
    AMediaCodec_stop(decoder);
    AMediaCodec_delete(decoder);
    AMediaFormat_delete(format);
  }

protected:
  AMediaCodec *decoder;
  AMediaFormat *format;
  int width = 1440 * 2;
  int height = 1584;
  int framerate = 30;

  std::map<int64_t, MTXR_FrameDataPtr> requested_frames;

  bool m_decoding_running = false;
  std::shared_ptr<std::thread> m_decoding_thread;
public:

  bool SendEncodedFrame(std::shared_ptr<MTXR_FrameData> sp_request_frame) {
    if (nullptr == sp_request_frame || 0 == sp_request_frame->raw_data.size()) {
      return false;
    }

    int data_size = sp_request_frame->raw_data.size();
    uint8_t *data = sp_request_frame->raw_data.data();

    /// 记录提交解码的Frame
    requested_frames[sp_request_frame->predictedDisplayTime] = sp_request_frame;

    int ret = 0;
    int timeoutUs = 0;

    ///
    /// 解码yyy
    ///
    ssize_t inputBufferIndex = AMediaCodec_dequeueInputBuffer(decoder, timeoutUs);
    if (inputBufferIndex >= 0) {

      // 填充输入数据到输入缓冲区
      size_t bufferSize = 0;
      uint8_t *inputBuffer = AMediaCodec_getInputBuffer(decoder, inputBufferIndex,
                                                        &bufferSize);
      memcpy(inputBuffer, data, data_size);
      // send to decoder
//    AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM
      AMediaCodec_queueInputBuffer(decoder, inputBufferIndex, 0, data_size,
                                   sp_request_frame->predictedDisplayTime,
                                   AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME);

      g_decode_pef.Start(sp_request_frame->predictedDisplayTime, "HardwareDecode");
    }

    return true;
  }

  MTXR_FrameDataPtr PullDecodedFrame() {
    int timeoutUs = 0;

    // 是否解码完成的标记
    AMediaCodecBufferInfo info;
    int outputBufferIndex = AMediaCodec_dequeueOutputBuffer(decoder, &info, timeoutUs);
    if (outputBufferIndex < 0) {
      return nullptr;
    }
    BufferReleaseGuard auto_release(decoder, outputBufferIndex);

    uint8_t *outputBuffer = AMediaCodec_getOutputBuffer(decoder, outputBufferIndex, nullptr);
    if (!outputBuffer) {
      return nullptr;
    }
    // 解码器输出可用数据
    int64_t time = info.presentationTimeUs;

    std::map<int64_t, MTXR_FrameDataPtr>::iterator find = requested_frames.find(time);
    if (find == requested_frames.end()) {
      return nullptr;
    }
    MTXR_FrameDataPtr sp_decoded_frame = find->second;

    g_decode_pef.Stop(sp_decoded_frame->predictedDisplayTime, "HardwareDecode");
    g_decode_pef.Print(sp_decoded_frame->predictedDisplayTime, "Decode", width, height);
    ///
    /// 备份YUV数据
    ///
    sp_decoded_frame->width = width;
    sp_decoded_frame->height = height;

    sp_decoded_frame->data.resize(info.size);
    memcpy(sp_decoded_frame->data.data(), outputBuffer + info.offset, info.size);

    requested_frames.erase(find);
    return sp_decoded_frame;
  }

  void Start() {
    m_decoding_running = true;
    m_decoding_thread = std::make_shared<std::thread>([this]() {
      ReadVideo();

      std::vector< std::vector<char> >::iterator video_iter = g_video_frames.begin();
      int g_frame_index = 0;
      auto last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

      while(m_decoding_running) {
        std::shared_ptr<MTXR_FrameData> sp_frame;

        auto current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (video_iter != g_video_frames.end() && current_ms - last_ms > 30)
        {
          last_ms = current_ms;
          sp_frame = std::make_shared<MTXR_FrameData>();
          sp_frame->predictedDisplayTime = ++g_frame_index;

          std::vector<char>& frame_data = *video_iter;
          sp_frame->raw_data.resize(frame_data.size());
          memcpy(sp_frame->raw_data.data(), frame_data.data(), frame_data.size());
          video_iter++;
        }

        SendEncodedFrame(sp_frame);
        PullDecodedFrame();
      }
    });
  }
  void Stop() {
    m_decoding_running = false;
    if (m_decoding_thread) {
      m_decoding_thread->join();
      m_decoding_thread = nullptr;
    }
  }
  void ReadVideo() {
    if (g_video_frames.empty()) {
//      const char* file_path = "/storage/emulated/0/Android/data/org.freedesktop.gstreamer.tutorials.tutorial_6/files/video_500.h264";
//      FILE* file = fopen(file_path, "rb");
//
//      if (file) {
//        while (true) {
//          int sz = 0;
//          int read_count = sizeof(int);
//          int result = fread(&sz, read_count, 1, file);
//          if (sz == 0) {
//            fclose(file);
//            break;
//          }
//
//          g_video_frames.push_back(std::vector<char>());
//          std::vector<char>& datas = g_video_frames.back();
//          datas.resize(sz);
//          result = fread(datas.data(), 1, sz, file);
//        }
//        fclose(file);
//      }

      const std::string app_path = "/storage/emulated/0/Android/data/org.freedesktop.gstreamer.tutorials.tutorial_6/";

//      const int file_count = 264;
//      const std::string file_path = app_path + "files/allnv/output_";

      const int file_count = 102;
      const std::string file_path = app_path + "files/mt5/output_";

      for(int i = 1; i <= file_count; i++) {
        std::string filename = file_path + std::to_string(i) + ".h264";
        FILE* file = fopen(filename.c_str(), "rb");
        if (file) {
          fseek(file, 0, SEEK_END);
          long fileSize = ftell(file);
          fseek(file, 0, SEEK_SET);

          g_video_frames.push_back(std::vector<char>());
          std::vector<char>& datas = g_video_frames.back();
          datas.resize(fileSize);
          fread(datas.data(), 1, fileSize, file);

          fclose(file);
        }
      }
      Log::Write(Log::Level::Error, Fmt("XR-Decode frame count:%d, %s", g_video_frames.size(), file_path.c_str()));
    }
  }
  std::vector< std::vector<char> > g_video_frames;
};

VideoDecoder::VideoDecoder(int width, int height) {
  m_impl = std::shared_ptr<Impl>(new Impl(width, height));
}
VideoDecoder::~VideoDecoder() {

}

void VideoDecoder::Start() {
  if(m_impl) {
    m_impl->Start();
  }
}

void VideoDecoder::Stop() {
  if(m_impl) {
    m_impl->Stop();
  }
}
