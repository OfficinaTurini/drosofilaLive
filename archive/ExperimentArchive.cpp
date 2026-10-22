#include "ExperimentArchive.h"
#include "../model/CausalCircuit.h"

#include <QByteArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace DrosofilaLive
{
namespace
{
// -----------------------------------------------------------------------------
// Minimal ZIP store reader/writer.
// Only method 0 (stored, no compression) is used. This keeps .dro archives
// standard ZIP files while adding no external runtime dependency.
// -----------------------------------------------------------------------------
struct ZipEntry
{
    QByteArray name;
    QByteArray data;
    quint32 crc = 0;
    quint32 localOffset = 0;
};

void appendU16(QByteArray& out, quint16 v)
{
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
}

void appendU32(QByteArray& out, quint32 v)
{
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
    out.append(static_cast<char>((v >> 16) & 0xff));
    out.append(static_cast<char>((v >> 24) & 0xff));
}

bool readU16(const QByteArray& in, int offset, quint16* value)
{
    if (!value || offset < 0 || offset + 2 > in.size()) return false;
    const uchar* p = reinterpret_cast<const uchar*>(in.constData() + offset);
    *value = static_cast<quint16>(p[0] | (static_cast<quint16>(p[1]) << 8));
    return true;
}

bool readU32(const QByteArray& in, int offset, quint32* value)
{
    if (!value || offset < 0 || offset + 4 > in.size()) return false;
    const uchar* p = reinterpret_cast<const uchar*>(in.constData() + offset);
    *value = static_cast<quint32>(p[0]) |
             (static_cast<quint32>(p[1]) << 8) |
             (static_cast<quint32>(p[2]) << 16) |
             (static_cast<quint32>(p[3]) << 24);
    return true;
}

quint32 crc32(const QByteArray& data)
{
    quint32 crc = 0xffffffffu;
    for (int i = 0; i < data.size(); ++i)
    {
        crc ^= static_cast<uchar>(data.at(i));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & static_cast<quint32>(-(static_cast<int>(crc & 1u))));
    }
    return crc ^ 0xffffffffu;
}

bool writeZipStore(const QString& filePath, QVector<ZipEntry> entries, QString* error)
{
    QByteArray out;
    for (int i = 0; i < entries.size(); ++i)
    {
        ZipEntry& e = entries[i];
        e.crc = crc32(e.data);
        e.localOffset = static_cast<quint32>(out.size());
        appendU32(out, 0x04034b50u);
        appendU16(out, 20);       // version needed
        appendU16(out, 0x0800);   // UTF-8 filename
        appendU16(out, 0);        // stored
        appendU16(out, 0);        // time
        appendU16(out, 0);        // date
        appendU32(out, e.crc);
        appendU32(out, static_cast<quint32>(e.data.size()));
        appendU32(out, static_cast<quint32>(e.data.size()));
        appendU16(out, static_cast<quint16>(e.name.size()));
        appendU16(out, 0);
        out.append(e.name);
        out.append(e.data);
    }

    const quint32 centralOffset = static_cast<quint32>(out.size());
    for (int i = 0; i < entries.size(); ++i)
    {
        const ZipEntry& e = entries.at(i);
        appendU32(out, 0x02014b50u);
        appendU16(out, 20);       // version made by
        appendU16(out, 20);       // version needed
        appendU16(out, 0x0800);   // UTF-8 filename
        appendU16(out, 0);        // stored
        appendU16(out, 0);
        appendU16(out, 0);
        appendU32(out, e.crc);
        appendU32(out, static_cast<quint32>(e.data.size()));
        appendU32(out, static_cast<quint32>(e.data.size()));
        appendU16(out, static_cast<quint16>(e.name.size()));
        appendU16(out, 0); // extra
        appendU16(out, 0); // comment
        appendU16(out, 0); // disk
        appendU16(out, 0); // internal attrs
        appendU32(out, 0); // external attrs
        appendU32(out, e.localOffset);
        out.append(e.name);
    }

    const quint32 centralSize = static_cast<quint32>(out.size()) - centralOffset;
    appendU32(out, 0x06054b50u);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, static_cast<quint16>(entries.size()));
    appendU16(out, static_cast<quint16>(entries.size()));
    appendU32(out, centralSize);
    appendU32(out, centralOffset);
    appendU16(out, 0);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error) *error = QStringLiteral("Cannot create archive: %1").arg(file.errorString());
        return false;
    }
    if (file.write(out) != out.size())
    {
        if (error) *error = QStringLiteral("Archive write failed: %1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        if (error) *error = QStringLiteral("Cannot commit archive: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool readZipStore(const QString& filePath, QHash<QString, QByteArray>* files, QString* error)
{
    if (!files) return false;
    files->clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error) *error = QStringLiteral("Cannot open archive: %1").arg(file.errorString());
        return false;
    }
    const QByteArray in = file.readAll();
    if (in.size() < 22)
    {
        if (error) *error = QStringLiteral("Archive is too small to be a ZIP file.");
        return false;
    }

    int eocd = -1;
    const int first = std::max(0, in.size() - (65535 + 22));
    for (int i = in.size() - 22; i >= first; --i)
    {
        quint32 sig = 0;
        if (readU32(in, i, &sig) && sig == 0x06054b50u)
        {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
    {
        if (error) *error = QStringLiteral("ZIP end-of-central-directory not found.");
        return false;
    }

    quint16 entryCount = 0;
    quint32 centralSize = 0;
    quint32 centralOffset = 0;
    if (!readU16(in, eocd + 10, &entryCount) ||
        !readU32(in, eocd + 12, &centralSize) ||
        !readU32(in, eocd + 16, &centralOffset))
    {
        if (error) *error = QStringLiteral("Invalid ZIP directory header.");
        return false;
    }
    if (centralOffset + centralSize > static_cast<quint32>(in.size()))
    {
        if (error) *error = QStringLiteral("ZIP central directory is outside the file.");
        return false;
    }

    int pos = static_cast<int>(centralOffset);
    for (quint16 n = 0; n < entryCount; ++n)
    {
        quint32 sig = 0, crc = 0, compressed = 0, uncompressed = 0, localOffset = 0;
        quint16 flags = 0, method = 0, nameLen = 0, extraLen = 0, commentLen = 0;
        if (!readU32(in, pos, &sig) || sig != 0x02014b50u ||
            !readU16(in, pos + 8, &flags) || !readU16(in, pos + 10, &method) ||
            !readU32(in, pos + 16, &crc) || !readU32(in, pos + 20, &compressed) ||
            !readU32(in, pos + 24, &uncompressed) || !readU16(in, pos + 28, &nameLen) ||
            !readU16(in, pos + 30, &extraLen) || !readU16(in, pos + 32, &commentLen) ||
            !readU32(in, pos + 42, &localOffset))
        {
            if (error) *error = QStringLiteral("Invalid ZIP central-directory entry.");
            return false;
        }
        if (method != 0 || compressed != uncompressed)
        {
            if (error) *error = QStringLiteral("Unsupported compressed entry in .dro archive.");
            return false;
        }
        if (pos + 46 + nameLen + extraLen + commentLen > in.size())
        {
            if (error) *error = QStringLiteral("Truncated ZIP central-directory entry.");
            return false;
        }
        const QByteArray nameBytes = in.mid(pos + 46, nameLen);
        const QString name = QString::fromUtf8(nameBytes);

        quint32 localSig = 0;
        quint16 localNameLen = 0, localExtraLen = 0, localMethod = 0;
        if (!readU32(in, static_cast<int>(localOffset), &localSig) || localSig != 0x04034b50u ||
            !readU16(in, static_cast<int>(localOffset) + 8, &localMethod) || localMethod != 0 ||
            !readU16(in, static_cast<int>(localOffset) + 26, &localNameLen) ||
            !readU16(in, static_cast<int>(localOffset) + 28, &localExtraLen))
        {
            if (error) *error = QStringLiteral("Invalid local ZIP entry for %1.").arg(name);
            return false;
        }
        const quint32 dataOffset = localOffset + 30u + localNameLen + localExtraLen;
        if (dataOffset + uncompressed > static_cast<quint32>(in.size()))
        {
            if (error) *error = QStringLiteral("Truncated ZIP data for %1.").arg(name);
            return false;
        }
        const QByteArray data = in.mid(static_cast<int>(dataOffset), static_cast<int>(uncompressed));
        if (crc32(data) != crc)
        {
            if (error) *error = QStringLiteral("CRC mismatch for %1.").arg(name);
            return false;
        }
        files->insert(name, data);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

QString u64(quint64 v) { return QString::number(static_cast<qulonglong>(v)); }
quint64 parseU64(const QJsonValue& v)
{
    if (v.isString()) return v.toString().toULongLong();
    return static_cast<quint64>(v.toDouble());
}

QJsonArray rootsToJson(const QVector<std::uint64_t>& roots)
{
    QJsonArray a;
    for (int i = 0; i < roots.size(); ++i)
        a.append(QString::number(static_cast<qulonglong>(roots.at(i))));
    return a;
}

QVector<std::uint64_t> rootsFromJson(const QJsonValue& value)
{
    QVector<std::uint64_t> out;
    const QJsonArray a = value.toArray();
    for (int i = 0; i < a.size(); ++i)
        out.append(static_cast<std::uint64_t>(parseU64(a.at(i))));
    return out;
}

QJsonObject neuronSummaryToJson(const ExperimentNeuronSummary& n)
{
    QJsonObject o;
    o.insert(QStringLiteral("rootId"), QString::number(static_cast<qulonglong>(n.rootId)));
    o.insert(QStringLiteral("type"), n.type);
    o.insert(QStringLiteral("meanHz"), n.meanHz);
    o.insert(QStringLiteral("meanFirstSpikeMs"), n.meanFirstSpikeMs);
    o.insert(QStringLiteral("activationProbability"), n.activationProbability);
    return o;
}

ExperimentNeuronSummary neuronSummaryFromJson(const QJsonObject& o)
{
    ExperimentNeuronSummary n;
    n.rootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootId"))));
    n.type = o.value(QStringLiteral("type")).toString();
    n.meanHz = o.value(QStringLiteral("meanHz")).toDouble();
    n.meanFirstSpikeMs = o.value(QStringLiteral("meanFirstSpikeMs")).toDouble(-1.0);
    n.activationProbability = o.value(QStringLiteral("activationProbability")).toDouble();
    return n;
}

QJsonObject frameToJson(const ExperimentFrame& f)
{
    QJsonObject o;
    o.insert(QStringLiteral("startMs"), f.startMs);
    o.insert(QStringLiteral("endMs"), f.endMs);
    QJsonArray neurons;
    for (int i = 0; i < f.neurons.size(); ++i)
    {
        const ExperimentFrameNeuron& n = f.neurons.at(i);
        QJsonObject jn;
        jn.insert(QStringLiteral("rootId"), QString::number(static_cast<qulonglong>(n.rootId)));
        jn.insert(QStringLiteral("rateHz"), n.rateHz);
        jn.insert(QStringLiteral("spiked"), n.spiked);
        neurons.append(jn);
    }
    o.insert(QStringLiteral("neurons"), neurons);
    return o;
}

ExperimentFrame frameFromJson(const QJsonObject& o)
{
    ExperimentFrame f;
    f.startMs = o.value(QStringLiteral("startMs")).toDouble();
    f.endMs = o.value(QStringLiteral("endMs")).toDouble();
    const QJsonArray neurons = o.value(QStringLiteral("neurons")).toArray();
    for (int i = 0; i < neurons.size(); ++i)
    {
        const QJsonObject jn = neurons.at(i).toObject();
        ExperimentFrameNeuron n;
        n.rootId = static_cast<std::uint64_t>(parseU64(jn.value(QStringLiteral("rootId"))));
        n.rateHz = jn.value(QStringLiteral("rateHz")).toDouble();
        n.spiked = jn.value(QStringLiteral("spiked")).toBool();
        f.neurons.append(n);
    }
    return f;
}

QJsonObject lifModelToJson(const LifModelSnapshot& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("dtMs"), p.dtMs);
    o.insert(QStringLiteral("vRestMv"), p.vRestMv);
    o.insert(QStringLiteral("vThresholdMv"), p.vThresholdMv);
    o.insert(QStringLiteral("tauMembraneMs"), p.tauMembraneMs);
    o.insert(QStringLiteral("tauSynapseMs"), p.tauSynapseMs);
    o.insert(QStringLiteral("refractoryMs"), p.refractoryMs);
    o.insert(QStringLiteral("delayMs"), p.delayMs);
    o.insert(QStringLiteral("weightPerSynapseMv"), p.weightPerSynapseMv);
    o.insert(QStringLiteral("poissonForceScale"), p.poissonForceScale);
    o.insert(QStringLiteral("edgeMode"), p.edgeMode);
    o.insert(QStringLiteral("minimumConnectivity"), p.minimumConnectivity);
    o.insert(QStringLiteral("randomRetentionProbability"), p.randomRetentionProbability);
    o.insert(QStringLiteral("edgeControlSeed"), static_cast<double>(p.edgeControlSeed));
    o.insert(QStringLiteral("activityBinMs"), p.activityBinMs);
    o.insert(QStringLiteral("maxRecordedSpikes"), QString::number(static_cast<qulonglong>(p.maxRecordedSpikes)));
    o.insert(QStringLiteral("runawayAbortSpikesPerTrial"), QString::number(static_cast<qulonglong>(p.runawayAbortSpikesPerTrial)));
    o.insert(QStringLiteral("monitorEnabled"), p.monitorEnabled);
    o.insert(QStringLiteral("traceEnabled"), p.traceEnabled);
    o.insert(QStringLiteral("temporalBinMs"), p.temporalBinMs);
    return o;
}

LifModelSnapshot lifModelFromJson(const QJsonObject& o)
{
    LifModelSnapshot p;
    p.dtMs = o.value(QStringLiteral("dtMs")).toDouble(p.dtMs);
    p.vRestMv = o.value(QStringLiteral("vRestMv")).toDouble(p.vRestMv);
    p.vThresholdMv = o.value(QStringLiteral("vThresholdMv")).toDouble(p.vThresholdMv);
    p.tauMembraneMs = o.value(QStringLiteral("tauMembraneMs")).toDouble(p.tauMembraneMs);
    p.tauSynapseMs = o.value(QStringLiteral("tauSynapseMs")).toDouble(p.tauSynapseMs);
    p.refractoryMs = o.value(QStringLiteral("refractoryMs")).toDouble(p.refractoryMs);
    p.delayMs = o.value(QStringLiteral("delayMs")).toDouble(p.delayMs);
    p.weightPerSynapseMv = o.value(QStringLiteral("weightPerSynapseMv")).toDouble(p.weightPerSynapseMv);
    p.poissonForceScale = o.value(QStringLiteral("poissonForceScale")).toDouble(p.poissonForceScale);
    p.edgeMode = o.value(QStringLiteral("edgeMode")).toInt(p.edgeMode);
    p.minimumConnectivity = o.value(QStringLiteral("minimumConnectivity")).toInt(p.minimumConnectivity);
    p.randomRetentionProbability = o.value(QStringLiteral("randomRetentionProbability")).toDouble(p.randomRetentionProbability);
    p.edgeControlSeed = static_cast<quint32>(o.value(QStringLiteral("edgeControlSeed")).toDouble(p.edgeControlSeed));
    p.activityBinMs = o.value(QStringLiteral("activityBinMs")).toDouble(p.activityBinMs);
    p.maxRecordedSpikes = parseU64(o.value(QStringLiteral("maxRecordedSpikes")));
    p.runawayAbortSpikesPerTrial = parseU64(o.value(QStringLiteral("runawayAbortSpikesPerTrial")));
    p.monitorEnabled = o.value(QStringLiteral("monitorEnabled")).toBool(p.monitorEnabled);
    p.traceEnabled = o.value(QStringLiteral("traceEnabled")).toBool(p.traceEnabled);
    p.temporalBinMs = o.value(QStringLiteral("temporalBinMs")).toDouble(p.temporalBinMs);
    return p;
}

const ExperimentResult* representativeResult(const ExperimentArchiveBundle& b)
{
    if (b.experiment.valid) return &b.experiment;
    if (b.comparison.reference.valid) return &b.comparison.reference;
    if (!b.epistasis.isEmpty() && b.epistasis.first().wildType.valid) return &b.epistasis.first().wildType;
    if (!b.statistics.isEmpty() && b.statistics.first().lastEpistasis.wildType.valid) return &b.statistics.first().lastEpistasis.wildType;
    for (int i = 0; i < b.discovery.entries.size(); ++i)
        if (b.discovery.entries.at(i).comparison.reference.valid) return &b.discovery.entries.at(i).comparison.reference;
    for (int i = 0; i < b.discovery.epistasisEntries.size(); ++i)
        if (b.discovery.epistasisEntries.at(i).lastEpistasis.wildType.valid) return &b.discovery.epistasisEntries.at(i).lastEpistasis.wildType;
    return 0;
}

QJsonObject experimentToJson(const ExperimentResult& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), r.valid);
    o.insert(QStringLiteral("error"), r.error);
    o.insert(QStringLiteral("stimulusName"), r.stimulusName);
    o.insert(QStringLiteral("stimulusPath"), r.stimulusPath);
    o.insert(QStringLiteral("stimulusSource"), r.stimulusSource);
    o.insert(QStringLiteral("stimulusSha256"), r.stimulusSha256);
    o.insert(QStringLiteral("stimulusRootIds"), rootsToJson(r.stimulusRootIds));
    o.insert(QStringLiteral("sugarRateHz"), r.sugarRateHz);
    o.insert(QStringLiteral("stimulusStartMs"), r.stimulusStartMs);
    o.insert(QStringLiteral("stimulusEndMs"), r.stimulusEndMs);
    o.insert(QStringLiteral("lifModel"), lifModelToJson(r.lifModel));
    o.insert(QStringLiteral("durationMs"), r.durationMs);
    o.insert(QStringLiteral("temporalBinMs"), r.temporalBinMs);
    o.insert(QStringLiteral("trialsRequested"), r.trialsRequested);
    o.insert(QStringLiteral("trialsCompleted"), r.trialsCompleted);
    o.insert(QStringLiteral("randomSeed"), static_cast<double>(r.randomSeed));
    o.insert(QStringLiteral("wallTimeMs"), QString::number(r.wallTimeMs));
    o.insert(QStringLiteral("totalSpikes"), u64(r.totalSpikes));
    o.insert(QStringLiteral("poissonEvents"), u64(r.poissonEvents));
    o.insert(QStringLiteral("targetMeanHz"), r.targetMeanHz);
    o.insert(QStringLiteral("targetStdHz"), r.targetStdHz);
    o.insert(QStringLiteral("targetMeanLatencyMs"), r.targetMeanLatencyMs);
    o.insert(QStringLiteral("watchedRootIds"), rootsToJson(r.watchedRootIds));
    o.insert(QStringLiteral("ablatedRootIds"), rootsToJson(r.ablatedRootIds));

    QJsonArray neurons;
    for (int i = 0; i < r.neurons.size(); ++i) neurons.append(neuronSummaryToJson(r.neurons.at(i)));
    o.insert(QStringLiteral("neurons"), neurons);

    QJsonArray frames;
    for (int i = 0; i < r.frames.size(); ++i) frames.append(frameToJson(r.frames.at(i)));
    o.insert(QStringLiteral("frames"), frames);
    return o;
}

ExperimentResult experimentFromJson(const QJsonObject& o)
{
    ExperimentResult r;
    r.valid = o.value(QStringLiteral("valid")).toBool();
    r.error = o.value(QStringLiteral("error")).toString();
    r.stimulusName = o.value(QStringLiteral("stimulusName")).toString();
    r.stimulusPath = o.value(QStringLiteral("stimulusPath")).toString();
    r.stimulusSource = o.value(QStringLiteral("stimulusSource")).toString();
    r.stimulusSha256 = o.value(QStringLiteral("stimulusSha256")).toString();
    r.stimulusRootIds = rootsFromJson(o.value(QStringLiteral("stimulusRootIds")));
    r.sugarRateHz = o.value(QStringLiteral("sugarRateHz")).toDouble();
    r.stimulusStartMs = o.value(QStringLiteral("stimulusStartMs")).toDouble();
    r.stimulusEndMs = o.value(QStringLiteral("stimulusEndMs")).toDouble();
    r.lifModel = lifModelFromJson(o.value(QStringLiteral("lifModel")).toObject());
    r.durationMs = o.value(QStringLiteral("durationMs")).toDouble();
    r.temporalBinMs = o.value(QStringLiteral("temporalBinMs")).toDouble();
    r.trialsRequested = o.value(QStringLiteral("trialsRequested")).toInt();
    r.trialsCompleted = o.value(QStringLiteral("trialsCompleted")).toInt();
    r.randomSeed = static_cast<quint32>(o.value(QStringLiteral("randomSeed")).toDouble());
    r.wallTimeMs = static_cast<qint64>(parseU64(o.value(QStringLiteral("wallTimeMs"))));
    r.totalSpikes = parseU64(o.value(QStringLiteral("totalSpikes")));
    r.poissonEvents = parseU64(o.value(QStringLiteral("poissonEvents")));
    r.targetMeanHz = o.value(QStringLiteral("targetMeanHz")).toDouble();
    r.targetStdHz = o.value(QStringLiteral("targetStdHz")).toDouble();
    r.targetMeanLatencyMs = o.value(QStringLiteral("targetMeanLatencyMs")).toDouble(-1.0);
    r.watchedRootIds = rootsFromJson(o.value(QStringLiteral("watchedRootIds")));
    r.ablatedRootIds = rootsFromJson(o.value(QStringLiteral("ablatedRootIds")));

    const QJsonArray neurons = o.value(QStringLiteral("neurons")).toArray();
    for (int i = 0; i < neurons.size(); ++i) r.neurons.append(neuronSummaryFromJson(neurons.at(i).toObject()));
    const QJsonArray frames = o.value(QStringLiteral("frames")).toArray();
    for (int i = 0; i < frames.size(); ++i) r.frames.append(frameFromJson(frames.at(i).toObject()));
    return r;
}

QJsonObject comparisonToJson(const ComparisonResult& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), r.valid);
    o.insert(QStringLiteral("error"), r.error);
    o.insert(QStringLiteral("perturbedRootIds"), rootsToJson(r.perturbedRootIds));
    o.insert(QStringLiteral("targetDeltaHz"), r.targetDeltaHz);
    o.insert(QStringLiteral("targetRatio"), r.targetRatio);
    o.insert(QStringLiteral("sustainedOnsetMs"), r.sustainedOnsetMs);
    o.insert(QStringLiteral("sustainedThresholdHz"), r.sustainedThresholdHz);
    o.insert(QStringLiteral("sustainedBins"), r.sustainedBins);
    o.insert(QStringLiteral("reference"), experimentToJson(r.reference));
    o.insert(QStringLiteral("perturbed"), experimentToJson(r.perturbed));
    return o;
}

ComparisonResult comparisonFromJson(const QJsonObject& o)
{
    ComparisonResult r;
    r.valid = o.value(QStringLiteral("valid")).toBool();
    r.error = o.value(QStringLiteral("error")).toString();
    r.perturbedRootIds = rootsFromJson(o.value(QStringLiteral("perturbedRootIds")));
    r.targetDeltaHz = o.value(QStringLiteral("targetDeltaHz")).toDouble();
    r.targetRatio = o.value(QStringLiteral("targetRatio")).toDouble();
    r.sustainedOnsetMs = o.value(QStringLiteral("sustainedOnsetMs")).toDouble(-1.0);
    r.sustainedThresholdHz = o.value(QStringLiteral("sustainedThresholdHz")).toDouble(20.0);
    r.sustainedBins = o.value(QStringLiteral("sustainedBins")).toInt(5);
    r.reference = experimentFromJson(o.value(QStringLiteral("reference")).toObject());
    r.perturbed = experimentFromJson(o.value(QStringLiteral("perturbed")).toObject());
    return r;
}

QJsonObject epistasisToJson(const EpistasisResult& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), r.valid);
    o.insert(QStringLiteral("error"), r.error);
    o.insert(QStringLiteral("rootA"), QString::number(static_cast<qulonglong>(r.rootA)));
    o.insert(QStringLiteral("rootB"), QString::number(static_cast<qulonglong>(r.rootB)));
    o.insert(QStringLiteral("typeA"), r.typeA);
    o.insert(QStringLiteral("typeB"), r.typeB);
    o.insert(QStringLiteral("aDeltaHz"), r.aDeltaHz);
    o.insert(QStringLiteral("bDeltaHz"), r.bDeltaHz);
    o.insert(QStringLiteral("doubleDeltaHz"), r.doubleDeltaHz);
    o.insert(QStringLiteral("expectedAdditiveHz"), r.expectedAdditiveHz);
    o.insert(QStringLiteral("interactionHz"), r.interactionHz);
    o.insert(QStringLiteral("wildType"), experimentToJson(r.wildType));
    o.insert(QStringLiteral("aOff"), experimentToJson(r.aOff));
    o.insert(QStringLiteral("bOff"), experimentToJson(r.bOff));
    o.insert(QStringLiteral("doubleOff"), experimentToJson(r.doubleOff));
    return o;
}

EpistasisResult epistasisFromJson(const QJsonObject& o)
{
    EpistasisResult r;
    r.valid = o.value(QStringLiteral("valid")).toBool();
    r.error = o.value(QStringLiteral("error")).toString();
    r.rootA = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootA"))));
    r.rootB = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootB"))));
    r.typeA = o.value(QStringLiteral("typeA")).toString();
    r.typeB = o.value(QStringLiteral("typeB")).toString();
    r.aDeltaHz = o.value(QStringLiteral("aDeltaHz")).toDouble();
    r.bDeltaHz = o.value(QStringLiteral("bDeltaHz")).toDouble();
    r.doubleDeltaHz = o.value(QStringLiteral("doubleDeltaHz")).toDouble();
    r.expectedAdditiveHz = o.value(QStringLiteral("expectedAdditiveHz")).toDouble();
    r.interactionHz = o.value(QStringLiteral("interactionHz")).toDouble();
    r.wildType = experimentFromJson(o.value(QStringLiteral("wildType")).toObject());
    r.aOff = experimentFromJson(o.value(QStringLiteral("aOff")).toObject());
    r.bOff = experimentFromJson(o.value(QStringLiteral("bOff")).toObject());
    r.doubleOff = experimentFromJson(o.value(QStringLiteral("doubleOff")).toObject());
    return r;
}

QJsonObject statisticalSummaryToJson(const StatisticalSummary& s)
{
    QJsonObject o;
    o.insert(QStringLiteral("n"), s.n);
    o.insert(QStringLiteral("mean"), s.mean);
    o.insert(QStringLiteral("stddev"), s.stddev);
    o.insert(QStringLiteral("sem"), s.sem);
    o.insert(QStringLiteral("sumSquaredDeviation"), s.sumSquaredDeviation);
    o.insert(QStringLiteral("ci95Low"), s.ci95Low);
    o.insert(QStringLiteral("ci95High"), s.ci95High);
    o.insert(QStringLiteral("positiveCount"), s.positiveCount);
    o.insert(QStringLiteral("negativeCount"), s.negativeCount);
    o.insert(QStringLiteral("zeroCount"), s.zeroCount);
    o.insert(QStringLiteral("signConsistency"), s.signConsistency);
    o.insert(QStringLiteral("uncertaintyAvailable"), s.uncertaintyAvailable);
    return o;
}

StatisticalSummary statisticalSummaryFromJson(const QJsonObject& o)
{
    StatisticalSummary s;
    s.n = o.value(QStringLiteral("n")).toInt();
    s.mean = o.value(QStringLiteral("mean")).toDouble();
    s.stddev = o.value(QStringLiteral("stddev")).toDouble();
    s.sem = o.value(QStringLiteral("sem")).toDouble();
    s.sumSquaredDeviation = o.value(QStringLiteral("sumSquaredDeviation")).toDouble();
    s.ci95Low = o.value(QStringLiteral("ci95Low")).toDouble();
    s.ci95High = o.value(QStringLiteral("ci95High")).toDouble();
    s.positiveCount = o.value(QStringLiteral("positiveCount")).toInt();
    s.negativeCount = o.value(QStringLiteral("negativeCount")).toInt();
    s.zeroCount = o.value(QStringLiteral("zeroCount")).toInt();
    s.signConsistency = o.value(QStringLiteral("signConsistency")).toDouble();
    s.uncertaintyAvailable = o.value(QStringLiteral("uncertaintyAvailable")).toBool();
    return s;
}

QJsonObject statisticalReplicateToJson(const StatisticalEpistasisReplicate& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("seed"), static_cast<double>(r.seed));
    o.insert(QStringLiteral("wildTypeHz"), r.wildTypeHz);
    o.insert(QStringLiteral("aOffHz"), r.aOffHz);
    o.insert(QStringLiteral("bOffHz"), r.bOffHz);
    o.insert(QStringLiteral("doubleOffHz"), r.doubleOffHz);
    o.insert(QStringLiteral("expectedAdditiveHz"), r.expectedAdditiveHz);
    o.insert(QStringLiteral("interactionHz"), r.interactionHz);
    o.insert(QStringLiteral("wildTypeLatencyMs"), r.wildTypeLatencyMs);
    o.insert(QStringLiteral("aOffLatencyMs"), r.aOffLatencyMs);
    o.insert(QStringLiteral("bOffLatencyMs"), r.bOffLatencyMs);
    o.insert(QStringLiteral("doubleOffLatencyMs"), r.doubleOffLatencyMs);
    return o;
}

StatisticalEpistasisReplicate statisticalReplicateFromJson(const QJsonObject& o)
{
    StatisticalEpistasisReplicate r;
    r.seed = static_cast<quint32>(o.value(QStringLiteral("seed")).toDouble());
    r.wildTypeHz = o.value(QStringLiteral("wildTypeHz")).toDouble();
    r.aOffHz = o.value(QStringLiteral("aOffHz")).toDouble();
    r.bOffHz = o.value(QStringLiteral("bOffHz")).toDouble();
    r.doubleOffHz = o.value(QStringLiteral("doubleOffHz")).toDouble();
    r.expectedAdditiveHz = o.value(QStringLiteral("expectedAdditiveHz")).toDouble();
    r.interactionHz = o.value(QStringLiteral("interactionHz")).toDouble();
    r.wildTypeLatencyMs = o.value(QStringLiteral("wildTypeLatencyMs")).toDouble(-1.0);
    r.aOffLatencyMs = o.value(QStringLiteral("aOffLatencyMs")).toDouble(-1.0);
    r.bOffLatencyMs = o.value(QStringLiteral("bOffLatencyMs")).toDouble(-1.0);
    r.doubleOffLatencyMs = o.value(QStringLiteral("doubleOffLatencyMs")).toDouble(-1.0);
    return r;
}

QJsonObject statisticalEpistasisToJson(const StatisticalEpistasisResult& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), r.valid);
    o.insert(QStringLiteral("error"), r.error);
    o.insert(QStringLiteral("rootA"), QString::number(static_cast<qulonglong>(r.rootA)));
    o.insert(QStringLiteral("rootB"), QString::number(static_cast<qulonglong>(r.rootB)));
    o.insert(QStringLiteral("typeA"), r.typeA);
    o.insert(QStringLiteral("typeB"), r.typeB);
    o.insert(QStringLiteral("trialsPerReplicate"), r.trialsPerReplicate);
    o.insert(QStringLiteral("replicatesRequested"), r.replicatesRequested);
    o.insert(QStringLiteral("replicatesCompleted"), r.replicatesCompleted);
    o.insert(QStringLiteral("baseSeed"), static_cast<double>(r.baseSeed));
    QJsonArray reps;
    for (int i = 0; i < r.replicates.size(); ++i) reps.append(statisticalReplicateToJson(r.replicates.at(i)));
    o.insert(QStringLiteral("replicates"), reps);
    o.insert(QStringLiteral("wildType"), statisticalSummaryToJson(r.wildType));
    o.insert(QStringLiteral("aOff"), statisticalSummaryToJson(r.aOff));
    o.insert(QStringLiteral("bOff"), statisticalSummaryToJson(r.bOff));
    o.insert(QStringLiteral("doubleOff"), statisticalSummaryToJson(r.doubleOff));
    o.insert(QStringLiteral("interaction"), statisticalSummaryToJson(r.interaction));
    o.insert(QStringLiteral("lastEpistasis"), epistasisToJson(r.lastEpistasis));
    return o;
}

StatisticalEpistasisResult statisticalEpistasisFromJson(const QJsonObject& o)
{
    StatisticalEpistasisResult r;
    r.valid = o.value(QStringLiteral("valid")).toBool();
    r.error = o.value(QStringLiteral("error")).toString();
    r.rootA = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootA"))));
    r.rootB = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootB"))));
    r.typeA = o.value(QStringLiteral("typeA")).toString();
    r.typeB = o.value(QStringLiteral("typeB")).toString();
    r.trialsPerReplicate = o.value(QStringLiteral("trialsPerReplicate")).toInt(1);
    r.replicatesRequested = o.value(QStringLiteral("replicatesRequested")).toInt();
    r.replicatesCompleted = o.value(QStringLiteral("replicatesCompleted")).toInt();
    r.baseSeed = static_cast<quint32>(o.value(QStringLiteral("baseSeed")).toDouble());
    const QJsonArray reps = o.value(QStringLiteral("replicates")).toArray();
    for (int i = 0; i < reps.size(); ++i) r.replicates.append(statisticalReplicateFromJson(reps.at(i).toObject()));
    r.wildType = statisticalSummaryFromJson(o.value(QStringLiteral("wildType")).toObject());
    r.aOff = statisticalSummaryFromJson(o.value(QStringLiteral("aOff")).toObject());
    r.bOff = statisticalSummaryFromJson(o.value(QStringLiteral("bOff")).toObject());
    r.doubleOff = statisticalSummaryFromJson(o.value(QStringLiteral("doubleOff")).toObject());
    r.interaction = statisticalSummaryFromJson(o.value(QStringLiteral("interaction")).toObject());
    r.lastEpistasis = epistasisFromJson(o.value(QStringLiteral("lastEpistasis")).toObject());
    return r;
}


QJsonObject experimentParametersToJson(const ExperimentParameters& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("durationMs"), p.durationMs);
    o.insert(QStringLiteral("sugarRateHz"), p.sugarRateHz);
    o.insert(QStringLiteral("trials"), p.trials);
    o.insert(QStringLiteral("randomSeed"), static_cast<double>(p.randomSeed));
    o.insert(QStringLiteral("temporalBinMs"), p.temporalBinMs);
    o.insert(QStringLiteral("stimulusPath"), p.stimulusPath);
    o.insert(QStringLiteral("watchedRootIds"), rootsToJson(p.watchedRootIds));
    o.insert(QStringLiteral("ablatedRootIds"), rootsToJson(p.ablatedRootIds));
    return o;
}

ExperimentParameters experimentParametersFromJson(const QJsonObject& o)
{
    ExperimentParameters p;
    p.durationMs = o.value(QStringLiteral("durationMs")).toDouble(p.durationMs);
    p.sugarRateHz = o.value(QStringLiteral("sugarRateHz")).toDouble(p.sugarRateHz);
    p.trials = o.value(QStringLiteral("trials")).toInt(p.trials);
    p.randomSeed = static_cast<quint32>(o.value(QStringLiteral("randomSeed")).toDouble(p.randomSeed));
    p.temporalBinMs = o.value(QStringLiteral("temporalBinMs")).toDouble(p.temporalBinMs);
    p.stimulusPath = o.value(QStringLiteral("stimulusPath")).toString();
    p.watchedRootIds = rootsFromJson(o.value(QStringLiteral("watchedRootIds")));
    p.ablatedRootIds = rootsFromJson(o.value(QStringLiteral("ablatedRootIds")));
    return p;
}

QJsonObject explorerParametersToJson(const CircuitExploreParameters& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("rootText"), p.rootText);
    o.insert(QStringLiteral("direction"), p.direction);
    o.insert(QStringLiteral("maxDepth"), p.maxDepth);
    o.insert(QStringLiteral("minimumSynapses"), p.minimumSynapses);
    o.insert(QStringLiteral("maximumNeurons"), p.maximumNeurons);
    return o;
}

CircuitExploreParameters explorerParametersFromJson(const QJsonObject& o)
{
    CircuitExploreParameters p;
    p.rootText = o.value(QStringLiteral("rootText")).toString();
    p.direction = o.value(QStringLiteral("direction")).toInt(p.direction);
    p.maxDepth = o.value(QStringLiteral("maxDepth")).toInt(p.maxDepth);
    p.minimumSynapses = o.value(QStringLiteral("minimumSynapses")).toInt(p.minimumSynapses);
    p.maximumNeurons = o.value(QStringLiteral("maximumNeurons")).toInt(p.maximumNeurons);
    return p;
}

QJsonObject explorerCandidateToJson(const CircuitExploreCandidate& c)
{
    QJsonObject o;
    o.insert(QStringLiteral("rootId"), u64(c.rootId));
    o.insert(QStringLiteral("index"), static_cast<double>(c.index));
    o.insert(QStringLiteral("primaryType"), c.primaryType);
    o.insert(QStringLiteral("additionalTypes"), c.additionalTypes);
    o.insert(QStringLiteral("sign"), static_cast<int>(c.sign));
    o.insert(QStringLiteral("depth"), c.depth);
    o.insert(QStringLiteral("upstream"), c.upstream);
    o.insert(QStringLiteral("downstream"), c.downstream);
    o.insert(QStringLiteral("pathCount"), u64(c.pathCount));
    o.insert(QStringLiteral("synapseStrength"), u64(c.synapseStrength));
    o.insert(QStringLiteral("directSynapses"), static_cast<double>(c.directSynapses));
    o.insert(QStringLiteral("rootFraction"), c.rootFraction);
    o.insert(QStringLiteral("score"), c.score);
    return o;
}

CircuitExploreCandidate explorerCandidateFromJson(const QJsonObject& o)
{
    CircuitExploreCandidate c;
    c.rootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootId"))));
    c.index = static_cast<quint32>(o.value(QStringLiteral("index")).toDouble());
    c.primaryType = o.value(QStringLiteral("primaryType")).toString();
    c.additionalTypes = o.value(QStringLiteral("additionalTypes")).toString();
    c.sign = static_cast<qint8>(o.value(QStringLiteral("sign")).toInt());
    c.depth = o.value(QStringLiteral("depth")).toInt();
    c.upstream = o.value(QStringLiteral("upstream")).toBool();
    c.downstream = o.value(QStringLiteral("downstream")).toBool();
    c.pathCount = parseU64(o.value(QStringLiteral("pathCount")));
    c.synapseStrength = parseU64(o.value(QStringLiteral("synapseStrength")));
    c.directSynapses = static_cast<quint32>(o.value(QStringLiteral("directSynapses")).toDouble());
    c.rootFraction = o.value(QStringLiteral("rootFraction")).toDouble();
    c.score = o.value(QStringLiteral("score")).toDouble();
    return c;
}

QJsonObject explorerEdgeToJson(const CircuitExploreEdge& e)
{
    QJsonObject o;
    o.insert(QStringLiteral("sourceRootId"), u64(e.sourceRootId));
    o.insert(QStringLiteral("targetRootId"), u64(e.targetRootId));
    o.insert(QStringLiteral("weight"), static_cast<int>(e.weight));
    return o;
}

CircuitExploreEdge explorerEdgeFromJson(const QJsonObject& o)
{
    CircuitExploreEdge e;
    e.sourceRootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("sourceRootId"))));
    e.targetRootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("targetRootId"))));
    e.weight = static_cast<qint16>(o.value(QStringLiteral("weight")).toInt());
    return e;
}

QJsonObject explorerToJson(const CircuitExploreResult& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), r.valid);
    o.insert(QStringLiteral("error"), r.error);
    o.insert(QStringLiteral("rootQuery"), r.rootQuery);
    o.insert(QStringLiteral("rootId"), u64(r.rootId));
    o.insert(QStringLiteral("rootIndex"), static_cast<double>(r.rootIndex));
    o.insert(QStringLiteral("rootType"), r.rootType);
    o.insert(QStringLiteral("direction"), r.direction);
    o.insert(QStringLiteral("maxDepth"), r.maxDepth);
    o.insert(QStringLiteral("minimumSynapses"), r.minimumSynapses);
    o.insert(QStringLiteral("maximumNeurons"), r.maximumNeurons);
    o.insert(QStringLiteral("reverseIndexBuiltNow"), r.reverseIndexBuiltNow);
    o.insert(QStringLiteral("truncated"), r.truncated);
    o.insert(QStringLiteral("exploredEdges"), u64(r.exploredEdges));
    QJsonArray candidates;
    for (int i = 0; i < r.candidates.size(); ++i) candidates.append(explorerCandidateToJson(r.candidates.at(i)));
    o.insert(QStringLiteral("candidates"), candidates);
    QJsonArray edges;
    for (int i = 0; i < r.edges.size(); ++i) edges.append(explorerEdgeToJson(r.edges.at(i)));
    o.insert(QStringLiteral("edges"), edges);
    return o;
}

CircuitExploreResult explorerFromJson(const QJsonObject& o)
{
    CircuitExploreResult r;
    r.valid = o.value(QStringLiteral("valid")).toBool();
    r.error = o.value(QStringLiteral("error")).toString();
    r.rootQuery = o.value(QStringLiteral("rootQuery")).toString();
    r.rootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootId"))));
    r.rootIndex = static_cast<quint32>(o.value(QStringLiteral("rootIndex")).toDouble());
    r.rootType = o.value(QStringLiteral("rootType")).toString();
    r.direction = o.value(QStringLiteral("direction")).toInt();
    r.maxDepth = o.value(QStringLiteral("maxDepth")).toInt();
    r.minimumSynapses = o.value(QStringLiteral("minimumSynapses")).toInt();
    r.maximumNeurons = o.value(QStringLiteral("maximumNeurons")).toInt();
    r.reverseIndexBuiltNow = o.value(QStringLiteral("reverseIndexBuiltNow")).toBool();
    r.truncated = o.value(QStringLiteral("truncated")).toBool();
    r.exploredEdges = parseU64(o.value(QStringLiteral("exploredEdges")));
    const QJsonArray candidates = o.value(QStringLiteral("candidates")).toArray();
    for (int i = 0; i < candidates.size(); ++i) r.candidates.append(explorerCandidateFromJson(candidates.at(i).toObject()));
    const QJsonArray edges = o.value(QStringLiteral("edges")).toArray();
    for (int i = 0; i < edges.size(); ++i) r.edges.append(explorerEdgeFromJson(edges.at(i).toObject()));
    return r;
}

QJsonObject discoveryCandidateToJson(const CausalDiscoveryCandidate& c)
{
    QJsonObject o;
    o.insert(QStringLiteral("rootId"), u64(c.rootId));
    o.insert(QStringLiteral("type"), c.type);
    o.insert(QStringLiteral("graphRank"), c.graphRank);
    o.insert(QStringLiteral("depth"), c.depth);
    o.insert(QStringLiteral("synapseStrength"), u64(c.synapseStrength));
    o.insert(QStringLiteral("graphScore"), c.graphScore);
    return o;
}

CausalDiscoveryCandidate discoveryCandidateFromJson(const QJsonObject& o)
{
    CausalDiscoveryCandidate c;
    c.rootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootId"))));
    c.type = o.value(QStringLiteral("type")).toString();
    c.graphRank = o.value(QStringLiteral("graphRank")).toInt();
    c.depth = o.value(QStringLiteral("depth")).toInt();
    c.synapseStrength = parseU64(o.value(QStringLiteral("synapseStrength")));
    c.graphScore = o.value(QStringLiteral("graphScore")).toDouble();
    return c;
}

QJsonObject discoveryEntryToJson(const CausalDiscoveryEntry& e)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), e.valid);
    o.insert(QStringLiteral("error"), e.error);
    o.insert(QStringLiteral("causalRank"), e.causalRank);
    o.insert(QStringLiteral("candidate"), discoveryCandidateToJson(e.candidate));
    o.insert(QStringLiteral("comparison"), comparisonToJson(e.comparison));
    o.insert(QStringLiteral("screenReplicatesRequested"), e.screenReplicatesRequested);
    o.insert(QStringLiteral("screenReplicatesCompleted"), e.screenReplicatesCompleted);
    o.insert(QStringLiteral("screenReference"), statisticalSummaryToJson(e.screenReference));
    o.insert(QStringLiteral("screenPerturbed"), statisticalSummaryToJson(e.screenPerturbed));
    o.insert(QStringLiteral("screenDelta"), statisticalSummaryToJson(e.screenDelta));
    o.insert(QStringLiteral("validationAttempted"), e.validationAttempted);
    o.insert(QStringLiteral("validationValid"), e.validationValid);
    o.insert(QStringLiteral("validationError"), e.validationError);
    o.insert(QStringLiteral("validationReplicatesRequested"), e.validationReplicatesRequested);
    o.insert(QStringLiteral("validationReplicatesCompleted"), e.validationReplicatesCompleted);
    o.insert(QStringLiteral("validationReference"), statisticalSummaryToJson(e.validationReference));
    o.insert(QStringLiteral("validationPerturbed"), statisticalSummaryToJson(e.validationPerturbed));
    o.insert(QStringLiteral("validationDelta"), statisticalSummaryToJson(e.validationDelta));
    return o;
}

CausalDiscoveryEntry discoveryEntryFromJson(const QJsonObject& o)
{
    CausalDiscoveryEntry e;
    e.valid = o.value(QStringLiteral("valid")).toBool();
    e.error = o.value(QStringLiteral("error")).toString();
    e.causalRank = o.value(QStringLiteral("causalRank")).toInt();
    e.candidate = discoveryCandidateFromJson(o.value(QStringLiteral("candidate")).toObject());
    e.comparison = comparisonFromJson(o.value(QStringLiteral("comparison")).toObject());
    e.screenReplicatesRequested = o.value(QStringLiteral("screenReplicatesRequested")).toInt();
    e.screenReplicatesCompleted = o.value(QStringLiteral("screenReplicatesCompleted")).toInt();
    e.screenReference = statisticalSummaryFromJson(o.value(QStringLiteral("screenReference")).toObject());
    e.screenPerturbed = statisticalSummaryFromJson(o.value(QStringLiteral("screenPerturbed")).toObject());
    e.screenDelta = statisticalSummaryFromJson(o.value(QStringLiteral("screenDelta")).toObject());
    e.validationAttempted = o.value(QStringLiteral("validationAttempted")).toBool();
    e.validationValid = o.value(QStringLiteral("validationValid")).toBool();
    e.validationError = o.value(QStringLiteral("validationError")).toString();
    e.validationReplicatesRequested = o.value(QStringLiteral("validationReplicatesRequested")).toInt();
    e.validationReplicatesCompleted = o.value(QStringLiteral("validationReplicatesCompleted")).toInt();
    e.validationReference = statisticalSummaryFromJson(o.value(QStringLiteral("validationReference")).toObject());
    e.validationPerturbed = statisticalSummaryFromJson(o.value(QStringLiteral("validationPerturbed")).toObject());
    e.validationDelta = statisticalSummaryFromJson(o.value(QStringLiteral("validationDelta")).toObject());
    return e;
}

QJsonObject discoveryEpistasisEntryToJson(const CausalDiscoveryEpistasisEntry& e)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), e.valid);
    o.insert(QStringLiteral("error"), e.error);
    o.insert(QStringLiteral("interactionRank"), e.interactionRank);
    o.insert(QStringLiteral("rootA"), u64(e.rootA));
    o.insert(QStringLiteral("rootB"), u64(e.rootB));
    o.insert(QStringLiteral("typeA"), e.typeA);
    o.insert(QStringLiteral("typeB"), e.typeB);
    o.insert(QStringLiteral("causalRankA"), e.causalRankA);
    o.insert(QStringLiteral("causalRankB"), e.causalRankB);
    o.insert(QStringLiteral("validatedDeltaAHz"), e.validatedDeltaAHz);
    o.insert(QStringLiteral("validatedDeltaBHz"), e.validatedDeltaBHz);
    o.insert(QStringLiteral("replicatesRequested"), e.replicatesRequested);
    o.insert(QStringLiteral("replicatesCompleted"), e.replicatesCompleted);
    o.insert(QStringLiteral("wildType"), statisticalSummaryToJson(e.wildType));
    o.insert(QStringLiteral("aOff"), statisticalSummaryToJson(e.aOff));
    o.insert(QStringLiteral("bOff"), statisticalSummaryToJson(e.bOff));
    o.insert(QStringLiteral("doubleOff"), statisticalSummaryToJson(e.doubleOff));
    o.insert(QStringLiteral("expectedAdditive"), statisticalSummaryToJson(e.expectedAdditive));
    o.insert(QStringLiteral("interaction"), statisticalSummaryToJson(e.interaction));
    o.insert(QStringLiteral("lastEpistasis"), epistasisToJson(e.lastEpistasis));
    return o;
}

CausalDiscoveryEpistasisEntry discoveryEpistasisEntryFromJson(const QJsonObject& o)
{
    CausalDiscoveryEpistasisEntry e;
    e.valid = o.value(QStringLiteral("valid")).toBool();
    e.error = o.value(QStringLiteral("error")).toString();
    e.interactionRank = o.value(QStringLiteral("interactionRank")).toInt();
    e.rootA = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootA"))));
    e.rootB = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootB"))));
    e.typeA = o.value(QStringLiteral("typeA")).toString();
    e.typeB = o.value(QStringLiteral("typeB")).toString();
    e.causalRankA = o.value(QStringLiteral("causalRankA")).toInt();
    e.causalRankB = o.value(QStringLiteral("causalRankB")).toInt();
    e.validatedDeltaAHz = o.value(QStringLiteral("validatedDeltaAHz")).toDouble();
    e.validatedDeltaBHz = o.value(QStringLiteral("validatedDeltaBHz")).toDouble();
    e.replicatesRequested = o.value(QStringLiteral("replicatesRequested")).toInt();
    e.replicatesCompleted = o.value(QStringLiteral("replicatesCompleted")).toInt();
    e.wildType = statisticalSummaryFromJson(o.value(QStringLiteral("wildType")).toObject());
    e.aOff = statisticalSummaryFromJson(o.value(QStringLiteral("aOff")).toObject());
    e.bOff = statisticalSummaryFromJson(o.value(QStringLiteral("bOff")).toObject());
    e.doubleOff = statisticalSummaryFromJson(o.value(QStringLiteral("doubleOff")).toObject());
    e.expectedAdditive = statisticalSummaryFromJson(o.value(QStringLiteral("expectedAdditive")).toObject());
    e.interaction = statisticalSummaryFromJson(o.value(QStringLiteral("interaction")).toObject());
    e.lastEpistasis = epistasisFromJson(o.value(QStringLiteral("lastEpistasis")).toObject());
    return e;
}

QJsonObject provenanceToJson(const CausalDiscoveryProvenance& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), p.valid);
    o.insert(QStringLiteral("origin"), p.origin);
    o.insert(QStringLiteral("explorerParameters"), explorerParametersToJson(p.explorerParameters));
    o.insert(QStringLiteral("selectionMode"), p.selectionMode);
    o.insert(QStringLiteral("selectionTopN"), p.selectionTopN);
    o.insert(QStringLiteral("selectedCandidates"), p.selectedCandidates);
    o.insert(QStringLiteral("screenParametersValid"), p.screenParametersValid);
    o.insert(QStringLiteral("screenParameters"), experimentParametersToJson(p.screenParameters));
    o.insert(QStringLiteral("validationTopN"), p.validationTopN);
    o.insert(QStringLiteral("validationParametersValid"), p.validationParametersValid);
    o.insert(QStringLiteral("validationParameters"), experimentParametersToJson(p.validationParameters));
    o.insert(QStringLiteral("epistasisTopN"), p.epistasisTopN);
    o.insert(QStringLiteral("epistasisParametersValid"), p.epistasisParametersValid);
    o.insert(QStringLiteral("epistasisParameters"), experimentParametersToJson(p.epistasisParameters));
    o.insert(QStringLiteral("epistasisGateMode"), p.epistasisGateMode);
    o.insert(QStringLiteral("epistasisMinimumSignConsistency"), p.epistasisMinimumSignConsistency);
    o.insert(QStringLiteral("startedUtc"), p.startedUtc);
    o.insert(QStringLiteral("updatedUtc"), p.updatedUtc);
    return o;
}

CausalDiscoveryProvenance provenanceFromJson(const QJsonObject& o)
{
    CausalDiscoveryProvenance p;
    p.valid = o.value(QStringLiteral("valid")).toBool();
    p.origin = o.value(QStringLiteral("origin")).toString();
    p.explorerParameters = explorerParametersFromJson(o.value(QStringLiteral("explorerParameters")).toObject());
    p.selectionMode = o.value(QStringLiteral("selectionMode")).toString();
    p.selectionTopN = o.value(QStringLiteral("selectionTopN")).toInt();
    p.selectedCandidates = o.value(QStringLiteral("selectedCandidates")).toInt();
    p.screenParametersValid = o.value(QStringLiteral("screenParametersValid")).toBool();
    p.screenParameters = experimentParametersFromJson(o.value(QStringLiteral("screenParameters")).toObject());
    p.validationTopN = o.value(QStringLiteral("validationTopN")).toInt();
    p.validationParametersValid = o.value(QStringLiteral("validationParametersValid")).toBool();
    p.validationParameters = experimentParametersFromJson(o.value(QStringLiteral("validationParameters")).toObject());
    p.epistasisTopN = o.value(QStringLiteral("epistasisTopN")).toInt();
    p.epistasisParametersValid = o.value(QStringLiteral("epistasisParametersValid")).toBool();
    p.epistasisParameters = experimentParametersFromJson(o.value(QStringLiteral("epistasisParameters")).toObject());
    p.epistasisGateMode = o.value(QStringLiteral("epistasisGateMode")).toString();
    p.epistasisMinimumSignConsistency = o.value(QStringLiteral("epistasisMinimumSignConsistency")).toDouble();
    p.startedUtc = o.value(QStringLiteral("startedUtc")).toString();
    p.updatedUtc = o.value(QStringLiteral("updatedUtc")).toString();
    return p;
}

QJsonObject discoveryToJson(const CausalDiscoveryResult& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), r.valid);
    o.insert(QStringLiteral("stopped"), r.stopped);
    o.insert(QStringLiteral("error"), r.error);
    o.insert(QStringLiteral("targetRootId"), u64(r.targetRootId));
    o.insert(QStringLiteral("targetType"), r.targetType);
    o.insert(QStringLiteral("candidatesRequested"), r.candidatesRequested);
    o.insert(QStringLiteral("candidatesCompleted"), r.candidatesCompleted);
    o.insert(QStringLiteral("screenReplicatesRequested"), r.screenReplicatesRequested);
    o.insert(QStringLiteral("validationCandidatesRequested"), r.validationCandidatesRequested);
    o.insert(QStringLiteral("validationCandidatesCompleted"), r.validationCandidatesCompleted);
    o.insert(QStringLiteral("validationReplicatesRequested"), r.validationReplicatesRequested);
    o.insert(QStringLiteral("validationRootIds"), rootsToJson(r.validationRootIds));
    o.insert(QStringLiteral("epistasisPairsRequested"), r.epistasisPairsRequested);
    o.insert(QStringLiteral("epistasisPairsCompleted"), r.epistasisPairsCompleted);
    o.insert(QStringLiteral("epistasisReplicatesRequested"), r.epistasisReplicatesRequested);
    o.insert(QStringLiteral("epistasisRootIds"), rootsToJson(r.epistasisRootIds));
    o.insert(QStringLiteral("parameters"), experimentParametersToJson(r.parameters));
    QJsonArray entries;
    for (int i = 0; i < r.entries.size(); ++i) entries.append(discoveryEntryToJson(r.entries.at(i)));
    o.insert(QStringLiteral("entries"), entries);
    QJsonArray epistasisEntries;
    for (int i = 0; i < r.epistasisEntries.size(); ++i) epistasisEntries.append(discoveryEpistasisEntryToJson(r.epistasisEntries.at(i)));
    o.insert(QStringLiteral("epistasisEntries"), epistasisEntries);
    return o;
}

CausalDiscoveryResult discoveryFromJson(const QJsonObject& o)
{
    CausalDiscoveryResult r;
    r.valid = o.value(QStringLiteral("valid")).toBool();
    r.stopped = o.value(QStringLiteral("stopped")).toBool();
    r.error = o.value(QStringLiteral("error")).toString();
    r.targetRootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("targetRootId"))));
    r.targetType = o.value(QStringLiteral("targetType")).toString();
    r.candidatesRequested = o.value(QStringLiteral("candidatesRequested")).toInt();
    r.candidatesCompleted = o.value(QStringLiteral("candidatesCompleted")).toInt();
    r.screenReplicatesRequested = o.value(QStringLiteral("screenReplicatesRequested")).toInt(1);
    r.validationCandidatesRequested = o.value(QStringLiteral("validationCandidatesRequested")).toInt();
    r.validationCandidatesCompleted = o.value(QStringLiteral("validationCandidatesCompleted")).toInt();
    r.validationReplicatesRequested = o.value(QStringLiteral("validationReplicatesRequested")).toInt();
    r.validationRootIds = rootsFromJson(o.value(QStringLiteral("validationRootIds")));
    r.epistasisPairsRequested = o.value(QStringLiteral("epistasisPairsRequested")).toInt();
    r.epistasisPairsCompleted = o.value(QStringLiteral("epistasisPairsCompleted")).toInt();
    r.epistasisReplicatesRequested = o.value(QStringLiteral("epistasisReplicatesRequested")).toInt();
    r.epistasisRootIds = rootsFromJson(o.value(QStringLiteral("epistasisRootIds")));
    r.parameters = experimentParametersFromJson(o.value(QStringLiteral("parameters")).toObject());
    const QJsonArray entries = o.value(QStringLiteral("entries")).toArray();
    for (int i = 0; i < entries.size(); ++i) r.entries.append(discoveryEntryFromJson(entries.at(i).toObject()));
    const QJsonArray epi = o.value(QStringLiteral("epistasisEntries")).toArray();
    for (int i = 0; i < epi.size(); ++i) r.epistasisEntries.append(discoveryEpistasisEntryFromJson(epi.at(i).toObject()));
    return r;
}

QJsonObject causalSurpriseEntryToJson(const CausalSurpriseEntry& e)
{
    QJsonObject o;
    o.insert(QStringLiteral("rootId"), u64(e.rootId));
    o.insert(QStringLiteral("type"), e.type);
    o.insert(QStringLiteral("graphRank"), e.graphRank);
    o.insert(QStringLiteral("causalRank"), e.causalRank);
    o.insert(QStringLiteral("surpriseRank"), e.surpriseRank);
    o.insert(QStringLiteral("depth"), e.depth);
    o.insert(QStringLiteral("pathCount"), u64(e.pathCount));
    o.insert(QStringLiteral("synapseStrength"), u64(e.synapseStrength));
    o.insert(QStringLiteral("directSynapses"), static_cast<double>(e.directSynapses));
    o.insert(QStringLiteral("graphScore"), e.graphScore);
    o.insert(QStringLiteral("validationDeltaHz"), e.validationDeltaHz);
    o.insert(QStringLiteral("absoluteCausalEffectHz"), e.absoluteCausalEffectHz);
    o.insert(QStringLiteral("ci95LowHz"), e.ci95LowHz);
    o.insert(QStringLiteral("ci95HighHz"), e.ci95HighHz);
    o.insert(QStringLiteral("signConsistency"), e.signConsistency);
    o.insert(QStringLiteral("structuralPercentile"), e.structuralPercentile);
    o.insert(QStringLiteral("causalPercentile"), e.causalPercentile);
    o.insert(QStringLiteral("surpriseScore"), e.surpriseScore);
    o.insert(QStringLiteral("validationValid"), e.validationValid);
    o.insert(QStringLiteral("gatePassed"), e.gatePassed);
    o.insert(QStringLiteral("quadrant"), e.quadrant);
    return o;
}

CausalSurpriseEntry causalSurpriseEntryFromJson(const QJsonObject& o)
{
    CausalSurpriseEntry e;
    e.rootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("rootId"))));
    e.type = o.value(QStringLiteral("type")).toString();
    e.graphRank = o.value(QStringLiteral("graphRank")).toInt();
    e.causalRank = o.value(QStringLiteral("causalRank")).toInt();
    e.surpriseRank = o.value(QStringLiteral("surpriseRank")).toInt();
    e.depth = o.value(QStringLiteral("depth")).toInt();
    e.pathCount = parseU64(o.value(QStringLiteral("pathCount")));
    e.synapseStrength = parseU64(o.value(QStringLiteral("synapseStrength")));
    e.directSynapses = static_cast<quint32>(o.value(QStringLiteral("directSynapses")).toDouble());
    e.graphScore = o.value(QStringLiteral("graphScore")).toDouble();
    e.validationDeltaHz = o.value(QStringLiteral("validationDeltaHz")).toDouble();
    e.absoluteCausalEffectHz = o.value(QStringLiteral("absoluteCausalEffectHz")).toDouble();
    e.ci95LowHz = o.value(QStringLiteral("ci95LowHz")).toDouble();
    e.ci95HighHz = o.value(QStringLiteral("ci95HighHz")).toDouble();
    e.signConsistency = o.value(QStringLiteral("signConsistency")).toDouble();
    e.structuralPercentile = o.value(QStringLiteral("structuralPercentile")).toDouble();
    e.causalPercentile = o.value(QStringLiteral("causalPercentile")).toDouble();
    e.surpriseScore = o.value(QStringLiteral("surpriseScore")).toDouble();
    e.validationValid = o.value(QStringLiteral("validationValid")).toBool();
    e.gatePassed = o.value(QStringLiteral("gatePassed")).toBool();
    e.quadrant = o.value(QStringLiteral("quadrant")).toString();
    return e;
}

QJsonObject causalSurpriseToJson(const CausalStructureAnalysisResult& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("valid"), r.valid);
    o.insert(QStringLiteral("error"), r.error);
    o.insert(QStringLiteral("createdUtc"), r.createdUtc);
    o.insert(QStringLiteral("targetRootId"), u64(r.targetRootId));
    o.insert(QStringLiteral("targetType"), r.targetType);
    o.insert(QStringLiteral("gateMode"), r.gateMode);
    o.insert(QStringLiteral("minimumSignConsistency"), r.minimumSignConsistency);
    o.insert(QStringLiteral("validationRows"), r.validationRows);
    o.insert(QStringLiteral("gatePassedRows"), r.gatePassedRows);
    o.insert(QStringLiteral("correlationsAvailable"), r.correlationsAvailable);
    o.insert(QStringLiteral("pearson"), r.pearson);
    o.insert(QStringLiteral("spearman"), r.spearman);
    QJsonArray entries;
    for (int i = 0; i < r.entries.size(); ++i) entries.append(causalSurpriseEntryToJson(r.entries.at(i)));
    o.insert(QStringLiteral("entries"), entries);
    return o;
}

CausalStructureAnalysisResult causalSurpriseFromJson(const QJsonObject& o)
{
    CausalStructureAnalysisResult r;
    r.valid = o.value(QStringLiteral("valid")).toBool();
    r.error = o.value(QStringLiteral("error")).toString();
    r.createdUtc = o.value(QStringLiteral("createdUtc")).toString();
    r.targetRootId = static_cast<std::uint64_t>(parseU64(o.value(QStringLiteral("targetRootId"))));
    r.targetType = o.value(QStringLiteral("targetType")).toString();
    r.gateMode = o.value(QStringLiteral("gateMode")).toString();
    r.minimumSignConsistency = o.value(QStringLiteral("minimumSignConsistency")).toDouble();
    r.validationRows = o.value(QStringLiteral("validationRows")).toInt();
    r.gatePassedRows = o.value(QStringLiteral("gatePassedRows")).toInt();
    r.correlationsAvailable = o.value(QStringLiteral("correlationsAvailable")).toBool();
    r.pearson = o.value(QStringLiteral("pearson")).toDouble();
    r.spearman = o.value(QStringLiteral("spearman")).toDouble();
    const QJsonArray entries = o.value(QStringLiteral("entries")).toArray();
    for (int i = 0; i < entries.size(); ++i) r.entries.append(causalSurpriseEntryFromJson(entries.at(i).toObject()));
    return r;
}


QByteArray jsonBytes(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

bool parseJsonObject(const QByteArray& bytes, QJsonObject* object, QString* error, const QString& name)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error) *error = QStringLiteral("Invalid %1: %2").arg(name, parseError.errorString());
        return false;
    }
    if (object) *object = doc.object();
    return true;
}

QString csvQuote(const QString& s)
{
    QString t = s;
    t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + t + QStringLiteral("\"");
}

QString rootType(const ExperimentResult& r, std::uint64_t rootId)
{
    for (int i = 0; i < r.neurons.size(); ++i)
        if (r.neurons.at(i).rootId == rootId)
            return r.neurons.at(i).type;
    return QString::number(static_cast<qulonglong>(rootId));
}

void writeExperimentSummaryRows(QTextStream& s, const QString& source, const QString& condition,
                                const ExperimentResult& r, const QString& pairA = QString(),
                                const QString& pairB = QString())
{
    if (!r.valid) return;
    for (int i = 0; i < r.neurons.size(); ++i)
    {
        const ExperimentNeuronSummary& n = r.neurons.at(i);
        s << csvQuote(source) << ',' << csvQuote(condition) << ',' << csvQuote(pairA) << ',' << csvQuote(pairB) << ','
          << QString::number(static_cast<qulonglong>(n.rootId)) << ',' << csvQuote(n.type) << ','
          << QString::number(n.meanHz, 'f', 6) << ',' << QString() << ','
          << QString::number(n.meanFirstSpikeMs, 'f', 6) << ',' << QString::number(n.activationProbability, 'f', 6) << ','
          << r.randomSeed << ',' << r.trialsCompleted << ',' << QString::number(r.durationMs, 'f', 3) << ','
          << QString::number(r.sugarRateHz, 'f', 6) << '\n';
    }
}

void writeTimelineRows(QTextStream& s, const QString& source, const QString& condition,
                       const ExperimentResult& r, const QString& pairA = QString(),
                       const QString& pairB = QString())
{
    if (!r.valid) return;
    for (int fi = 0; fi < r.frames.size(); ++fi)
    {
        const ExperimentFrame& f = r.frames.at(fi);
        for (int ni = 0; ni < f.neurons.size(); ++ni)
        {
            const ExperimentFrameNeuron& n = f.neurons.at(ni);
            s << csvQuote(source) << ',' << csvQuote(condition) << ',' << csvQuote(pairA) << ',' << csvQuote(pairB) << ','
              << QString::number(f.startMs, 'f', 3) << ',' << QString::number(f.endMs, 'f', 3) << ','
              << QString::number(static_cast<qulonglong>(n.rootId)) << ',' << csvQuote(rootType(r, n.rootId)) << ','
              << QString::number(n.rateHz, 'f', 6) << ',' << (n.spiked ? 1 : 0) << '\n';
        }
    }
}

QString ensureCsvSuffix(const QString& path)
{
    return path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive) ? path : path + QStringLiteral(".csv");
}
}

bool ExperimentArchive::save(const QString& filePath, const ExperimentArchiveBundle& source, QString* error)
{
    if (filePath.isEmpty())
    {
        if (error) *error = QStringLiteral("Archive path is empty.");
        return false;
    }
    if (!source.hasAnyResult())
    {
        if (error) *error = QStringLiteral("There is no scientific state to archive.");
        return false;
    }

    ExperimentArchiveBundle bundle = source;
    // v0.16 always writes the current archive schema. Older v1/v2 files remain readable,
    // but a re-save upgrades them to v3 rather than creating a hybrid manifest.
    bundle.formatVersion = 3;
    if (bundle.applicationVersion.isEmpty()) bundle.applicationVersion = QStringLiteral("0.18-r1");
    if (bundle.connectomeVersion.isEmpty()) bundle.connectomeVersion = QStringLiteral("FlyWire v783");
    if (bundle.createdUtc.isEmpty()) bundle.createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("drosofilaLive experiment archive"));
    manifest.insert(QStringLiteral("formatVersion"), bundle.formatVersion);
    manifest.insert(QStringLiteral("applicationVersion"), bundle.applicationVersion);
    manifest.insert(QStringLiteral("connectomeVersion"), bundle.connectomeVersion);
    manifest.insert(QStringLiteral("createdUtc"), bundle.createdUtc);
    manifest.insert(QStringLiteral("experimentPresent"), bundle.experiment.valid);
    manifest.insert(QStringLiteral("comparisonPresent"), bundle.comparison.valid);
    manifest.insert(QStringLiteral("epistasisPairs"), bundle.epistasis.size());
    manifest.insert(QStringLiteral("statisticalPairs"), bundle.statistics.size());
    manifest.insert(QStringLiteral("explorerPresent"), bundle.explorer.valid);
    manifest.insert(QStringLiteral("discoveryEntries"), bundle.discovery.entries.size());
    manifest.insert(QStringLiteral("discoveryEpistasisPairs"), bundle.discovery.epistasisEntries.size());
    manifest.insert(QStringLiteral("provenancePresent"), bundle.discovery.provenance.valid);
    manifest.insert(QStringLiteral("causalSurprisePresent"), bundle.causalSurprise.valid);
    const ExperimentResult* modelResult = representativeResult(bundle);
    manifest.insert(QStringLiteral("lifModel"), modelResult ? lifModelToJson(modelResult->lifModel) : QJsonObject());

    QJsonArray circuit;
    const std::vector<CausalNeuronSpec>& specs = validatedCausalCircuit();
    for (std::vector<CausalNeuronSpec>::const_iterator it = specs.begin(); it != specs.end(); ++it)
    {
        QJsonObject c;
        c.insert(QStringLiteral("type"), QString::fromLatin1(it->type));
        c.insert(QStringLiteral("role"), QString::fromLatin1(it->role));
        c.insert(QStringLiteral("rootId"), QString::number(static_cast<qulonglong>(it->rootId)));
        circuit.append(c);
    }
    manifest.insert(QStringLiteral("causalCircuit"), circuit);

    QByteArray stimulusBytes = bundle.embeddedStimulus;
    if (modelResult)
    {
        manifest.insert(QStringLiteral("stimulusName"), modelResult->stimulusName);
        manifest.insert(QStringLiteral("stimulusSource"), modelResult->stimulusSource);
        manifest.insert(QStringLiteral("stimulusPathAtRun"), modelResult->stimulusPath);
        manifest.insert(QStringLiteral("stimulusSha256AtRun"), modelResult->stimulusSha256);
        manifest.insert(QStringLiteral("stimulusRootIds"), rootsToJson(modelResult->stimulusRootIds));

        if (!modelResult->stimulusPath.isEmpty())
        {
            QFile stimulusFile(modelResult->stimulusPath);
            if (stimulusFile.open(QIODevice::ReadOnly))
            {
                const QByteArray candidate = stimulusFile.readAll();
                const QString candidateSha = QString::fromLatin1(
                    QCryptographicHash::hash(candidate, QCryptographicHash::Sha256).toHex());
                if (!candidate.isEmpty() &&
                    (modelResult->stimulusSha256.isEmpty() || candidateSha == modelResult->stimulusSha256))
                    stimulusBytes = candidate;
            }
        }
        manifest.insert(QStringLiteral("stimulusEmbedded"), !stimulusBytes.isEmpty());
    }

    QJsonObject experimentObject;
    experimentObject.insert(QStringLiteral("result"), experimentToJson(bundle.experiment));

    QJsonObject comparisonObject;
    comparisonObject.insert(QStringLiteral("result"), comparisonToJson(bundle.comparison));

    QJsonArray epistasisArray;
    for (int i = 0; i < bundle.epistasis.size(); ++i)
        epistasisArray.append(epistasisToJson(bundle.epistasis.at(i)));
    QJsonObject epistasisObject;
    epistasisObject.insert(QStringLiteral("pairs"), epistasisArray);

    QJsonArray statisticsArray;
    for (int i = 0; i < bundle.statistics.size(); ++i)
        statisticsArray.append(statisticalEpistasisToJson(bundle.statistics.at(i)));
    QJsonObject statisticsObject;
    statisticsObject.insert(QStringLiteral("pairs"), statisticsArray);

    QJsonObject explorerObject;
    explorerObject.insert(QStringLiteral("result"), explorerToJson(bundle.explorer));

    QJsonObject discoveryObject;
    discoveryObject.insert(QStringLiteral("result"), discoveryToJson(bundle.discovery));

    QJsonObject provenanceObject;
    provenanceObject.insert(QStringLiteral("result"), provenanceToJson(bundle.discovery.provenance));

    QJsonObject causalSurpriseObject;
    causalSurpriseObject.insert(QStringLiteral("result"), causalSurpriseToJson(bundle.causalSurprise));

    QVector<ZipEntry> entries;
    ZipEntry e;
    e.name = QByteArray("manifest.json"); e.data = jsonBytes(manifest); entries.append(e);
    e = ZipEntry(); e.name = QByteArray("experiment.json"); e.data = jsonBytes(experimentObject); entries.append(e);
    e = ZipEntry(); e.name = QByteArray("compare.json"); e.data = jsonBytes(comparisonObject); entries.append(e);
    e = ZipEntry(); e.name = QByteArray("epistasis.json"); e.data = jsonBytes(epistasisObject); entries.append(e);
    e = ZipEntry(); e.name = QByteArray("statistics.json"); e.data = jsonBytes(statisticsObject); entries.append(e);
    e = ZipEntry(); e.name = QByteArray("explorer.json"); e.data = jsonBytes(explorerObject); entries.append(e);
    e = ZipEntry(); e.name = QByteArray("discovery.json"); e.data = jsonBytes(discoveryObject); entries.append(e);
    e = ZipEntry(); e.name = QByteArray("provenance.json"); e.data = jsonBytes(provenanceObject); entries.append(e);
    if (bundle.causalSurprise.valid)
    {
        e = ZipEntry(); e.name = QByteArray("causal_surprise.json"); e.data = jsonBytes(causalSurpriseObject); entries.append(e);
    }
    if (!stimulusBytes.isEmpty())
    {
        e = ZipEntry(); e.name = QByteArray("stimulus.txt"); e.data = stimulusBytes; entries.append(e);
    }

    return writeZipStore(filePath, entries, error);
}

bool ExperimentArchive::load(const QString& filePath, ExperimentArchiveBundle* bundle, QString* error)
{
    if (!bundle)
    {
        if (error) *error = QStringLiteral("Archive destination is null.");
        return false;
    }

    QHash<QString, QByteArray> files;
    if (!readZipStore(filePath, &files, error)) return false;
    if (!files.contains(QStringLiteral("manifest.json")))
    {
        if (error) *error = QStringLiteral("manifest.json is missing from the .dro archive.");
        return false;
    }

    QJsonObject manifest;
    if (!parseJsonObject(files.value(QStringLiteral("manifest.json")), &manifest, error, QStringLiteral("manifest.json")))
        return false;
    if (manifest.value(QStringLiteral("format")).toString() != QStringLiteral("drosofilaLive experiment archive"))
    {
        if (error) *error = QStringLiteral("This ZIP file is not a drosofilaLive archive.");
        return false;
    }
    const int formatVersion = manifest.value(QStringLiteral("formatVersion")).toInt();
    if (formatVersion != 1 && formatVersion != 2 && formatVersion != 3)
    {
        if (error) *error = QStringLiteral("Unsupported .dro format version %1.").arg(formatVersion);
        return false;
    }

    ExperimentArchiveBundle out;
    out.formatVersion = formatVersion;
    out.applicationVersion = manifest.value(QStringLiteral("applicationVersion")).toString();
    out.connectomeVersion = manifest.value(QStringLiteral("connectomeVersion")).toString();
    out.createdUtc = manifest.value(QStringLiteral("createdUtc")).toString();
    out.sourcePath = filePath;

    if (files.contains(QStringLiteral("experiment.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("experiment.json")), &o, error, QStringLiteral("experiment.json"))) return false;
        out.experiment = experimentFromJson(o.value(QStringLiteral("result")).toObject());
    }
    if (files.contains(QStringLiteral("compare.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("compare.json")), &o, error, QStringLiteral("compare.json"))) return false;
        out.comparison = comparisonFromJson(o.value(QStringLiteral("result")).toObject());
    }
    if (files.contains(QStringLiteral("epistasis.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("epistasis.json")), &o, error, QStringLiteral("epistasis.json"))) return false;
        const QJsonArray a = o.value(QStringLiteral("pairs")).toArray();
        for (int i = 0; i < a.size(); ++i)
        {
            const EpistasisResult r = epistasisFromJson(a.at(i).toObject());
            if (r.valid) out.epistasis.append(r);
        }
    }
    if (files.contains(QStringLiteral("statistics.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("statistics.json")), &o, error, QStringLiteral("statistics.json"))) return false;
        const QJsonArray a = o.value(QStringLiteral("pairs")).toArray();
        for (int i = 0; i < a.size(); ++i)
        {
            const StatisticalEpistasisResult r = statisticalEpistasisFromJson(a.at(i).toObject());
            if (r.valid) out.statistics.append(r);
        }
    }
    if (formatVersion >= 3 && files.contains(QStringLiteral("explorer.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("explorer.json")), &o, error, QStringLiteral("explorer.json"))) return false;
        out.explorer = explorerFromJson(o.value(QStringLiteral("result")).toObject());
    }
    if (formatVersion >= 3 && files.contains(QStringLiteral("discovery.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("discovery.json")), &o, error, QStringLiteral("discovery.json"))) return false;
        out.discovery = discoveryFromJson(o.value(QStringLiteral("result")).toObject());
    }
    if (formatVersion >= 3 && files.contains(QStringLiteral("provenance.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("provenance.json")), &o, error, QStringLiteral("provenance.json"))) return false;
        out.discovery.provenance = provenanceFromJson(o.value(QStringLiteral("result")).toObject());
    }
    if (formatVersion >= 3 && files.contains(QStringLiteral("causal_surprise.json")))
    {
        QJsonObject o;
        if (!parseJsonObject(files.value(QStringLiteral("causal_surprise.json")), &o, error, QStringLiteral("causal_surprise.json"))) return false;
        out.causalSurprise = causalSurpriseFromJson(o.value(QStringLiteral("result")).toObject());
    }
    if (files.contains(QStringLiteral("stimulus.txt")))
        out.embeddedStimulus = files.value(QStringLiteral("stimulus.txt"));

    if (!out.hasAnyResult())
    {
        if (error) *error = QStringLiteral("Archive contains no valid results.");
        return false;
    }
    *bundle = out;
    return true;
}

bool ExperimentArchive::exportCsv(const QString& baseFilePath, const ExperimentArchiveBundle& bundle,
                                  QStringList* writtenFiles, QString* error)
{
    if (writtenFiles) writtenFiles->clear();
    if (!bundle.hasAnyResult())
    {
        if (error) *error = QStringLiteral("There are no results to export.");
        return false;
    }

    const QString summaryPath = ensureCsvSuffix(baseFilePath);
    QFileInfo fi(summaryPath);
    const QString stem = fi.completeBaseName();
    const QString dir = fi.absolutePath();
    const QString timelinePath = dir + QLatin1Char('/') + stem + QStringLiteral("_timeline.csv");
    const QString epistasisPath = dir + QLatin1Char('/') + stem + QStringLiteral("_epistasis.csv");
    const QString statisticsPath = dir + QLatin1Char('/') + stem + QStringLiteral("_statistics.csv");
    const QString statisticsReplicatesPath = dir + QLatin1Char('/') + stem + QStringLiteral("_statistics_replicates.csv");

    const bool hasLegacyCsvState = bundle.experiment.valid || bundle.comparison.valid ||
                                   !bundle.epistasis.isEmpty() || !bundle.statistics.isEmpty();
    if (hasLegacyCsvState)
    {
    QSaveFile summaryFile(summaryPath);
    if (!summaryFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = QStringLiteral("Cannot create CSV: %1").arg(summaryFile.errorString());
        return false;
    }
    QTextStream summary(&summaryFile);
    summary.setCodec("UTF-8");
    summary << "source,condition,pair_a,pair_b,root_id,type,mean_hz,reserved_std_hz,first_spike_ms,activation_probability,seed,trials,duration_ms,sugar_rate_hz\n";
    writeExperimentSummaryRows(summary, QStringLiteral("experiment"), QStringLiteral("single"), bundle.experiment);
    writeExperimentSummaryRows(summary, QStringLiteral("comparison"), QStringLiteral("reference"), bundle.comparison.reference);
    writeExperimentSummaryRows(summary, QStringLiteral("comparison"), QStringLiteral("perturbed"), bundle.comparison.perturbed);
    for (int i = 0; i < bundle.epistasis.size(); ++i)
    {
        const EpistasisResult& e = bundle.epistasis.at(i);
        writeExperimentSummaryRows(summary, QStringLiteral("epistasis"), QStringLiteral("WT"), e.wildType, e.typeA, e.typeB);
        writeExperimentSummaryRows(summary, QStringLiteral("epistasis"), QStringLiteral("A_OFF"), e.aOff, e.typeA, e.typeB);
        writeExperimentSummaryRows(summary, QStringLiteral("epistasis"), QStringLiteral("B_OFF"), e.bOff, e.typeA, e.typeB);
        writeExperimentSummaryRows(summary, QStringLiteral("epistasis"), QStringLiteral("AB_OFF"), e.doubleOff, e.typeA, e.typeB);
    }
    summary.flush();
    if (!summaryFile.commit())
    {
        if (error) *error = QStringLiteral("Cannot commit summary CSV.");
        return false;
    }
    if (writtenFiles) writtenFiles->append(summaryPath);

    QSaveFile timelineFile(timelinePath);
    if (!timelineFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = QStringLiteral("Cannot create timeline CSV: %1").arg(timelineFile.errorString());
        return false;
    }
    QTextStream timeline(&timelineFile);
    timeline.setCodec("UTF-8");
    timeline << "source,condition,pair_a,pair_b,start_ms,end_ms,root_id,type,rate_hz,spiked\n";
    writeTimelineRows(timeline, QStringLiteral("experiment"), QStringLiteral("single"), bundle.experiment);
    writeTimelineRows(timeline, QStringLiteral("comparison"), QStringLiteral("reference"), bundle.comparison.reference);
    writeTimelineRows(timeline, QStringLiteral("comparison"), QStringLiteral("perturbed"), bundle.comparison.perturbed);
    for (int i = 0; i < bundle.epistasis.size(); ++i)
    {
        const EpistasisResult& e = bundle.epistasis.at(i);
        writeTimelineRows(timeline, QStringLiteral("epistasis"), QStringLiteral("WT"), e.wildType, e.typeA, e.typeB);
        writeTimelineRows(timeline, QStringLiteral("epistasis"), QStringLiteral("A_OFF"), e.aOff, e.typeA, e.typeB);
        writeTimelineRows(timeline, QStringLiteral("epistasis"), QStringLiteral("B_OFF"), e.bOff, e.typeA, e.typeB);
        writeTimelineRows(timeline, QStringLiteral("epistasis"), QStringLiteral("AB_OFF"), e.doubleOff, e.typeA, e.typeB);
    }
    timeline.flush();
    if (!timelineFile.commit())
    {
        if (error) *error = QStringLiteral("Cannot commit timeline CSV.");
        return false;
    }
    if (writtenFiles) writtenFiles->append(timelinePath);

    QSaveFile epistasisFile(epistasisPath);
    if (!epistasisFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = QStringLiteral("Cannot create epistasis CSV: %1").arg(epistasisFile.errorString());
        return false;
    }
    QTextStream epi(&epistasisFile);
    epi.setCodec("UTF-8");
    epi << "neuron_a,neuron_b,wt_hz,a_off_hz,b_off_hz,double_off_hz,expected_additive_hz,interaction_hz\n";
    for (int i = 0; i < bundle.epistasis.size(); ++i)
    {
        const EpistasisResult& e = bundle.epistasis.at(i);
        epi << csvQuote(e.typeA) << ',' << csvQuote(e.typeB) << ','
            << QString::number(e.wildType.targetMeanHz, 'f', 6) << ','
            << QString::number(e.aOff.targetMeanHz, 'f', 6) << ','
            << QString::number(e.bOff.targetMeanHz, 'f', 6) << ','
            << QString::number(e.doubleOff.targetMeanHz, 'f', 6) << ','
            << QString::number(e.expectedAdditiveHz, 'f', 6) << ','
            << QString::number(e.interactionHz, 'f', 6) << '\n';
    }
    epi.flush();
    if (!epistasisFile.commit())
    {
        if (error) *error = QStringLiteral("Cannot commit epistasis CSV.");
        return false;
    }
    if (writtenFiles) writtenFiles->append(epistasisPath);

    QSaveFile statisticsFile(statisticsPath);
    if (!statisticsFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = QStringLiteral("Cannot create statistics CSV: %1").arg(statisticsFile.errorString());
        return false;
    }
    QTextStream stats(&statisticsFile);
    stats.setCodec("UTF-8");
    stats << "neuron_a,neuron_b,n,trials_per_replicate,base_seed,mean_interaction_hz,std_hz,sem_hz,ci95_low_hz,ci95_high_hz,sign_consistency,positive,negative,zero\n";
    for (int i = 0; i < bundle.statistics.size(); ++i)
    {
        const StatisticalEpistasisResult& r = bundle.statistics.at(i);
        stats << csvQuote(r.typeA) << ',' << csvQuote(r.typeB) << ','
              << r.interaction.n << ',' << r.trialsPerReplicate << ',' << r.baseSeed << ','
              << QString::number(r.interaction.mean, 'f', 6) << ','
              << QString::number(r.interaction.stddev, 'f', 6) << ','
              << QString::number(r.interaction.sem, 'f', 6) << ','
              << QString::number(r.interaction.ci95Low, 'f', 6) << ','
              << QString::number(r.interaction.ci95High, 'f', 6) << ','
              << QString::number(r.interaction.signConsistency, 'f', 6) << ','
              << r.interaction.positiveCount << ',' << r.interaction.negativeCount << ',' << r.interaction.zeroCount << '\n';
    }
    stats.flush();
    if (!statisticsFile.commit())
    {
        if (error) *error = QStringLiteral("Cannot commit statistics CSV.");
        return false;
    }
    if (writtenFiles) writtenFiles->append(statisticsPath);

    QSaveFile replicateFile(statisticsReplicatesPath);
    if (!replicateFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = QStringLiteral("Cannot create statistics replicate CSV: %1").arg(replicateFile.errorString());
        return false;
    }
    QTextStream reps(&replicateFile);
    reps.setCodec("UTF-8");
    reps << "neuron_a,neuron_b,replicate,seed,trials_per_condition,wt_hz,a_off_hz,b_off_hz,double_off_hz,expected_additive_hz,interaction_hz,wt_latency_ms,a_off_latency_ms,b_off_latency_ms,double_off_latency_ms\n";
    for (int i = 0; i < bundle.statistics.size(); ++i)
    {
        const StatisticalEpistasisResult& r = bundle.statistics.at(i);
        for (int k = 0; k < r.replicates.size(); ++k)
        {
            const StatisticalEpistasisReplicate& v = r.replicates.at(k);
            reps << csvQuote(r.typeA) << ',' << csvQuote(r.typeB) << ',' << (k + 1) << ',' << v.seed << ',' << r.trialsPerReplicate << ','
                 << QString::number(v.wildTypeHz, 'f', 6) << ',' << QString::number(v.aOffHz, 'f', 6) << ','
                 << QString::number(v.bOffHz, 'f', 6) << ',' << QString::number(v.doubleOffHz, 'f', 6) << ','
                 << QString::number(v.expectedAdditiveHz, 'f', 6) << ',' << QString::number(v.interactionHz, 'f', 6) << ','
                 << QString::number(v.wildTypeLatencyMs, 'f', 6) << ',' << QString::number(v.aOffLatencyMs, 'f', 6) << ','
                 << QString::number(v.bOffLatencyMs, 'f', 6) << ',' << QString::number(v.doubleOffLatencyMs, 'f', 6) << '\n';
        }
    }
    reps.flush();
    if (!replicateFile.commit())
    {
        if (error) *error = QStringLiteral("Cannot commit statistics replicate CSV.");
        return false;
    }
    if (writtenFiles) writtenFiles->append(statisticsReplicatesPath);
    }

    if (bundle.explorer.valid)
    {
        const QString explorerPath = dir + QLatin1Char('/') + stem + QStringLiteral("_explorer.csv");
        QSaveFile file(explorerPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            if (error) *error = QStringLiteral("Cannot create Explorer CSV: %1").arg(file.errorString());
            return false;
        }
        QTextStream out(&file); out.setCodec("UTF-8");
        out << "rank,root_id,type,additional_types,sign,depth,upstream,downstream,path_count,synapse_strength,direct_synapses,root_fraction,graph_score\n";
        for (int i = 0; i < bundle.explorer.candidates.size(); ++i)
        {
            const CircuitExploreCandidate& c = bundle.explorer.candidates.at(i);
            out << (i + 1) << ',' << static_cast<qulonglong>(c.rootId) << ',' << csvQuote(c.primaryType) << ',' << csvQuote(c.additionalTypes) << ','
                << static_cast<int>(c.sign) << ',' << c.depth << ',' << (c.upstream ? 1 : 0) << ',' << (c.downstream ? 1 : 0) << ','
                << static_cast<qulonglong>(c.pathCount) << ',' << static_cast<qulonglong>(c.synapseStrength) << ',' << c.directSynapses << ','
                << QString::number(c.rootFraction, 'f', 9) << ',' << QString::number(c.score, 'f', 9) << '\n';
        }
        out.flush();
        if (!file.commit()) { if (error) *error = QStringLiteral("Cannot commit Explorer CSV."); return false; }
        if (writtenFiles) writtenFiles->append(explorerPath);
    }

    if (!bundle.discovery.entries.isEmpty())
    {
        const QString discoveryPath = dir + QLatin1Char('/') + stem + QStringLiteral("_discovery.csv");
        QSaveFile file(discoveryPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            if (error) *error = QStringLiteral("Cannot create Discovery CSV: %1").arg(file.errorString());
            return false;
        }
        QTextStream out(&file); out.setCodec("UTF-8");
        out << "causal_rank,root_id,type,graph_rank,depth,graph_score,synapse_strength,screen_n,screen_requested,screen_mean_delta_hz,screen_std_hz,screen_ci95_low_hz,screen_ci95_high_hz,screen_sign_consistency,validation_attempted,validation_valid,validation_n,validation_requested,validation_mean_delta_hz,validation_std_hz,validation_sem_hz,validation_ci95_low_hz,validation_ci95_high_hz,validation_sign_consistency,error,validation_error\n";
        for (int i = 0; i < bundle.discovery.entries.size(); ++i)
        {
            const CausalDiscoveryEntry& e = bundle.discovery.entries.at(i);
            out << e.causalRank << ',' << static_cast<qulonglong>(e.candidate.rootId) << ',' << csvQuote(e.candidate.type) << ','
                << e.candidate.graphRank << ',' << e.candidate.depth << ',' << QString::number(e.candidate.graphScore, 'f', 9) << ','
                << static_cast<qulonglong>(e.candidate.synapseStrength) << ',' << e.screenReplicatesCompleted << ',' << e.screenReplicatesRequested << ','
                << QString::number(e.screenDelta.mean, 'f', 9) << ',' << QString::number(e.screenDelta.stddev, 'f', 9) << ','
                << QString::number(e.screenDelta.ci95Low, 'f', 9) << ',' << QString::number(e.screenDelta.ci95High, 'f', 9) << ','
                << QString::number(e.screenDelta.signConsistency, 'f', 9) << ',' << (e.validationAttempted ? 1 : 0) << ',' << (e.validationValid ? 1 : 0) << ','
                << e.validationReplicatesCompleted << ',' << e.validationReplicatesRequested << ',' << QString::number(e.validationDelta.mean, 'f', 9) << ','
                << QString::number(e.validationDelta.stddev, 'f', 9) << ',' << QString::number(e.validationDelta.sem, 'f', 9) << ','
                << QString::number(e.validationDelta.ci95Low, 'f', 9) << ',' << QString::number(e.validationDelta.ci95High, 'f', 9) << ','
                << QString::number(e.validationDelta.signConsistency, 'f', 9) << ',' << csvQuote(e.error) << ',' << csvQuote(e.validationError) << '\n';
        }
        out.flush();
        if (!file.commit()) { if (error) *error = QStringLiteral("Cannot commit Discovery CSV."); return false; }
        if (writtenFiles) writtenFiles->append(discoveryPath);
    }

    if (!bundle.discovery.epistasisEntries.isEmpty())
    {
        const QString discoveryEpiPath = dir + QLatin1Char('/') + stem + QStringLiteral("_discovery_epistasis.csv");
        QSaveFile file(discoveryEpiPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            if (error) *error = QStringLiteral("Cannot create Discovery epistasis CSV: %1").arg(file.errorString());
            return false;
        }
        QTextStream out(&file); out.setCodec("UTF-8");
        out << "interaction_rank,root_a,type_a,root_b,type_b,causal_rank_a,causal_rank_b,validated_delta_a_hz,validated_delta_b_hz,n,requested,mean_interaction_hz,std_hz,sem_hz,ci95_low_hz,ci95_high_hz,sign_consistency,error\n";
        for (int i = 0; i < bundle.discovery.epistasisEntries.size(); ++i)
        {
            const CausalDiscoveryEpistasisEntry& e = bundle.discovery.epistasisEntries.at(i);
            out << e.interactionRank << ',' << static_cast<qulonglong>(e.rootA) << ',' << csvQuote(e.typeA) << ','
                << static_cast<qulonglong>(e.rootB) << ',' << csvQuote(e.typeB) << ',' << e.causalRankA << ',' << e.causalRankB << ','
                << QString::number(e.validatedDeltaAHz, 'f', 9) << ',' << QString::number(e.validatedDeltaBHz, 'f', 9) << ','
                << e.replicatesCompleted << ',' << e.replicatesRequested << ',' << QString::number(e.interaction.mean, 'f', 9) << ','
                << QString::number(e.interaction.stddev, 'f', 9) << ',' << QString::number(e.interaction.sem, 'f', 9) << ','
                << QString::number(e.interaction.ci95Low, 'f', 9) << ',' << QString::number(e.interaction.ci95High, 'f', 9) << ','
                << QString::number(e.interaction.signConsistency, 'f', 9) << ',' << csvQuote(e.error) << '\n';
        }
        out.flush();
        if (!file.commit()) { if (error) *error = QStringLiteral("Cannot commit Discovery epistasis CSV."); return false; }
        if (writtenFiles) writtenFiles->append(discoveryEpiPath);
    }

    if (bundle.causalSurprise.valid)
    {
        const QString surprisePath = dir + QLatin1Char('/') + stem + QStringLiteral("_causal_surprise.csv");
        QSaveFile file(surprisePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            if (error) *error = QStringLiteral("Cannot create Causal Surprise CSV: %1").arg(file.errorString());
            return false;
        }
        QTextStream out(&file); out.setCodec("UTF-8");
        out << "surprise_rank,root_id,type,graph_rank,causal_rank,depth,graph_score,path_count,synapse_strength,direct_synapses,validation_delta_hz,abs_causal_effect_hz,ci95_low_hz,ci95_high_hz,sign_consistency,structural_percentile,causal_percentile,surprise_score,gate_passed,quadrant\n";
        for (int i = 0; i < bundle.causalSurprise.entries.size(); ++i)
        {
            const CausalSurpriseEntry& e = bundle.causalSurprise.entries.at(i);
            out << e.surpriseRank << ',' << static_cast<qulonglong>(e.rootId) << ',' << csvQuote(e.type) << ','
                << e.graphRank << ',' << e.causalRank << ',' << e.depth << ',' << QString::number(e.graphScore, 'f', 9) << ','
                << static_cast<qulonglong>(e.pathCount) << ',' << static_cast<qulonglong>(e.synapseStrength) << ',' << e.directSynapses << ','
                << QString::number(e.validationDeltaHz, 'f', 9) << ',' << QString::number(e.absoluteCausalEffectHz, 'f', 9) << ','
                << QString::number(e.ci95LowHz, 'f', 9) << ',' << QString::number(e.ci95HighHz, 'f', 9) << ','
                << QString::number(e.signConsistency, 'f', 9) << ',' << QString::number(e.structuralPercentile, 'f', 9) << ','
                << QString::number(e.causalPercentile, 'f', 9) << ',' << QString::number(e.surpriseScore, 'f', 9) << ','
                << (e.gatePassed ? 1 : 0) << ',' << csvQuote(e.quadrant) << '\n';
        }
        out.flush();
        if (!file.commit()) { if (error) *error = QStringLiteral("Cannot commit Causal Surprise CSV."); return false; }
        if (writtenFiles) writtenFiles->append(surprisePath);
    }

    if (bundle.discovery.provenance.valid)
    {
        const QString provenancePath = dir + QLatin1Char('/') + stem + QStringLiteral("_provenance.csv");
        QSaveFile file(provenancePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            if (error) *error = QStringLiteral("Cannot create provenance CSV: %1").arg(file.errorString());
            return false;
        }
        QTextStream out(&file); out.setCodec("UTF-8");
        const CausalDiscoveryProvenance& p = bundle.discovery.provenance;
        out << "field,value\n";
        out << "origin," << csvQuote(p.origin) << '\n';
        out << "started_utc," << csvQuote(p.startedUtc) << '\n';
        out << "updated_utc," << csvQuote(p.updatedUtc) << '\n';
        out << "explorer_root," << csvQuote(p.explorerParameters.rootText) << '\n';
        out << "explorer_direction," << p.explorerParameters.direction << '\n';
        out << "explorer_max_depth," << p.explorerParameters.maxDepth << '\n';
        out << "explorer_min_synapses," << p.explorerParameters.minimumSynapses << '\n';
        out << "explorer_max_neurons," << p.explorerParameters.maximumNeurons << '\n';
        out << "selection_mode," << csvQuote(p.selectionMode) << '\n';
        out << "selection_top_n," << p.selectionTopN << '\n';
        out << "selected_candidates," << p.selectedCandidates << '\n';
        out << "validation_top_n," << p.validationTopN << '\n';
        out << "epistasis_top_n," << p.epistasisTopN << '\n';
        out << "epistasis_gate," << csvQuote(p.epistasisGateMode) << '\n';
        out << "epistasis_min_sign_consistency," << QString::number(p.epistasisMinimumSignConsistency, 'f', 9) << '\n';
        if (p.screenParametersValid)
        {
            out << "screen_seed," << p.screenParameters.randomSeed << '\n';
            out << "screen_duration_ms," << QString::number(p.screenParameters.durationMs, 'f', 3) << '\n';
            out << "screen_sugar_hz," << QString::number(p.screenParameters.sugarRateHz, 'f', 6) << '\n';
            out << "screen_trials," << p.screenParameters.trials << '\n';
        }
        if (p.validationParametersValid) out << "validation_seed," << p.validationParameters.randomSeed << '\n';
        if (p.epistasisParametersValid) out << "epistasis_seed," << p.epistasisParameters.randomSeed << '\n';
        out.flush();
        if (!file.commit()) { if (error) *error = QStringLiteral("Cannot commit provenance CSV."); return false; }
        if (writtenFiles) writtenFiles->append(provenancePath);
    }
    return true;
}

QString ExperimentArchive::summary(const ExperimentArchiveBundle& b)
{
    QStringList lines;
    lines << QStringLiteral("drosofilaLive Experiment Archive")
          << QStringLiteral("Format: .dro v%1 (standard ZIP / stored entries)").arg(b.formatVersion)
          << QStringLiteral("Application: %1").arg(b.applicationVersion.isEmpty() ? QStringLiteral("-") : b.applicationVersion)
          << QStringLiteral("Connectome: %1").arg(b.connectomeVersion.isEmpty() ? QStringLiteral("-") : b.connectomeVersion)
          << QStringLiteral("Created UTC: %1").arg(b.createdUtc.isEmpty() ? QStringLiteral("-") : b.createdUtc)
          << QStringLiteral("");

    if (b.experiment.valid)
        lines << QStringLiteral("Experiment: yes | TARGET %1 Hz | Sugar %2 Hz | seed %3 | trials %4")
                    .arg(b.experiment.targetMeanHz, 0, 'f', 2)
                    .arg(b.experiment.sugarRateHz, 0, 'f', 1)
                    .arg(b.experiment.randomSeed)
                    .arg(b.experiment.trialsCompleted);
    else
        lines << QStringLiteral("Experiment: no");

    if (b.comparison.valid)
        lines << QStringLiteral("Comparison: yes | ref %1 Hz | pert %2 Hz | delta %3 Hz | onset %4 ms")
                    .arg(b.comparison.reference.targetMeanHz, 0, 'f', 2)
                    .arg(b.comparison.perturbed.targetMeanHz, 0, 'f', 2)
                    .arg(b.comparison.targetDeltaHz, 0, 'f', 2)
                    .arg(b.comparison.sustainedOnsetMs, 0, 'f', 1);
    else
        lines << QStringLiteral("Comparison: no");

    lines << QStringLiteral("Epistasis matrix: %1 pair(s)").arg(b.epistasis.size())
          << QStringLiteral("Statistical validation: %1 pair(s)").arg(b.statistics.size())
          << QStringLiteral("Circuit Explorer: %1%2")
                .arg(b.explorer.valid ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(b.explorer.valid ? QStringLiteral(" | %1 candidate(s), %2 edge(s)").arg(b.explorer.candidates.size()).arg(b.explorer.edges.size()) : QString())
          << QStringLiteral("Discovery: %1 screen row(s) | %2 validation root(s) | %3 epistasis pair(s)")
                .arg(b.discovery.entries.size()).arg(b.discovery.validationRootIds.size()).arg(b.discovery.epistasisEntries.size())
          << QStringLiteral("Causal Surprise: %1%2")
                .arg(b.causalSurprise.valid ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(b.causalSurprise.valid ? QStringLiteral(" | %1 validation row(s), %2 gate-passed").arg(b.causalSurprise.validationRows).arg(b.causalSurprise.gatePassedRows) : QString())
          << QStringLiteral("Embedded stimulus: %1%2")
                .arg(b.embeddedStimulus.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"))
                .arg(b.embeddedStimulus.isEmpty() ? QString() : QStringLiteral(" | %1 bytes").arg(b.embeddedStimulus.size()));
    if (b.discovery.provenance.valid)
        lines << QStringLiteral("Provenance: %1 | selection %2 top %3 | gate %4 | started %5")
                    .arg(b.discovery.provenance.origin.isEmpty() ? QStringLiteral("-") : b.discovery.provenance.origin)
                    .arg(b.discovery.provenance.selectionMode.isEmpty() ? QStringLiteral("-") : b.discovery.provenance.selectionMode)
                    .arg(b.discovery.provenance.selectionTopN)
                    .arg(b.discovery.provenance.epistasisGateMode.isEmpty() ? QStringLiteral("NONE") : b.discovery.provenance.epistasisGateMode)
                    .arg(b.discovery.provenance.startedUtc.isEmpty() ? QStringLiteral("-") : b.discovery.provenance.startedUtc);
    if (!b.sourcePath.isEmpty())
        lines << QStringLiteral("File: %1").arg(QFileInfo(b.sourcePath).fileName());
    return lines.join(QLatin1Char('\n'));
}
}
