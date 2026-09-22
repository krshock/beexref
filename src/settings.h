#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <functional>

namespace settings {

// Sets the base directory for settings, log and cache files, mirroring
// the Python --settings-dir option. An empty directory restores the
// default locations.
void setSettingsDir(const QString &dir);

QString configDir();
QString iniPath();
QString logPath();
QString cacheDir();

// INI file in the format shared by the Python and Go ports: sections,
// key=value lines, arrays as "1\path=..." plus "size". Writes are
// deferred to sync().
class File
{
public:
    explicit File(QString path);

    void load();
    QString path() const;

    bool contains(const QString &section, const QString &key) const;
    QString value(const QString &section, const QString &key,
                  const QString &def = QString()) const;
    bool boolValue(const QString &section, const QString &key, bool def) const;

    void setValue(const QString &section, const QString &key, const QString &value);
    void remove(const QString &section, const QString &key);
    void removeSection(const QString &section);

    // Writes the file atomically, sections and keys sorted.
    bool sync();

    // Recent file list in order. With existingOnly, entries whose file
    // no longer exists are dropped.
    QStringList recentFiles(bool existingOnly = false) const;

    // Moves filename to the front of the list (max 10) and persists.
    void updateRecentFiles(const QString &filename);

private:
    QString path_;
    QMap<QString, QMap<QString, QString>> sections_;
};

// One configurable setting, mirroring the reference's FIELDS table.
struct FieldSpec
{
    QString key; // "Section/key"
    QVariant defaultValue;
    std::function<QVariant(const QVariant &)> cast;    // optional
    std::function<bool(const QVariant &)> validate;    // optional
};

const QVector<FieldSpec> &fields();

// Typed value with the reference's semantics: a missing value or a
// failed cast/validation yields the default.
QVariant valueOrDefault(const File &file, const QString &key);

// Whether the value differs from its default.
bool valueChanged(const File &file, const QString &key);

// Removes every field, restoring defaults; the caller syncs the file.
void restoreDefaults(File &file);

} // namespace settings
