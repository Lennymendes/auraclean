// ==============================================================================
//  Main.cpp — AuraClean (NextDSP) — application Standalone
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include "AuraLookAndFeel.h"
#include "MainComponent.h"
#include "UninstallerMac.h"

//==============================================================================
/** Menus macOS : « AuraClean » (À propos, Rechercher des mises à jour…) et « Aide ». */
class AppMenuModel : public juce::MenuBarModel
{
public:
    enum CommandIds
    {
        aboutId = 1,
        checkUpdatesId,
        websiteId
    };

    explicit AppMenuModel (std::function<aura::MainComponent*()> mainComponentGetter)
        : getMain (std::move (mainComponentGetter)) {}

    /** Éléments ajoutés au menu de l'application (sous le nom AuraClean). */
    static juce::PopupMenu createAppleMenuItems()
    {
        juce::PopupMenu m;
        m.addItem (aboutId, juce::String::fromUTF8 ("À propos d'AuraClean…"));
        m.addItem (checkUpdatesId, juce::String::fromUTF8 ("Rechercher des mises à jour…"));
        m.addSeparator();
        return m;
    }

    juce::StringArray getMenuBarNames() override { return { "Aide" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String&) override
    {
        juce::PopupMenu m;
        m.addItem (checkUpdatesId, juce::String::fromUTF8 ("Rechercher des mises à jour…"));
        m.addSeparator();
        m.addItem (websiteId, juce::String::fromUTF8 ("Site web NextDSP"));
        return m;
    }

    void menuItemSelected (int menuItemID, int /*topLevelMenuIndex : -1 pour le menu de l'app*/) override
    {
        auto* main = getMain ? getMain() : nullptr;

        switch (menuItemID)
        {
            case aboutId:        if (main != nullptr) main->showAbout(); break;
            case checkUpdatesId: if (main != nullptr) main->checkForUpdates (true); break;
            case websiteId:      juce::URL ("https://nextdsp.com").launchInDefaultBrowser(); break;
            default: break;
        }
    }

private:
    std::function<aura::MainComponent*()> getMain;
};

//==============================================================================
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

       #if JUCE_MAC
        const auto appleItems = AppMenuModel::createAppleMenuItems();
        juce::MenuBarModel::setMacMainMenu (&menuModel, &appleItems);
       #endif
    }

    void shutdown() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);  // avant la destruction du modèle
       #endif

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

        aura::MainComponent* getMainComponent()
        {
            return dynamic_cast<aura::MainComponent*> (getContentComponent());
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
    AppMenuModel                menuModel { [this] { return mainWindow != nullptr ? mainWindow->getMainComponent() : nullptr; } };
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (AuraCleanApplication)
