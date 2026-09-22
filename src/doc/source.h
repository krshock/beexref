#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

namespace board {
class Board;
}

namespace doc {

// Immutable encoded image bytes. Implementations never mutate state
// after construction, so every method is safe to call from any thread
// and a Source may outlive the Item that referenced it. That is what
// lets the decode and save workers hold a payload without touching
// document state.
class Source
{
public:
    virtual ~Source() = default;

    // Whether bytes can be produced at all.
    virtual bool isValid() const = 0;

    // Encoded bytes; empty when unavailable.
    virtual QByteArray bytes() const = 0;
};

using SourcePtr = std::shared_ptr<const Source>;

// Encoded bytes held in memory (pasted or dropped images). Returning
// them is cheap: QByteArray is implicitly shared.
class BytesSource final : public Source
{
public:
    explicit BytesSource(QByteArray data);

    bool isValid() const override;
    QByteArray bytes() const override;

private:
    const QByteArray data_;
};

// Reads an item's blob from an open board. The shared Board keeps the
// connection alive as long as any source holds it; a closed board
// yields empty bytes instead of a dangling read.
class BoardSource final : public Source
{
public:
    BoardSource(std::shared_ptr<board::Board> board, qint64 itemId);

    bool isValid() const override;
    QByteArray bytes() const override;

private:
    const std::shared_ptr<board::Board> board_;
    const qint64 itemId_ = 0;
};

} // namespace doc
