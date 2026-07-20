#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Parses a standard RIFF/WAVE file (PCM only — 8- or 16-bit, mono/stereo;
// no ADPCM) into interleaved int16 samples. Returns false on anything else
// (malformed header, unsupported format/bit depth).
bool KisakAudioParseWav(
    const uint8_t* data, size_t size, std::vector<int16_t>& outSamples, int& outChannels, int& outRate
);

// Minimal AAudio-backed one-shot mixer for short SFX (gunshots, UI sounds).
// No streaming, no 3D positioning, no per-category volume — just "play this
// clip now" with a handful of concurrent voices so overlapping shots don't
// cut each other off. Lazily opens a single playback stream on first use,
// matched to the first clip's own sample rate/channel count (this port only
// ever plays one weapon's fire sound today, so there's no format-mismatch
// case in practice yet).
void KisakAudioPlayClip(const int16_t* samples, size_t sampleCount, int channels, int sampleRateHz);

// Closes the stream and drops all voices. Safe to call even if audio was
// never used.
void KisakAudioShutdown();
