#include "logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>
#include <utility>

namespace logging {
namespace {

constexpr qint64 kMaxLogBytes = 1024 * 1000;
constexpr int kLogBackups = 1;

QMutex g_mutex;
Level g_consoleLevel = Level::Info;
QFile g_file;
QString g_path;
qint64 g_size = 0;

QString levelName(Level level)
{
    switch (level) {
    case Level::Trace:
        return QStringLiteral("INFO-8");
    case Level::Debug:
        return QStringLiteral("DEBUG-4");
    case Level::Info:
        return QStringLiteral("INFO");
    case Level::Warn:
        return QStringLiteral("WARN");
    case Level::Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

bool needsQuoting(const QString &value)
{
    if (value.isEmpty())
        return true;
    for (const QChar c : value) {
        if (c.unicode() <= u' ' || c == u'=' || c == u'"' || !c.isPrint())
            return true;
    }
    return false;
}

QString quote(const QString &value)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : value) {
        switch (c.unicode()) {
        case u'"':
            out += QStringLiteral("\\\"");
            break;
        case u'\\':
            out += QStringLiteral("\\\\");
            break;
        case u'\n':
            out += QStringLiteral("\\n");
            break;
        case u'\r':
            out += QStringLiteral("\\r");
            break;
        case u'\t':
            out += QStringLiteral("\\t");
            break;
        default:
            if (c.isPrint())
                out += c;
            else
                out += QStringLiteral("\\u%1").arg(static_cast<uint>(c.unicode()), 4, 16,
                                                  QLatin1Char('0'));
            break;
        }
    }
    out += QLatin1Char('"');
    return out;
}

QString formatValue(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return value.toString();
    default: {
        const QString text = value.toString();
        return needsQuoting(text) ? quote(text) : text;
    }
    }
}

QString formatLine(Level level, const QString &message, const Attrs &attrs)
{
    QString line = QStringLiteral("time=%1 level=%2 msg=%3")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                            levelName(level), needsQuoting(message) ? quote(message) : message);
    for (const auto &attr : attrs)
        line += QStringLiteral(" %1=%2").arg(attr.first, formatValue(attr.second));
    line += QLatin1Char('\n');
    return line;
}

bool openFile()
{
    if (g_file.isOpen())
        return true;
    g_file.setFileName(g_path);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered))
        return false;
    g_size = g_file.size();
    return true;
}

void rotate()
{
    if (g_file.isOpen())
        g_file.close();

    for (int i = kLogBackups - 1; i > 0; --i) {
        const QString from = QStringLiteral("%1.%2").arg(g_path).arg(i);
        const QString to = QStringLiteral("%1.%2").arg(g_path).arg(i + 1);
        QFile::remove(to);
        QFile::rename(from, to);
    }
    if (kLogBackups > 0) {
        const QString to = g_path + QStringLiteral(".1");
        QFile::remove(to);
        QFile::rename(g_path, to);
    }
    g_size = 0;
}

void writeLine(const QString &line)
{
    if (!g_file.isOpen())
        return;
    const QByteArray bytes = line.toUtf8();
    if (g_size + bytes.size() > kMaxLogBytes) {
        rotate();
        if (!openFile())
            return;
    }
    if (g_file.write(bytes) == bytes.size())
        g_size += bytes.size();
}

// Caller holds g_mutex.
void logLocked(Level level, const QString &message, const Attrs &attrs)
{
    const QString line = formatLine(level, message, attrs);
    writeLine(line);
    if (static_cast<int>(level) >= static_cast<int>(g_consoleLevel)) {
        const QByteArray bytes = line.toUtf8();
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
        std::fflush(stderr);
    }
}

} // namespace

void setup(Level consoleLevel, const QString &logFile)
{
    QMutexLocker locker(&g_mutex);
    g_consoleLevel = consoleLevel;
    if (g_file.isOpen())
        g_file.close();
    g_path = logFile;
    g_size = 0;
    QDir().mkpath(QFileInfo(logFile).absolutePath());
    if (!openFile())
        return;
    logLocked(Level::Info, QStringLiteral("Log session start"), {});
}

void log(Level level, const QString &message, const Attrs &attrs)
{
    QMutexLocker locker(&g_mutex);
    logLocked(level, message, attrs);
}

void trace(const QString &message, const Attrs &attrs)
{
    log(Level::Trace, message, attrs);
}

void debug(const QString &message, const Attrs &attrs)
{
    log(Level::Debug, message, attrs);
}

void info(const QString &message, const Attrs &attrs)
{
    log(Level::Info, message, attrs);
}

void warn(const QString &message, const Attrs &attrs)
{
    log(Level::Warn, message, attrs);
}

void error(const QString &message, const Attrs &attrs)
{
    log(Level::Error, message, attrs);
}

QString sessionText(const QString &logFile)
{
    QFile file(logFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString text = QString::fromUtf8(file.readAll());
    const QString marker = QStringLiteral("msg=\"Log session start\"");
    const qsizetype index = text.lastIndexOf(marker);
    if (index < 0)
        return text;
    const qsizetype newline = text.indexOf(QLatin1Char('\n'), index);
    if (newline < 0)
        return {};
    return text.mid(newline + 1);
}

} // namespace logging
