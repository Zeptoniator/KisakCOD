#include "kisak_audio_android.h"

#include <aaudio/AAudio.h>

#include <algorithm>
#include <android/log.h>
#include <cstring>
#include <mutex>
#include <vector>

namespace {
constexpr const char* kLogTag = "KisakCODAndroid";

struct Voice {
    std::vector<int16_t> samples; // interleaved, same channel count as the stream
    size_t frame = 0;             // next frame index to mix
};

std::mutex g_mutex;
std::vector<Voice> g_voices;
AAudioStream* g_stream = nullptr;
int g_channels = 0;

// Runs on AAudio's own real-time thread: sums every active voice into the
// output buffer (int16 saturating add) and drops voices that finished.
aaudio_data_callback_result_t DataCallback(
    AAudioStream* /*stream*/, void* /*userData*/, void* audioData, int32_t numFrames
) {
    auto* out = static_cast<int16_t*>(audioData);
    std::fill(out, out + static_cast<size_t>(numFrames) * g_channels, static_cast<int16_t>(0));
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_voices.begin(); it != g_voices.end();) {
        Voice& voice = *it;
        const size_t totalFrames = voice.samples.size() / static_cast<size_t>(g_channels);
        int32_t mixed = 0;
        while (mixed < numFrames && voice.frame < totalFrames) {
            for (int c = 0; c < g_channels; ++c) {
                const size_t srcIndex = voice.frame * static_cast<size_t>(g_channels) + static_cast<size_t>(c);
                const size_t dstIndex = static_cast<size_t>(mixed) * static_cast<size_t>(g_channels) + static_cast<size_t>(c);
                const int32_t sum = static_cast<int32_t>(out[dstIndex]) + static_cast<int32_t>(voice.samples[srcIndex]);
                out[dstIndex] = static_cast<int16_t>(std::clamp(sum, -32768, 32767));
            }
            ++voice.frame;
            ++mixed;
        }
        if (voice.frame >= totalFrames) {
            it = g_voices.erase(it);
        } else {
            ++it;
        }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// Lazily opens the single playback stream, matched to the first clip's own
// format. Only one weapon fire sound is played by this port today, so
// there's no need to support multiple concurrently-open formats.
bool EnsureStream(int channels, int sampleRateHz) {
    if (g_stream != nullptr) {
        return true;
    }
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
        return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setSampleRate(builder, sampleRateHz);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, DataCallback, nullptr);

    AAudioStream* stream = nullptr;
    const aaudio_result_t openResult = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (openResult != AAUDIO_OK || stream == nullptr) {
        __android_log_print(
            ANDROID_LOG_WARN, kLogTag, "AAudio openStream a echoue: %s",
            AAudio_convertResultToText(openResult)
        );
        return false;
    }
    const aaudio_result_t startResult = AAudioStream_requestStart(stream);
    if (startResult != AAUDIO_OK) {
        __android_log_print(
            ANDROID_LOG_WARN, kLogTag, "AAudio requestStart a echoue: %s",
            AAudio_convertResultToText(startResult)
        );
        AAudioStream_close(stream);
        return false;
    }
    g_stream = stream;
    g_channels = channels;
    __android_log_print(
        ANDROID_LOG_INFO, kLogTag, "AAudio stream ouvert: %d Hz, %d canaux", sampleRateHz, channels
    );
    return true;
}

} // namespace

bool KisakAudioParseWav(
    const uint8_t* data, size_t size, std::vector<int16_t>& outSamples, int& outChannels, int& outRate
) {
    if (data == nullptr || size < 12 || std::memcmp(data, "RIFF", 4) != 0
        || std::memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* dataChunk = nullptr;
    uint32_t dataSize = 0;
    size_t pos = 12;
    while (pos + 8 <= size) {
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, data + pos + 4, 4);
        const size_t chunkStart = pos + 8;
        if (chunkStart + chunkSize > size) {
            break;
        }
        if (std::memcmp(data + pos, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::memcpy(&audioFormat, data + chunkStart, 2);
            std::memcpy(&channels, data + chunkStart + 2, 2);
            std::memcpy(&sampleRate, data + chunkStart + 4, 4);
            std::memcpy(&bitsPerSample, data + chunkStart + 14, 2);
        } else if (std::memcmp(data + pos, "data", 4) == 0) {
            dataChunk = data + chunkStart;
            dataSize = chunkSize;
        }
        // RIFF chunks are word-aligned: an odd-sized chunk has a pad byte.
        pos = chunkStart + chunkSize + (chunkSize % 2);
    }
    if (audioFormat != 1 || dataChunk == nullptr || channels == 0 || sampleRate == 0) {
        return false;
    }
    outChannels = channels;
    outRate = static_cast<int>(sampleRate);
    if (bitsPerSample == 16) {
        outSamples.resize(dataSize / 2);
        std::memcpy(outSamples.data(), dataChunk, outSamples.size() * 2);
        return true;
    }
    if (bitsPerSample == 8) {
        // Standard WAV 8-bit PCM is unsigned, midpoint 128.
        outSamples.resize(dataSize);
        for (size_t i = 0; i < dataSize; ++i) {
            outSamples[i] = static_cast<int16_t>((static_cast<int>(dataChunk[i]) - 128) * 256);
        }
        return true;
    }
    return false;
}

void KisakAudioPlayClip(const int16_t* samples, size_t sampleCount, int channels, int sampleRateHz) {
    if (samples == nullptr || sampleCount == 0 || channels <= 0 || sampleRateHz <= 0) {
        return;
    }
    if (!EnsureStream(channels, sampleRateHz)) {
        return;
    }
    if (channels != g_channels) {
        __android_log_print(
            ANDROID_LOG_WARN, kLogTag,
            "Clip audio ignore: %d canaux != flux ouvert a %d canaux", channels, g_channels
        );
        return;
    }
    Voice voice;
    voice.samples.assign(samples, samples + sampleCount);
    std::lock_guard<std::mutex> lock(g_mutex);
    constexpr size_t kMaxVoices = 8;
    if (g_voices.size() >= kMaxVoices) {
        g_voices.erase(g_voices.begin());
    }
    g_voices.push_back(std::move(voice));
}

void KisakAudioShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_voices.clear();
    if (g_stream != nullptr) {
        AAudioStream_requestStop(g_stream);
        AAudioStream_close(g_stream);
        g_stream = nullptr;
    }
}
