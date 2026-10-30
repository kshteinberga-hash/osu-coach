#pragma once
#include <memory>
#include <string>

// Song playback for the replay viewer (miniaudio). Positions are in map time: .osu timestamps are audio positions.
// Every method is safe to call when nothing is loaded.
class Audio {
public:
    Audio();
    ~Audio();
    bool load(const std::wstring& path);  // false when missing or undecodable
    bool loaded() const;
    void play();
    void pause();
    bool playing() const;
    void seekMs(double mapMs);
    void setSpeed(float s);  // playback rate; pitch changes with it
    void setVolume(float v01);
    double positionMs() const;
    bool ended() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
