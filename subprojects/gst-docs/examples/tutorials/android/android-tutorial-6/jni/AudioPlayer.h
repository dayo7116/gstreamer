#pragma once
#include <stdio.h>

#define CAPTURE_2_FILE 0
#define PLAY_FILE 1

#define ON_WINDOWS 0

#ifdef __cplusplus
extern "C" {
#endif

  struct AudioBlock {
    unsigned char* data;
    int size;
  };

  void start_play();

  void add_audio_frame(struct AudioBlock* block);

  void start_soup_client(const char* server_ip);
  void stop_soup_client();

#ifdef __cplusplus
}
#endif
