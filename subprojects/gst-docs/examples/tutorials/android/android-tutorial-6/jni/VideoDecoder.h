//
// Created by dayong.li on 2023/7/27.
//

#ifndef ANDROID_VIDEODECODER_H
#define ANDROID_VIDEODECODER_H
#include <memory>
#include <thread>

class VideoDecoder {
public:
  VideoDecoder(int width, int height);
  ~VideoDecoder();

  void Start();
  void Stop();

protected:
  class Impl;
  std::shared_ptr<Impl> m_impl;
};


#endif //ANDROID_VIDEODECODER_H
