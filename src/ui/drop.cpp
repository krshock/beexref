#include "drop.h"

#include <QMimeData>
#include <QRegularExpression>
#include <QStringDecoder>

namespace ui {
namespace {

const QString kChromiumFormat = QStringLiteral("chromium/x-web-custom-data");
const QString kWebkitFormat = QStringLiteral("org.webkitgtk.WebKit.custom-pasteboard-data");

const QRegularExpression &urlPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(https?://[^\s"'<>\\\x00]+)"));
    return pattern;
}

const QRegularExpression &imageUrlPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(https?://\S+\.(?:jpe?g|png|gif|webp|avif)(?:\?\S*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

const QRegularExpression &htmlUrlPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"((?:src|href)\s*=\s*["'](https?://[^"']+))"));
    return pattern;
}

const QRegularExpression &plainUrlPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^https?://\S+$)"));
    return pattern;
}

// Finds http(s) URLs in arbitrary bytes, trying UTF-16LE (Chromium's
// custom payloads) and latin-1 (plain ASCII/UTF-8 blobs).
QStringList scanPayloadForUrls(const QByteArray &data)
{
    QStringList candidates;
    const auto collect = [&candidates](const QString &text) {
        auto matches = urlPattern().globalMatch(text);
        while (matches.hasNext()) {
            QString url = matches.next().captured();
            while (!url.isEmpty()
                   && QStringLiteral(".,;)]}'\"").contains(url.at(url.size() - 1))) {
                url.chop(1);
            }
            if (!url.isEmpty())
                candidates.append(url);
        }
    };

    QStringDecoder utf16(QStringDecoder::Utf16LE);
    collect(utf16(data));
    collect(QString::fromLatin1(data));
    return candidates;
}

// Picks the candidate most likely to be an image file, falling back to
// the first one.
QString preferImageUrl(const QStringList &candidates)
{
    for (const QString &url : candidates) {
        if (imageUrlPattern().match(url).hasMatch())
            return url;
    }
    return candidates.isEmpty() ? QString() : candidates.first();
}

DropResult noneResult(const QString &message = QString::fromLatin1(kNoDropMessage))
{
    DropResult result;
    result.message = message;
    return result;
}

} // namespace

QList<QUrl> parseUriList(const QByteArray &data)
{
    QList<QUrl> urls;
    for (const QByteArray &line : data.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;
        urls.append(QUrl(QString::fromUtf8(trimmed)));
    }
    return urls;
}

namespace {

// Local files, remote URLs (with the manual text/uri-list fallback),
// raw image data, HTML or plain-text URLs.
DropResult extractStandard(const QMimeData &data)
{
    QList<QUrl> urls = data.urls();
    if (urls.isEmpty() && data.hasFormat(QStringLiteral("text/uri-list")))
        urls = parseUriList(data.data(QStringLiteral("text/uri-list")));
    if (!urls.isEmpty()) {
        DropResult result;
        result.kind = DropKind::Urls;
        result.urls = urls;
        result.accepted = true;
        return result;
    }

    if (data.hasImage()) {
        const QImage image = qvariant_cast<QImage>(data.imageData());
        if (!image.isNull()) {
            DropResult result;
            result.kind = DropKind::Image;
            result.image = image;
            result.accepted = true;
            return result;
        }
    }

    if (data.hasHtml()) {
        const auto match = htmlUrlPattern().match(data.html());
        if (match.hasMatch()) {
            DropResult result;
            result.kind = DropKind::Urls;
            result.urls = {QUrl(match.captured(1))};
            result.accepted = true;
            return result;
        }
    }

    if (data.hasText()) {
        const QString text = data.text().trimmed();
        if (plainUrlPattern().match(text).hasMatch()) {
            DropResult result;
            result.kind = DropKind::Urls;
            result.urls = {QUrl(text)};
            result.accepted = true;
            return result;
        }
    }

    return noneResult();
}

// Chromium drags without standard formats: the payload is a UTF-16LE
// serialized map scanned for URLs, preferring image-file URLs.
DropResult extractChromium(const QMimeData &data)
{
    if (!data.hasFormat(kChromiumFormat))
        return noneResult();

    const QStringList skipped{QStringLiteral("text/plain"), QStringLiteral("text/html"),
                              QStringLiteral("text/uri-list"),
                              QStringLiteral("application/octet-stream"),
                              QStringLiteral("xdnddirectsave0")};
    QStringList candidates;
    for (const QString &format : data.formats()) {
        const QString lower = format.toLower();
        if (skipped.contains(lower) || lower.contains(QStringLiteral("taint")))
            continue;
        candidates += scanPayloadForUrls(data.data(format));
    }

    const QString url = preferImageUrl(candidates);
    if (url.isEmpty())
        return noneResult();

    DropResult result;
    result.kind = DropKind::Urls;
    result.urls = {QUrl(url)};
    result.accepted = true;
    return result;
}

// WebKitGTK drags that may only offer the engine's internal format. An
// unreadable payload is accepted provisionally and reported at drop
// time, in case a WebKitGTK version serves it on demand.
DropResult extractWebKit(const QMimeData &data)
{
    if (!data.hasFormat(kWebkitFormat))
        return noneResult();

    const QString url = preferImageUrl(scanPayloadForUrls(data.data(kWebkitFormat)));
    if (!url.isEmpty()) {
        DropResult result;
        result.kind = DropKind::Urls;
        result.urls = {QUrl(url)};
        result.accepted = true;
        return result;
    }

    DropResult result = noneResult(QString::fromLatin1(kWebKitEmptyMessage));
    result.accepted = true;
    return result;
}

} // namespace

bool dropAccepts(const QMimeData &data)
{
    if (data.hasUrls() || data.hasImage() || data.hasHtml() || data.hasText())
        return true;
    return data.hasFormat(kChromiumFormat) || data.hasFormat(kWebkitFormat);
}

DropResult inspectDrop(const QMimeData &data)
{
    DropResult fallback = noneResult();
    for (const auto &extractor : {extractStandard, extractChromium, extractWebKit}) {
        DropResult result = extractor(data);
        if (result.kind != DropKind::None)
            return result;
        fallback = result;
    }
    return fallback;
}

} // namespace ui
