#include "MainComponent.h"
#include "WebUi.h"

using namespace juce;

// UI state (drawbars, room, Rotary, MIDI channels, ...) persisted between two program starts
static File uiStateFile() {
    return File::getSpecialLocation(File::userApplicationDataDirectory)
        .getChildFile("Wheelhouse")
        .getChildFile("ui-state.json");
}

MainComponent::MainComponent() {
    WebUiHooks h;
    h.dspMessage = [this](const var& m) {
        handleDspMessage(m);
    };
    h.audioSettings = [this] {
        openAudioSettings();
    };
    h.audioInfo = [this] {
        sendAudioInfo();
    };
    h.saveUi = [](const String& s) {
        auto f = uiStateFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText(s);
    };
    h.loadUi = [] {
        auto f = uiStateFile();
        return f.existsAsFile() ? f.loadFileAsString() : String();
    };
    h.chantest = [this](int ch, bool lfe) {
        engine.startTestTone(ch, lfe);
    };
    web = std::make_unique<WebBrowserComponent>(wheelhouseWebOptions(h, "WheelhouseWebView"));
    addAndMakeVisible(*web);
    web->goToURL(WebBrowserComponent::getResourceProviderRoot());

    // Audio: 0 inputs, up to 16 outputs; restore the saved device setup from the properties file, otherwise use the
    // default device
    PropertiesFile::Options po;
    po.applicationName = "Wheelhouse";
    po.filenameSuffix = ".settings";
    po.folderName = "Wheelhouse";
    po.osxLibrarySubFolder = "Application Support";
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
    PropertiesFile::Options po;
    po.applicationName = "Wheelhouse";
    po.filenameSuffix = ".settings";
    po.folderName = "Wheelhouse";
    po.osxLibrarySubFolder = "Application Support";
    if (auto xml = deviceManager.createStateXml()) {
        auto f = po.getDefaultFile();
        f.getParentDirectory().createDirectory();
        xml->writeTo(f);
    }
    web = nullptr;
}

void MainComponent::resized() {
    web->setBounds(getLocalBounds());
}

// Newly appearing MIDI inputs are switched on automatically; inputs the user disabled in the dialog stay off.
void MainComponent::enableAllMidiInputs() {
    for (auto& d : MidiInput::getAvailableDevices()) {
        if (knownMidiInputs.contains(d.identifier)) continue;
        knownMidiInputs.add(d.identifier);
        deviceManager.setMidiInputDeviceEnabled(d.identifier, true);
    }
}

// ---------------- Audio ----------------
void MainComponent::audioDeviceAboutToStart(AudioIODevice* device) {
    if (engine.prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples()))
        MessageManager::callAsync([this] {
            if (web) web->emitEventIfBrowserIsVisible("resend", var());
        }); // new core: UI re-sends everything
    for (auto& s : scratch) s.assign((size_t) device->getCurrentBufferSizeSamples() + 16, 0.f);
    MessageManager::callAsync([this] { sendAudioInfo(); });
}
void MainComponent::audioDeviceStopped() {}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData,
                                                     int numOutputChannels, int numSamples,
                                                     const AudioIODeviceCallbackContext&) {
    ScopedNoDenormals noDenormals; // decaying filters and reverb tails must not fall into slow denormal arithmetic
    for (int c = 0; c < numOutputChannels; c++)
        if (outputChannelData[c]) FloatVectorOperations::clear(outputChannelData[c], numSamples);
    const int nCh = jmin(16, numOutputChannels);
    float* ptrs[16];
    for (int c = 0; c < 16; c++) {
        if (scratch[c].size() < (size_t) numSamples) scratch[c].assign((size_t) numSamples, 0.f);
        ptrs[c] = scratch[c].data();
    }
    engine.render(ptrs, numSamples);
    for (int c = 0; c < nCh; c++)
        if (outputChannelData[c]) FloatVectorOperations::copy(outputChannelData[c], ptrs[c], numSamples);
    engine.addTestTone(outputChannelData, numOutputChannels, numSamples);
    // Output level meters: accumulate energy, publish RMS per channel every 1024 samples
    for (int c = 0; c < nCh; c++) {
        float s = 0;
        const float* x = outputChannelData[c] ? outputChannelData[c] : ptrs[c];
        for (int i = 0; i < numSamples; i++) s += x[i] * x[i];
        meterAcc[(size_t) c] += s;
    }
    meterCount += numSamples;
    if (meterCount >= 1024) {
        for (int c = 0; c < 16; c++) {
            meterRms[(size_t) c] = std::sqrt(meterAcc[(size_t) c] / (float) meterCount);
            meterAcc[(size_t) c] = 0;
        }
        meterCount = 0;
    }
}

void MainComponent::handleIncomingMidiMessage(MidiInput*, const MidiMessage& m) {
    var ev = engine.midiFromDevice(m); // never touches the DSP lock
    if (!ev.isVoid())
        MessageManager::callAsync([this, ev] {
            if (web) web->emitEventIfBrowserIsVisible("midi", ev);
        });
}

void MainComponent::changeListenerCallback(ChangeBroadcaster*) {
    enableAllMidiInputs();
    sendAudioInfo();
}

// 30 Hz UI update: rotor state, meters, CPU, amp voltages, latency, dropped blocks, mic meters
void MainComponent::timerCallback() {
    if (!web) return;
    var ev = engine.makeRotEvent(deviceManager.getCpuUsage());
    Array<var> m;
    for (int c = 0; c < 16; c++) m.add(20.0 * std::log10(meterRms[(size_t) c].load() + 1e-6));
    ev.getDynamicObject()->setProperty("meters", m);
    web->emitEventIfBrowserIsVisible("rot", ev);
}

void MainComponent::handleDspMessage(const var& m) {
    if (engine.handleMessage(m) && web) web->emitEventIfBrowserIsVisible("roomInfo", engine.makeRoomInfoEvent());
}

// "audio" event: device name/type, sample rate, buffer size, outputs and their names, latency, enabled MIDI inputs
void MainComponent::sendAudioInfo() {
    if (!web) return;
    auto* o = new DynamicObject();
    auto* dev = deviceManager.getCurrentAudioDevice();
    o->setProperty("device", dev ? dev->getName() : "no device");
    o->setProperty("type", dev ? dev->getTypeName() : "");
    o->setProperty("sampleRate", dev ? dev->getCurrentSampleRate() : 0.0);
    o->setProperty("bufferSize", dev ? dev->getCurrentBufferSizeSamples() : 0);
    o->setProperty("outputs", dev ? dev->getActiveOutputChannels().countNumberOfSetBits() : 0);
    // Output latency as reported by the driver. ASIO drivers (and most others) already include the buffer in this
    // value, so it must not be added to the buffer length again; it can never be shorter than one buffer, though.
    o->setProperty("outLatencySamples",
                   dev ? jmax(dev->getOutputLatencyInSamples(), dev->getCurrentBufferSizeSamples()) : 0);
    o->setProperty("driverLatencySamples", dev ? dev->getOutputLatencyInSamples() : 0);
    StringArray names;
    if (dev) {
        auto chans = dev->getOutputChannelNames();
        auto active = dev->getActiveOutputChannels();
        for (int i = 0; i < chans.size(); i++)
            if (active[i]) names.add(chans[i]);
    }
    Array<var> n;
    for (auto& s : names) n.add(s);
    o->setProperty("channelNames", n);
    Array<var> md;
    for (auto& d : MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled(d.identifier)) md.add(d.name);
    o->setProperty("midiDevices", md);
    web->emitEventIfBrowserIsVisible("audio", var(o));
}

void MainComponent::openAudioSettings() {
    if (settingsWindow != nullptr) {
        settingsWindow->toFront(true);
        return;
    }
    auto* sel = new AudioDeviceSelectorComponent(deviceManager, 0, 0, 2, 16, true, false, false, false);
    sel->setSize(520, 560);
    DialogWindow::LaunchOptions lo;
    lo.content.setOwned(sel);
    lo.dialogTitle = "Wheelhouse – Audio & MIDI Settings";
    lo.dialogBackgroundColour = Colour(0xff1d1a16);
    lo.escapeKeyTriggersCloseButton = true;
    lo.useNativeTitleBar = true;
    lo.resizable = true;
    settingsWindow = lo.launchAsync();
}
