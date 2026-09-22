// Format gate: migrates a board on a copy, decodes every image blob,
// cross-checks decoded dimensions against the recorded original sizes,
// round-trips through VACUUM INTO and diffs all tables. With
// --write-back it re-saves the loaded board through the streaming
// writer and validates the result, so the writer can be checked
// against the other ports.
#include <QBuffer>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QTextStream>

#include "board/board.h"
#include "board/schema.h"
#include "board/sqlite.h"
#include "board/write.h"

namespace {

struct Totals
{
    int items = 0;
    int pixmaps = 0;
    int decoded = 0;
    int decodeFail = 0;
    int dimMismatch = 0;
    int countMismatch = 0;
    int failures = 0;
};

board::Status roundTrip(const QString &src, const QString &dst)
{
    auto db = board::Connection::open(src, board::Connection::OpenMode::ReadWrite);
    if (!db)
        return db.error();
    QFile::remove(dst);
    return db.value().exec(
        QStringLiteral("VACUUM INTO %1").arg(board::Connection::quote(dst)));
}

// Row-by-row comparison through ATTACH plus the header fields; an empty
// result means identical.
board::Result<QHash<QString, qint64>> diff(const QString &origPath, const QString &rtPath)
{
    auto db = board::Connection::open(QStringLiteral(":memory:"),
                                    board::Connection::OpenMode::Create);
    if (!db)
        return db.error();

    for (const auto &attach :
         {std::pair<const char *, QString>{"orig", origPath}, {"rt", rtPath}}) {
        auto statement = db.value().prepare(
            QStringLiteral("ATTACH DATABASE ? AS %1").arg(QLatin1String(attach.first)));
        if (!statement)
            return statement.error();
        if (board::Status status = statement.value().bind(1, attach.second); !status)
            return status.error();
        if (board::Status status = statement.value().exec(); !status)
            return status.error();
    }

    QHash<QString, qint64> differences;
    for (const QString &table : {QStringLiteral("items"), QStringLiteral("sqlar"),
                                 QStringLiteral("lod")}) {
        auto statement = db.value().prepare(
            QStringLiteral("SELECT "
                           "(SELECT COUNT(*) FROM (SELECT * FROM orig.%1 EXCEPT "
                           "SELECT * FROM rt.%1)) "
                           "+ (SELECT COUNT(*) FROM (SELECT * FROM rt.%1 EXCEPT "
                           "SELECT * FROM orig.%1))")
                .arg(table));
        if (!statement)
            return statement.error();
        auto row = statement.value().step();
        if (!row)
            return row.error();
        const qint64 differing = row.value() ? statement.value().columnInt64(0) : 0;
        if (differing != 0)
            differences.insert(table, differing);
    }

    // Compare user_version and application_id of both attached files.
    for (const QString &field :
         {QStringLiteral("user_version"), QStringLiteral("application_id")}) {
        const auto read = [&db, &field](const QString &schema) -> board::Result<qint64> {
            auto statement =
                db.value().prepare(QStringLiteral("PRAGMA %1.%2").arg(schema, field));
            if (!statement)
                return statement.error();
            auto row = statement.value().step();
            if (!row)
                return row.error();
            return statement.value().columnInt64(0);
        };
        const auto orig = read(QStringLiteral("orig"));
        const auto rt = read(QStringLiteral("rt"));
        if (!orig || !rt)
            return orig ? rt.error() : orig.error();
        if (orig.value() != rt.value())
            differences.insert(field, orig.value() - rt.value());
    }
    return differences;
}

QString diffSummary(const QHash<QString, qint64> &diff)
{
    if (diff.isEmpty())
        return QStringLiteral("identical");
    QStringList parts;
    for (const QString &key :
         {QStringLiteral("items"), QStringLiteral("sqlar"), QStringLiteral("lod"),
          QStringLiteral("user_version"), QStringLiteral("application_id")}) {
        if (diff.contains(key))
            parts << QStringLiteral("%1:%2").arg(key).arg(diff.value(key));
    }
    return parts.join(QLatin1Char(' '));
}

// Re-saves the loaded board through the streaming writer and validates
// the result, so the writer can be checked against the other ports.
bool writeBack(const QString &path, const QString &destination, board::Board &prepared,
               const QVector<board::ItemRow> &items, QHash<qint64, QSize> sizes,
               QHash<qint64, board::FloorLevel> floors, QTextStream &out)
{
    QVector<board::Record> records;
    records.reserve(items.size());
    for (const board::ItemRow &item : items) {
        board::Record record;
        record.saveId = item.id;
        record.type = item.type;
        record.x = item.x;
        record.y = item.y;
        record.z = item.z;
        record.scale = item.scale;
        record.rotation = item.rotation;
        record.flip = static_cast<double>(item.flip);
        record.dataJson = item.data;
        record.metaJson = item.meta;
        record.uuid = item.uuid;
        if (item.type == QLatin1String("pixmap")) {
            const qint64 id = item.id;
            record.pixmapSource = [&prepared, id]() {
                auto blob = prepared.blob(id);
                return blob.isOk() ? blob.take() : QByteArray();
            };
            if (floors.contains(id)) {
                const board::FloorLevel &floor = floors.value(id);
                record.floorData = floor.data;
                record.floorFraction = floor.fraction;
                record.floorFormat = floor.format;
                const QSize size = sizes.value(id);
                record.origW = size.width();
                record.origH = size.height();
            }
        }
        records.append(record);
    }

    const auto status = board::save(destination, records, true);
    if (!status) {
        out << "  write-back: " << status.error().toString() << "\n";
        return false;
    }

    auto check = board::Board::open(destination, QDir::tempPath());
    if (!check) {
        out << "  write-back check: " << check.error().toString() << "\n";
        return false;
    }
    auto writtenCounts = check.value().counts();
    auto originalCounts = prepared.counts();
    if (!writtenCounts || !originalCounts
        || writtenCounts.value().value(QStringLiteral("items"))
            != originalCounts.value().value(QStringLiteral("items"))
        || writtenCounts.value().value(QStringLiteral("sqlar"))
            != originalCounts.value().value(QStringLiteral("sqlar"))) {
        out << "  write-back: row counts differ\n";
        return false;
    }
    const auto version = board::schema::readUserVersion(check.value().connection());
    if (!version || version.value() != board::schema::kUserVersion) {
        out << "  write-back: wrong version\n";
        return false;
    }
    Q_UNUSED(path);
    return true;
}

bool checkBoard(const QString &path, const QString &writeBackPath, Totals &totals)
{
    QTextStream out(stdout);
    const QString name = QFileInfo(path).fileName();
    const QString prefix = name.leftJustified(28, QLatin1Char(' '));

    auto orig = board::Connection::open(path, board::Connection::OpenMode::ReadOnly);
    if (!orig) {
        out << prefix << " ERROR " << orig.error().toString() << "\n";
        ++totals.failures;
        return false;
    }
    auto origCounts = board::counts(orig.value());
    auto fromVersion = board::schema::readUserVersion(orig.value());
    if (!origCounts || !fromVersion) {
        out << prefix << " ERROR "
            << (origCounts ? fromVersion.error().toString() : origCounts.error().toString())
            << "\n";
        ++totals.failures;
        return false;
    }

    auto prepared = board::Board::prepare(path, QDir::tempPath());
    if (!prepared) {
        out << prefix << " ERROR " << prepared.error().toString() << "\n";
        ++totals.failures;
        return false;
    }
    const QString tempPath = prepared.value().tempPath();

    auto toVersion = board::schema::readUserVersion(prepared.value().connection());
    auto counts = prepared.value().counts();
    auto items = prepared.value().items();
    auto sizes = prepared.value().originalSizes();
    auto floors = prepared.value().floorLevels();
    if (!toVersion || !counts || !items || !sizes || !floors) {
        out << prefix << " ERROR reading migrated copy\n";
        ++totals.failures;
        return false;
    }

    int itemsCount = items.value().size();
    int pixmaps = 0;
    int decoded = 0;
    int decodeFail = 0;
    int dimMismatch = 0;
    int countMismatch = 0;

    if (counts.value().value(QStringLiteral("items"))
            != origCounts.value().value(QStringLiteral("items"))
        || counts.value().value(QStringLiteral("sqlar"))
            != origCounts.value().value(QStringLiteral("sqlar"))) {
        countMismatch = 1;
        out << "  " << name << ": counts changed items "
            << origCounts.value().value(QStringLiteral("items")) << "->"
            << counts.value().value(QStringLiteral("items")) << " sqlar "
            << origCounts.value().value(QStringLiteral("sqlar")) << "->"
            << counts.value().value(QStringLiteral("sqlar")) << "\n";
    }

    for (const board::ItemRow &item : items.value()) {
        if (item.type != QLatin1String("pixmap"))
            continue;
        ++pixmaps;
        auto blob = prepared.value().blob(item.id);
        if (!blob || blob.value().isEmpty()) {
            ++decodeFail;
            out << "  item " << item.id << ": no blob\n";
            continue;
        }
        QBuffer buffer;
        buffer.setData(blob.value());
        if (!buffer.open(QIODevice::ReadOnly)) {
            ++decodeFail;
            out << "  item " << item.id << ": cannot open blob\n";
            continue;
        }
        QImageReader reader(&buffer);
        const QImage image = reader.read();
        if (image.isNull()) {
            ++decodeFail;
            out << "  item " << item.id << ": decode failed\n";
            continue;
        }
        ++decoded;
        const QSize expected = sizes.value().value(item.id);
        if (expected.isValid() && expected != image.size()) {
            ++dimMismatch;
            out << "  item " << item.id << ": size " << image.width() << "x" << image.height()
                << " decoded, lod says " << expected.width() << "x" << expected.height() << "\n";
        }
    }

    const QString rtPath = tempPath + QStringLiteral(".rt");
    QHash<QString, qint64> differences;
    QString roundTripText = QStringLiteral("identical");
    if (board::Status status = roundTrip(tempPath, rtPath); !status) {
        out << "  roundtrip: " << status.error().toString() << "\n";
        roundTripText = QStringLiteral("error");
        ++countMismatch;
    } else {
        auto diffResult = diff(tempPath, rtPath);
        if (!diffResult) {
            out << "  diff: " << diffResult.error().toString() << "\n";
            roundTripText = QStringLiteral("error");
            ++countMismatch;
        } else {
            differences = diffResult.take();
            roundTripText = diffSummary(differences);
            if (!differences.isEmpty())
                ++countMismatch;
        }
        QFile::remove(rtPath);
    }

    bool wroteBack = true;
    QString writeBackText;
    if (!writeBackPath.isEmpty()) {
        wroteBack = writeBack(path, writeBackPath, prepared.value(), items.value(), sizes.take(),
                              floors.take(), out);
        writeBackText = wroteBack ? QStringLiteral(" write_back=ok")
                                  : QStringLiteral(" write_back=FAIL");
    }

    const bool ok = decodeFail == 0 && dimMismatch == 0 && countMismatch == 0 && wroteBack;
    QString migration;
    if (fromVersion.value() != toVersion.value())
        migration = QStringLiteral(" v%1->v%2").arg(fromVersion.value()).arg(toVersion.value());

    out << prefix << " items=" << QString::number(itemsCount).leftJustified(5)
        << " pixmaps=" << QString::number(pixmaps).leftJustified(5)
        << " decoded=" << QString::number(decoded).leftJustified(5)
        << " decode_fail=" << QString::number(decodeFail).leftJustified(3)
        << " dim_mismatch=" << QString::number(dimMismatch).leftJustified(3)
        << " counts=" << counts.value().value(QStringLiteral("items")) << "/"
        << counts.value().value(QStringLiteral("sqlar")) << "/"
        << counts.value().value(QStringLiteral("lod")) << migration
        << " roundtrip=" << roundTripText << writeBackText
        << (ok ? QStringLiteral(" ok") : QStringLiteral(" FAIL")) << "\n";

    totals.items += itemsCount;
    totals.pixmaps += pixmaps;
    totals.decoded += decoded;
    totals.decodeFail += decodeFail;
    totals.dimMismatch += dimMismatch;
    totals.countMismatch += countMismatch;
    if (!ok)
        ++totals.failures;
    return ok;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Format gate: migrate, decode, cross-check and round-trip board files."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("boards"), QStringLiteral("Board files to check."));
    QCommandLineOption writeBackOption(
        QStringLiteral("write-back"),
        QStringLiteral("Re-save the loaded board through the writer to <path> and validate it."),
        QStringLiteral("path"));
    parser.addOption(writeBackOption);
    parser.process(app);

    const QStringList paths = parser.positionalArguments();
    if (paths.isEmpty()) {
        QTextStream(stderr) << "usage: beexref-boardcheck [--write-back PATH] <board>...\n";
        return 2;
    }

    board::sweepStaleTempFiles(QDir::tempPath());

    Totals totals;
    for (const QString &path : paths)
        checkBoard(path, parser.value(writeBackOption), totals);

    QTextStream out(stdout);
    out << "\nTOTAL items=" << totals.items << " pixmaps=" << totals.pixmaps
        << " decoded=" << totals.decoded << " decode_fail=" << totals.decodeFail
        << " dim_mismatch=" << totals.dimMismatch << " count_mismatch=" << totals.countMismatch
        << " board_failures=" << totals.failures << "\n";
    return totals.failures == 0 ? 0 : 1;
}
