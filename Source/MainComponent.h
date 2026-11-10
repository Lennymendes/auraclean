#pragma once
// ==============================================================================
//  MainComponent.h — AuraClean (NextDSP)
//
//    ┌──────────────────────────── Header : logo · recherche · filtres ─────────┐
//    │ Marques        │ Sélection rapide : VST3 · AU · AAX · Marque · Tout · Aucun│
//    │ (VendorSidebar)│──────────────────────────────────────────────────────────│
//    │  nom, compteur │ PluginTable (TableListBox) : ☐ · Plugin · Marque ·        │
//    │  espace disque │   Formats [VST3][AU][AAX] · Arch · Version · Taille · Date│
//    ├────────────────┴──────────────────────────────────────────────────────────┤
//    │ "14 plugins sélectionnés — 1,42 Go à libérer"  [Réactiver][Désactiver][⌫]│
//    └──────────────────────────────────────────────────────────────────────────┘
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <thread>
#include <vector>

#include "DeepCleanDialog.h"
#include "LeftoverScanner.h"
#include "PluginScanner.h"
#include "UninstallerMac.h"
#include "UpdateChecker.h"

namespace aura
{
inline juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }
juce::String formatBytes (juce::int64 bytes);

//==============================================================================
enum class ArchFilter   { All = 1, AppleSilicon, IntelOnly, Universal };
enum class StatusFilter { All = 1, Active, Disabled };
enum class CheckState   { None, Partial, All };

enum ColumnId
{
    colCheck = 1,
    colName,
    colVendor,
    colFormats,
    colArch,
    colVersion,
    colSize,
    colDate
};

struct Filters
{
    juce::String                   search;
    juce::String                   vendorKey;  // vide = toutes les marques
    std::array<bool, kNumFormats>  formats { true, true, true };
    ArchFilter                     arch   = ArchFilter::All;
    StatusFilter                   status = StatusFilter::All;
};

struct SelectionSummary
{
    int         plugins = 0, bundles = 0, hidden = 0, enabledBundles = 0, disabledBundles = 0;
    juce::int64 bytes = 0;
};

//==============================================================================
/** Liste (ou table) qui propose d'abord les raccourcis globaux à l'application :
    sans cela, ListBox consomme ⌘A et Suppr avant qu'ils n'atteignent MainComponent. */
template <typename ListType>
class ShortcutForwarding : public ListType
{
public:
    using ListType::ListType;

    std::function<bool (const juce::KeyPress&)> onShortcut;

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (onShortcut != nullptr && onShortcut (key))
            return true;

        return ListType::keyPressed (key);
    }
};

//==============================================================================
/** Modèle : résultat du scan + filtres + sélection (par bundle). Message thread uniquement. */
class Library
{
public:
    void setScanResult (ScanResult newResult);
    const ScanResult& data() const noexcept { return result; }

    Filters filters;

    void rebuild();  // applique filtres + tri
    void setSort (int columnId, bool forwards);

    const std::vector<int>& visibleEntries() const noexcept { return visibleRows; }
    bool isEntryVisible (int entryIndex) const noexcept;
    bool bundlePasses (int bundleIndex) const noexcept;  // format / arch / état

    bool isSelected (int bundleIndex) const noexcept;
    void setSelected (int bundleIndex, bool shouldBeSelected) noexcept;

    CheckState entryState (int entryIndex) const noexcept;
    void       toggleEntry (int entryIndex);
    CheckState formatState (int entryIndex, PluginFormat) const noexcept;
    void       toggleFormat (int entryIndex, PluginFormat);

    void selectFormatInVisible (PluginFormat);
    void selectVendor (const juce::String& vendorKey, bool shouldSelect);
    void selectAllVisible (bool shouldSelect);
    void clearSelection();

    SelectionSummary summary() const;
    std::vector<int> selectedBundles() const;

    /** Presets / données détectés par entrée (estimation en arrière-plan) ; -1 = pas encore connu. */
    void        setLeftoverEstimates (std::vector<juce::int64> bytesPerEntry);
    juce::int64 leftoverEstimate (int entryIndex) const noexcept;

private:
    bool computeEntryVisible (int entryIndex) const;
    void sortRows();

    ScanResult           result;
    std::vector<uint8_t> selection;      // 1 octet par bundle
    std::vector<uint8_t> entryVisible;   // 1 octet par entrée
    std::vector<int>     visibleRows;    // indices d'entrées, triés
    juce::StringArray    searchTokens;
    std::vector<juce::int64> leftoverBytes;
    int                  sortColumn  = colVendor;
    bool                 sortForward = true;
};

//==============================================================================
class VendorSidebar : public juce::Component,
                      private juce::ListBoxModel
{
public:
    explicit VendorSidebar (Library&);

    void refresh();
    void setStats (const juce::String& text);

    std::function<void()> onFilterChanged;
    std::function<void()> onSelectionChanged;
    std::function<bool (const juce::KeyPress&)> onShortcut;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    juce::String keyForRow (int row) const;

    Library&                          library;
    ShortcutForwarding<juce::ListBox> list;
    juce::String stats;
    bool         ignoreSelectionCallback = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VendorSidebar)
};

//==============================================================================
class PluginTable : public juce::Component,
                    private juce::TableListBoxModel,
                    private juce::Timer
{
public:
    explicit PluginTable (Library&);

    void refresh();

    /** Apparition progressive des lignes (fondu décalé de haut en bas). */
    void startReveal();

    std::function<void()>                       onSelectionChanged;
    std::function<void (int entryIndex)>        onUninstallEntry;
    std::function<bool (const juce::KeyPress&)> onShortcut;

    void deselectAllRows() { table.deselectAllRows(); }

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

private:
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool rowIsSelected) override;
    void cellClicked (int row, int columnId, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int columnId, const juce::MouseEvent&) override;
    void sortOrderChanged (int newSortColumnId, bool isForwards) override;
    void returnKeyPressed (int lastRowSelected) override;
    juce::String getCellTooltip (int row, int columnId) override;

    void timerCallback() override;

    int  entryForRow (int row) const noexcept;
    void showContextMenu (int entryIndex);
    void notifyChanged();
    float rowRevealAlpha (int row) const noexcept;

    juce::String formatTooltip (int entryIndex, PluginFormat) const;
    juce::String sizeTooltip (int entryIndex) const;

    Library&                               library;
    ShortcutForwarding<juce::TableListBox> table;
    double                                 revealStartMs = 0.0;
    bool                                   revealing     = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginTable)
};

//==============================================================================
class BusyOverlay : public juce::Component,
                    private juce::Timer
{
public:
    BusyOverlay();

    void show (const juce::String& message, const std::atomic<float>* progress);
    void hide();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override {}

private:
    void timerCallback() override;

    juce::String             message;
    const std::atomic<float>* progress = nullptr;
    float                    phase    = 0.0f;
};

//==============================================================================
/** Animation d'introduction (~600 ms, 60 FPS) au lancement et à chaque « Rescanner » :
    titre « Aura Clean » + barre de chargement fine, puis fondu de sortie qui révèle la liste. */
class IntroOverlay : public juce::Component,
                     private juce::Timer
{
public:
    IntroOverlay();

    void start (const std::atomic<float>* scanProgress);
    void finish();  // les données sont prêtes : termine la barre puis s'efface
    bool isActive() const noexcept { return isVisible(); }

    std::function<void()> onRevealStart;  // début du fondu de sortie
    std::function<void()> onHidden;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override {}

private:
    void timerCallback() override;

    const std::atomic<float>* progress = nullptr;
    double startMs = 0.0, lastTickMs = 0.0, fadeStartMs = 0.0;
    float  shown   = 0.0f;   // valeur affichée (lissée) de la barre
    float  opacity = 1.0f;
    bool   finished = false, fading = false;
};

//==============================================================================
/** Bannière « nouvelle version » en haut de la fenêtre (glisse depuis le haut en 220 ms).
    Couleurs unies uniquement (cf. Theme::fillPathWithGradient). */
class UpdateBanner : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    static constexpr int fullHeight = 58;

    UpdateBanner();

    void show (const UpdateInfo&);
    void hide();
    int  getCurrentHeight() const noexcept;
    bool isShowingOrAnimating() const noexcept { return target > 0.0f || reveal > 0.0f; }

    std::function<void()>                         onLayoutChanged;
    std::function<void (const juce::URL&)>        onDownload;
    std::function<void (const juce::String& ver)> onSkipVersion;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    UpdateInfo       info;
    float            reveal = 0.0f, target = 0.0f, startValue = 0.0f;
    double           animationStartMs = 0.0;
    juce::TextButton downloadButton, laterButton, skipButton;
};

//==============================================================================
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Refuse la fermeture pendant une opération sur disque. */
    bool canQuit();

    /** Vérification de mise à jour. userInitiated = depuis le menu : affiche aussi « à jour » / erreurs. */
    void checkForUpdates (bool userInitiated);
    void showAbout();

    static juce::String applicationVersion();

private:
    enum class Operation { Uninstall, Disable, Enable };

    void startScan (bool withIntro = true);
    void scanFinished (ScanResult result);
    void introHidden();
    bool handleShortcut (const juce::KeyPress&);
    void uninstallEntry (int entryIndex);

    // Estimation des presets / données par plugin (colonne Taille), hors ~/Documents
    void startLeftoverEstimate();
    void stopLeftoverEstimate();
    void filtersChanged();
    void selectionChanged();
    void updateButtons();

    void requestOperation (Operation);
    void runOperation (Operation, std::vector<int> bundleIndices);
    void operationFinished (Operation, const OperationReport&, juce::int64 bytesOverride = -1);

    // Deep Clean (désinstallation complète)
    void startDeepClean (std::vector<int> bundleIndices, int hiddenCount);
    void showDeepClean (std::vector<CleanGroup> groups, const juce::String& warning, bool leftoversSearched);
    void closeDeepClean();
    void runTrash (std::vector<CleanCandidate> items);

    void joinWorker();
    void setupChip (juce::TextButton&, const juce::String& text, juce::Colour accent);
    void setupButton (juce::TextButton&, const juce::String& text, const char* style = "");

    Library library;  // doit précéder les vues qui le référencent

    // Header
    juce::TextEditor                    searchBox;
    std::array<juce::TextButton, kNumFormats> formatChips;
    juce::ComboBox                      archBox, statusBox;
    juce::TextButton                    rescanButton;

    // Sélection rapide
    juce::TextButton selectVst3, selectAu, selectAax, selectVendor, selectVisible, selectNone;

    VendorSidebar sidebar { library };
    PluginTable   table { library };

    // Barre inférieure
    juce::TextButton enableButton, disableButton, uninstallButton;
    SelectionSummary summary;

    UpdateChecker                    updateChecker { applicationVersion() };
    UpdateBanner                     updateBanner;
    UpdateInfo                       pendingUpdate;
    bool                             hasPendingUpdate = false;

    void handleUpdateResult (const UpdateInfo&, bool userInitiated);

    BusyOverlay                      overlay;
    IntroOverlay                     intro;
    std::unique_ptr<DeepCleanDialog> deepCleanDialog;
    std::unique_ptr<DeepCleanDialog> closingDialog;  // en cours de fondu de sortie
    juce::TooltipWindow tooltips { this, 450 };

    // Travail en arrière-plan (un seul job à la fois)
    std::thread        worker;
    std::atomic<bool>  cancelScan { false };
    std::atomic<float> progress { 0.0f };
    bool               busy             = false;
    bool               operationRunning = false;

    std::thread        estimateWorker;
    std::atomic<bool>  cancelEstimate { false };
    int                scanGeneration = 0;

    juce::Rectangle<int> headerArea, bulkArea, bottomArea, summaryArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace aura
