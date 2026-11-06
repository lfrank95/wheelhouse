#pragma once
// Wheelhouse - the part shared by the standalone app and the VST3 plugin: owns the DSP core, applies the UI's JSON
// messages, keeps the real-time rules (bounded lock wait, lock-free play queue, published telemetry), records the
// settings so they can be stored with a project, and maps the automatable plugin parameters.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <mutex>
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include "RotaryRoomDSP.h"

class WheelhouseEngine {
public:
    explicit WheelhouseEngine(bool plugin);

    // ---------------- Lifecycle ----------------
    /** (Re)creates the DSP core for a sample rate (keeps the thread count). Returns true if it was rebuilt, then
        the UI must re-send all settings (the engine replays the recorded ones itself). */
    bool prepare(double sampleRate, int maxBlock);
    double sampleRate() const { return fs; }

    // ---------------- Audio thread ----------------
    /** Renders n samples into out[0..15] (8 room/speaker channels + 8 mic direct outs). MIDI events (plugin) are
        applied at their sample positions. Waits at most 30 % of the block for the settings lock, else outputs silence.
     */
    void render(float* const* out16, int n, const juce::MidiBuffer* midi = nullptr);
    /** Adds the channel test tone ("chantest") to an output buffer. */
    void addTestTone(float* const* out, int nCh, int n);

    // ---------------- Any thread ----------------
    /** MIDI from a device (standalone): goes through the play queue. Returns a UI event (key display) or void. */
    juce::var midiFromDevice(const juce::MidiMessage& m);
    void startTestTone(int channel, bool lfe);

    // ---------------- Message thread ----------------
    /** One UI message (JSON object with "type"). Records it for the project state. Returns true if the room info
     * changed. */
    bool handleMessage(const juce::var& m, bool record = true);
    juce::var makeRotEvent(double cpu); // 30 Hz telemetry for the UI ("rot" event)
    juce::var makeRoomInfoEvent();      // "roomInfo" event
    bool popUiEvent(juce::var& ev);     // key display from host MIDI, parameter changes from automation
    int latencySamples() const { return teleLatency.load(); }
    int droppedBlockCount() const { return droppedBlocks.load(); }

    // ---------------- State ----------------
    juce::String uiState;                      // the UI's own saved state (JSON string, see saveUi/loadUi)
    juce::var saveState() const;               // recorded messages + UI state
    void restoreState(const juce::var& state); // replays the messages

    // ---------------- Automatable parameters (plugin) ----------------
    struct ParamDef {
        std::string id, name;
        int kind;
        float min, max, def;
    }; // kind: 0 float, 1 int, 2 bool, 3 choice
    static const std::vector<ParamDef>& params();
    static const juce::StringArray& choices(int index);
    /** Plugin, audio thread: the host's current (plain) values, applied to the DSP inside render(). */
    std::atomic<float>* hostParam = nullptr; // array of params().size(), owned by the processor
    /** Called on the message thread when the UI changed an automatable value: the processor tells the host. */
    std::function<void(int index, float plain)> onUiParam;

    // offline render (plugin bounce): maximum quality
    std::atomic<bool> offlineMax{true};
    void setNonRealtime(bool offline); // audio thread
    void restoreRealtimeQuality();     // message thread

private:
    void pushPlay(uint8_t type, int man, int key, int value);
    void drainPlay();
    void applyHostParams(); // audio thread, lock held
    void uiParamsFromMessage(const juce::String& type, const juce::var& m);
    void applyAmpModelOffLock();
    void record(const juce::String& type, const juce::var& m);
    static int autoThreads(bool plugin);

    const bool isPlugin;
    double fs = 48000;
    std::unique_ptr<rotary::RotaryRoomDSP> dsp;
    std::mutex dspLock;

    struct PlayCmd {
        uint8_t type = 0;
        int8_t man = 0;
        int16_t key = 0;
        int16_t value = 0;
    };
    enum PlayType : uint8_t { NOTE_ON = 1, NOTE_OFF, ROTARY, SWELL, PANIC };
    juce::AbstractFifo playFifo{512};
    std::array<PlayCmd, 512> playBuf{};
    std::mutex playWriteLock;

    // UI events produced on the audio thread (host MIDI notes, automation): small lock-free queue
    struct UiEv {
        uint8_t kind = 0;
        int8_t man = 0;
        int16_t key = 0;
        int16_t note = -1;
    }; // kind 1 note on, 2 note off, 3 param (key = index); note = incoming MIDI note
    juce::AbstractFifo uiFifo{256};
    std::array<UiEv, 256> uiBuf{};
    void pushUi(UiEv e);

    std::atomic<double> teleHorn{0}, teleDrum{0}, teleHornHz{0}, teleDrumHz{0}, telePlate{0}, teleCathode{0};
    std::atomic<int> teleLatency{0};
    std::atomic<bool> teleMics{false};
    std::array<std::atomic<float>, 6> teleMicMeter{};
    std::atomic<int> droppedBlocks{0};

public:
    /** Latest snapshot of the rendered room paths (for the floor plan). Returns false if none is available yet.
        Safe to call from the message thread; the audio thread refreshes it about 30 times a second. */
    bool readPathViz(rotary::PathViz& out) const;

private:
    rotary::PathViz vizBuf;          // written by the audio thread under the sequence counter below (seqlock)
    std::atomic<uint32_t> vizSeq{0}; // odd while writing
    int vizCountdown = 0;            // samples until the next snapshot

    std::atomic<int> midiUpper{1}, midiLower{2}, midiPedal{3}, rotaryCC{1}, swellCC{11};

    // Keyboard split (one keyboard on the upper channel plays pedal | lower | upper) and octave transposition per
    // manual. A note-off always goes where its note-on went, even if split or transposition changed in between.
    std::atomic<int> splitMode{0}, splitHi{60}, splitLo{48}; // mode 0 off, 1 lower|upper, 2 pedal|lower|upper
    std::atomic<int> octShift[3]{{0}, {0}, {0}};
    int8_t heldMan[2][16][128], heldKey[2][16][128]; // [path: device, host][channel][note]
    bool mapNote(int path, int channel, int note, bool on, int& man, int& key);
    struct AmpRequest {
        int model = 0, quality = 1;
        double sag = 0.5;
    } ampRequest;

    // parameters as last applied to the DSP (shared by the audio thread and UI-originated changes)
    std::vector<std::atomic<float>> applied;
    std::atomic<bool> paramsInit{false};

    // recorded settings: last message per key, in first-seen order
    juce::StringArray recKeys;
    std::map<juce::String, juce::var> recMsgs;
    mutable std::mutex recLock;

    // test tone
    std::atomic<int> testChannel{-1}, testSamplesLeft{0};
    std::atomic<bool> testIsLfe{false};
    double testPhase = 0;

    bool offlineActive = false;
};
