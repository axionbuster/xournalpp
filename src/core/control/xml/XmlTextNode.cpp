#include "XmlTextNode.h"

#include <utility>  // for move

#include "control/xml/XmlAudioNode.h"  // for XmlAudioNode
#include "util/OutputStream.h"         // for OutputStream
#include "util/StringUtils.h"          // for StaticStringView, replaceAllChars, replace_pair

XmlTextNode::XmlTextNode(StringUtils::StaticStringView tag, std::string text):
        XmlAudioNode(tag), text(std::move(text)) {}

XmlTextNode::XmlTextNode(StringUtils::StaticStringView tag): XmlAudioNode(tag) {}

void XmlTextNode::setText(std::string text) { this->text = std::move(text); }

void XmlTextNode::writeOut(OutputStream* out) {
    out->write("<");
    out->write(tag);
    writeAttributes(out);

    out->write(">");

    /*
     * The carriage return is escaped along with the markup characters, and for the same reason:
     * written literally it would not survive being read back. XML parsers fold a CR, and the CR
     * of a CRLF pair, into a single line feed before the document ever reaches the application,
     * so a text pasted in from a Windows program would come back one byte shorter per line --
     * silently shifting the byte offsets a <text> element's "runs" attribute is written with. A
     * numeric character reference is exempt from that folding and reads back as the byte itself.
     */
    std::string tmp(this->text);
    StringUtils::replaceAllChars(tmp, {replace_pair('&', "&amp;"), replace_pair('<', "&lt;"), replace_pair('>', "&gt;"),
                                       replace_pair('\r', "&#xD;")});
    out->write(tmp);

    out->write("</");
    out->write(tag);
    out->write(">\n");
}
