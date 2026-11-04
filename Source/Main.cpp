#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

class WheelhouseApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Wheelhouse"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override { mainWindow.reset(new MainWindow(getApplicationName())); }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name, juce::Colour(0xff14120f), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
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
