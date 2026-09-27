#include "item.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <utility>

namespace doc {
namespace {

const QString kOriginalWidth = QStringLiteral("original_width");
const QString kOriginalHeight = QStringLiteral("original_height");

} // namespace

Item::Item(QString itemType)
    : type(std::move(itemType))
{
}

QSize Item::originalSize() const
{
    const int width = data.value(kOriginalWidth).toInt();
    const int height = data.value(kOriginalHeight).toInt();
    if (width <= 0 || height <= 0)
        return {};
    return QSize(width, height);
}

void Item::setOriginalSize(const QSize &size)
{
    data.insert(kOriginalWidth, size.width());
    data.insert(kOriginalHeight, size.height());
}

QString Item::text() const
{
    return data.value(QStringLiteral("text")).toString();
}

void Item::setText(const QString &text)
{
    data.insert(QStringLiteral("text"), text);
}

double Item::opacity() const
{
    return data.value(QStringLiteral("opacity")).toDouble(1.0);
}

void Item::setOpacity(double opacity)
{
    data.insert(QStringLiteral("opacity"), opacity);
}

bool Item::grayscale() const
{
    return data.value(QStringLiteral("grayscale")).toBool(false);
}

void Item::setGrayscale(bool grayscale)
{
    data.insert(QStringLiteral("grayscale"), grayscale);
}

QRectF Item::crop() const
{
    const QJsonValue value = data.value(QStringLiteral("crop"));
    if (!value.isArray())
        return {};
    const QJsonArray array = value.toArray();
    if (array.size() != 4)
        return {};
    return QRectF(array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble(),
                  array.at(3).toDouble());
}

void Item::setCrop(const QRectF &crop)
{
    if (!crop.isValid()) {
        data.remove(QStringLiteral("crop"));
        return;
    }
    data.insert(QStringLiteral("crop"),
                QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()});
}

QString Item::ensureUuid()
{
    if (uuid.isEmpty())
        uuid = newUuid();
    return uuid;
}

std::shared_ptr<Item> Item::createCopy() const
{
    auto copy = std::make_shared<Item>();
    copy->type = type;
    copy->x = x;
    copy->y = y;
    copy->z = z;
    copy->scale = scale;
    copy->rotation = rotation;
    copy->flip = flip;
    copy->data = data;
    copy->meta = meta;
    copy->source = source;
    copy->format = format;
    copy->filename = filename;
    copy->floorData = floorData;
    copy->floorFraction = floorFraction;
    copy->floorFormat = floorFormat;
    copy->uuid = newUuid();
    copy->id = 0;
    return copy;
}

QString newUuid()
{
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    uuid.remove(QLatin1Char('-'));
    return uuid;
}

ItemPtr createItem(const QString &type)
{
    return std::make_shared<Item>(type);
}

bool isKnownType(const QString &type)
{
    return type == QLatin1String(kTypePixmap) || type == QLatin1String(kTypeText)
        || type == QLatin1String(kTypeError);
}

} // namespace doc
