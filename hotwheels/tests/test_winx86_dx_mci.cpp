#include <winx86-dx/winmm/mci.h>

#include <cassert>
#include <string>
#include <vector>

using namespace winx86::dx::winmm;

class CaptureSink final : public MusicSink {
public:
    void play(int track, const std::string& path, uint32_t, uint32_t) override {
        played_track = track;
        played_path = path;
    }
    void stop() override {}
    void pause() override {}
    int played_track = 0;
    std::string played_path;
};

int main() {
    TrackCatalog catalog{{{"audio/green.wav", 12.0}, {"music/final.ogg", 9.5}}};
    CaptureSink sink;
    Cdaudio cd(sink, catalog);
    MciOpenParms open{};
    open.lpstrDeviceType = "cdaudio";
    assert(cd.send_command(0, MCI_OPEN, MCI_OPEN_TYPE, &open) == 0);

    MciStatusParms status{};
    status.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
    assert(cd.send_command(open.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM, &status) == 0);
    assert(status.dwReturn == 2);

    MciPlayParms play{};
    play.dwFrom = MCI_MAKE_TMSF(2, 0, 0, 0);
    assert(cd.send_command(open.wDeviceID, MCI_PLAY, MCI_FROM, &play) == 0);
    assert(sink.played_track == 2);
    assert(sink.played_path == "music/final.ogg");

    Cdaudio empty_cd(sink, TrackCatalog{});
    MciOpenParms empty_open{};
    empty_open.lpstrDeviceType = "cdaudio";
    assert(empty_cd.send_command(0, MCI_OPEN, MCI_OPEN_TYPE, &empty_open) == 0);
    MciStatusParms empty_status{};
    empty_status.dwItem = MCI_STATUS_MEDIA_PRESENT;
    assert(empty_cd.send_command(empty_open.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM, &empty_status) == 0);
    assert(empty_status.dwReturn == 0);
    empty_status = {};
    empty_status.dwItem = MCI_STATUS_READY;
    assert(empty_cd.send_command(empty_open.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM, &empty_status) == 0);
    assert(empty_status.dwReturn == 0);
}
