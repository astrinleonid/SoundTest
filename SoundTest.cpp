
#include <SDL.h>
#include <iostream>
#include <windows.h>
#include <vector>
#include <mutex>
#include <condition_variable>

// The audio callback function. SDL calls this function when it needs more data.
void audioCallback(void* userdata, Uint8 * stream, int len) {
    // Cast userdata to the type of your audio buffer
    Sint16* buffer = (Sint16*)userdata;
    // Copy data from your buffer to the audio stream, converting to Uint8
    SDL_memcpy(stream, buffer, len);
    // Here, you would advance your buffer position and handle loop/end of data.
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return -1;
    }


    int audioDeviceCount = SDL_GetNumAudioDevices(0); // 0 for playback devices, 1 for capture devices
    const char* deviceName = nullptr;

    if (audioDeviceCount > 1) {
        // Attempt to open the second device in the list (index 1)
        deviceName = SDL_GetAudioDeviceName(0, 0);
        std::cout << "Opening audio device: " << deviceName << std::endl;
    }
    else {
        std::cerr << "Not enough audio devices available." << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000; // Sample rate
    want.format = AUDIO_S16SYS; // Sample format (signed 16-bit)
    want.channels = 2; // Stereo
    want.samples = 48; // Buffer size
    want.callback = audioCallback; // Your callback function
    // Assuming you have an array of Sint16 type audio data    


    const int sampleRate = 48000; // Same as in your SDL setup
    const float frequency = 220.0; // Frequency of the sine wave
    const int duration = 1; // Duration in seconds
    const int numSamples = sampleRate * duration; // Total number of samples
    Sint16 audioData[numSamples]; // Array to hold the audio data

    // Generate the sine wave
    const Sint16 amplitude = 32767; // Half the max value of Sint16
    for (int i = 0; i < numSamples; ++i) {
        audioData[i] = static_cast<Sint16>(amplitude * sin((2.0 * M_PI * frequency * i) / sampleRate));
    }



    want.userdata = audioData;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(deviceName, 0, &want, &have, 0);
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



    SDL_PauseAudioDevice(dev, 0); // Start playing

    // Your application code here. You should keep the application running
    // to hear the sound, perhaps wait or process user input.

    int audioDurationMs = 2000;


    SDL_Delay(audioDurationMs);

    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
