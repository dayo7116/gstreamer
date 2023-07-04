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


#ifdef __cplusplus
}
#endif
