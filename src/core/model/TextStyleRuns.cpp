#include "TextStyleRuns.h"

#include <algorithm>     // for sort, stable_sort
#include <charconv>      // for from_chars
#include <cinttypes>     // for PRIx32
#include <cstdint>       // for uint32_t
#include <cstdio>        // for snprintf
#include <system_error>  // for errc

#include "util/Color.h"  // for Color, Util::argb_to_ColorU16

/// Number of hexadecimal digits of a color in the "runs" attribute: RRGGBBAA
static constexpr size_t COLOR_CODE_LENGTH = 8;

/// The bytes of a color, in the RRGGBBAA order the file format uses everywhere else
static auto colorToCode(Color c) -> uint32_t { return (static_cast<uint32_t>(c) << 8U) | c.alpha; }

static auto colorFromCode(uint32_t code) -> Color {
    // The Color constructor takes AARRGGBB, so the alpha byte moves from the bottom to the top
    return Color{(code >> 8U) | (code << 24U)};
}

auto xoj::text::serializeStyleRuns(const TextStyleRuns& runs) -> std::string {
    std::string res;
    for (const auto& run: runs) {
        if (run.isPlain() || run.end <= run.start) {
            continue;
        }
        if (!res.empty()) {
            res += ';';
        }
        res += std::to_string(run.start);
        res += '-';
        res += std::to_string(run.end);
        res += ':';
        if (run.bold) {
            res += *run.bold ? 'b' : 'B';
        }
        if (run.italic) {
            res += *run.italic ? 'i' : 'I';
        }
        if (run.color) {
            char code[COLOR_CODE_LENGTH + 3];
            snprintf(code, sizeof(code), "c#%08" PRIx32, colorToCode(*run.color));
            res += code;
        }
    }
    return res;
}

auto xoj::text::parseStyleRuns(std::string_view attribute) -> std::optional<TextStyleRuns> {
    TextStyleRuns runs;
    if (attribute.empty()) {
        return runs;
    }

    for (size_t pos = 0; pos <= attribute.size();) {
        const size_t separator = attribute.find(';', pos);
        const std::string_view item =
                attribute.substr(pos, separator == std::string_view::npos ? std::string_view::npos : separator - pos);
        pos = separator == std::string_view::npos ? attribute.size() + 1 : separator + 1;

        const size_t dash = item.find('-');
        const size_t colon = item.find(':');
        if (dash == std::string_view::npos || colon == std::string_view::npos || colon < dash) {
            return std::nullopt;
        }

        TextStyleRun run;
        const char* const itemBegin = item.data();
        auto [startEnd, startEc] = std::from_chars(itemBegin, itemBegin + dash, run.start);
        if (startEc != std::errc{} || startEnd != itemBegin + dash) {
            return std::nullopt;
        }
        auto [endEnd, endEc] = std::from_chars(itemBegin + dash + 1, itemBegin + colon, run.end);
        if (endEc != std::errc{} || endEnd != itemBegin + colon) {
            return std::nullopt;
        }
        if (run.end <= run.start) {
            return std::nullopt;
        }
        // The runs describe disjoint stretches of the text, in order
        if (!runs.empty() && run.start < runs.back().end) {
            return std::nullopt;
        }

        for (std::string_view flags = item.substr(colon + 1); !flags.empty();) {
            if ((flags.front() == 'b' || flags.front() == 'B') && !run.bold) {
                run.bold = flags.front() == 'b';
                flags.remove_prefix(1);
            } else if ((flags.front() == 'i' || flags.front() == 'I') && !run.italic) {
                run.italic = flags.front() == 'i';
                flags.remove_prefix(1);
            } else if (flags.front() == 'c' && !run.color && flags.size() == COLOR_CODE_LENGTH + 2 && flags[1] == '#') {
                uint32_t code{};
                const char* const codeBegin = flags.data() + 2;
                const char* const codeEnd = flags.data() + flags.size();
                auto [ptr, ec] = std::from_chars(codeBegin, codeEnd, code, 16);
                if (ec != std::errc{} || ptr != codeEnd) {
                    return std::nullopt;
                }
                run.color = colorFromCode(code);
                flags = {};
            } else {
                return std::nullopt;
            }
        }

        // A run styled exactly like its element is never written, so reading one means the
        // attribute did not come from this fork's serializer
        if (run.isPlain()) {
            return std::nullopt;
        }

        runs.push_back(run);
    }

    return runs;
}

void xoj::text::normalizeStyleRuns(TextStyleRuns& runs) {
    std::stable_sort(runs.begin(), runs.end(),
                     [](const TextStyleRun& a, const TextStyleRun& b) { return a.start < b.start; });

    TextStyleRuns res;
    res.reserve(runs.size());
    for (const auto& run: runs) {
        if (run.end <= run.start || run.isPlain()) {
            continue;
        }
        if (!res.empty() && res.back().end == run.start && res.back().sameStyleAs(run)) {
            res.back().end = run.end;
        } else {
            res.push_back(run);
        }
    }
    runs = std::move(res);
}

void xoj::text::clampStyleRuns(TextStyleRuns& runs, size_t textLength) {
    for (auto& run: runs) {
        run.end = std::min(run.end, textLength);
    }
    runs.erase(std::remove_if(runs.begin(), runs.end(), [](const TextStyleRun& run) { return run.end <= run.start; }),
               runs.end());
}

void xoj::text::alignStyleRunsToText(TextStyleRuns& runs, std::string_view text) {
    clampStyleRuns(runs, text.size());

    // A byte is the start of a character unless it is a UTF-8 continuation byte, 10xxxxxx
    auto isCharacterBoundary = [&](size_t offset) {
        return offset >= text.size() || (static_cast<unsigned char>(text[offset]) & 0xC0U) != 0x80U;
    };

    for (auto& run: runs) {
        // Both offsets move into the run, so a run never grows over a character it did not cover
        while (run.start < text.size() && !isCharacterBoundary(run.start)) {
            run.start++;
        }
        while (run.end > run.start && !isCharacterBoundary(run.end)) {
            run.end--;
        }
    }

    runs.erase(std::remove_if(runs.begin(), runs.end(), [](const TextStyleRun& run) { return run.end <= run.start; }),
               runs.end());
}

void xoj::text::appendStyleRunAttributes(PangoAttrList* attrs, const TextStyleRuns& runs, size_t insertPosition,
                                         size_t insertLength, const TextStyleRun* insertStyle) {
    auto emit = [&](const TextStyleRun& style, size_t start, size_t end) {
        if (end <= start) {
            return;
        }
        auto add = [&](PangoAttribute* attribute) {
            attribute->start_index = static_cast<unsigned int>(start);
            attribute->end_index = static_cast<unsigned int>(end);
            pango_attr_list_insert(attrs, attribute);  // attrs takes ownership of attribute
        };

        if (style.bold) {
            add(pango_attr_weight_new(*style.bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL));
        }
        if (style.italic) {
            add(pango_attr_style_new(*style.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL));
        }
        if (style.color) {
            const auto c = Util::argb_to_ColorU16(*style.color);
            add(pango_attr_foreground_new(c.red, c.green, c.blue));
        }
    };

    for (const auto& run: runs) {
        if (insertLength == 0) {
            emit(run, run.start, run.end);
        } else if (insertStyle == nullptr) {
            /*
             * Nothing was inserted at `insertPosition` as far as the run list is concerned, so an
             * offset sitting exactly there belongs after the insertion — except that a run ending
             * there is stretched over it, which is the same rule the committed text follows when
             * it inherits the styling of the character to its left.
             */
            emit(run, run.start >= insertPosition ? run.start + insertLength : run.start,
                 run.end >= insertPosition ? run.end + insertLength : run.end);
        } else {
            // The insertion carries its own styling, so the runs are split around it
            emit(run, std::min(run.start, insertPosition), std::min(run.end, insertPosition));
            emit(run, std::max(run.start, insertPosition) + insertLength,
                 std::max(run.end, insertPosition) + insertLength);
        }
    }

    if (insertStyle != nullptr && insertLength != 0) {
        emit(*insertStyle, insertPosition, insertPosition + insertLength);
    }
}
