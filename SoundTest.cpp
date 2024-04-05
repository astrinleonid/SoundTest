
#include <SDL.h>
#include <iostream>
#define NOMINMAX
#include <windows.h>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <limits>




struct ToneGeneratorState {
    std::atomic<float> frequency{ 440.0 }; // Default frequency (A4)
    std::mutex mutex; // Protects access to the state if needed for complex types
}; 


class CircularBuffer {
public:
    CircularBuffer(size_t capacity)
        : begin(0), end(0), size(0), capacity(capacity), buffer(capacity) {}

    // Add data to the buffer
    void produce(const Sint16* data, size_t samples) {
        std::unique_lock<std::mutex> lock(mutex);
        for (size_t i = 0; i < samples; ++i) {
            if (size < capacity) { // Avoid overwriting data that hasn't been consumed
                buffer[end] = data[i];
                end = (end + 1) % capacity;
                ++size;
            }
        }
        dataAvailable.notify_one();
    }

    // Consume data from the buffer
    void consume(Sint16* data, size_t samples) {
        std::unique_lock<std::mutex> lock(mutex);
        dataAvailable.wait(lock, [&] { return size > 0; });
        for (size_t i = 0; i < samples; ++i) {
            if (size > 0) {
                data[i] = buffer[begin];
                begin = (begin + 1) % capacity;
                --size;
            }
        }
    }

private:
    size_t begin, end, size, capacity;
    std::vector<Sint16> buffer;
    std::mutex mutex;
    std::condition_variable dataAvailable;
};


// The audio callback function. SDL calls this function when it needs more data.
CircularBuffer audioBuffer(48000 * 2); // Example capacity

void audioCallback(void* userdata, Uint8* stream, int len) {
    // Assuming len is always a multiple of sizeof(Sint16) * 2 (stereo)
    int samplesNeeded = len / sizeof(Sint16);
    audioBuffer.consume(reinterpret_cast<Sint16*>(stream), samplesNeeded);
}

std::atomic<bool> keepRunning(true);
void generateWave(CircularBuffer& buffer, ToneGeneratorState& state) {
    double phase = 0.0;
    Sint16 sampleBuffer[48];
    const int sampleRate = 48000;
    while (keepRunning) {
        double phaseStep = state.frequency.load() * 2 * M_PI / sampleRate;

        std::unique_lock<std::mutex> lock(state.mutex, std::defer_lock);
        if (lock.try_lock()) { // Lock if possible, skip if not to avoid blocking audio generation
            for (int i = 0; i < 48; ++i) {
                sampleBuffer[i] = static_cast<Sint16>(sin(phase) * 32767);
                phase += phaseStep;
                if (phase > 2 * M_PI) phase -= 2 * M_PI;
            }
            buffer.produce(sampleBuffer, 48);
        }

        // Adjust sleep/delay to control production rate, if necessary
    }
}



int main(int argc, char* argv[]) {
    
        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
            return -1;
        }

        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 48000; // Sample rate
        want.format = AUDIO_S16SYS; // Sample format (signed 16-bit)
        want.channels = 2; // Stereo
        want.samples = 2048; // Buffer size
        want.callback = audioCallback; // Audio callback function

        // Open the default audio device (NULL specifies the default audio device)
        SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
        if (dev == 0) {
            std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return -1;
        }
        else {
            std::cout << "Default audio device opened successfully." << std::endl;
            // Optional: Check if 'have' matches 'want' and handle any discrepancies
        }

    const int sampleRate = 48000; // Same as in your SDL setup
    //const float frequency = 220.0; // Frequency of the sine wave
    const int duration = 1; // Duration in seconds
    const int numSamples = sampleRate * duration; // Total number of samples
    Sint16 audioData[numSamples]; // Array to hold the audio data

    



    want.userdata = audioData;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    else {
        // Here, 'have' is filled by SDL_OpenAudioDevice
        // You can now check if 'have' matches 'want'
        if (have.freq != want.freq || have.format != want.format || have.channels != want.channels) {
            std::cerr << "Audio device specifications differ from requested:\n";
            std::cerr << "Frequency: got " << have.freq << ", wanted " << want.freq << "\n";
            std::cerr << "Format: got " << have.format << ", wanted " << want.format << "\n";
            std::cerr << "Channels: got " << have.channels << ", wanted " << want.channels << "\n";
            // Handle the discrepancy or exit
        }
    }
    


    ToneGeneratorState toneState;
    std::thread producerThread(generateWave, std::ref(audioBuffer), std::ref(toneState));

    bool applicationIsRunning = true;
    while (applicationIsRunning) {
        std::cout << "Enter 'u' to increase tone, 'd' to decrease, 'q' to quit: ";
        char command;
        std::cin >> command;

        // Clear potential errors and flush the input buffer
        if (std::cin.fail()) {
            std::cin.clear(); // Clear error flags
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Discard the rest of the line
            continue; // Skip further processing and prompt for input again
        }
        else {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Always flush the buffer
        }

        if (command == 'u') {
            float currentFrequency = toneState.frequency.load();
            toneState.frequency.store(currentFrequency + 10.0f);
        }
        else if (command == 'd') {
            float currentFrequency = toneState.frequency.load();
            toneState.frequency.store(currentFrequency - 10.0f);
        }
        else if (command == 'q') {
            applicationIsRunning = false;
        }
    }


    // Signal to stop the producer thread and clean up
   
    producerThread.join();
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}