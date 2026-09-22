#include "source.h"

#include "board/board.h"

#include <utility>

namespace doc {

BytesSource::BytesSource(QByteArray data)
    : data_(std::move(data))
{
}

bool BytesSource::isValid() const
{
    return !data_.isEmpty();
}

QByteArray BytesSource::bytes() const
{
    return data_;
}

qint64 BytesSource::residentBytes() const
{
    return data_.size();
}

BoardSource::BoardSource(std::shared_ptr<board::Board> board, qint64 itemId)
    : board_(std::move(board))
    , itemId_(itemId)
{
}

bool BoardSource::isValid() const
{
    return board_ && itemId_ != 0;
}

QByteArray BoardSource::bytes() const
{
    if (!board_ || itemId_ == 0)
        return {};
    auto blob = board_->blob(itemId_);
    return blob.isOk() ? blob.take() : QByteArray();
}

qint64 BoardSource::residentBytes() const
{
    // Blobs are read from the board on demand and not retained.
    return 0;
}

ProviderSource::ProviderSource(Provider provider)
    : provider_(std::move(provider))
{
}

bool ProviderSource::isValid() const
{
    return static_cast<bool>(provider_);
}

QByteArray ProviderSource::bytes() const
{
    return provider_ ? provider_() : QByteArray();
}

qint64 ProviderSource::residentBytes() const
{
    return 0;
}

} // namespace doc
