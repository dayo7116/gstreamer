//
// Created by dayong.li on 2023/7/27.
//
#include "DecodeAPI.h"
#include "VideoDecoder.h"

std::shared_ptr<VideoDecoder> g_decoder;
void test_decode() {
  if (!g_decoder) {
    g_decoder = std::shared_ptr<VideoDecoder>(new VideoDecoder(4096, 2048));
  }
  g_decoder->Stop();
  g_decoder->Start();
}
