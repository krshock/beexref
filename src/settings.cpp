#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <utility>

namespace settings {
namespace {

QString g_settingsDir;

QString defaultConfigDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!base.isEmpty())
        return base + QStringLiteral("/BeeXRef");
    return QDir::homePath() + QStringLiteral("/.config/BeeXRef");
}

QString defaultCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (!base.isEmpty())
        return base + QStringLiteral("/BeeXRef");
    return QDir::tempPath() + QStringLiteral("/BeeXRef");
}

bool parseBool(const QString &text, bool &value)
{
    if (text == QLatin1String("1") || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("t"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0) {
        value = true;
        return true;
    }
    if (text == QLatin1String("0") || text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("f"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("no"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) {
        value = false;
        return true;
    }
    (void)value;
    return false;
}

} // namespace

void setSettingsDir(const QString &dir)
{
    g_settingsDir = dir;
}

QString configDir()
{
    return g_settingsDir.isEmpty() ? defaultConfigDir() : g_settingsDir;
}

QString iniPath()
{
    return configDir() + QStringLiteral("/BeeXRef.ini");
}

QString logPath()
{
    return configDir() + QStringLiteral("/BeeXRef.log");
}

QString cacheDir()
{
    return g_settingsDir.isEmpty() ? defaultCacheDir() : configDir() + QStringLiteral("/cache");
}

File::File(QString path)
    : path_(std::move(path))
{
}

void File::load()
{
    sections_.clear();

    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    QString section;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))
            || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (section.isEmpty() || key.isEmpty())
            continue;
        sections_[section][key] = value;
    }
}

QString File::path() const
{
    return path_;
}

bool File::contains(const QString &section, const QString &key) const
{
    const auto sit = sections_.constFind(section);
    return sit != sections_.constEnd() && sit->contains(key);
}

QString File::value(const QString &section, const QString &key, const QString &def) const
{
    const auto sit = sections_.constFind(section);
    if (sit == sections_.constEnd())
        return def;
    const auto kit = sit->constFind(key);
    return kit == sit->constEnd() ? def : kit.value();
}

bool File::boolValue(const QString &section, const QString &key, bool def) const
{
    bool parsed = def;
    if (!parseBool(value(section, key), parsed))
        return def;
    return parsed;
}

void File::setValue(const QString &section, const QString &key, const QString &value)
{
    sections_[section][key] = value;
}

void File::remove(const QString &section, const QString &key)
{
    const auto sit = sections_.find(section);
    if (sit == sections_.end())
        return;
    sit->remove(key);
    if (sit->isEmpty())
        sections_.erase(sit);
}

void File::removeSection(const QString &section)
{
    sections_.remove(section);
}

bool File::sync()
{
    const QFileInfo info(path_);
    if (!QDir().mkpath(info.absolutePath()))
        return false;

    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    for (auto sit = sections_.cbegin(); sit != sections_.cend(); ++sit) {
        out << '[' << sit.key() << "]\n";
        for (auto kit = sit->cbegin(); kit != sit->cend(); ++kit)
            out << kit.key() << '=' << kit.value() << '\n';
        out << '\n';
    }
    out.flush();
    return file.commit();
}

QStringList File::recentFiles(bool existingOnly) const
{
    const auto sit = sections_.constFind(QStringLiteral("RecentFiles"));
    if (sit == sections_.constEnd())
        return {};

    static const QString suffix = QStringLiteral("\\path");
    QMap<int, QString> indexed;
    for (auto kit = sit->cbegin(); kit != sit->cend(); ++kit) {
        if (!kit.key().endsWith(suffix))
            continue;
        bool ok = false;
        const int index = kit.key().left(kit.key().size() - suffix.size()).toInt(&ok);
        if (ok)
            indexed.insert(index, kit.value());
    }

    QStringList values;
    for (const QString &value : std::as_const(indexed)) {
        if (!existingOnly || QFileInfo::exists(value))
            values.append(value);
    }
    return values;
}

void File::updateRecentFiles(const QString &filename)
{
    const QString absolute = QFileInfo(filename).absoluteFilePath();

    QStringList values = recentFiles();
    values.removeAll(absolute);
    values.prepend(absolute);
    while (values.size() > 10)
        values.removeLast();

    removeSection(QStringLiteral("RecentFiles"));
    for (qsizetype i = 0; i < values.size(); ++i) {
        setValue(QStringLiteral("RecentFiles"), QStringLiteral("%1\\path").arg(i + 1),
                 values.at(i));
    }
    setValue(QStringLiteral("RecentFiles"), QStringLiteral("size"),
             QString::number(values.size()));
    sync();
}

} // namespace settings
