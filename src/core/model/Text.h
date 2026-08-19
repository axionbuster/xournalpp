/*
 * Xournal++
 *
 * A text element
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string
#include <vector>

#include <pango/pango.h>

#include "model/Element.h"
#include "util/Point.h"
#include "util/raii/GObjectSPtr.h"

#include "AudioElement.h"  // for AudioElement
#include "Font.h"          // for XojFont
#include "TextAlignment.h"
#include "TextStyleRuns.h"  // for TextStyleRuns

class Element;
class ObjectInputStream;
class ObjectOutputStream;
class XojPdfRectangle;

class Text: public AudioElement {
public:
    Text();
    ~Text() override;

    static constexpr double NO_WRAP = -1;

public:
    void setFont(const XojFont& font);
    XojFont& getFont();
    const XojFont& getFont() const;
    double getFontSize() const;       // same result as getFont()->getSize(), but const
    std::string getFontName() const;  // same result as getFont()->getName(), but const

    const std::string& getText() const;
    void setText(std::string text);

    void setInEditing(bool inEditing);
    bool isInEditing() const;

    /**
     * @brief The stretches of the text that are styled differently from the element, if any
     *
     * An empty list — the only thing a stock Xournal++ document ever produces — means the whole
     * text is drawn with the element's own font and color.
     */
    const TextStyleRuns& getStyleRuns() const;
    void setStyleRuns(TextStyleRuns runs);

    xoj::util::GObjectSPtr<PangoLayout> createPangoLayout() const;
    void updatePangoFont(PangoLayout* layout) const;
    /// Apply the style runs to a layout holding this element's text
    void updatePangoAttributes(PangoLayout* layout) const;

    void scale(double x0, double y0, double fx, double fy, double rotation, bool restoreLineWidth) override;
    void rotate(double x0, double y0, double th) override;

    void setOrigin(double x, double y) override;
    const xoj::util::Point<double>& getOrigin() const override;

    bool rescaleOnlyAspectRatio() const override;

    /// Set the text's wrapping width. Use Text::NO_WRAP to disable wrapping
    void setWrap(double wrap);

    /// Return Text::NO_WRAP if wrapping is disabled
    inline double getWrap() const { return wrapWidth; }

    void setAlignment(TextAlignment a);
    inline TextAlignment getAlign() const { return align; }

    inline void setJustify(bool j) { this->justify = j; }
    inline bool getJustify() const { return justify; }

    auto cloneText() const -> std::unique_ptr<Text>;
    auto clone() const -> ElementPtr override;

public:
    // Serialize interface
    void serialize(ObjectOutputStream& out) const override;
    void readSerialized(ObjectInputStream& in) override;

    struct Boxes {
        xoj::util::Rectangle<double> snap;
        xoj::util::Rectangle<double> bounds;
    };
    static Boxes computeBoxesForLayout(PangoLayout* layout, xoj::util::Point<double> origin, double wrapWidth);

protected:
    void calcSize() const override;

public:
    std::vector<XojPdfRectangle> findText(const std::string& search) const;

private:
    XojFont font;

    std::string text;

    TextStyleRuns styleRuns;  ///< Empty unless the text carries inline styling

    double wrapWidth = NO_WRAP;  ///< NO_WRAP for no wrap
    TextAlignment align = TextAlignment::LEFT;
    bool justify = false;  ///< Stretch whitespaces to make all complete lines have the same width

    bool inEditing = false;
};
