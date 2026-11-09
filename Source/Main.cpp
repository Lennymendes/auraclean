// ==============================================================================
//  Main.cpp — AuraClean (NextDSP) — application Standalone
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include "AuraLookAndFeel.h"
#include "MainComponent.h"
#include "UninstallerMac.h"

class AuraCleanApplication : public juce::JUCEApplication
{
public:
    AuraCleanApplication() = default;

    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        aura::UninstallerMac::applyDarkAppearance();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr && ! mainWindow->canQuit())
            return;

        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, aura::Theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new aura::MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1180, 640, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        bool canQuit()
        {
            if (auto* content = dynamic_cast<aura::MainComponent*> (getContentComponent()))
                return content->canQuit();

            return true;
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    aura::AuraLookAndFeel       lookAndFeel;  // déclaré avant la fenêtre : détruit après elle
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (AuraCleanApplication)
