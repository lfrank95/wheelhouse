#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "StartupLog.h"

class WheelhouseApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Wheelhouse"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override {
        wh::beginStartupLog(getApplicationVersion()); // runs before initialise(): first thing in the process
        wh::logStartup("checking for another instance");
        return false;
    }
    void anotherInstanceStarted(const juce::String&) override { wh::logStartup("another instance was started"); }

    void initialise(const juce::String&) override {
        wh::logStartup("initialising");
        mainWindow.reset(new MainWindow(getApplicationName()));
        wh::logStartup("window visible");
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name, juce::Colour(0xff14120f), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            wh::logStartup("creating main component");
            setContentOwned(new MainComponent(), true);
            wh::logStartup("main component ready");
            setResizable(true, true);
            setResizeLimits(960, 540, 10000, 10000);
            centreWithSize(1600, 900);
            setVisible(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(WheelhouseApplication)
