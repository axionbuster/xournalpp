#include "Text.h"

#include <memory>
#include <utility>  // for move

#include <glib.h>  // for g_warning
#include <pango/pangocairo.h>

#include "model/AudioElement.h"   // for AudioElement
#include "model/Element.h"        // for ELEMENT_TEXT, Eleme...
#include "model/Font.h"           // for XojFont
#include "pdf/base/XojPdfPage.h"  // for XojPdfRectangle
#include "util/Rectangle.h"       // for Rectangle
#include "util/Stacktrace.h"      // for Stacktrace
#include "util/StringUtils.h"
#include "util/raii/GObjectSPtr.h"
#include "util/raii/PangoSPtr.h"  // for PangoAttrListSPtr
#include "util/safe_casts.h"                      // for round_cast
#include "util/serializing/ObjectInputStream.h"   // for ObjectInputStream
#include "util/serializing/ObjectOutputStream.h"  // for ObjectOutputStream

using xoj::util::Rectangle;

Text::Text(): AudioElement(ELEMENT_TEXT) {
    this->font.setName("Sans");
    this->font.setSize(12);
}

Text::~Text() = default;

auto Text::cloneText() const -> std::unique_ptr<Text> {
    auto text = std::make_unique<Text>();
    text->font = this->font;
    text->text = this->text;
    text->styleRuns = this->styleRuns;
    text->setColor(this->getColor());
    text->setEditorOnly(this->isEditorOnly());
    text->boundingBox = this->boundingBox;
    text->cloneAudioData(this);
    text->snappedBounds = this->snappedBounds;
    text->sizeCalculated = this->sizeCalculated;
    text->inEditing = this->inEditing;
    text->wrapWidth = this->wrapWidth;
    text->align = this->align;
    text->justify = this->justify;

    return text;
}

auto Text::clone() const -> ElementPtr { return cloneText(); }

auto Text::getFont() -> XojFont& { return font; }
auto Text::getFont() const -> const XojFont& { return font; }

void Text::setFont(const XojFont& font) {
    this->font = font;
    sizeCalculated = false;
}

auto Text::getFontSize() const -> double { return font.getSize(); }

auto Text::getFontName() const -> std::string { return font.getName(); }

auto Text::getText() const -> const std::string& { return this->text; }

void Text::setText(std::string text) {
    this->text = std::move(text);
    // Whatever the runs described, they now describe a different string; keep them inside it
    xoj::text::alignStyleRunsToText(this->styleRuns, this->text);
    sizeCalculated = false;
}

auto Text::getStyleRuns() const -> const TextStyleRuns& { return this->styleRuns; }

void Text::setStyleRuns(TextStyleRuns runs) {
    this->styleRuns = std::move(runs);
    xoj::text::normalizeStyleRuns(this->styleRuns);
    /*
     * The runs are the element's own, so they must fit the element's own text — both setters
     * enforce it, and the invariant then holds whichever of the two is called last.
     */
    xoj::text::alignStyleRunsToText(this->styleRuns, this->text);
    sizeCalculated = false;  // Bold and italic runs change the text's extents
}

void Text::setWrap(double wrap) {
    this->wrapWidth = wrap;
    sizeCalculated = false;
}

void Text::setAlignment(TextAlignment a) {
    this->align = a;
    sizeCalculated = false;
}

Text::Boxes Text::computeBoxesForLayout(PangoLayout* layout, xoj::util::Point<double> origin, double wrapWidth) {
    PangoRectangle box;
    pango_layout_get_extents(layout, nullptr, &box);

    xoj::util::Point<double> offset{static_cast<double>(box.x) / PANGO_SCALE, static_cast<double>(box.y) / PANGO_SCALE};

    Boxes res;

    res.bounds.width = static_cast<double>(box.width) / PANGO_SCALE;
    res.bounds.height = static_cast<double>(box.height) / PANGO_SCALE;
    res.bounds.x = origin.x + offset.x;
    res.bounds.y = origin.y + offset.y;

    res.snap.x = origin.x;
    res.snap.y = origin.y;

    if (wrapWidth != NO_WRAP) {
        res.snap.width = wrapWidth;
    } else {
        res.snap.width = res.bounds.width + offset.x;
    }
    res.snap.height = res.bounds.height + offset.y;

    return res;
}

void Text::setOrigin(double x, double y) {
    this->snappedBounds.x = x;
    this->snappedBounds.y = y;
    this->sizeCalculated = false;  // Recompute Element::x,y
}

auto Text::getOrigin() const -> const xoj::util::Point<double>& { return this->snappedBounds.getOrigin(); }

void Text::calcSize() const {
    auto layout = createPangoLayout();
    pango_layout_set_text(layout.get(), this->text.c_str(), static_cast<int>(this->text.length()));

    auto boxes = computeBoxesForLayout(layout.get(), this->getOrigin(), this->wrapWidth);

    this->boundingBox = boxes.bounds;
    this->snappedBounds = boxes.snap;
}

void Text::setInEditing(bool inEditing) { this->inEditing = inEditing; }

auto Text::createPangoLayout() const -> xoj::util::GObjectSPtr<PangoLayout> {
    xoj::util::GObjectSPtr<PangoContext> c(pango_font_map_create_context(pango_cairo_font_map_get_default()),
                                           xoj::util::adopt);
    pango_context_set_round_glyph_positions(c.get(), false);  // Avoid weird glyph positioning on small fonts
    xoj::util::GObjectSPtr<PangoLayout> layout(pango_layout_new(c.get()), xoj::util::adopt);

    pango_layout_set_width(layout.get(),
                           this->wrapWidth == NO_WRAP ? -1 : round_cast<int>(this->wrapWidth * PANGO_SCALE));

    pango_layout_set_justify(layout.get(), this->justify);
    pango_layout_set_alignment(layout.get(), this->align.toPango());

#if PANGO_VERSION_CHECK(1, 48, 5)  // see https://gitlab.gnome.org/GNOME/pango/-/issues/499
    pango_layout_set_line_spacing(layout.get(), 1.0);
#endif

    updatePangoFont(layout.get());
    updatePangoAttributes(layout.get());

    return layout;
}

void Text::updatePangoFont(PangoLayout* layout) const {
    PangoFontDescription* desc = pango_font_description_from_string(this->getFontName().c_str());
    pango_font_description_set_absolute_size(desc, this->getFontSize() * PANGO_SCALE);

    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
}

void Text::updatePangoAttributes(PangoLayout* layout) const {
    if (this->styleRuns.empty()) {
        // An unstyled element leaves the layout exactly as stock Xournal++ leaves it
        pango_layout_set_attributes(layout, nullptr);
        return;
    }

    /*
     * Every consumer of a text element goes through createPangoLayout: the canvas, the PDF and
     * image exports, calcSize and findText. So attaching the runs here is all it takes for the
     * styling to show up everywhere, and for the extents to account for it.
     */
    xoj::util::PangoAttrListSPtr attrs(pango_attr_list_new(), xoj::util::adopt);
    xoj::text::appendStyleRunAttributes(attrs.get(), this->styleRuns);
    pango_layout_set_attributes(layout, attrs.get());
}

void Text::scale(double x0, double y0, double fx, double fy, double rotation,
                 bool) {  // line width scaling option is not used
    // only proportional scale allowed...
    if (fx != fy) {
        g_warning("rescale font with fx != fy not supported: %lf / %lf", fx, fy);
        Stacktrace::printStacktrace();
    }

    this->boundingBox.x -= x0;
    this->boundingBox.x *= fx;
    this->boundingBox.x += x0;
    this->boundingBox.y -= y0;
    this->boundingBox.y *= fy;
    this->boundingBox.y += y0;

    double size = this->font.getSize() * fx;
    this->font.setSize(size);

    if (this->wrapWidth != NO_WRAP) {
        this->wrapWidth *= fx;
    }

    sizeCalculated = false;
}

void Text::rotate(double x0, double y0, double th) {}

auto Text::isInEditing() const -> bool { return this->inEditing; }

auto Text::rescaleOnlyAspectRatio() const -> bool { return true; }

void Text::serialize(ObjectOutputStream& out) const {
    out.writeObject("Text");

    this->AudioElement::serialize(out);

    out.writeString(this->text);

    font.serialize(out);

    out.writeDouble(this->wrapWidth);
    out.writeInt(static_cast<int>(this->align));
    out.writeInt(this->justify);

    /*
     * Optional inline style runs, appended after every field a stock Xournal++ build writes. An
     * unstyled element writes nothing at all here, so its blob is byte-identical to a stock one
     * and still pastes into a stock build running alongside this fork. A styled element is a
     * fork-only construct, and pasting one into a stock build does not work.
     */
    if (!this->styleRuns.empty()) {
        // Bold and italic are tri-state: -1 leaves the element's font in charge, 0 and 1 override it
        auto encode = [](const std::optional<bool>& flag) { return flag ? (*flag ? 1 : 0) : -1; };

        out.writeSizeT(this->styleRuns.size());
        for (const auto& run: this->styleRuns) {
            out.writeSizeT(run.start);
            out.writeSizeT(run.end);
            out.writeInt(encode(run.bold));
            out.writeInt(encode(run.italic));
            out.writeInt(run.color.has_value());
            out.writeUInt(run.color ? static_cast<uint32_t>(*run.color) : 0U);
        }
    }

    out.endObject();
}

void Text::readSerialized(ObjectInputStream& in) {
    in.readObject("Text");

    this->AudioElement::readSerialized(in);

    this->text = in.readString();

    font.readSerialized(in);

    this->wrapWidth = in.readDouble();
    this->align = static_cast<TextAlignment::Value>(in.readInt());
    this->align.validate();
    this->justify = in.readInt() != 0;

    // Optional inline style runs: absent both from an unstyled element and from a blob written
    // by a stock Xournal++ build, which ends the object right here.
    this->styleRuns.clear();
    if (!in.atEndOfObject()) {
        /*
         * The count comes off the wire, so nothing is reserved on the strength of it: the vector
         * grows as runs are actually read, and a count larger than the blob holds runs out of
         * data and throws instead of asking for the memory it claims to need.
         */
        const size_t count = in.readSizeT();
        auto decode = [](int encoded) {
            return encoded < 0 ? std::optional<bool>{} : std::optional<bool>{encoded != 0};
        };

        for (size_t i = 0; i < count; i++) {
            TextStyleRun run;
            run.start = in.readSizeT();
            run.end = in.readSizeT();
            run.bold = decode(in.readInt());
            run.italic = decode(in.readInt());
            const bool hasColor = in.readInt() != 0;
            const auto color = Color(in.readUInt());
            if (hasColor) {
                run.color = color;
            }
            this->styleRuns.push_back(run);
        }
        xoj::text::normalizeStyleRuns(this->styleRuns);
        xoj::text::alignStyleRunsToText(this->styleRuns, this->text);
    }

    in.endObject();
}

auto Text::findText(const std::string& search) const -> std::vector<XojPdfRectangle> {
    size_t patternLength = search.length();
    if (patternLength == 0) {
        return {};
    }

    auto layout = this->createPangoLayout();
    pango_layout_set_text(layout.get(), this->text.c_str(), static_cast<int>(this->text.length()));


    std::string text = StringUtils::toLowerCase(this->text);
    std::string pattern = StringUtils::toLowerCase(search);

    const auto& origin = this->getOrigin();

    std::vector<XojPdfRectangle> list;

    for (size_t pos = text.find(pattern); pos != std::string::npos; pos = text.find(pattern, pos + 1)) {
        XojPdfRectangle mark;
        PangoRectangle rect = {0};
        pango_layout_index_to_pos(layout.get(), static_cast<int>(pos), &rect);
        mark.x1 = (static_cast<double>(rect.x)) / PANGO_SCALE + origin.x;
        mark.y1 = (static_cast<double>(rect.y)) / PANGO_SCALE + origin.y;

        pango_layout_index_to_pos(layout.get(), static_cast<int>(pos + patternLength - 1), &rect);
        mark.x2 = (static_cast<double>(rect.x) + rect.width) / PANGO_SCALE + origin.x;
        mark.y2 = (static_cast<double>(rect.y) + rect.height) / PANGO_SCALE + origin.y;

        list.push_back(mark);
    }

    return list;
}
