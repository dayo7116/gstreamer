#pragma once
#include <memory>

namespace PLAY {

    struct AudioBlock {
        AudioBlock() {
            data = nullptr;
            size = 0;
        }
        ~AudioBlock() {
            if (data) {
                delete[] data;
            }
        }
        unsigned char* data;
        int size;
    };

    class AudioPlayer {
    public:
        AudioPlayer();
        ~AudioPlayer();

        void StartPlay();
        void AddOneAudioFrame(const std::shared_ptr<AudioBlock>& audio_frame);

        class Impl;
    protected:
        std::shared_ptr<Impl> m_impl;
    };

}

