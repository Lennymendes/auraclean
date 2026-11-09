#pragma once
// ==============================================================================
//  DeepCleanDialog.h — AuraClean (NextDSP)
//
//  Modale de confirmation « Mettre à la Corbeille » : arborescence
//  plugin ▸ bundles + fichiers résiduels, cases à cocher individuelles,
//  badge de catégorie, chemin, taille disque, total à libérer.
//
//  Règle de sécurité n°2 appliquée en direct : si l'on décoche un bundle d'un
//  plugin, ses fichiers résiduels sont décochés et verrouillés.
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "UninstallerMac.h"

namespace aura
{
struct CleanCandidate
{
    juce::File   file;
    juce::String badge;
    juce::Colour badgeColour;
    juce::int64  sizeBytes = 0;
    bool         checked   = true;
    bool         defaultChecked = true;  // presets utilisateur : false
    bool         enabled   = true;
    bool         isBundle  = false;
    bool         isAudioUnit   = false;
    bool         requiresAdmin = false;
    juce::Array<juce::File> pruneIfEmpty;
    juce::String vendorKey;
    juce::String detail;
};

struct CleanGroup
{
    juce::String                title, vendor, note;
    std::vector<CleanCandidate> items;  // bundles d'abord, puis résidus
    bool                        leftoversEligible = false;
    bool                        expanded          = true;
};

class DeepCleanDialog : public juce::Component,
                        private juce::ListBoxModel
{
public:
    DeepCleanDialog (std::vector<CleanGroup> groups, juce::String warning, bool leftoversSearched);

    /** Reçoit uniquement les éléments cochés (et actifs). */
    std::function<void (std::vector<CleanCandidate>)> onConfirm;
    std::function<void()>                             onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}

private:
    struct RowRef
    {
        int group;
        int item;  // -1 = en-tête de groupe
    };

    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int row) override;

    void rebuildRows();
    void enforceRules (CleanGroup&);
    void toggleGroup (int group);
    void updateTotals();
    juce::Rectangle<int> cardBounds() const;

    static juce::String displayPath (const juce::File&);

    std::vector<CleanGroup> groups;
    std::vector<RowRef>     rows;
    juce::String            warning;
    bool                    searched;

    juce::ListBox    list;
    juce::TextButton cancelButton, confirmButton, expandButton;

    juce::int64 totalBytes = 0, leftoverBytes = 0;
    int         checkedCount = 0, leftoverCount = 0;
    bool        anyAdmin = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeepCleanDialog)
};

} // namespace aura
