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

namespace {

QString sectionOfKey(const QString &key)
{
    return key.left(key.indexOf(QLatin1Char('/')));
}

QString nameOfKey(const QString &key)
{
    return key.mid(key.indexOf(QLatin1Char('/')) + 1);
}

std::function<QVariant(const QVariant &)> boolCast()
{
    return [](const QVariant &value) -> QVariant {
        bool parsed = false;
        if (!parseBool(value.toString(), parsed))
            return {};
        return parsed;
    };
}

std::function<QVariant(const QVariant &)> intCast()
{
    return [](const QVariant &value) -> QVariant {
        bool ok = false;
        const int number = value.toString().toInt(&ok);
        return ok ? QVariant(number) : QVariant();
    };
}

std::function<bool(const QVariant &)> oneOf(const QStringList &allowed)
{
    return [allowed](const QVariant &value) { return allowed.contains(value.toString()); };
}

std::function<bool(const QVariant &)> atLeast(int minimum)
{
    return [minimum](const QVariant &value) { return value.toInt() >= minimum; };
}

const FieldSpec *findField(const QString &key)
{
    for (const FieldSpec &field : fields()) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

} // namespace

const QVector<FieldSpec> &fields()
{
    static const QVector<FieldSpec> specs = {
        {QStringLiteral("Save/confirm_close_unsaved"), true, boolCast(), nullptr},
        {QStringLiteral("Save/incremental"), true, boolCast(), nullptr},
        {QStringLiteral("Items/grayscale_method"), QStringLiteral("classic"), nullptr, nullptr},
        {QStringLiteral("Items/image_storage_format"), QStringLiteral("best"), nullptr,
         oneOf({QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("best")})},
        {QStringLiteral("Items/arrange_gap"), 0, intCast(),
         [](const QVariant &value) { return value.toInt() >= 0 && value.toInt() <= 200; }},
        {QStringLiteral("Items/arrange_default"), QStringLiteral("optimal"), nullptr,
         oneOf({QStringLiteral("optimal"), QStringLiteral("horizontal"),
                QStringLiteral("vertical"), QStringLiteral("square")})},
        {QStringLiteral("Items/double_click_spotlight"), true, boolCast(), nullptr},
        {QStringLiteral("Items/image_allocation_limit"), 256, intCast(), atLeast(0)},
        {QStringLiteral("Items/lod_method"), QStringLiteral("fixed"), nullptr,
         oneOf({QStringLiteral("single"), QStringLiteral("fixed"),
                QStringLiteral("ram_budget")})},
        {QStringLiteral("Items/lod_fractions"), QStringLiteral("1,0.5,0.25,0.125,0.0625"), nullptr,
         [](const QVariant &value) { return !value.toString().isEmpty(); }},
        {QStringLiteral("Items/lod_ram_budget_mb"), 1024, intCast(), atLeast(1)},
        // Always-on cap on the decoded LOD bytes the manager keeps; 0 is
        // unlimited. The ram_budget method tightens it further.
        {QStringLiteral("Items/lod_primary_budget_mb"), 0, intCast(), atLeast(0)},
        // In-RAM LRU of decoded levels held by the decode worker; 0
        // disables it.
        {QStringLiteral("Items/lod_ram_cache_mb"), 150, intCast(), atLeast(0)},
        // Decode worker threads; always at least one.
        {QStringLiteral("Items/lod_decode_threads"), 3, intCast(), atLeast(1)},
        // Percent of the off-screen level cache released every 10 s of
        // inactivity; 0 disables settling.
        {QStringLiteral("Items/lod_cache_settle_percent"), 30, intCast(),
         [](const QVariant &value) { return value.toInt() >= 0 && value.toInt() <= 50; }},
        {QStringLiteral("Items/lod_quality"), QStringLiteral("smooth"), nullptr,
         oneOf({QStringLiteral("fast"), QStringLiteral("smooth")})},
        {QStringLiteral("Items/undo_cache"), true, boolCast(), nullptr},
    };
    return specs;
}

QVariant valueOrDefault(const File &file, const QString &key)
{
    const FieldSpec *field = findField(key);
    if (!field)
        return {};

    const QString section = sectionOfKey(key);
    const QString name = nameOfKey(key);
    if (!file.contains(section, name))
        return field->defaultValue;

    QVariant value = file.value(section, name);
    if (field->cast) {
        value = field->cast(value);
        if (!value.isValid())
            return field->defaultValue;
    }
    if (field->validate && !field->validate(value))
        return field->defaultValue;
    return value;
}

bool valueChanged(const File &file, const QString &key)
{
    const FieldSpec *field = findField(key);
    if (!field)
        return false;
    return valueOrDefault(file, key) != field->defaultValue;
}

void restoreDefaults(File &file)
{
    for (const FieldSpec &field : fields())
        file.remove(sectionOfKey(field.key), nameOfKey(field.key));
}

} // namespace settings
