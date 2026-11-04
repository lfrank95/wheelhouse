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

    // Oberfläche
    void handleDspMessage(const juce::var& m);
    std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);
    void openAudioSettings();
    void sendAudioInfo();
    void sendRoomInfo();
    void enableAllMidiInputs();

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<leslie::LeslieRoomDSP> dsp;
    std::mutex dspLock;
    double currentSampleRate = 48000;
    int currentOutputs = 0;
    std::unique_ptr<juce::WebBrowserComponent> web;

    // Kanaltest
    std::atomic<int> testChannel { -1 };
    int testSamplesLeft = 0;
    double testPhase = 0;

    // Pegel
    std::array<std::atomic<float>, 8> meterRms {};
    std::array<float, 8> meterAcc {};
    int meterCount = 0;

    std::vector<float> scratch[8];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
