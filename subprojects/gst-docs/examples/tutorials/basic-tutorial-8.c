
#include "AudioSrc.h"
#include "AudioCapture.h"
#include "play_ogg.h"

#include <gst/gst.h>
int
main (int argc, char *argv[])
{
  g_setenv("GST_DEBUG", "*:5", TRUE);
  //return test_audio_src(argc, argv);
  return test_audio_capture(argc, argv);
  //return play_ogg(argc, argv, "D:/test_audio.ogg");
}
