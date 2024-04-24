
#include <SDL.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cmath> // For M_PI
#include <thread>
#include <limits>
const int buffersize = 48;
struct ToneGeneratorState {
    std::atomic<float> frequency{ 440.0 }; // Default frequency (A4)
    std::atomic<bool> applicationIsRunning{ true };
 };

class CircularBuffer {
public:
    CircularBuffer(size_t capacity) : begin(0), end(0), size(0), capacity(capacity), buffer(capacity) {}

    void produce(const Sint16* data, size_t samples) {
        std::unique_lock<std::mutex> lock(mutex);
        // Wait until there is enough space to produce new data
        canProduce.wait(lock, [&] { return size + samples <= capacity; });
        for (size_t i = 0; i < samples; ++i) {
            buffer[end] = data[i];
            end = (end + 1) % capacity;  // Wrap around at the end of the buffer
            ++size;
        }
        lock.unlock();
        canConsume.notify_one(); // Notify consumer that data is available
    }

    void consume(Sint16* data, size_t samples) {
        std::unique_lock<std::mutex> lock(mutex);
        // Wait until there is enough data to consume
        canConsume.wait(lock, [&] { return size >= samples; });
        for (size_t i = 0; i < samples; ++i) {
            data[i] = buffer[begin];
            begin = (begin + 1) % capacity;  // Wrap around at the beginning of the buffer
            --size;
        }
        lock.unlock();
        canProduce.notify_one(); // Notify producer that space is available
    }

private:
    std::vector<Sint16> buffer;
    std::mutex mutex;
    std::condition_variable canProduce, canConsume;
    size_t begin, end, size, capacity;
};


CircularBuffer audioBuffer(buffersize * 2);

void audioCallback(void* userdata, Uint8* stream, int len) {
    int samplesNeeded = len / sizeof(Sint16);
    audioBuffer.consume(reinterpret_cast<Sint16*>(stream), samplesNeeded);
}

void generateWave(CircularBuffer& buffer, ToneGeneratorState& state) {
    double phase = 0.0;
    const int sampleRate = 48000;
    std::vector<Sint16> sampleBuffer(buffersize);

    while (state.applicationIsRunning.load()) { // Keep running indefinitely
       
        double frequency = state.frequency.load();
        double phaseStep = frequency * 2 * M_PI / sampleRate;
        for (int i = 0; i < buffersize; ++i) {
        sampleBuffer[i] = static_cast<Sint16>(sin(phase) * 32767);
            phase += phaseStep;
            if (phase > 2 * M_PI) phase -= 2 * M_PI;
        }
        buffer.produce(sampleBuffer.data(), buffersize);
     
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = buffersize;
    want.callback = audioCallback;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    
    SDL_PauseAudioDevice(dev, 0); // Start the audio device

    ToneGeneratorState toneState;
    std::thread waveThread(generateWave, std::ref(audioBuffer), std::ref(toneState));

   
    while (toneState.applicationIsRunning.load()) {
        std::cout << "Enter 'u' to increase tone, 'd' to decrease, 'q' to quit: ";
        char command;
        std::cin >> command;

        if (std::cin.fail()) {
            std::cin.clear(); // Clear error flags
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Discard the rest of the line
            continue; // Skip further processing and prompt for input again
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Always flush the buffer

        switch (command) {
        case 'u':
            toneState.frequency.store(toneState.frequency.load() + 10.0f);
            break;
        case 'd':
            toneState.frequency.store(toneState.frequency.load() - 10.0f);
            break;
        case 'q':
            toneState.applicationIsRunning.store(false);
            break;
        }
    }

    waveThread.join();
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}