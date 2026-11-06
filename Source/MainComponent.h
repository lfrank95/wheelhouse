#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include "Engine.h"

class WebUiView;

// Standalone app: audio/MIDI devices, the WebView and the 30 Hz UI timer. Sound and settings live in the engine.
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
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void handleDspMessage(const juce::var& m);
    void openAudioSettings();
    void sendAudioInfo();
    void enableAllMidiInputs();
    juce::StringArray knownMidiInputs;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;

    juce::AudioDeviceManager deviceManager;
    WheelhouseEngine engine{false};
    std::unique_ptr<WebUiView> web;

    std::array<std::atomic<float>, 16> meterRms{};
    std::array<float, 16> meterAcc{};
    int meterCount = 0;
    std::vector<float> scratch[16];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
