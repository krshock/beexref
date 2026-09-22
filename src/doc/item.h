#pragma once

#include "source.h"

#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>

#include <memory>

namespace doc {

inline constexpr char kTypePixmap[] = "pixmap";
inline constexpr char kTypeText[] = "text";
inline constexpr char kTypeError[] = "error";

// One board item. Item state is owned by the UI thread; the only
// member a worker may touch is `source`, whose immutability is what
// makes cross-thread decoding safe.
class Item
{
public:
    Item() = default;
    explicit Item(QString type);

    qint64 id = 0; // board row id / save id; 0 for unsaved items
    QString uuid;
    QString type;
    double x = 0;
    double y = 0;
    double z = 0;
    double scale = 1;
    double rotation = 0;
    double flip = 1;
    QJsonObject data;
    QJsonObject meta;

    // Encoded bytes; null for text and error items.
    SourcePtr source;
    QString format;
    QString filename;

    // Thumbnail loaded with the board; reused verbatim on save so it is
    // never regenerated from the source.
    QByteArray floorData;
    double floorFraction = 0;
    QString floorFormat;

    bool isPixmap() const { return type == QLatin1String(kTypePixmap); }
    bool isText() const { return type == QLatin1String(kTypeText); }
    bool isError() const { return type == QLatin1String(kTypeError); }

    // Original pixel size, from the data dictionary; invalid when
    // unknown.
    QSize originalSize() const;
    void setOriginalSize(const QSize &size);

    QString text() const;
    void setText(const QString &text);

    double opacity() const; // default 1
    void setOpacity(double opacity);

    bool grayscale() const; // default false
    void setGrayscale(bool grayscale);

    // Crop rectangle in original image coordinates; null rect when the
    // item is not cropped.
    QRectF crop() const;
    void setCrop(const QRectF &crop);

    bool hasSource() const { return source && source->isValid(); }

    // Gives the item a UUID when it has none, e.g. after loading a file
    // that predates the uuid column.
    QString ensureUuid();

    // Copy with a fresh UUID, no save id and the same immutable source.
    std::shared_ptr<Item> createCopy() const;
};

using ItemPtr = std::shared_ptr<Item>;

// New stable item identity: 32 lowercase hex characters, the same form
// the Python and Go ports use.
QString newUuid();

} // namespace doc
