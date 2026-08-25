/*
 * Xournal++
 *
 * Namespace for view related classes
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>

#include <gtk/gtk.h>

class Element;

namespace xoj {
namespace view {

enum NonAudioTreatment : bool { FADE_OUT_NON_AUDIO_ = true, NORMAL_NON_AUDIO = false };
enum EditionTreatment : bool { SHOW_CURRENT_EDITING = true, HIDE_CURRENT_EDITING = false };
enum ColorTreatment : bool { COLORBLIND = true, NORMAL_COLOR = false };
/**
 * What to do with editor-only elements (Element::isEditorOnly()): the editing canvas shows
 * them faded, while everything meant for an audience -- recordings, the projector, exports,
 * prints, the file preview -- leaves them out entirely.
 */
enum EditorOnlyTreatment : bool { HIDE_EDITOR_ONLY = true, SHOW_EDITOR_ONLY = false };

class Context {
public:
    cairo_t* cr;
    NonAudioTreatment fadeOutNonAudio;
    EditionTreatment showCurrentEdition;
    ColorTreatment noColor;
    EditorOnlyTreatment hideEditorOnly;

    static Context createDefault(cairo_t* cr) {
        return {cr, NORMAL_NON_AUDIO, HIDE_CURRENT_EDITING, NORMAL_COLOR, SHOW_EDITOR_ONLY};
    }
    static Context createColorBlind(cairo_t* cr) {
        return {cr, NORMAL_NON_AUDIO, HIDE_CURRENT_EDITING, COLORBLIND, SHOW_EDITOR_ONLY};
    }
};

class ElementView {
public:
    virtual ~ElementView() = default;
    virtual void draw(const Context& ctx) const = 0;
    static std::unique_ptr<ElementView> createFromElement(const Element* e);
};

/**
 * Draw one element through its ElementView, applying the context's editor-only treatment:
 * skipped under HIDE_EDITOR_ONLY, faded to OPACITY_EDITOR_ONLY under SHOW_EDITOR_ONLY.
 * Every place that draws elements out of the document model goes through here.
 */
void drawElement(const Element* e, const Context& ctx);

class TexImageView;
class ImageView;
class StrokeView;
class TextView;
class LinkView;

constexpr double OPACITY_NO_AUDIO = 0.3;
constexpr double OPACITY_EDITOR_ONLY = 0.4;
};  // namespace view
};  // namespace xoj
