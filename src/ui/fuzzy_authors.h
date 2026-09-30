#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace ui::fuzzy {

// The searchable key of a candidate: the display text without case,
// diacritics (tilde, accents, dieresis...) or non-printable characters,
// with whitespace runs collapsed into single spaces. Empty when nothing
// searchable remains.
QString foldedKey(const QString &text);

// The display form: the original text without its non-printable
// characters, trimmed. Case, accents and inner spacing are kept; this is
// what a suggestion shows.
QString displayText(const QString &text);

// One unique author: the display form, its folded key, and how many raw
// entries collapsed into it (also the ranking tie-breaker).
struct Candidate
{
    QString display;
    QString key;
    int count = 0;
};

// The unique authors of the raw list, in first-seen order, dropping the
// entries with nothing searchable left (empty, blanks only, invisible
// characters only) and folding duplicates (case and diacritics) into the
// first display form seen.
QVector<Candidate> candidates(const QStringList &rawAuthors);

// The best matches of query, best first, at most limit (0 means no cap).
// An empty query returns the most used candidates.
QVector<Candidate> search(const QVector<Candidate> &candidates, const QString &query, int limit);

} // namespace ui::fuzzy
