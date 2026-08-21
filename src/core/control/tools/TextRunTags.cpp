#include "TextRunTags.h"

#include <charconv>    // for from_chars
#include <cinttypes>   // for PRIx32
#include <cstdint>     // for uint32_t
#include <cstdio>      // for snprintf
#include <cstring>     // for strncmp
#include <functional>  // for function
#include <string>      // for string
#include <utility>     // for exchange
#include <vector>      // for vector

#include "util/Assert.h"  // for xoj_assert

/// Every tag this fork puts on a text buffer starts with this, and nothing else does
static constexpr const char* TAG_PREFIX = "xopp-";
static constexpr const char* BOLD_TAG_NAME = "xopp-bold";
static constexpr const char* NOT_BOLD_TAG_NAME = "xopp-bold-off";
static constexpr const char* ITALIC_TAG_NAME = "xopp-italic";
static constexpr const char* NOT_ITALIC_TAG_NAME = "xopp-italic-off";
static constexpr const char* COLOR_TAG_PREFIX = "xopp-color-";

/// The name of the tag standing for one color override, e.g. "xopp-color-ff0000ff"
static auto colorTagName(Color color) -> std::string {
    char name[64];
    snprintf(name, sizeof(name), "%s%08" PRIx32, COLOR_TAG_PREFIX, (static_cast<uint32_t>(color) << 8U) | color.alpha);
    return name;
}

/// A tag's name. GtkTextTag hands out a copy of it, which this takes care of freeing.
static auto tagName(GtkTextTag* tag) -> std::string {
    gchar* name = nullptr;
    g_object_get(tag, "name", &name, nullptr);
    std::string res = name ? name : "";
    g_free(name);
    return res;
}

/// The color a color tag stands for, or nothing if the tag is not one
static auto colorOfTagName(const std::string& name) -> std::optional<Color> {
    const size_t prefixLength = strlen(COLOR_TAG_PREFIX);
    if (name.compare(0, prefixLength, COLOR_TAG_PREFIX) != 0) {
        return std::nullopt;
    }
    uint32_t code{};
    const char* const begin = name.data() + prefixLength;
    const char* const end = name.data() + name.size();
    auto [ptr, ec] = std::from_chars(begin, end, code, 16);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    // The Color constructor takes AARRGGBB, so the alpha byte moves from the bottom to the top
    return Color{(code >> 8U) | (code << 24U)};
}

static auto lookupOrCreateTag(GtkTextBuffer* buffer, const char* name) -> GtkTextTag* {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    if (GtkTextTag* tag = gtk_text_tag_table_lookup(table, name)) {
        return tag;
    }
    return gtk_text_buffer_create_tag(buffer, name, nullptr);
}

auto xoj::text::getByteOffsetOfIterator(GtkTextIter it) -> int {
    // Bytes from beginning of line to iterator
    int pos = gtk_text_iter_get_line_index(&it);
    gtk_text_iter_set_line_index(&it, 0);
    // Count bytes of previous lines
    while (gtk_text_iter_backward_line(&it)) {
        pos += gtk_text_iter_get_bytes_in_line(&it);
    }
    return pos;
}

auto xoj::text::getIteratorAtByteOffset(GtkTextBuffer* buf, int byteIndex) -> GtkTextIter {
    xoj_assert(byteIndex >= 0);
    GtkTextIter it = {nullptr};
    gtk_text_buffer_get_start_iter(buf, &it);

    // Fast forward to the beginning of the line containing our target destination
    for (int linelength = gtk_text_iter_get_bytes_in_line(&it);
         linelength <= byteIndex && gtk_text_iter_forward_line(&it);
         byteIndex -= std::exchange(linelength, gtk_text_iter_get_bytes_in_line(&it))) {}

    if (!gtk_text_iter_is_end(&it)) {
        if (gtk_text_iter_get_chars_in_line(&it) == gtk_text_iter_get_bytes_in_line(&it)) {
            // One byte per character on this line, so every index is a character boundary
            gtk_text_iter_set_line_index(&it, byteIndex);
        } else {
            /*
             * The line holds multi-byte characters, and an index inside one of them is fatal:
             * GTK warns that it "will crash the text buffer" and then aborts on the next tagging
             * or edit. Walk to the last boundary at or before the index instead.
             */
            const int line = gtk_text_iter_get_line(&it);
            for (GtkTextIter next = it; gtk_text_iter_get_line_index(&it) < byteIndex;) {
                if (!gtk_text_iter_forward_char(&next) || gtk_text_iter_get_line(&next) != line ||
                    gtk_text_iter_get_line_index(&next) > byteIndex) {
                    break;
                }
                it = next;
            }
        }
    }
    // else { // byteIndex was either past-the-end or pointed to the end }

    return it;
}

auto xoj::text::boldTag(GtkTextBuffer* buffer, bool bold) -> GtkTextTag* {
    return lookupOrCreateTag(buffer, bold ? BOLD_TAG_NAME : NOT_BOLD_TAG_NAME);
}

auto xoj::text::italicTag(GtkTextBuffer* buffer, bool italic) -> GtkTextTag* {
    return lookupOrCreateTag(buffer, italic ? ITALIC_TAG_NAME : NOT_ITALIC_TAG_NAME);
}

auto xoj::text::colorTag(GtkTextBuffer* buffer, Color color) -> GtkTextTag* {
    return lookupOrCreateTag(buffer, colorTagName(color).c_str());
}

auto xoj::text::styleAt(const GtkTextIter* it) -> InlineStyle {
    InlineStyle style;
    GSList* tags = gtk_text_iter_get_tags(it);
    for (GSList* l = tags; l; l = l->next) {
        const std::string name = tagName(static_cast<GtkTextTag*>(l->data));
        if (name == BOLD_TAG_NAME) {
            style.bold = true;
        } else if (name == NOT_BOLD_TAG_NAME) {
            style.bold = false;
        } else if (name == ITALIC_TAG_NAME) {
            style.italic = true;
        } else if (name == NOT_ITALIC_TAG_NAME) {
            style.italic = false;
        } else if (auto color = colorOfTagName(name)) {
            style.color = color;
        }
    }
    g_slist_free(tags);
    return style;
}

auto xoj::text::styleLeftOf(const GtkTextIter* it) -> InlineStyle {
    GtkTextIter left = *it;
    if (!gtk_text_iter_backward_char(&left)) {
        // At the very beginning of the buffer, take after the character to the right instead
        return styleAt(it);
    }
    return styleAt(&left);
}

void xoj::text::clearStyle(GtkTextBuffer* buffer, const GtkTextIter* start, const GtkTextIter* end) {
    std::vector<GtkTextTag*> ours;
    gtk_text_tag_table_foreach(
            gtk_text_buffer_get_tag_table(buffer),
            [](GtkTextTag* tag, gpointer data) {
                if (tagName(tag).compare(0, strlen(TAG_PREFIX), TAG_PREFIX) == 0) {
                    static_cast<std::vector<GtkTextTag*>*>(data)->push_back(tag);
                }
            },
            &ours);

    for (GtkTextTag* tag: ours) {
        gtk_text_buffer_remove_tag(buffer, tag, start, end);
    }
}

void xoj::text::applyStyle(GtkTextBuffer* buffer, const GtkTextIter* start, const GtkTextIter* end,
                           const InlineStyle& style) {
    // Wipe first: the range must end up with exactly this style and nothing of what it had
    clearStyle(buffer, start, end);

    if (style.bold) {
        gtk_text_buffer_apply_tag(buffer, boldTag(buffer, *style.bold), start, end);
    }
    if (style.italic) {
        gtk_text_buffer_apply_tag(buffer, italicTag(buffer, *style.italic), start, end);
    }
    if (style.color) {
        gtk_text_buffer_apply_tag(buffer, colorTag(buffer, *style.color), start, end);
    }
}

/// Run `visit` on every stretch of [start, end) that carries one style, stopping when it says so
static void forEachStyledStretch(const GtkTextIter* start, const GtkTextIter* end,
                                 const std::function<bool(const xoj::text::InlineStyle&)>& visit) {
    GtkTextIter it = *start;
    while (gtk_text_iter_compare(&it, end) < 0) {
        if (!visit(xoj::text::styleAt(&it))) {
            return;
        }
        if (!gtk_text_iter_forward_to_tag_toggle(&it, nullptr)) {
            return;
        }
    }
}

auto xoj::text::rangeIsFullyStyled(std::optional<bool> InlineStyle::* which, bool baseState, const GtkTextIter* start,
                                   const GtkTextIter* end) -> bool {
    if (gtk_text_iter_compare(start, end) >= 0) {
        return false;
    }

    bool fully = true;
    forEachStyledStretch(start, end, [&](const InlineStyle& style) {
        fully = (style.*which).value_or(baseState);
        return fully;
    });
    return fully;
}

auto xoj::text::rangeCarriesStyle(const GtkTextIter* start, const GtkTextIter* end) -> bool {
    bool styled = false;
    forEachStyledStretch(start, end, [&](const InlineStyle& style) {
        styled = !style.isPlain();
        return !styled;
    });
    return styled;
}

auto xoj::text::runsFromBuffer(GtkTextBuffer* buffer) -> TextStyleRuns {
    TextStyleRuns runs;

    GtkTextIter it = {nullptr};
    gtk_text_buffer_get_start_iter(buffer, &it);
    GtkTextIter bufferEnd = {nullptr};
    gtk_text_buffer_get_end_iter(buffer, &bufferEnd);

    while (!gtk_text_iter_is_end(&it)) {
        const InlineStyle style = styleAt(&it);

        // The style is constant until the next tag toggle, wherever it is
        GtkTextIter next = it;
        if (!gtk_text_iter_forward_to_tag_toggle(&next, nullptr)) {
            next = bufferEnd;
        }

        if (!style.isPlain()) {
            TextStyleRun run;
            run.start = static_cast<size_t>(getByteOffsetOfIterator(it));
            run.end = static_cast<size_t>(getByteOffsetOfIterator(next));
            run.bold = style.bold;
            run.italic = style.italic;
            run.color = style.color;
            runs.push_back(run);
        }

        it = next;
    }

    normalizeStyleRuns(runs);
    return runs;
}

void xoj::text::applyRunsToBuffer(GtkTextBuffer* buffer, const TextStyleRuns& runs) {
    for (const auto& run: runs) {
        GtkTextIter start = getIteratorAtByteOffset(buffer, static_cast<int>(run.start));
        GtkTextIter end = getIteratorAtByteOffset(buffer, static_cast<int>(run.end));
        applyStyle(buffer, &start, &end, InlineStyle{run.bold, run.italic, run.color});
    }
}
