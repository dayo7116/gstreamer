
#include "AudioSrc.h"
#include "AudioCapture.h"
#include "play_ogg.h"
#include "SoupServer.h"
#include "SimpleClient.h"

#include <gst/gst.h>
int
main (int argc, char *argv[])
{
  //1 error, 2 warning, 3 fixme, 4 info, 5 debug, 6 log, 7 trace
  g_setenv("GST_DEBUG", "*:3", TRUE);
  //return test_audio_src(argc, argv);
  //return test_audio_capture(argc, argv);
  //return play_ogg(argc, argv, "D:/test_audio.ogg");

  start_soup_server();
  //test_simple_client();

  return 0;
}
