#include "MainComponent.h"
#include "BinaryData.h"

using namespace juce;

static double numProp(const var& v, const char* name, double def) { auto x = v.getProperty(name, var()); return x.isVoid() ? def : (double) x; }
static bool hasProp(const var& v, const char* name) { return v.hasProperty(name); }

MainComponent::MainComponent() {
    dsp = std::make_unique<leslie::LeslieRoomDSP>(48000);

    auto opts = WebBrowserComponent::Options {}
        .withBackend(WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(WebBrowserComponent::Options::WinWebView2 {}
                                    .withUserDataFolder(File::getSpecialLocation(File::SpecialLocationType::tempDirectory).getChildFile("WheelhouseWebView")))
        .withNativeIntegrationEnabled()
        .withResourceProvider([this](const auto& url) { return getResource(url); })
        .withNativeFunction("dsp", [this](const Array<var>& args, WebBrowserComponent::NativeFunctionCompletion complete) {
            if (args.size() > 0) handleDspMessage(args[0]);
            complete(var());
        })
        .withNativeFunction("audioSettings", [this](const Array<var>&, WebBrowserComponent::NativeFunctionCompletion complete) {
            openAudioSettings(); complete(var());
        })
        .withNativeFunction("audioInfo", [this](const Array<var>&, WebBrowserComponent::NativeFunctionCompletion complete) {
            sendAudioInfo(); complete(var());
        })
        .withNativeFunction("chantest", [this](const Array<var>& args, WebBrowserComponent::NativeFunctionCompletion complete) {
            if (args.size() > 0) { testChannel = (int) args[0]; testSamplesLeft = (int) (currentSampleRate * 0.5); testPhase = 0; }
            complete(var());
        });
    web = std::make_unique<WebBrowserComponent>(opts);
    addAndMakeVisible(*web);
    web->goToURL(WebBrowserComponent::getResourceProviderRoot());

    // Audio: 0 Eingänge, bis zu 8 Ausgänge; gespeicherte Einstellungen aus der Properties-Datei, sonst Standardgerät
    PropertiesFile::Options po; po.applicationName = "Wheelhouse"; po.filenameSuffix = ".settings"; po.folderName = "Wheelhouse"; po.osxLibrarySubFolder = "Application Support";
    auto settingsFile = po.getDefaultFile();
    std::unique_ptr<XmlElement> savedState;
    if (settingsFile.existsAsFile()) savedState = XmlDocument::parse(settingsFile);
    deviceManager.initialise(0, 8, savedState.get(), true);
    deviceManager.addAudioCallback(this);
    deviceManager.addChangeListener(this);
    enableAllMidiInputs();
    deviceManager.addMidiInputDeviceCallback({}, this);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
    deviceManager.removeAudioCallback(this);
    deviceManager.removeMidiInputDeviceCallback({}, this);
    deviceManager.removeChangeListener(this);
    // Audio-Einstellungen sichern
    PropertiesFile::Options po; po.applicationName = "Wheelhouse"; po.filenameSuffix = ".settings"; po.folderName = "Wheelhouse"; po.osxLibrarySubFolder = "Application Support";
    if (auto xml = deviceManager.createStateXml()) { auto f = po.getDefaultFile(); f.getParentDirectory().createDirectory(); xml->writeTo(f); }
    web = nullptr;
}

void MainComponent::resized() { web->setBounds(getLocalBounds()); }

void MainComponent::enableAllMidiInputs() {
    for (auto& d : MidiInput::getAvailableDevices()) deviceManager.setMidiInputDeviceEnabled(d.identifier, true);
}

// ---------------- Audio ----------------
void MainComponent::audioDeviceAboutToStart(AudioIODevice* device) {
    currentSampleRate = device->getCurrentSampleRate();
    currentOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
    std::lock_guard<std::mutex> lock(dspLock);
    if (std::abs(dsp->fs - currentSampleRate) > 0.5) {
        // Kern mit neuer Abtastrate aufbauen (Zustand geht verloren, Einstellungen schickt die Oberfläche erneut)
        dsp = std::make_unique<leslie::LeslieRoomDSP>(currentSampleRate);
        MessageManager::callAsync([this] { if (web) web->emitEventIfBrowserIsVisible("resend", var()); });
    }
    for (auto& s : scratch) s.assign((size_t) device->getCurrentBufferSizeSamples() + 16, 0.f);
    MessageManager::callAsync([this] { sendAudioInfo(); });
}
void MainComponent::audioDeviceStopped() {}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int numOutputChannels, int numSamples, const AudioIODeviceCallbackContext&) {
    for (int c = 0; c < numOutputChannels; c++) if (outputChannelData[c]) FloatVectorOperations::clear(outputChannelData[c], numSamples);
    const int nCh = jmin(8, numOutputChannels);
    float* ptrs[8];
    for (int c = 0; c < 8; c++) { if (scratch[c].size() < (size_t) numSamples) scratch[c].assign((size_t) numSamples, 0.f); ptrs[c] = scratch[c].data(); }
    {
        std::lock_guard<std::mutex> lock(dspLock);
        dsp->process(ptrs, 8, numSamples);
    }
    for (int c = 0; c < nCh; c++) if (outputChannelData[c]) FloatVectorOperations::copy(outputChannelData[c], ptrs[c], numSamples);

    // Kanaltest-Piep
    int tc = testChannel.load();
    if (tc >= 0 && testSamplesLeft > 0 && tc < numOutputChannels && outputChannelData[tc]) {
        double f = tc == 3 && numOutputChannels >= 6 ? 55.0 : 440.0;
        for (int i = 0; i < numSamples && testSamplesLeft > 0; i++, testSamplesLeft--) {
            double env = jmin(1.0, testSamplesLeft / (0.02 * currentSampleRate), (currentSampleRate * 0.5 - testSamplesLeft) / (0.02 * currentSampleRate));
            testPhase += f / currentSampleRate; if (testPhase >= 1) testPhase -= 1;
            double tri = 1 - 4 * std::fabs(testPhase - 0.5);
            outputChannelData[tc][i] += (float) (tri * 0.18 * env);
        }
        if (testSamplesLeft <= 0) testChannel = -1;
    }
    // Pegel
    for (int c = 0; c < nCh; c++) { float s = 0; const float* x = outputChannelData[c] ? outputChannelData[c] : ptrs[c]; for (int i = 0; i < numSamples; i++) s += x[i] * x[i]; meterAcc[(size_t) c] += s; }
    meterCount += numSamples;
    if (meterCount >= 1024) { for (int c = 0; c < 8; c++) { meterRms[(size_t) c] = std::sqrt(meterAcc[(size_t) c] / (float) meterCount); meterAcc[(size_t) c] = 0; } meterCount = 0; }
}

void MainComponent::handleIncomingMidiMessage(MidiInput*, const MidiMessage& m) {
    var ev;
    {
        std::lock_guard<std::mutex> lock(dspLock);
        if (m.isNoteOn()) { dsp->noteOn(m.getNoteNumber() - 36); auto* o = new DynamicObject(); o->setProperty("type", "noteOn"); o->setProperty("key", m.getNoteNumber() - 36); ev = var(o); }
        else if (m.isNoteOff()) { dsp->noteOff(m.getNoteNumber() - 36); auto* o = new DynamicObject(); o->setProperty("type", "noteOff"); o->setProperty("key", m.getNoteNumber() - 36); ev = var(o); }
        else if (m.isController()) {
            int cc = m.getControllerNumber(), v = m.getControllerValue();
            if (cc == 1 || cc == 64) { dsp->setLeslie(v >= 64 ? "fast" : "slow"); auto* o = new DynamicObject(); o->setProperty("type", "leslie"); o->setProperty("v", v >= 64 ? "fast" : "slow"); ev = var(o); }
            else if (cc == 11) { dsp->setVolume(v / 127.0); auto* o = new DynamicObject(); o->setProperty("type", "volume"); o->setProperty("v", v / 127.0); ev = var(o); }
            else if (cc == 123) dsp->panic();
        }
    }
    if (!ev.isVoid()) MessageManager::callAsync([this, ev] { if (web) web->emitEventIfBrowserIsVisible("midi", ev); });
}

void MainComponent::changeListenerCallback(ChangeBroadcaster*) { enableAllMidiInputs(); sendAudioInfo(); }

void MainComponent::timerCallback() {
    if (!web) return;
    auto* o = new DynamicObject();
    {
        std::lock_guard<std::mutex> lock(dspLock);
        o->setProperty("horn", dsp->hornAngle()); o->setProperty("drum", dsp->drumAngle());
        o->setProperty("hornHz", dsp->hornHz()); o->setProperty("drumHz", dsp->drumHz());
    }
    Array<var> m; for (int c = 0; c < 8; c++) m.add(20.0 * std::log10(meterRms[(size_t) c].load() + 1e-6));
    o->setProperty("meters", m);
    web->emitEventIfBrowserIsVisible("rot", var(o));
}

void MainComponent::sendAudioInfo() {
    if (!web) return;
    auto* o = new DynamicObject();
    auto* dev = deviceManager.getCurrentAudioDevice();
    o->setProperty("device", dev ? dev->getName() : "kein Gerät");
    o->setProperty("type", dev ? dev->getTypeName() : "");
    o->setProperty("sampleRate", dev ? dev->getCurrentSampleRate() : 0.0);
    o->setProperty("bufferSize", dev ? dev->getCurrentBufferSizeSamples() : 0);
    o->setProperty("outputs", dev ? dev->getActiveOutputChannels().countNumberOfSetBits() : 0);
    o->setProperty("latencyMs", dev ? dev->getOutputLatencyInSamples() * 1000.0 / jmax(1.0, dev->getCurrentSampleRate()) : 0.0);
    StringArray names; if (dev) { auto chans = dev->getOutputChannelNames(); auto active = dev->getActiveOutputChannels(); for (int i = 0; i < chans.size(); i++) if (active[i]) names.add(chans[i]); }
    Array<var> n; for (auto& s : names) n.add(s); o->setProperty("channelNames", n);
    web->emitEventIfBrowserIsVisible("audio", var(o));
}

void MainComponent::sendRoomInfo() {
    if (!web) return;
    leslie::RoomInfo ri; { std::lock_guard<std::mutex> lock(dspLock); ri = dsp->info(); }
    auto* o = new DynamicObject();
    Array<var> rt; rt.add(ri.rt60[0]); rt.add(ri.rt60[1]); rt.add(ri.rt60[2]);
    o->setProperty("rt60", rt); o->setProperty("rc", ri.rc); o->setProperty("paths", ri.paths);
    web->emitEventIfBrowserIsVisible("roomInfo", var(o));
}

void MainComponent::openAudioSettings() {
    auto* sel = new AudioDeviceSelectorComponent(deviceManager, 0, 0, 2, 8, true, false, false, false);
    sel->setSize(520, 560);
    DialogWindow::LaunchOptions lo;
    lo.content.setOwned(sel);
    lo.dialogTitle = "Wheelhouse – Audio- und MIDI-Einstellungen";
    lo.dialogBackgroundColour = Colour(0xff1d1a16);
    lo.escapeKeyTriggersCloseButton = true;
    lo.useNativeTitleBar = true;
    lo.resizable = true;
    lo.launchAsync();
}

// ---------------- Nachrichten der Oberfläche (gleiches Format wie im Browser) ----------------
void MainComponent::handleDspMessage(const var& m) {
    String type = m.getProperty("type", "").toString();
    bool roomInfoChanged = false;
    {
        std::lock_guard<std::mutex> lock(dspLock);
        auto& d = *dsp;
        if (type == "noteOn") d.noteOn((int) m.getProperty("key", 0));
        else if (type == "noteOff") d.noteOff((int) m.getProperty("key", 0));
        else if (type == "panic") d.panic();
        else if (type == "drawbars") { int v[9] = {0}; if (auto* a = m.getProperty("v", var()).getArray()) for (int i = 0; i < 9 && i < a->size(); i++) v[i] = (int) (*a)[i]; d.setDrawbars(v); }
        else if (type == "perc") { leslie::Perc p; auto v = m.getProperty("v", var()); p.on = (bool) v.getProperty("on", true); p.third = (bool) v.getProperty("third", true); p.fast = (bool) v.getProperty("fast", true); p.soft = (bool) v.getProperty("soft", false); d.setPerc(p); }
        else if (type == "vib") d.setVib(m.getProperty("v", "c3").toString().toStdString());
        else if (type == "drive") d.setDrive(numProp(m, "v", 0.35));
        else if (type == "click") d.setClick(numProp(m, "v", 0.5));
        else if (type == "volume") d.setVolume(numProp(m, "v", 0.8));
        else if (type == "balance") d.setBalance(numProp(m, "v", 0.5));
        else if (type == "leslie") d.setLeslie(m.getProperty("v", "slow").toString().toStdString());
        else if (type == "lfe") d.setLfe(numProp(m, "v", 0.5));
        else if (type == "cabYaw") d.setCabYaw(numProp(m, "v", 0), (bool) m.getProperty("jump", false));
        else if (type == "room") {
            auto v = m.getProperty("v", var());
            auto& r = d.roomRef();
            if (hasProp(v, "Lx")) r.Lx = numProp(v, "Lx", r.Lx);
            if (hasProp(v, "Ly")) r.Ly = numProp(v, "Ly", r.Ly);
            if (hasProp(v, "Lz")) r.Lz = numProp(v, "Lz", r.Lz);
            if (hasProp(v, "absorb")) r.absorb = numProp(v, "absorb", r.absorb);
            if (hasProp(v, "reverb")) r.reverb = numProp(v, "reverb", r.reverb);
            if (hasProp(v, "order")) r.order = (int) numProp(v, "order", r.order);
            if (hasProp(v, "material")) r.material = v.getProperty("material", "holz").toString().toStdString();
            if (hasProp(v, "shape")) r.shape = v.getProperty("shape", "quader").toString().toStdString();
            if (hasProp(v, "tentV")) r.tentV = numProp(v, "tentV", 0);
            if (hasProp(v, "tentS")) r.tentS = numProp(v, "tentS", 0);
            if (hasProp(v, "absBands")) { auto ab = v.getProperty("absBands", var()); if (auto* a = ab.getArray(); a && a->size() >= 3) { r.hasAbsBands = true; for (int i = 0; i < 3; i++) r.absBands[i] = (double) (*a)[i]; } else r.hasAbsBands = false; }
            d.roomChanged(); roomInfoChanged = true;
        }
        else if (type == "character") {
            leslie::Character c; auto v = m.getProperty("v", var());
            c.cond = numProp(v, "cond", 0.5); c.leak = numProp(v, "leak", 0.5); c.contact = numProp(v, "contact", 0.5); c.motor = numProp(v, "motor", 0.4);
            c.gearTune = (bool) v.getProperty("gearTune", true); c.busComp = (bool) v.getProperty("busComp", true);
            d.setCharacter(c);
        }
        else if (type == "pos") {
            auto les = m.getProperty("les", var()), lis = m.getProperty("lis", var());
            d.setPos(!les.isVoid(), numProp(les, "x", 0), numProp(les, "y", 0), !lis.isVoid(), numProp(lis, "x", 0), numProp(lis, "y", 0), hasProp(lis, "yaw"), numProp(lis, "yaw", 0), (bool) m.getProperty("jump", false));
        }
        else if (type == "speakers") {
            leslie::Speakers s;
            if (auto* a = m.getProperty("az", var()).getArray()) for (auto& x : *a) s.az.push_back((double) x);
            if (auto* a = m.getProperty("ch", var()).getArray()) for (auto& x : *a) s.ch.push_back((int) x);
            s.lfe = (int) m.getProperty("lfe", -1); s.stereo = (bool) m.getProperty("stereo", false);
            if (!s.az.empty() && s.az.size() == s.ch.size()) d.setSpeakers(s);
        }
        else if (type == "taps") {
            std::vector<leslie::Tap> taps;
            if (auto* a = m.getProperty("taps", var()).getArray()) for (auto& t : *a) {
                leslie::Tap tp; tp.t = numProp(t, "t", 0); tp.az = numProp(t, "az", 0); tp.el = numProp(t, "el", 0); tp.order = (int) numProp(t, "order", 1);
                if (auto* dp = t.getProperty("dep", var()).getArray(); dp && dp->size() >= 3) for (int i = 0; i < 3; i++) tp.dep[i] = (double) (*dp)[i];
                if (auto* e = t.getProperty("E", var()).getArray(); e && e->size() >= 3) for (int i = 0; i < 3; i++) tp.E[i] = (double) (*e)[i];
                taps.push_back(tp);
            }
            d.setTaps(std::move(taps)); roomInfoChanged = true;
        }
    }
    if (roomInfoChanged) sendRoomInfo();
}

std::optional<WebBrowserComponent::Resource> MainComponent::getResource(const String& url) {
    if (url == "/" || url == "/index.html") {
        std::vector<std::byte> data((size_t) BinaryData::index_htmlSize);
        std::memcpy(data.data(), BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
        return WebBrowserComponent::Resource { std::move(data), String("text/html") };
    }
    return std::nullopt;
}
