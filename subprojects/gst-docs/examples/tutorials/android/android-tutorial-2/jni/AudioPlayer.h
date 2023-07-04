#pragma once
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

  struct AudioBlock {
    unsigned char* data;
    int size;
  };

  void start_play();

  void add_audio_frame(struct AudioBlock* block);


#ifdef __cplusplus
}
#endif
