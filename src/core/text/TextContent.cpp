#include "core/text/TextContent.h"

#include <algorithm>

namespace doodlebold {

int TextContent::length() const {
    int n = 0;
    for (const TextRun& r : runs)
        n += int(r.text.size());
    return n;
}

QString TextContent::plainText() const {
    QString out;
    for (const TextRun& r : runs)
        out += r.text;
    return out;
}

TextStyle TextContent::styleAt(int pos) const {
    if (runs.empty())
        return emptyStyle;
    int start = 0;
    for (const TextRun& r : runs) {
        const int end = start + int(r.text.size());
        if (pos > start && pos <= end)
            return r.style;
        start = end;
    }
    return pos <= 0 ? runs.front().style : runs.back().style;
}

TextStyle TextContent::styleOf(int from, int to) const {
    if (from >= to)
        return styleAt(from);
    int start = 0;
    for (const TextRun& r : runs) {
        const int end = start + int(r.text.size());
        if (end > from && start < to)
            return r.style;
        start = end;
    }
    return styleAt(from);
}

std::size_t TextContent::splitAt(int pos) {
    int start = 0;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const int end = start + int(runs[i].text.size());
        if (pos == start)
            return i;
        if (pos > start && pos < end) {
            TextRun tail{runs[i].text.mid(pos - start), runs[i].style};
            runs[i].text.truncate(pos - start);
            runs.insert(runs.begin() + std::ptrdiff_t(i) + 1, std::move(tail));
            return i + 1;
        }
        start = end;
    }
    return runs.size();
}

void TextContent::normalize() {
    std::vector<TextRun> out;
    for (TextRun& r : runs) {
        if (r.text.isEmpty())
            continue;
        if (!out.empty() && out.back().style == r.style)
            out.back().text += r.text;
        else
            out.push_back(std::move(r));
    }
    runs = std::move(out);
}

void TextContent::insert(int pos, const QString& text, const TextStyle& style) {
    if (text.isEmpty())
        return;
    pos = std::clamp(pos, 0, length());
    if (runs.empty())
        emptyStyle = style;
    const std::size_t at = splitAt(pos);
    runs.insert(runs.begin() + std::ptrdiff_t(at), TextRun{text, style});
    normalize();
}

void TextContent::remove(int from, int to) {
    from = std::clamp(from, 0, length());
    to = std::clamp(to, 0, length());
    if (from >= to)
        return;
    const TextStyle first = styleOf(from, to);
    const std::size_t a = splitAt(from);
    const std::size_t b = splitAt(to);
    runs.erase(runs.begin() + std::ptrdiff_t(a), runs.begin() + std::ptrdiff_t(b));
    normalize();
    if (runs.empty())
        emptyStyle = first;  // typing into the emptied layer continues in its style
}

void TextContent::restyle(int from, int to, const std::function<void(TextStyle&)>& change) {
    from = std::clamp(from, 0, length());
    to = std::clamp(to, 0, length());
    if (from >= to)
        return;
    const std::size_t a = splitAt(from);
    const std::size_t b = splitAt(to);
    for (std::size_t i = a; i < b; ++i)
        change(runs[i].style);
    normalize();
}

}
