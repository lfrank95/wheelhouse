#pragma once
// Wheelhouse as a VST3 instrument: MIDI in; main output stereo, quad or 5.1 (whatever the track offers) plus four
// optional stereo buses for the mic direct outs 9-16. Automatable: drawbars, Rotary, swell, drive, vibrato,
// percussion. Everything else is stored with the project as the UI's settings.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "Engine.h"

class WheelhouseProcessor : public juce::AudioProcessor, private juce::Timer, private juce::AsyncUpdater {
public:
    WheelhouseProcessor();
    ~WheelhouseProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Wheelhouse"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    WheelhouseEngine engine{true};
    std::array<std::atomic<float>, 16> meterRms{};
    int blockSize() const { return lastBlock; }
    juce::var paramValues() const;
    juce::StringArray mainChannelNames() const;

private:
    static BusesProperties buses();
    void timerCallback() override;     // latency changes -> host
    void handleAsyncUpdate() override; // after a bounce: back to the real-time quality
    std::vector<juce::RangedAudioParameter*> plist;
    std::unique_ptr<std::atomic<float>[]> hostVals;
    std::vector<float> scratch[16];
    std::array<float, 16> meterAcc{};
    int meterCount = 0;
    int lastBlock = 512;
    bool wasOffline = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WheelhouseProcessor)
};

class WheelhouseEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit WheelhouseEditor(WheelhouseProcessor&);
    ~WheelhouseEditor() override;
    void resized() override;

private:
    void timerCallback() override;
    void sendAudioInfo();
    WheelhouseProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WheelhouseEditor)
};
