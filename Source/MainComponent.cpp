#include "MainComponent.h"
#include "BinaryData.h"

using namespace juce;

static double numProp(const var& v, const char* name, double def) { auto x = v.getProperty(name, var()); return x.isVoid() ? def : (double) x; }
static bool hasProp(const var& v, const char* name) { return v.hasProperty(name); }
// Oberflächen-Zustand (Zugriegel, Raum, Leslie, MIDI-Kanäle …) zwischen zwei Starts
static File uiStateFile() { return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Wheelhouse").getChildFile("ui-state.json"); }

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
        .withNativeFunction("saveUi", [](const Array<var>& args, WebBrowserComponent::NativeFunctionCompletion complete) {
            if (args.size() > 0) { auto f = uiStateFile(); f.getParentDirectory().createDirectory(); f.replaceWithText(args[0].toString()); }
            complete(var());
        })
        .withNativeFunction("loadUi", [](const Array<var>&, WebBrowserComponent::NativeFunctionCompletion complete) {
            auto f = uiStateFile(); complete(f.existsAsFile() ? var(f.loadFileAsString()) : var(""));
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
    deviceManager.initialise(0, 16, savedState.get(), true);
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
    const int nCh = jmin(16, numOutputChannels);
    float* ptrs[16];
    for (int c = 0; c < 16; c++) { if (scratch[c].size() < (size_t) numSamples) scratch[c].assign((size_t) numSamples, 0.f); ptrs[c] = scratch[c].data(); }
    {
        std::lock_guard<std::mutex> lock(dspLock);
        dsp->process(ptrs, 16, numSamples);
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
    if (meterCount >= 1024) { for (int c = 0; c < 16; c++) { meterRms[(size_t) c] = std::sqrt(meterAcc[(size_t) c] / (float) meterCount); meterAcc[(size_t) c] = 0; } meterCount = 0; }
}

void MainComponent::handleIncomingMidiMessage(MidiInput*, const MidiMessage& m) {
    var ev;
    const int ch = m.getChannel();
    // Zuordnung: Pedal und Untermanual über ihren Kanal, alles andere (oder Omni) aufs Obermanual
    int man = 0;
    if (ch == midiPedal.load()) man = 2;
    else if (ch == midiLower.load()) man = 1;
    else if (midiUpper.load() != 0 && ch != midiUpper.load()) man = -1;
    auto noteEvent = [&](const char* type, int key) {
        auto* o = new DynamicObject(); o->setProperty("type", type); o->setProperty("key", key); o->setProperty("man", man); ev = var(o);
    };
    {
        std::lock_guard<std::mutex> lock(dspLock);
        if (man >= 0 && (m.isNoteOn() || m.isNoteOff())) {
            int key = m.getNoteNumber() - (man == 2 ? 24 : 36);
            if (m.isNoteOn()) { dsp->noteOn(key, man); noteEvent("noteOn", key); }
            else { dsp->noteOff(key, man); noteEvent("noteOff", key); }
        }
        else if (m.isController()) {
            int cc = m.getControllerNumber(), v = m.getControllerValue();
            if (cc == leslieCC.load()) { dsp->setLeslie(v >= 64 ? "fast" : "slow"); auto* o = new DynamicObject(); o->setProperty("type", "leslie"); o->setProperty("v", v >= 64 ? "fast" : "slow"); ev = var(o); }
            else if (cc == swellCC.load()) { dsp->setVolume(v / 127.0); auto* o = new DynamicObject(); o->setProperty("type", "volume"); o->setProperty("v", v / 127.0); ev = var(o); }
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
    Array<var> m; for (int c = 0; c < 16; c++) m.add(20.0 * std::log10(meterRms[(size_t) c].load() + 1e-6));
    o->setProperty("meters", m);
    o->setProperty("cpu", deviceManager.getCpuUsage());
    {
        std::lock_guard<std::mutex> lock(dspLock); o->setProperty("vb", dsp->ampPlate()); o->setProperty("vk", dsp->ampCathode());
        if (dsp->mics().on) { Array<var> mm; for (int c = 0; c < leslie::MIC_CH; c++) mm.add(20.0 * std::log10(dsp->micMeter(c) + 1e-6)); o->setProperty("mm", mm); }
    }
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
    Array<var> md; for (auto& d : MidiInput::getAvailableDevices()) if (deviceManager.isMidiInputDeviceEnabled(d.identifier)) md.add(d.name);
    o->setProperty("midiDevices", md);
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
    auto* sel = new AudioDeviceSelectorComponent(deviceManager, 0, 0, 2, 16, true, false, false, false);
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
        const int man = (int) m.getProperty("man", 0);
        if (type == "noteOn") d.noteOn((int) m.getProperty("key", 0), man);
        else if (type == "noteOff") d.noteOff((int) m.getProperty("key", 0), man);
        else if (type == "panic") d.panic();
        else if (type == "drawbars") { int v[9] = {0}; if (auto* a = m.getProperty("v", var()).getArray()) for (int i = 0; i < 9 && i < a->size(); i++) v[i] = (int) (*a)[i]; d.setDrawbars(v, man); }
        else if (type == "perc") {
            leslie::Perc p; auto v = m.getProperty("v", var());
            p.on = (bool) v.getProperty("on", true); p.third = (bool) v.getProperty("third", true); p.fast = (bool) v.getProperty("fast", true); p.soft = (bool) v.getProperty("soft", false);
            d.setPerc(p); d.setVibRouting((bool) v.getProperty("vibU", true), (bool) v.getProperty("vibL", false));
        }
        else if (type == "amp") d.setAmp((int) numProp(m, "model", 0), (int) numProp(m, "quality", 1), numProp(m, "sag", 0.5));
        else if (type == "rotor") d.setRotor((int) numProp(m, "r", 0), numProp(m, "slow", 0.75), numProp(m, "fast", 6.8), numProp(m, "up", 0.8), numProp(m, "down", 1.4));
        else if (type == "preamp") d.setPreamp((bool) m.getProperty("on", true), numProp(m, "gain", 0.5));
        else if (type == "mics") {
            leslie::MicSetup ms; auto v = m.getProperty("v", var());
            auto b = [&](const char* k, bool def) { auto x = v.getProperty(k, var()); return x.isVoid() ? def : (bool) x; };
            auto n = [&](const char* k, double def) { return numProp(v, k, def); };
            ms.on = b("on", false);
            ms.hornStereo = b("hornStereo", true); ms.hornAngle = n("hornAngle", 180); ms.hornCenter = n("hornCenter", 0); ms.hornDist = n("hornDist", 0.15); ms.hornHeight = n("hornHeight", 0);
            ms.hornType = (int) n("hornType", 0); ms.hornPattern = (int) n("hornPattern", 0); ms.windscreen = b("windscreen", false);
            ms.drumStereo = b("drumStereo", false); ms.drumAngle = n("drumAngle", 180); ms.drumCenter = n("drumCenter", 0); ms.drumDist = n("drumDist", 0.15);
            ms.drumType = (int) n("drumType", 2); ms.drumPattern = (int) n("drumPattern", 0);
            ms.roomArr = (int) n("roomArr", 0); ms.roomX = n("roomX", 3); ms.roomY = n("roomY", 3); ms.roomZ = n("roomZ", 2); ms.roomYaw = n("roomYaw", 1.5708);
            ms.roomSpacing = n("roomSpacing", 0.6); ms.roomType = (int) n("roomType", 4); ms.roomPattern = (int) n("roomPattern", 1); ms.roomOrder = (int) n("roomOrder", 2);
            if (auto* a = v.getProperty("ch", var()).getArray())
                for (int c = 0; c < leslie::MIC_CH && c < a->size(); c++) {
                    auto& C = ms.ch[c]; auto x = (*a)[c];
                    C.gainDb = numProp(x, "gain", C.gainDb); C.pan = numProp(x, "pan", C.pan); C.delayMs = numProp(x, "delay", 0);
                    C.pol = (bool) x.getProperty("pol", false); C.mute = (bool) x.getProperty("mute", false); C.solo = (bool) x.getProperty("solo", false);
                    C.out = (int) numProp(x, "out", -1);
                }
            d.setMics(ms);
        }
        else if (type == "brake") d.setBrake((bool) m.getProperty("v", true));
        else if (type == "cabDetail") d.setCabinetDetail(numProp(m, "hornfach", 0.45), numProp(m, "wobble", -1));
        else if (type == "organDetail") d.setOrganDetail(numProp(m, "percLevel", 1), numProp(m, "percShort", 0.2), numProp(m, "percLong", 1.0), (bool) m.getProperty("complex", true));
        else if (type == "output") d.setOutput(numProp(m, "masterDb", 0), numProp(m, "lfeHz", 100));
        else if (type == "midiChannels") { midiUpper = (int) numProp(m, "upper", 1); midiLower = (int) numProp(m, "lower", 2); midiPedal = (int) numProp(m, "pedal", 3); leslieCC = (int) numProp(m, "leslieCC", 1); swellCC = (int) numProp(m, "swellCC", 11); }
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
    // Alles aus BinaryData: "/" = index.html, sonst Dateiname (z. B. /fonts/BarlowCondensed-SemiBold.ttf)
    String file = url.upToFirstOccurrenceOf("?", false, false).fromLastOccurrenceOf("/", false, false);
    if (file.isEmpty()) file = "index.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; i++) {
        if (file != BinaryData::originalFilenames[i]) continue;
        int size = 0; const char* p = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        if (p == nullptr) return std::nullopt;
        std::vector<std::byte> data((size_t) size); std::memcpy(data.data(), p, (size_t) size);
        String mime = file.endsWith(".html") ? "text/html" : file.endsWith(".ttf") ? "font/ttf" : file.endsWith(".woff2") ? "font/woff2" : file.endsWith(".js") ? "text/javascript" : "application/octet-stream";
        return WebBrowserComponent::Resource { std::move(data), mime };
    }
    return std::nullopt;
}
