#include "PluginProcessor.h"
#include "WebUi.h"

using namespace juce;

AudioProcessor::BusesProperties WheelhouseProcessor::buses() {
    auto b = BusesProperties().withOutput("Main", AudioChannelSet::stereo(), true);
    for (int k = 0; k < 4; k++) b = b.withOutput("Direct " + String(9 + 2 * k) + "/" + String(10 + 2 * k), AudioChannelSet::stereo(), false);
    return b;
}

WheelhouseProcessor::WheelhouseProcessor() : AudioProcessor(buses()) {
    const auto& P = WheelhouseEngine::params();
    hostVals = std::make_unique<std::atomic<float>[]>(P.size());
    for (size_t i = 0; i < P.size(); i++) {
        const auto& d = P[i];
        std::unique_ptr<RangedAudioParameter> p;
        const ParameterID id { d.id, 1 };
        if (d.kind == 1) p = std::make_unique<AudioParameterInt>(id, d.name, (int) d.min, (int) d.max, (int) d.def);
        else if (d.kind == 2) p = std::make_unique<AudioParameterBool>(id, d.name, d.def > 0.5f);
        else if (d.kind == 3) p = std::make_unique<AudioParameterChoice>(id, d.name, WheelhouseEngine::choices((int) i), (int) d.def);
        else p = std::make_unique<AudioParameterFloat>(id, d.name, NormalisableRange<float>(d.min, d.max), d.def);
        plist.push_back(p.get());
        hostVals[i] = d.def;
        addParameter(p.release());
    }
    engine.hostParam = hostVals.get();
    // the UI moved a drawbar etc.: tell the host so it records automation
    engine.onUiParam = [this](int i, float plain) {
        auto* p = plist[(size_t) i];
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(plain)); p->endChangeGesture();
        hostVals[(size_t) i] = plain;
    };
    startTimerHz(4);
}
WheelhouseProcessor::~WheelhouseProcessor() { stopTimer(); cancelPendingUpdate(); }

bool WheelhouseProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto main = l.getMainOutputChannelSet();
    if (main != AudioChannelSet::stereo() && main != AudioChannelSet::quadraphonic() && main != AudioChannelSet::create5point1()) return false;
    for (int b = 1; b < l.outputBuses.size(); b++) if (!l.outputBuses[b].isDisabled() && l.outputBuses[b] != AudioChannelSet::stereo()) return false;
    return l.inputBuses.isEmpty() || l.getMainInputChannelSet().isDisabled();
}

void WheelhouseProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    lastBlock = samplesPerBlock;
    engine.prepare(sampleRate, samplesPerBlock);
    for (auto& s : scratch) s.assign((size_t) jmax(64, samplesPerBlock) + 16, 0.f);
    setLatencySamples(engine.latencySamples());
}

void WheelhouseProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi) {
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = 0; c < 16; c++) if (scratch[c].size() < (size_t) n) scratch[c].assign((size_t) n, 0.f);   // only if the host exceeds its announced block size
    float* ptrs[16]; for (int c = 0; c < 16; c++) ptrs[c] = scratch[c].data();
    for (size_t i = 0; i < plist.size(); i++) hostVals[i] = plist[i]->convertFrom0to1(plist[i]->getValue());

    const bool offline = isNonRealtime();
    engine.setNonRealtime(offline);
    if (wasOffline && !offline) triggerAsyncUpdate();
    wasOffline = offline;

    engine.render(ptrs, n, &midi);
    midi.clear();

    // main bus: speaker channels 0..; aux buses k: mic direct outs 9+2k / 10+2k (DSP channels 8+2k, 9+2k)
    buffer.clear();
    const int mainCh = getBus(false, 0) ? getBus(false, 0)->getNumberOfChannels() : 2;
    for (int c = 0; c < mainCh && c < buffer.getNumChannels(); c++) buffer.copyFrom(c, 0, ptrs[c], n);
    int ch = mainCh;
    for (int b = 1; b < getBusCount(false); b++) {
        auto* bus = getBus(false, b); if (!bus || !bus->isEnabled()) continue;
        for (int k = 0; k < bus->getNumberOfChannels() && ch < buffer.getNumChannels(); k++, ch++) buffer.copyFrom(ch, 0, ptrs[8 + 2 * (b - 1) + k], n);
    }
    engine.addTestTone(buffer.getArrayOfWritePointers(), jmin(mainCh, buffer.getNumChannels()), n);
    for (int c = 0; c < jmin(16, buffer.getNumChannels()); c++) { const float* x = buffer.getReadPointer(c); float s = 0; for (int i = 0; i < n; i++) s += x[i] * x[i]; meterAcc[(size_t) c] += s; }
    meterCount += n;
    if (meterCount >= 1024) { for (int c = 0; c < 16; c++) { meterRms[(size_t) c] = std::sqrt(meterAcc[(size_t) c] / (float) meterCount); meterAcc[(size_t) c] = 0; } meterCount = 0; }
}

void WheelhouseProcessor::timerCallback() { if (engine.latencySamples() != getLatencySamples()) setLatencySamples(engine.latencySamples()); }
void WheelhouseProcessor::handleAsyncUpdate() { engine.restoreRealtimeQuality(); }

var WheelhouseProcessor::paramValues() const {
    auto* o = new DynamicObject();
    const auto& P = WheelhouseEngine::params();
    for (size_t i = 0; i < P.size(); i++) o->setProperty(Identifier(P[i].id), hostVals[i].load());
    return var(o);
}
StringArray WheelhouseProcessor::mainChannelNames() const {
    StringArray names;
    if (auto* b = getBus(false, 0)) for (auto t : b->getCurrentLayout().getChannelTypes()) names.add(AudioChannelSet::getAbbreviatedChannelTypeName(t));
    return names;
}

// Project state: the engine's recorded settings + UI state, and the parameter values (applied after the settings)
void WheelhouseProcessor::getStateInformation(MemoryBlock& dest) {
    auto* o = new DynamicObject();
    o->setProperty("engine", engine.saveState());
    o->setProperty("params", paramValues());
    MemoryOutputStream(dest, false).writeString(JSON::toString(var(o), true));
}
void WheelhouseProcessor::setStateInformation(const void* data, int size) {
    var st = JSON::parse(MemoryInputStream(data, (size_t) size, false).readString());
    if (!st.isObject()) return;
    engine.restoreState(st.getProperty("engine", var()));
    auto pv = st.getProperty("params", var());
    const auto& P = WheelhouseEngine::params();
    for (size_t i = 0; i < P.size(); i++) {
        auto v = pv.getProperty(Identifier(P[i].id), var());
        if (v.isVoid()) continue;
        plist[i]->setValueNotifyingHost(plist[i]->convertTo0to1((float) (double) v));
        hostVals[i] = (float) (double) v;
    }
}

AudioProcessorEditor* WheelhouseProcessor::createEditor() { return new WheelhouseEditor(*this); }

// ---------------- Editor: the same WebView UI as the standalone ----------------
WheelhouseEditor::WheelhouseEditor(WheelhouseProcessor& p) : AudioProcessorEditor(p), proc(p) {
    WebUiHooks h;
    h.dspMessage = [this](const var& m) { if (proc.engine.handleMessage(m) && web) web->emitEventIfBrowserIsVisible("roomInfo", proc.engine.makeRoomInfoEvent()); };
    h.audioInfo = [this] { sendAudioInfo(); };
    h.saveUi = [this](const String& s) { proc.engine.uiState = s; };
    h.loadUi = [this] { return proc.engine.uiState; };
    h.chantest = [this](int ch, bool lfe) { proc.engine.startTestTone(ch, lfe); };
    h.params = [this] { return proc.paramValues(); };
    web = std::make_unique<WebBrowserComponent>(wheelhouseWebOptions(h, "WheelhouseWebViewPlugin"));
    addAndMakeVisible(*web);
    web->goToURL(WebBrowserComponent::getResourceProviderRoot());
    setResizable(true, true);
    setResizeLimits(960, 540, 3200, 1800);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(16.0 / 9.0);
    setSize(1280, 720);
    startTimerHz(30);
}
WheelhouseEditor::~WheelhouseEditor() { stopTimer(); web = nullptr; }
void WheelhouseEditor::resized() { if (web) web->setBounds(getLocalBounds()); }

void WheelhouseEditor::timerCallback() {
    if (!web) return;
    var ev = proc.engine.makeRotEvent(0.0);
    Array<var> m; for (int c = 0; c < 16; c++) m.add(20.0 * std::log10(proc.meterRms[(size_t) c].load() + 1e-6));
    ev.getDynamicObject()->setProperty("meters", m);
    ev.getDynamicObject()->removeProperty("cpu");   // the host shows its own load
    web->emitEventIfBrowserIsVisible("rot", ev);
    var u;
    for (int i = 0; i < 64 && proc.engine.popUiEvent(u); i++) web->emitEventIfBrowserIsVisible(u.getProperty("type", "") == "param" ? "param" : "midi", u);
}

void WheelhouseEditor::sendAudioInfo() {
    auto* o = new DynamicObject();
    PluginHostType host;
    o->setProperty("device", String("Host: ") + host.getHostDescription());
    o->setProperty("type", "Plugin");
    o->setProperty("sampleRate", proc.getSampleRate());
    o->setProperty("bufferSize", proc.blockSize());
    auto names = proc.mainChannelNames();
    o->setProperty("outputs", names.size());
    Array<var> n; for (auto& s : names) n.add(s); o->setProperty("channelNames", n);
    o->setProperty("outLatencySamples", proc.blockSize());
    o->setProperty("driverLatencySamples", -1);
    o->setProperty("midiDevices", Array<var>());
    web->emitEventIfBrowserIsVisible("audio", var(o));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WheelhouseProcessor(); }
