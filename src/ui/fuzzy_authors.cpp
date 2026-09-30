#include "fuzzy_authors.h"

#include <QChar>
#include <QHash>
#include <QPair>

#include <algorithm>

namespace ui::fuzzy {
namespace {

// Whether the character is a combining mark (a diacritic after NFD
// decomposition); those are stripped for matching.
bool isMark(QChar c)
{
    const QChar::Category category = c.category();
    return category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
        || category == QChar::Mark_Enclosing;
}

// The text without its non-printable characters; inner spacing is kept.
QString withoutInvisibles(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (QChar c : text) {
        if (c.isPrint())
            out.append(c);
    }
    return out;
}

// Every whitespace run becomes one space; the edges are trimmed.
QString collapseSpaces(const QString &text)
{
    QString out;
    out.reserve(text.size());
    bool pending = false;
    for (QChar c : text) {
        if (c.isSpace()) {
            pending = true;
            continue;
        }
        if (pending && !out.isEmpty())
            out.append(QLatin1Char(' '));
        pending = false;
        out.append(c);
    }
    return out;
}

// Whether query appears in key in order, gaps allowed.
bool isSubsequence(const QString &key, const QString &query)
{
    qsizetype at = 0;
    for (QChar c : key) {
        if (at < query.size() && c == query.at(at))
            ++at;
    }
    return at == query.size();
}

// 3 exact, 2 prefix, 1 substring, 0 subsequence, -1 no match. An empty
// query matches everything with the same weak score.
int matchScore(const QString &key, const QString &query)
{
    if (query.isEmpty())
        return 1;
    if (key == query)
        return 3;
    if (key.startsWith(query))
        return 2;
    if (key.contains(query))
        return 1;
    return isSubsequence(key, query) ? 0 : -1;
}

} // namespace

QString foldedKey(const QString &text)
{
    const QString printable = withoutInvisibles(text);
    if (printable.isEmpty())
        return {};
    // NFD splits "é" into "e" plus a combining mark, so dropping the
    // marks folds the accents away.
    QString folded;
    folded.reserve(printable.size());
    for (QChar c : printable.normalized(QString::NormalizationForm_D)) {
        if (isMark(c))
            continue;
        folded.append(c.toLower());
    }
    return collapseSpaces(folded);
}

QString displayText(const QString &text)
{
    return withoutInvisibles(text).trimmed();
}

QVector<Candidate> candidates(const QStringList &rawAuthors)
{
    QVector<Candidate> unique;
    QHash<QString, int> byKey;
    unique.reserve(rawAuthors.size());
    for (const QString &raw : rawAuthors) {
        const QString key = foldedKey(raw);
        if (key.isEmpty())
            continue;
        const auto found = byKey.constFind(key);
        if (found != byKey.constEnd()) {
            ++unique[found.value()].count;
            continue;
        }
        Candidate candidate;
        candidate.display = displayText(raw);
        candidate.key = key;
        candidate.count = 1;
        byKey.insert(key, unique.size());
        unique.append(candidate);
    }
    return unique;
}

QVector<Candidate> search(const QVector<Candidate> &candidates, const QString &query, int limit)
{
    const QString foldedQuery = foldedKey(query);
    QVector<QPair<int, uint>> scored; // score, candidate index
    scored.reserve(candidates.size());
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        const int score = matchScore(candidates.at(i).key, foldedQuery);
        if (score >= 0)
            scored.append({score, uint(i)});
    }
    std::stable_sort(scored.begin(), scored.end(), [&candidates](const auto &a, const auto &b) {
        if (a.first != b.first)
            return a.first > b.first;
        const Candidate &left = candidates.at(a.second);
        const Candidate &right = candidates.at(b.second);
        if (left.count != right.count)
            return left.count > right.count;
        if (left.display.size() != right.display.size())
            return left.display.size() < right.display.size();
        return left.display < right.display;
    });
    const qsizetype cap = limit > 0 ? qMin<qsizetype>(limit, scored.size()) : scored.size();
    QVector<Candidate> out;
    out.reserve(cap);
    for (qsizetype i = 0; i < cap; ++i)
        out.append(candidates.at(scored.at(i).second));
    return out;
}

} // namespace ui::fuzzy
