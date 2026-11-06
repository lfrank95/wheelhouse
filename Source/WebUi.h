#pragma once
// Wheelhouse - the WebView that shows ui/wheelhouse.html, shared by the standalone window and the plugin editor.
// Native functions callable from JavaScript (window.__JUCE__ bridge):
//   dsp(msg)            one UI message (JSON object with "type"), see WheelhouseEngine::handleMessage
//   audioSettings()     opens the audio/MIDI device dialog (standalone only)
//   audioInfo()         asks for device/host info; answered with the "audio" event
//   saveUi(json) / loadUi()   the UI's own state (file in the standalone, project in the plugin)
//   chantest(ch, lfe)   0.5 s test tone on one output
//   params()            plugin: current values of the automatable parameters ({id: value}), null otherwise
//   pong()              answer to the host's "ping" event (watchdog, see WebUiView)
#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdint>
#include <functional>
#include "BinaryData.h"
#include "WebWatchdog.h"

struct WebUiHooks {
    std::function<void(const juce::var&)> dspMessage;
    std::function<void()> audioSettings, audioInfo;
    std::function<void(const juce::String&)> saveUi;
    std::function<juce::String()> loadUi;
    std::function<void(int, bool)> chantest;
    std::function<juce::var()> params;
    std::function<void()> pong;
};

// Everything is served from BinaryData: "/" = index.html, otherwise the file name (e.g.
// /fonts/BarlowCondensed-SemiBold.ttf)
inline std::optional<juce::WebBrowserComponent::Resource> wheelhouseResource(const juce::String& url) {
    using namespace juce;
    String file = url.upToFirstOccurrenceOf("?", false, false).fromLastOccurrenceOf("/", false, false);
    if (file.isEmpty()) file = "index.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; i++) {
        if (file != BinaryData::originalFilenames[i]) continue;
        int size = 0;
        const char* p = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        if (p == nullptr) return std::nullopt;
        std::vector<std::byte> data((size_t) size);
        std::memcpy(data.data(), p, (size_t) size);
        String mime = file.endsWith(".html")    ? "text/html"
                      : file.endsWith(".ttf")   ? "font/ttf"
                      : file.endsWith(".woff2") ? "font/woff2"
                      : file.endsWith(".js")    ? "text/javascript"
                                                : "application/octet-stream";
        return WebBrowserComponent::Resource{std::move(data), mime};
    }
    return std::nullopt;
}

inline juce::WebBrowserComponent::Options wheelhouseWebOptions(WebUiHooks h, const juce::String& dataFolder) {
    using namespace juce;
    using Done = WebBrowserComponent::NativeFunctionCompletion;
    return WebBrowserComponent::Options{}
        .withBackend(WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder(
            File::getSpecialLocation(File::SpecialLocationType::tempDirectory).getChildFile(dataFolder)))
        .withNativeIntegrationEnabled()
        .withResourceProvider([](const auto& url) { return wheelhouseResource(url); })
        .withNativeFunction("dsp",
                            [h](const Array<var>& a, Done done) {
                                if (a.size() > 0 && h.dspMessage) h.dspMessage(a[0]);
                                done(var());
                            })
        .withNativeFunction("audioSettings",
                            [h](const Array<var>&, Done done) {
                                if (h.audioSettings) h.audioSettings();
                                done(var());
                            })
        .withNativeFunction("audioInfo",
                            [h](const Array<var>&, Done done) {
                                if (h.audioInfo) h.audioInfo();
                                done(var());
                            })
        .withNativeFunction("saveUi",
                            [h](const Array<var>& a, Done done) {
                                if (a.size() > 0 && h.saveUi) h.saveUi(a[0].toString());
                                done(var());
                            })
        .withNativeFunction("loadUi", [h](const Array<var>&, Done done) { done(h.loadUi ? var(h.loadUi()) : var("")); })
        .withNativeFunction("chantest",
                            [h](const Array<var>& a, Done done) {
                                if (a.size() > 0 && h.chantest) h.chantest((int) a[0], a.size() > 1 && (bool) a[1]);
                                done(var());
                            })
        .withNativeFunction("params", [h](const Array<var>&, Done done) { done(h.params ? h.params() : var()); })
        .withNativeFunction("pong", [h](const Array<var>&, Done done) {
            if (h.pong) h.pong();
            done(var());
        });
}

/** The WebView with a watchdog. WebView2 can lose the process that draws the page (GPU or driver reset, a crash,
    memory pressure in the host); the page then stays white and JUCE does not notice - closing and reopening the
    window helped because that builds a new WebView. Here the host sends a "ping" event once a second and the page
    answers with pong(). If no answer comes in time while the view is on screen (see WebWatchdog), the WebView is
    rebuilt; the page then reloads its state from the engine exactly as after reopening the window. */
class WebUiView : public juce::Component, private juce::Timer {
public:
    WebUiView(WebUiHooks hooks, juce::String userDataFolder) : folder(std::move(userDataFolder)) {
        hooks.pong = [this] {
            watch.pong(juce::Time::getMillisecondCounter());
        };
        h = std::move(hooks);
        build();
        startTimer(1000);
    }
    ~WebUiView() override { stopTimer(); }

    void emitEventIfBrowserIsVisible(const juce::Identifier& name, const juce::var& payload) {
        if (web) web->emitEventIfBrowserIsVisible(name, payload);
    }
    void resized() override {
        if (web) web->setBounds(getLocalBounds());
    }
    int restartCount() const { return restarts; }

private:
    void build() {
        web = nullptr;
        web = std::make_unique<juce::WebBrowserComponent>(wheelhouseWebOptions(h, folder));
        addAndMakeVisible(*web);
        web->setBounds(getLocalBounds());
        web->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
        watch.reset(juce::Time::getMillisecondCounter());
    }
    void timerCallback() override {
        const bool showing = isShowing();
        if (showing) web->emitEventIfBrowserIsVisible("ping", juce::var());
        if (watch.tick(juce::Time::getMillisecondCounter(), showing)) {
            restarts++;
            build();
            watch.rebuilt(juce::Time::getMillisecondCounter());
        }
    }

    WebUiHooks h;
    juce::String folder;
    std::unique_ptr<juce::WebBrowserComponent> web;
    WebWatchdog watch;
    int restarts = 0;
};
