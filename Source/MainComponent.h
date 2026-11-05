#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <mutex>
#include <atomic>
#include "LeslieRoomDSP.h"

class MainComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::MidiInputCallback,
                      private juce::ChangeListener,
                      private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void resized() override;

private:
    // Audio
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // UI (WebView bridge)
    void handleDspMessage(const juce::var& m);
    std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);
    void openAudioSettings();
    void sendAudioInfo();
    void sendRoomInfo();
    void enableAllMidiInputs();
    void applyAmpModelOffLock();
    struct AmpRequest { int model = 0, quality = 1; double sag = 0.5; } ampRequest;
    juce::StringArray knownMidiInputs;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<leslie::LeslieRoomDSP> dsp;
    std::mutex dspLock;
    double currentSampleRate = 48000;
    int currentOutputs = 0;
    std::unique_ptr<juce::WebBrowserComponent> web;

    // Channel test tone (see "chantest" native function)
    std::atomic<int> testChannel { -1 };
    std::atomic<int> testSamplesLeft { 0 };
    std::atomic<bool> testIsLfe { false };
    double testPhase = 0;

    // Output level meters
    std::array<std::atomic<float>, 16> meterRms {};
    std::array<float, 16> meterAcc {};
    int meterCount = 0;

    std::vector<float> scratch[16];

    // Notes and performance controls (keys, Leslie switch, swell, panic) from MIDI and the on-screen keys go through a
    // lock-free queue that the audio thread empties at the start of each block: the MIDI threads never touch dspLock,
    // so a key press can never make the audio thread miss its lock.
    struct PlayCmd { uint8_t type = 0; int8_t man = 0; int16_t key = 0; int16_t value = 0; };   // type: see PlayType
    enum PlayType : uint8_t { NOTE_ON = 1, NOTE_OFF, LESLIE, SWELL, PANIC };
    juce::AbstractFifo playFifo { 512 };
    std::array<PlayCmd, 512> playBuf {};
    std::mutex playWriteLock;                                // several MIDI input threads may write; never taken by audio
    void pushPlay(PlayCmd c);
    void drainPlay();

    // Values the 30 Hz UI timer shows, published by the audio thread (no lock needed to read them)
    std::atomic<double> teleHorn { 0 }, teleDrum { 0 }, teleHornHz { 0 }, teleDrumHz { 0 }, telePlate { 0 }, teleCathode { 0 };
    std::atomic<int> teleLatency { 0 }; std::atomic<bool> teleMics { false };
    std::array<std::atomic<float>, 6> teleMicMeter {};
    std::atomic<int> droppedBlocks { 0 };
    // MIDI channels per manual (0 = omni for the upper manual) and controller numbers for Leslie speed / swell pedal
    std::atomic<int> midiUpper { 1 }, midiLower { 2 }, midiPedal { 3 }, leslieCC { 1 }, swellCC { 11 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
