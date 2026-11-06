#include "ModelCatalog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>

namespace titan {
namespace {
constexpr qint64 MetadataLimit = 64 * 1024 * 1024;
constexpr quint64 StringLimit = 2 * 1024 * 1024;
constexpr int MaxFiles = 4096;
constexpr int MaxDirectories = 2048;
constexpr int MaxEntries = 100000;
constexpr int MaxDepth = 8;

class GgufReader {
public:
    explicit GgufReader(QFile &file, const std::shared_ptr<std::atomic_bool> &cancel)
        : file(file), stream(&file), cancel(cancel) {
        timer.start();
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    }
    bool ok() const {
        if ((++operations & 1023) == 0 && (cancel->load() || timer.elapsed() > 1500)) valid = false;
        return valid && stream.status() == QDataStream::Ok && file.pos() <= MetadataLimit;
    }
    template<typename T> T number() { T value{}; stream >> value; valid = ok(); return value; }
    bool skip(quint64 count) {
        if (!ok() || count > quint64(MetadataLimit) || count > quint64(std::max(qint64(0), file.size() - file.pos()))
            || file.pos() > MetadataLimit - qint64(count)) { valid = false; return false; }
        valid = file.seek(file.pos() + qint64(count));
        return valid;
    }
    QString string(bool capture) {
        const quint64 size = number<quint64>();
        if (!ok() || size > quint64(MetadataLimit)) { valid = false; return {}; }
        if (!capture || size > StringLimit) { skip(size); return {}; }
        if (size > quint64(std::max(qint64(0), file.size() - file.pos())) || file.pos() > MetadataLimit - qint64(size)) {
            valid = false; return {};
        }
        const QByteArray data = file.read(qint64(size));
        valid = quint64(data.size()) == size;
        return QString::fromUtf8(data);
    }
    QVariant value(quint32 type, bool capture, int depth = 0) {
        if (!ok() || depth > 4) { valid = false; return {}; }
        switch (type) {
        case 0: return number<quint8>();
        case 1: return number<qint8>();
        case 2: return number<quint16>();
        case 3: return number<qint16>();
        case 4: return number<quint32>();
        case 5: return number<qint32>();
        case 6: { float n{}; stream >> n; return n; }
        case 7: { const auto n = number<quint8>(); if (n > 1) valid = false; return n != 0; }
        case 8: return string(capture);
        case 9: {
            const auto itemType = number<quint32>();
            const auto count = number<quint64>();
            if (count > 2'000'000 || itemType > 12 || !ok()) { valid = false; return {}; }
            static constexpr quint64 widths[] = {1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8};
            if (const auto width = widths[itemType]; width != 0) { skip(count * width); return {}; }
            for (quint64 i = 0; i < count && ok(); ++i) value(itemType, false, depth + 1);
            return {};
        }
        case 10: return QVariant::fromValue(number<quint64>());
        case 11: return QVariant::fromValue(number<qint64>());
        case 12: { stream.setFloatingPointPrecision(QDataStream::DoublePrecision); double n{}; stream >> n;
                   stream.setFloatingPointPrecision(QDataStream::SinglePrecision); return n; }
        default: valid = false; return {};
        }
    }
private:
    QFile &file;
    QDataStream stream;
    std::shared_ptr<std::atomic_bool> cancel;
    QElapsedTimer timer;
    mutable quint64 operations = 0;
    mutable bool valid = true;
};

QString fileTypeName(int type) {
    // general.file_type values are llama_ftype values, not tensor ggml_type values.
    static const QHash<int, QString> types {
        {0, "F32"}, {1, "F16"}, {2, "Q4_0"}, {3, "Q4_1"}, {7, "Q8_0"},
        {8, "Q5_0"}, {9, "Q5_1"}, {10, "Q2_K"}, {11, "Q3_K_S"}, {12, "Q3_K_M"},
        {13, "Q3_K_L"}, {14, "Q4_K_S"}, {15, "Q4_K_M"}, {16, "Q5_K_S"},
        {17, "Q5_K_M"}, {18, "Q6_K"}, {19, "IQ2_XXS"}, {20, "IQ2_XS"},
        {21, "Q2_K_S"}, {22, "IQ3_XS"}, {23, "IQ3_XXS"}, {24, "IQ1_S"},
        {25, "IQ4_NL"}, {26, "IQ3_S"}, {27, "IQ3_M"}, {28, "IQ2_S"},
        {29, "IQ2_M"}, {30, "IQ4_XS"}, {31, "IQ1_M"}, {32, "BF16"},
        {36, "TQ1_0"}, {37, "TQ2_0"}, {38, "MXFP4_MOE"}, {39, "NVFP4"},
        {40, "Q1_0"}, {41, "Q2_0"}, {42, "F8_E4M3"}
    };
    return types.value(type, QStringLiteral("GGUF"));
}

QVariantMap readMetadata(const QFileInfo &info, const std::shared_ptr<std::atomic_bool> &cancel) {
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.read(4) != QByteArrayLiteral("GGUF")) return {};
    GgufReader reader(file, cancel);
    const auto version = reader.number<quint32>();
    const auto tensors = reader.number<quint64>();
    const auto count = reader.number<quint64>();
    if ((version != 2 && version != 3) || tensors == 0 || tensors > 10'000'000 || count > 16384) return {};
    QVariantMap metadata;
    for (quint64 i = 0; i < count && reader.ok(); ++i) {
        const QString key = reader.string(true);
        if (key.size() > 1024) return {};
        const auto type = reader.number<quint32>();
        const bool wanted = key == "general.name" || key == "general.architecture" || key == "general.file_type"
            || key == "general.size_label" || key == "general.type" || key == "split.no" || key == "split.count"
            || key.startsWith("tokenizer.chat_template");
        const QVariant value = reader.value(type, wanted);
        if (wanted && value.isValid()) metadata.insert(key, value);
    }
    if (!reader.ok()) return {};
    const QString architecture = metadata.value("general.architecture").toString().left(128);
    const QString kind = metadata.value("general.type").toString();
    const QString lower = info.fileName().toLower();
    if (architecture == "clip" || architecture == "mmproj" || kind == "adapter"
        || lower.startsWith("mmproj-") || lower.startsWith("mtp-")) return {};
    QString name = metadata.value("general.name").toString().trimmed().left(256);
    if (name.isEmpty()) name = info.completeBaseName();
    QString quantization;
    const auto quant = QRegularExpression(QStringLiteral("(?:^|[-_.])((?:I?Q|TQ)[0-9]+(?:_[A-Za-z0-9]+)*|BF16|F16|F32)(?:[-.]|$)"),
                                         QRegularExpression::CaseInsensitiveOption).match(info.fileName());
    if (quant.hasMatch()) quantization = quant.captured(1).toUpper();
    else if (metadata.contains("general.file_type")) quantization = fileTypeName(metadata.value("general.file_type").toInt());
    else quantization = "GGUF";
    bool thinking = false;
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it) {
        if (!it.key().startsWith("tokenizer.chat_template")) continue;
        const QString templ = it.value().toString();
        thinking = thinking || templ.contains("<think>") || templ.contains("[THINK]")
            || templ.contains("enable_thinking") || templ.contains("reasoning_effort")
            || (templ.contains("analysis") && templ.contains("<|channel|>"));
    }
    QVariantMap result {
        {"name", name}, {"quantization", quantization}, {"architecture", architecture.isEmpty() ? "Unknown" : architecture},
        {"supportsThinking", thinking}, {"parameterSize", metadata.value("general.size_label").toString().left(64)},
        {"splitNumber", metadata.value("split.no", 0)}, {"splitCount", metadata.value("split.count", 1)},
        {"fileSize", info.size()}, {"modified", info.lastModified().toMSecsSinceEpoch()}
    };
    return result;
}

QString discoverDirectory() {
    QStringList candidates;
    const QString environment = qEnvironmentVariable("TITAN_MODELS_PATH");
    if (!environment.isEmpty()) candidates << environment;
    const QString home = QDir::homePath();
    candidates << QCoreApplication::applicationDirPath() + "/models" << QDir::currentPath() + "/models"
               << QCoreApplication::applicationDirPath() + "/../models" << home + "/.lmstudio/models"
               << home + "/.cache/lm-studio/models" << home + "/models"
               << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Models";
    for (const auto &candidate : candidates)
        if (QFileInfo(candidate).isDir()) return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Titan Local AI/Models");
}
} // namespace

ModelCatalog::ModelCatalog(QObject *parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(600);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_debounce, &QTimer::timeout, this, &ModelCatalog::refresh);
    // Also catches changes below the watcher limit and files copied in-place.
    m_periodicRefresh.setInterval(30000);
    connect(&m_periodicRefresh, &QTimer::timeout, this, &ModelCatalog::refresh);
    m_periodicRefresh.start();
    connect(&m_future, &QFutureWatcher<ScanResult>::finished, this, [this] {
        const auto result = m_future.result();
        m_scanning = false;
        if (!m_cancel->load()) {
            m_models = result.models;
            m_cache = result.cache;
            m_warning = result.warning;
            const auto watched = m_watcher.directories();
            if (!watched.isEmpty()) m_watcher.removePaths(watched);
            // A missing configured folder can still be noticed when its parent changes.
            QStringList paths = result.directories.mid(0, 256);
            if (paths.isEmpty() && QFileInfo(QFileInfo(m_directory).absolutePath()).isDir())
                paths << QFileInfo(m_directory).absolutePath();
            if (!paths.isEmpty()) m_watcher.addPaths(paths);
            emit modelsChanged();
        }
        emit scanningChanged();
        if (m_rescan) { m_rescan = false; beginScan(); }
    });
    setDirectory(discoverDirectory());
}

ModelCatalog::~ModelCatalog() { if (m_cancel) m_cancel->store(true); }

void ModelCatalog::setDirectory(const QString &directory) {
    const QString path = directory.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    if (path == m_directory) return;
    m_directory = path;
    m_cache.clear();
    m_models.clear();
    emit directoryChanged();
    emit modelsChanged();
    if (m_scanning) { m_cancel->store(true); m_rescan = true; }
    else beginScan();
}

void ModelCatalog::refresh() {
    if (m_scanning) { m_rescan = true; return; }
    beginScan();
}

void ModelCatalog::beginScan() {
    m_scanning = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const QString directory = m_directory;
    const auto cache = m_cache;
    const auto cancel = m_cancel;
    emit scanningChanged();
    m_future.setFuture(QtConcurrent::run([directory, cache, cancel] { return scan(directory, cache, cancel); }));
}

ModelCatalog::ScanResult ModelCatalog::scan(const QString &directory, const QHash<QString, QVariantMap> &cache,
                                           const std::shared_ptr<std::atomic_bool> &cancel) {
    ScanResult result;
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
        result.warning = "Choose a folder containing local GGUF models."; return result;
    }
    QList<QPair<QString, int>> pending{{directory, 0}};
    QElapsedTimer timer; timer.start();
    int entryCount = 0, fileCount = 0, skipped = 0;
    bool bounded = false;
    const QRegularExpression split(QStringLiteral("^(.*)-(\\d{5})-of-(\\d{5})\\.gguf$"), QRegularExpression::CaseInsensitiveOption);
    while (!pending.isEmpty() && !cancel->load()) {
        const auto [folder, depth] = pending.takeFirst();
        result.directories << folder;
        QDirIterator entries(folder, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::NoIteratorFlags);
        while (entries.hasNext() && !cancel->load()) {
            if (++entryCount > MaxEntries || timer.elapsed() > 15000) { bounded = true; break; }
            entries.next();
            const auto info = entries.fileInfo();
            if (info.isSymLink() || info.isJunction()) continue;
            if (info.isDir()) {
                if (depth < MaxDepth && result.directories.size() + pending.size() < MaxDirectories)
                    pending.append({info.absoluteFilePath(), depth + 1});
                else bounded = true;
                continue;
            }
            if (!info.isFile() || info.suffix().compare("gguf", Qt::CaseInsensitive) != 0) continue;
            if (++fileCount > MaxFiles) { bounded = true; break; }
            const QString path = info.absoluteFilePath();
            const auto shard = split.match(info.fileName());
            if (shard.hasMatch() && shard.captured(2).toInt() != 1) continue;
            QVariantMap model = cache.value(path);
            if (model.value("fileSize").toLongLong() != info.size()
                || model.value("modified").toLongLong() != info.lastModified().toMSecsSinceEpoch()) model.clear();
            if (model.isEmpty()) model = readMetadata(info, cancel);
            if (model.isEmpty()) { ++skipped; continue; }
            result.cache.insert(path, model);
            if (model.value("splitNumber").toInt() != 0) continue;
            qint64 bytes = info.size();
            bool complete = true;
            int shards = model.value("splitCount", 1).toInt();
            if (shard.hasMatch()) {
                shards = shard.captured(3).toInt();
                if (shards < 1 || shards > 1024) { ++skipped; continue; }
                for (int index = 2; index <= shards; ++index) {
                    const QString part = QStringLiteral("%1-%2-of-%3.gguf").arg(shard.captured(1))
                        .arg(index, 5, 10, QLatin1Char('0')).arg(shards, 5, 10, QLatin1Char('0'));
                    const QFileInfo partInfo(info.dir().filePath(part));
                    if (!partInfo.isFile() || partInfo.isSymLink()) complete = false;
                    else bytes += partInfo.size();
                }
            } else if (shards > 1) complete = false;
            model.insert("id", QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(24)));
            model.insert("path", path);
            model.insert("sizeLabel", QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat));
            model.insert("bytes", bytes);
            model.insert("shards", shards);
            model.insert("loadable", complete);
            if (!complete) model.insert("warning", "One or more GGUF shards are missing. Keep all parts in the same folder.");
            result.models << model;
        }
        if (entryCount > MaxEntries || fileCount > MaxFiles || timer.elapsed() > 15000) break;
    }
    std::sort(result.models.begin(), result.models.end(), [](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0;
    });
    if (bounded) result.warning = "Scan limit reached. Select a more specific models folder to see additional files.";
    else if (skipped) result.warning = QStringLiteral("Skipped %1 invalid, unsupported, or auxiliary GGUF file(s).").arg(skipped);
    return result;
}
} // namespace titan
