
#include <SDL.h>
#include <RtMidi.h> 
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



void setupMidiInput(RtMidiIn& midiIn, ToneGeneratorState& state) {
    try {
        midiIn.openPort(0);  // Open the first available MIDI port.
        midiIn.setCallback([](double deltatime, std::vector<unsigned char>* message, void* userData) {
            ToneGeneratorState* state = static_cast<ToneGeneratorState*>(userData);
            if (!message->empty()) {
                unsigned char status = message->at(0);
                unsigned char note = message->at(1);
                unsigned char velocity = message->at(2);

                // Example: MIDI note-on message handling (status == 144 for note-on)
                if (status == 144 && velocity > 0) { // Note On message with velocity > 0
                    // Example: Convert MIDI note to frequency (simplified, adjust as necessary)
                    float frequency = 440.0f * pow(2.0f, (note - 69) / 12.0f);
                    state->frequency.store(frequency);
                    std::cout << "Note ON, Pitch: " << (int)note << ", Frequency: " << frequency << " Hz\n";
                }
                else if (status == 128 || (status == 144 && velocity == 0)) { // Note Off
                    std::cout << "Note OFF, Pitch: " << (int)note << std::endl;
                }
            }
            }, &state);

        midiIn.ignoreTypes(false, false, false); // Optionally ignore sysex, timing, or active sensing messages.
    }
    catch (RtMidiError& error) {
        error.printMessage();
    }
}




int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_VIDEO) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return -1;
    }


    RtMidiIn* midiin = new RtMidiIn();
    std::vector<unsigned char> message;
    int nBytes, i;




    try {
        midiin->openPort(0);  // Open first available MIDI port
        midiin->ignoreTypes(false, false, false);  // Don't ignore any messages

        while (true) {
            midiin->getMessage(&message);
            nBytes = message.size();
            for (i = 0; i < nBytes; i++)
                std::cout << "Byte " << i << ": " << (int)message[i] << std::endl;

            if (nBytes > 0)
                break;  // Exit the loop after reading the first message

        }


    }
    catch (RtMidiError& error) {
        std::cerr << "RtMidiError: " << error.getMessage() << std::endl;
        delete midiin;
        return 1;  // Exit with error status
    }



    SDL_Window* window = SDL_CreateWindow("Audio Generator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    RtMidiIn midiIn;
    

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
    std::cout << "Audio device opened successfully.\n";

    ToneGeneratorState toneState;
    setupMidiInput(midiIn, toneState);
    std::thread waveThread(generateWave, std::ref(audioBuffer), std::ref(toneState));

   
    SDL_Event event;
    while (toneState.applicationIsRunning.load()) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                toneState.applicationIsRunning.store(false);
            }
            else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_w:
                    toneState.frequency.store(110.0f);
                    std::cout << "a";
                    break;
                case SDLK_e:
                    toneState.frequency.store(123.0f);
                    std::cout << "b";
                    break;
                case SDLK_r:
                    toneState.frequency.store(131.0f);
                    std::cout << "c";
                    break;
                case SDLK_t:
                    toneState.frequency.store(147.0f);
                    std::cout << "d";
                    break;
                case SDLK_y:
                    toneState.frequency.store(165.0f);
                    std::cout << "e";
                    break;
                case SDLK_u:
                    toneState.frequency.store(175.0f);
                    std::cout << "f";
                    break;
                case SDLK_i:
                    toneState.frequency.store(196.0f);
                    std::cout << "g";
                    break;
                case SDLK_q:
                    toneState.applicationIsRunning.store(false);
                    std::cout << "stop";
                    break;
                }
            }
        }
    }

    waveThread.join();
    delete midiin;
    SDL_CloseAudioDevice(dev);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}