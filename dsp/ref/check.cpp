#include "synth_song.h"
#include <cstdio>
int main() {
    SynthSong s; s.generate();
    printf("frames=%d beats=%zu\ncks=%d\n", s.frames, s.beatTimes.size(), dsp_checksum(s.pcm.data(), 0, (int)s.pcm.size()));
}
