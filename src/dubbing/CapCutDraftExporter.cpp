#include "dubbing/CapCutDraftExporter.h"
#include "dubbing/DubbingSubtitleService.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace LAStudio {
namespace {

constexpr qint64 kMicrosecondsPerMillisecond = 1000;

QString newId()
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    id.remove(QLatin1Char('-'));
    return id;
}

QString safeFolderName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")),
                 QStringLiteral("-"));
    name = name.simplified();
    if (name.isEmpty()) name = QStringLiteral("LA-Studio-Dubbing");
    return name.left(80);
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool copyAsset(const QString &sourcePath, const QString &destinationPath,
               bool required, QString *error)
{
    if (sourcePath.trimmed().isEmpty()) {
        return required ? fail(error, QStringLiteral("A required export asset is missing.")) : true;
    }
    const QFileInfo source(sourcePath);
    if (!source.isFile()) {
        return required ? fail(error, QStringLiteral("A required export asset is unavailable: %1")
                                           .arg(source.absoluteFilePath()))
                        : true;
    }
    if (!QDir().mkpath(QFileInfo(destinationPath).absolutePath()))
        return fail(error, QStringLiteral("Cannot create CapCut asset folder."));
    if (!QFile::copy(source.absoluteFilePath(), destinationPath))
        return fail(error, QStringLiteral("Cannot copy export asset %1: %2")
                           .arg(source.fileName(), QFile(destinationPath).errorString()));
    return true;
}

bool writeJson(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        return fail(error, QStringLiteral("Cannot write %1: %2")
                           .arg(QFileInfo(path).fileName(), file.errorString()));
    }
    return true;
}

bool writeText(const QString &path, const QString &text, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(text.toUtf8()) < 0
        || !file.commit()) {
        return fail(error, QStringLiteral("Cannot write %1: %2")
                           .arg(QFileInfo(path).fileName(), file.errorString()));
    }
    return true;
}

QJsonObject timerange(qint64 startUs, qint64 durationUs)
{
    return {{QStringLiteral("start"), qMax<qint64>(0, startUs)},
            {QStringLiteral("duration"), qMax<qint64>(0, durationUs)}};
}

QJsonObject speedMaterial(const QString &id)
{
    return {{QStringLiteral("curve_speed"), QJsonValue::Null},
            {QStringLiteral("id"), id},
            {QStringLiteral("mode"), 0},
            {QStringLiteral("speed"), 1.0},
            {QStringLiteral("type"), QStringLiteral("speed")}};
}

QJsonObject audioMaterial(const QString &id, const QString &path, const QString &name,
                          qint64 durationUs)
{
    return {{QStringLiteral("app_id"), 0},
            {QStringLiteral("category_id"), QString()},
            {QStringLiteral("category_name"), QStringLiteral("local")},
            {QStringLiteral("check_flag"), 3},
            {QStringLiteral("copyright_limit_type"), QStringLiteral("none")},
            {QStringLiteral("duration"), durationUs},
            {QStringLiteral("effect_id"), QString()},
            {QStringLiteral("formula_id"), QString()},
            {QStringLiteral("id"), id},
            {QStringLiteral("local_material_id"), id},
            {QStringLiteral("music_id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("path"), path},
            {QStringLiteral("source_platform"), 0},
            {QStringLiteral("type"), QStringLiteral("extract_music")},
            {QStringLiteral("wave_points"), QJsonArray{}}};
}

QJsonObject videoMaterial(const QString &id, const QString &path, const QString &name,
                          qint64 durationUs)
{
    const QJsonObject crop{{QStringLiteral("upper_left_x"), 0.0},
                           {QStringLiteral("upper_left_y"), 0.0},
                           {QStringLiteral("upper_right_x"), 1.0},
                           {QStringLiteral("upper_right_y"), 0.0},
                           {QStringLiteral("lower_left_x"), 0.0},
                           {QStringLiteral("lower_left_y"), 1.0},
                           {QStringLiteral("lower_right_x"), 1.0},
                           {QStringLiteral("lower_right_y"), 1.0}};
    return {{QStringLiteral("audio_fade"), QJsonValue::Null},
            {QStringLiteral("category_id"), QString()},
            {QStringLiteral("category_name"), QStringLiteral("local")},
            {QStringLiteral("check_flag"), 63487},
            {QStringLiteral("crop"), crop},
            {QStringLiteral("crop_ratio"), QStringLiteral("free")},
            {QStringLiteral("crop_scale"), 1.0},
            {QStringLiteral("duration"), durationUs},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("id"), id},
            {QStringLiteral("local_material_id"), id},
            {QStringLiteral("material_id"), id},
            {QStringLiteral("material_name"), name},
            {QStringLiteral("media_path"), QString()},
            {QStringLiteral("path"), path},
            {QStringLiteral("type"), QStringLiteral("video")},
            {QStringLiteral("width"), 1920}};
}

QJsonObject mediaSegment(const QString &materialId, qint64 targetStartUs,
                         qint64 targetDurationUs, bool video, QJsonArray *speeds,
                         double volume = 1.0)
{
    const QString speedId = newId();
    if (speeds) speeds->append(speedMaterial(speedId));
    QJsonObject result{{QStringLiteral("enable_adjust"), true},
                       {QStringLiteral("enable_color_correct_adjust"), false},
                       {QStringLiteral("enable_color_curves"), true},
                       {QStringLiteral("enable_color_match_adjust"), false},
                       {QStringLiteral("enable_color_wheels"), true},
                       {QStringLiteral("enable_lut"), true},
                       {QStringLiteral("enable_smart_color_adjust"), false},
                       {QStringLiteral("last_nonzero_volume"), 1.0},
                       {QStringLiteral("reverse"), false},
                       {QStringLiteral("track_attribute"), 0},
                       {QStringLiteral("track_render_index"), 0},
                       {QStringLiteral("visible"), true},
                       {QStringLiteral("id"), newId()},
                       {QStringLiteral("material_id"), materialId},
                       {QStringLiteral("target_timerange"), timerange(targetStartUs, targetDurationUs)},
                       {QStringLiteral("source_timerange"), timerange(0, targetDurationUs)},
                       {QStringLiteral("speed"), 1.0},
                       {QStringLiteral("volume"), qBound(0.0, volume, 4.0)},
                       {QStringLiteral("extra_material_refs"), QJsonArray{speedId}},
                       {QStringLiteral("common_keyframes"), QJsonArray{}},
                       {QStringLiteral("keyframe_refs"), QJsonArray{}},
                       {QStringLiteral("render_index"), 0}};
    if (video) {
        result.insert(QStringLiteral("clip"), QJsonObject{
            {QStringLiteral("alpha"), 1.0},
            {QStringLiteral("flip"), QJsonObject{{QStringLiteral("horizontal"), false},
                                                   {QStringLiteral("vertical"), false}}},
            {QStringLiteral("rotation"), 0.0},
            {QStringLiteral("scale"), QJsonObject{{QStringLiteral("x"), 1.0},
                                                    {QStringLiteral("y"), 1.0}}},
            {QStringLiteral("transform"), QJsonObject{{QStringLiteral("x"), 0.0},
                                                        {QStringLiteral("y"), 0.0}}}});
        result.insert(QStringLiteral("uniform_scale"), QJsonObject{{QStringLiteral("on"), true},
                                                                      {QStringLiteral("value"), 1.0}});
        result.insert(QStringLiteral("hdr_settings"), QJsonObject{{QStringLiteral("intensity"), 1.0},
                                                                     {QStringLiteral("mode"), 1},
                                                                     {QStringLiteral("nits"), 1000}});
    } else {
        result.insert(QStringLiteral("clip"), QJsonValue::Null);
        result.insert(QStringLiteral("hdr_settings"), QJsonValue::Null);
    }
    return result;
}

QString capCutColor(const QString &value, const QString &fallback, double opacity = 1.0)
{
    QColor color(value);
    if (!color.isValid()) color = QColor(fallback);
    color.setAlphaF(qBound(0.0, opacity, 1.0) * color.alphaF());
    return QStringLiteral("#%1%2%3%4")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .arg(color.alpha(), 2, 16, QLatin1Char('0')).toUpper();
}

QJsonArray capCutRgb(const QString &value, const QString &fallback)
{
    QColor color(value);
    if (!color.isValid()) color = QColor(fallback);
    return {color.redF(), color.greenF(), color.blueF()};
}

int capCutAlignment(const QVariantMap &style)
{
    const QString alignment = style.value(QStringLiteral("alignment")).toString();
    if (alignment == QStringLiteral("custom")) {
        const double x = style.value(QStringLiteral("positionX"), 0.5).toDouble();
        return x < 0.34 ? 0 : x > 0.66 ? 2 : 1;
    }
    return 1;
}

QJsonObject textMaterial(const QString &id, const QString &text, const QVariantMap &style)
{
    const QString fontPath = style.value(QStringLiteral("fontFile")).toString();
    const int utf16ByteLength = text.size() * 2;
    const QJsonObject fill{
        {QStringLiteral("content"), QJsonObject{
            {QStringLiteral("solid"), QJsonObject{
                {QStringLiteral("color"), capCutRgb(
                    style.value(QStringLiteral("textColor")).toString(),
                    QStringLiteral("#FFFFFFFF"))}}}}}
    };
    const QJsonObject rangeStyle{
        {QStringLiteral("range"), QJsonArray{0, utf16ByteLength}},
        {QStringLiteral("fill"), fill},
        {QStringLiteral("font"), QJsonObject{{QStringLiteral("id"), QString()},
                                                {QStringLiteral("path"), fontPath}}},
        {QStringLiteral("size"), style.value(QStringLiteral("fontSize")).toInt()},
        {QStringLiteral("bold"), style.value(QStringLiteral("fontWeight")).toInt() >= 600},
        {QStringLiteral("italic"), false},
        {QStringLiteral("underline"), false}
    };
    const QJsonObject content{
        {QStringLiteral("text"), text},
        {QStringLiteral("styles"), QJsonArray{rangeStyle}},
        {QStringLiteral("layer_weight"), 1},
        {QStringLiteral("effect"), QJsonArray{}}
    };
    return {{QStringLiteral("id"), id},
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("content"), QString::fromUtf8(
                QJsonDocument(content).toJson(QJsonDocument::Compact))},
            {QStringLiteral("font_name"), style.value(QStringLiteral("fontFamily")).toString()},
            {QStringLiteral("font_path"), fontPath},
            {QStringLiteral("font_size"), style.value(QStringLiteral("fontSize")).toDouble()},
            {QStringLiteral("text_color"), capCutColor(
                style.value(QStringLiteral("textColor")).toString(), QStringLiteral("#FFFFFFFF"))},
            {QStringLiteral("text_alpha"), 1.0},
            {QStringLiteral("border_color"), capCutColor(
                style.value(QStringLiteral("outlineColor")).toString(), QStringLiteral("#00000000"))},
            {QStringLiteral("border_width"), style.value(QStringLiteral("outlineWidth")).toDouble()},
            {QStringLiteral("border_alpha"), 1.0},
            {QStringLiteral("background_color"), capCutColor(
                style.value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#00000000"),
                style.value(QStringLiteral("backgroundOpacity")).toDouble())},
            {QStringLiteral("background_alpha"),
                style.value(QStringLiteral("backgroundOpacity")).toDouble()},
            {QStringLiteral("background_style"), 0},
            {QStringLiteral("background_round_radius"), 0.0},
            {QStringLiteral("background_width"), style.value(QStringLiteral("maxWidth")).toDouble()},
            {QStringLiteral("background_height"), 0.14},
            {QStringLiteral("background_horizontal_offset"), 0.0},
            {QStringLiteral("background_vertical_offset"), 0.0},
            {QStringLiteral("has_shadow"),
                style.value(QStringLiteral("shadowOffset")).toInt() > 0},
            {QStringLiteral("shadow_alpha"), 1.0},
            {QStringLiteral("shadow_angle"), -45.0},
            {QStringLiteral("shadow_color"), capCutColor(
                style.value(QStringLiteral("shadowColor")).toString(), QStringLiteral("#00000000"))},
            {QStringLiteral("shadow_distance"), style.value(QStringLiteral("shadowOffset")).toDouble()},
            {QStringLiteral("shadow_smoothing"), 1.0},
            {QStringLiteral("text_alignment"), capCutAlignment(style)},
            {QStringLiteral("vertical"), false},
            {QStringLiteral("fixed_width"), -1.0},
            {QStringLiteral("fixed_height"), -1.0},
            {QStringLiteral("letter_spacing"), 0.0},
            {QStringLiteral("line_feed"), 1},
            {QStringLiteral("line_spacing"), style.value(QStringLiteral("lineSpacing")).toDouble()},
            {QStringLiteral("is_rich_text"), false},
            {QStringLiteral("use_effect_default_color"), false}};
}

QJsonObject textSegment(const QString &materialId, qint64 targetStartUs,
                        qint64 targetDurationUs, const QVariantMap &style)
{
    QJsonObject result = mediaSegment(materialId, targetStartUs, targetDurationUs, false, nullptr);
    const double positionX = style.value(QStringLiteral("positionX"), 0.5).toDouble();
    const double positionY = style.value(QStringLiteral("positionY"), 0.9).toDouble();
    result.insert(QStringLiteral("clip"), QJsonObject{
        {QStringLiteral("alpha"), 1.0},
        {QStringLiteral("flip"), QJsonObject{{QStringLiteral("horizontal"), false},
                                               {QStringLiteral("vertical"), false}}},
        {QStringLiteral("rotation"), 0.0},
        {QStringLiteral("scale"), QJsonObject{{QStringLiteral("x"), 1.0},
                                                {QStringLiteral("y"), 1.0}}},
        {QStringLiteral("transform"), QJsonObject{{QStringLiteral("x"), (positionX - 0.5) * 2.0},
                                                    {QStringLiteral("y"), (0.5 - positionY) * 2.0}}}});
    result.insert(QStringLiteral("extra_material_refs"), QJsonArray{});
    return result;
}

QJsonObject track(const QString &type, const QString &name, const QJsonArray &segments)
{
    return {{QStringLiteral("attribute"), 0},
            {QStringLiteral("flag"), 0},
            {QStringLiteral("id"), newId()},
            {QStringLiteral("is_default_name"), false},
            {QStringLiteral("name"), name},
            {QStringLiteral("segments"), segments},
            {QStringLiteral("type"), type}};
}

QString srtTimestamp(qint64 milliseconds)
{
    milliseconds = qMax<qint64>(0, milliseconds);
    const qint64 hours = milliseconds / 3600000;
    milliseconds %= 3600000;
    const qint64 minutes = milliseconds / 60000;
    milliseconds %= 60000;
    const qint64 seconds = milliseconds / 1000;
    milliseconds %= 1000;
    return QStringLiteral("%1:%2:%3,%4").arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}

QString subtitles(const QVariantList &segments, bool target)
{
    QString result;
    int index = 1;
    for (const QVariant &value : segments) {
        const QVariantMap segment = value.toMap();
        const QString text = segment.value(target ? QStringLiteral("targetText")
                                                   : QStringLiteral("sourceText")).toString().trimmed();
        const qint64 start = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 end = segment.value(QStringLiteral("endMs")).toLongLong();
        if (text.isEmpty() || end <= start) continue;
        result += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(index++)
            .arg(srtTimestamp(start), srtTimestamp(end), text);
    }
    return result;
}

bool validateDraft(const QString &draftPath, const QString &publishedDraftPath, QString *error)
{
    const QString publishedRoot = QDir::cleanPath(QDir::fromNativeSeparators(publishedDraftPath));
    const auto validatePublishedAsset = [&](const QString &publishedAssetPath,
                                            const QString &description) {
        if (publishedAssetPath.trimmed().isEmpty()) return true;
        const QString publishedAsset = QDir::cleanPath(
            QDir::fromNativeSeparators(publishedAssetPath));
        const QString rootPrefix = publishedRoot + QLatin1Char('/');
        if (!publishedAsset.startsWith(rootPrefix)) {
            return fail(error, QStringLiteral("CapCut draft %1 refers outside the published draft folder.")
                                   .arg(description));
        }
        const QString relativePath = QDir(publishedRoot).relativeFilePath(publishedAsset);
        if (relativePath == QStringLiteral(".") || relativePath.startsWith(QStringLiteral("../"))) {
            return fail(error, QStringLiteral("CapCut draft %1 has an unsafe asset path.")
                                   .arg(description));
        }
        if (!QFileInfo(QDir(draftPath).filePath(relativePath)).isFile()) {
            return fail(error, QStringLiteral("CapCut draft %1 was not copied into the draft folder.")
                                   .arg(description));
        }
        return true;
    };

    QFile contentFile(QDir(draftPath).filePath(QStringLiteral("draft_content.json")));
    if (!contentFile.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("CapCut draft content was not written."));
    QJsonParseError parseError;
    const QJsonDocument content = QJsonDocument::fromJson(contentFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !content.isObject())
        return fail(error, QStringLiteral("CapCut draft content is invalid JSON."));
    const QJsonObject root = content.object();
    const QJsonObject materials = root.value(QStringLiteral("materials")).toObject();
    const QJsonArray tracks = root.value(QStringLiteral("tracks")).toArray();
    if (materials.isEmpty() || tracks.isEmpty())
        return fail(error, QStringLiteral("CapCut draft has no materials or tracks."));
    QHash<QString, QString> materialKindsById;
    for (const QString &kind : {QStringLiteral("videos"), QStringLiteral("audios")}) {
        for (const QJsonValue &value : materials.value(kind).toArray()) {
            const QJsonObject material = value.toObject();
            const QString id = material.value(QStringLiteral("id")).toString();
            const QString assetPath = material.value(QStringLiteral("path")).toString();
            if (assetPath.isEmpty())
                return fail(error, QStringLiteral("CapCut draft references a missing %1 asset.").arg(kind));
            if (!validatePublishedAsset(assetPath, QStringLiteral("%1 material").arg(kind)))
                return false;
            if (id.isEmpty() || materialKindsById.contains(id))
                return fail(error, QStringLiteral("CapCut draft has a duplicate or missing material identifier."));
            materialKindsById.insert(id, kind);
        }
    }
    const QJsonArray textMaterials = materials.value(QStringLiteral("texts")).toArray();
    if (textMaterials.isEmpty())
        return fail(error, QStringLiteral("CapCut draft has no editable subtitle text materials."));
    for (const QJsonValue &value : textMaterials) {
        const QJsonObject material = value.toObject();
        const QString id = material.value(QStringLiteral("id")).toString();
        QJsonParseError textError;
        const QJsonDocument textContent = QJsonDocument::fromJson(
            material.value(QStringLiteral("content")).toString().toUtf8(), &textError);
        if (id.isEmpty() || material.value(QStringLiteral("type")).toString() != QStringLiteral("text")
            || textError.error != QJsonParseError::NoError || !textContent.isObject()
            || textContent.object().value(QStringLiteral("text")).toString().trimmed().isEmpty()
            || materialKindsById.contains(id)) {
            return fail(error, QStringLiteral("CapCut editable subtitle material is invalid."));
        }
        const QJsonArray styles = textContent.object().value(QStringLiteral("styles")).toArray();
        for (const QJsonValue &styleValue : styles) {
            const QString fontPath = styleValue.toObject().value(QStringLiteral("font")).toObject()
                .value(QStringLiteral("path")).toString();
            if (!validatePublishedAsset(fontPath, QStringLiteral("subtitle font"))) return false;
        }
        materialKindsById.insert(id, QStringLiteral("texts"));
    }
    bool textTrackFound = false;
    QSet<QString> referencedTextMaterials;
    for (const QJsonValue &trackValue : tracks) {
        const QJsonObject trackObject = trackValue.toObject();
        const QString trackType = trackObject.value(QStringLiteral("type")).toString();
        if (trackType == QStringLiteral("text")) textTrackFound = true;
        for (const QJsonValue &segmentValue : trackObject.value(QStringLiteral("segments")).toArray()) {
            const QJsonObject segment = segmentValue.toObject();
            const QString materialId = segment.value(QStringLiteral("material_id")).toString();
            const QJsonObject target = segment.value(QStringLiteral("target_timerange")).toObject();
            const QString materialKind = materialKindsById.value(materialId);
            const bool correctKind = (trackType == QStringLiteral("video") && materialKind == QStringLiteral("videos"))
                || (trackType == QStringLiteral("audio") && materialKind == QStringLiteral("audios"))
                || (trackType == QStringLiteral("text") && materialKind == QStringLiteral("texts"));
            if (!correctKind || target.value(QStringLiteral("start")).toVariant().toLongLong() < 0
                || target.value(QStringLiteral("duration")).toVariant().toLongLong() <= 0) {
                return fail(error, QStringLiteral("CapCut draft has an invalid track-to-material timing reference."));
            }
            if (trackType == QStringLiteral("text")) referencedTextMaterials.insert(materialId);
        }
    }
    if (!textTrackFound || referencedTextMaterials.size() != textMaterials.size())
        return fail(error, QStringLiteral("CapCut draft has no editable subtitle text track."));
    if (!QFileInfo(QDir(draftPath).filePath(QStringLiteral("draft_meta_info.json"))).isFile())
        return fail(error, QStringLiteral("CapCut draft metadata was not written."));
    if (!QFileInfo(QDir(draftPath).filePath(QStringLiteral("LA_STUDIO_EDITABLE_MANIFEST.json"))).isFile())
        return fail(error, QStringLiteral("CapCut editable-draft manifest was not written."));
    return true;
}

} // namespace

bool CapCutDraftExporter::exportDraft(const QString &parentDirectory,
                                      const QString &projectName,
                                      const QString &sourceMediaPath,
                                      const QString &masterAudioPath,
                                      const QString &backgroundAudioPath,
                                      const QString &dubbedMixPath,
                                      bool sourceIsVideo,
                                      qint64 sourceDurationMs,
                                      const QVariantList &segments,
                                      const QString &vocalsAudioPath,
                                      const QVariantMap &subtitleConfiguration,
                                      const QVariantMap &timingConfiguration,
                                      QString *draftDirectory,
                                      QString *warning,
                                      QString *error)
{
    if (draftDirectory) draftDirectory->clear();
    if (warning) warning->clear();
    if (sourceMediaPath.trimmed().isEmpty() || !QFileInfo(sourceMediaPath).isFile())
        return fail(error, QStringLiteral("The original source media is required for a CapCut draft."));
    if (dubbedMixPath.trimmed().isEmpty() || !QFileInfo(dubbedMixPath).isFile())
        return fail(error, QStringLiteral("Render the dubbed mix before exporting a CapCut draft."));
    if (segments.isEmpty())
        return fail(error, QStringLiteral("Generate at least one timed segment before exporting a CapCut draft."));

    QVariantMap subtitleStyle;
    QString styleError;
    if (!DubbingSubtitleService::normalizeStyle(
            subtitleConfiguration.value(QStringLiteral("style")).toMap(), subtitleStyle, &styleError)) {
        return fail(error, QStringLiteral("Cannot export CapCut subtitle style: %1").arg(styleError));
    }

    QDir parent(QFileInfo(parentDirectory).absoluteFilePath());
    if (!parent.mkpath(QStringLiteral(".")))
        return fail(error, QStringLiteral("Cannot create the CapCut export folder."));

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString draftName = safeFolderName(projectName) + QStringLiteral("-CapCut-") + timestamp
        + QLatin1Char('-') + newId().left(8);
    const QString finalPath = parent.filePath(draftName);
    const QString stagingName = QStringLiteral(".%1.staging-%2").arg(draftName, newId());
    const QString stagingPath = parent.filePath(stagingName);
    if (QFileInfo::exists(finalPath) || QFileInfo::exists(stagingPath))
        return fail(error, QStringLiteral("Refusing to overwrite an existing CapCut export."));
    if (!QDir().mkpath(QDir(stagingPath).filePath(QStringLiteral("assets/clips")))
        || !QDir().mkpath(QDir(stagingPath).filePath(QStringLiteral("subtitles"))))
        return fail(error, QStringLiteral("Cannot create the CapCut draft staging folder."));

    const auto abort = [&]() {
        QDir(stagingPath).removeRecursively();
        return false;
    };
    const QString assetRoot = QDir(stagingPath).filePath(QStringLiteral("assets"));
    const auto publishedAssetPath = [&stagingPath, &finalPath](const QString &stagingAssetPath) {
        return QDir(finalPath).filePath(QDir(stagingPath).relativeFilePath(stagingAssetPath));
    };
    const QFileInfo sourceInfo(sourceMediaPath);
    const QString copiedSource = QDir(assetRoot).filePath(
        QStringLiteral("source-original.%1").arg(sourceInfo.suffix().isEmpty()
                                                    ? QStringLiteral("media") : sourceInfo.suffix()));
    if (!copyAsset(sourceMediaPath, copiedSource, true, error)) return abort();
    const QString copiedMix = QDir(assetRoot).filePath(QStringLiteral("dubbed-mix.wav"));
    if (!copyAsset(dubbedMixPath, copiedMix, true, error)) return abort();

    // A custom subtitle font must travel with an editable draft. Leaving the
    // user's original absolute path in draft_content.json makes the draft
    // machine-dependent and leaks a local path into the export.
    QVariantMap capCutSubtitleStyle = subtitleStyle;
    QString copiedSubtitleFont;
    const QString configuredSubtitleFont = subtitleStyle.value(QStringLiteral("fontFile")).toString().trimmed();
    if (!configuredSubtitleFont.isEmpty()) {
        const QFileInfo fontInfo(configuredSubtitleFont);
        if (!fontInfo.isFile()) {
            fail(error, QStringLiteral("The selected subtitle font file is unavailable: %1")
                            .arg(fontInfo.absoluteFilePath()));
            return abort();
        }
        const QString extension = fontInfo.suffix().isEmpty() ? QStringLiteral("ttf") : fontInfo.suffix();
        copiedSubtitleFont = QDir(assetRoot).filePath(
            QStringLiteral("fonts/subtitle-font.%1").arg(extension));
        if (!copyAsset(fontInfo.absoluteFilePath(), copiedSubtitleFont, true, error)) return abort();
        capCutSubtitleStyle.insert(QStringLiteral("fontFile"), publishedAssetPath(copiedSubtitleFont));
    }

    QString copiedMaster;
    if (!masterAudioPath.trimmed().isEmpty() && QFileInfo(masterAudioPath).isFile()) {
        copiedMaster = QDir(assetRoot).filePath(QStringLiteral("source-audio.wav"));
        if (!copyAsset(masterAudioPath, copiedMaster, false, error)) return abort();
    }
    QString copiedVocals;
    if (!vocalsAudioPath.trimmed().isEmpty() && QFileInfo(vocalsAudioPath).isFile()) {
        copiedVocals = QDir(assetRoot).filePath(QStringLiteral("source-vocals.wav"));
        if (!copyAsset(vocalsAudioPath, copiedVocals, false, error)) return abort();
    }
    QString copiedBackground;
    if (!backgroundAudioPath.trimmed().isEmpty() && QFileInfo(backgroundAudioPath).isFile()) {
        copiedBackground = QDir(assetRoot).filePath(QStringLiteral("background.wav"));
        if (!copyAsset(backgroundAudioPath, copiedBackground, false, error)) return abort();
    }

    qint64 timelineDurationMs = qMax<qint64>(0, sourceDurationMs);
    for (const QVariant &value : segments) {
        const QVariantMap segment = value.toMap();
        const qint64 start = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 end = segment.value(QStringLiteral("endMs")).toLongLong();
        const QString clip = segment.value(QStringLiteral("clipPath")).toString();
        if (end <= start || clip.isEmpty() || !QFileInfo(clip).isFile()) {
            fail(error, QStringLiteral("Every CapCut segment needs a valid generated audio clip."));
            return abort();
        }
        timelineDurationMs = qMax(timelineDurationMs, end);
    }
    const qint64 timelineDurationUs = qMax<qint64>(1, timelineDurationMs * kMicrosecondsPerMillisecond);

    const QString clipsRoot = QDir(assetRoot).filePath(QStringLiteral("clips"));
    QJsonArray clipSegments;
    QJsonArray audioMaterials;
    QJsonArray textMaterials;
    QJsonArray speedMaterials;
    QJsonArray segmentManifest;

    const QString mixId = newId();
    audioMaterials.append(audioMaterial(mixId, publishedAssetPath(copiedMix),
                                        QStringLiteral("Rendered dubbing mix (reference)"), timelineDurationUs));
    QJsonArray mixTrackSegments;
    mixTrackSegments.append(mediaSegment(mixId, 0, timelineDurationUs, false, &speedMaterials, 0.0));

    if (!copiedMaster.isEmpty()) {
        const QString id = newId();
        audioMaterials.append(audioMaterial(id, publishedAssetPath(copiedMaster),
                                            QStringLiteral("Original audio (reference)"), timelineDurationUs));
        QJsonArray originalTrack;
        originalTrack.append(mediaSegment(id, 0, timelineDurationUs, false, &speedMaterials, 0.0));
        clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Original audio (muted)"), originalTrack));
    }
    if (!copiedVocals.isEmpty()) {
        const QString id = newId();
        audioMaterials.append(audioMaterial(id, publishedAssetPath(copiedVocals),
                                            QStringLiteral("Source vocals (reference)"), timelineDurationUs));
        QJsonArray vocalsTrack;
        vocalsTrack.append(mediaSegment(id, 0, timelineDurationUs, false, &speedMaterials, 0.0));
        clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Source vocals (muted)"), vocalsTrack));
    }
    if (!copiedBackground.isEmpty()) {
        const QString id = newId();
        audioMaterials.append(audioMaterial(id, publishedAssetPath(copiedBackground),
                                            QStringLiteral("Background"), timelineDurationUs));
        QJsonArray backgroundTrack;
        backgroundTrack.append(mediaSegment(id, 0, timelineDurationUs, false, &speedMaterials));
        clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Background"), backgroundTrack));
    }
    clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Rendered dubbing mix (muted)"), mixTrackSegments));

    QJsonArray perSegmentTrack;
    QJsonArray subtitleTrack;
    int index = 0;
    for (const QVariant &value : segments) {
        const QVariantMap segment = value.toMap();
        const qint64 startMs = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 durationMs = segment.value(QStringLiteral("endMs")).toLongLong() - startMs;
        const QFileInfo clipInfo(segment.value(QStringLiteral("clipPath")).toString());
        const QString extension = clipInfo.suffix().isEmpty() ? QStringLiteral("wav") : clipInfo.suffix();
        const QString relativeAsset = QStringLiteral("assets/clips/%1.%2")
            .arg(++index, 4, 10, QLatin1Char('0')).arg(extension);
        const QString copiedClip = QDir(stagingPath).filePath(relativeAsset);
        if (!copyAsset(clipInfo.absoluteFilePath(), copiedClip, true, error)) return abort();
        const QString id = newId();
        const qint64 durationUs = durationMs * kMicrosecondsPerMillisecond;
        audioMaterials.append(audioMaterial(id, publishedAssetPath(copiedClip),
                                            QStringLiteral("Segment %1").arg(index), durationUs));
        perSegmentTrack.append(mediaSegment(id, startMs * kMicrosecondsPerMillisecond,
                                            durationUs, false, &speedMaterials,
                                            segment.value(QStringLiteral("volume"), 1.0).toDouble()));
        const QString subtitleText = segment.value(QStringLiteral("targetText")).toString().trimmed().isEmpty()
            ? segment.value(QStringLiteral("sourceText")).toString().trimmed()
            : segment.value(QStringLiteral("targetText")).toString().trimmed();
        QString subtitleMaterialId;
        if (!subtitleText.isEmpty()) {
            subtitleMaterialId = newId();
            textMaterials.append(textMaterial(subtitleMaterialId, subtitleText, capCutSubtitleStyle));
            subtitleTrack.append(textSegment(subtitleMaterialId,
                                              startMs * kMicrosecondsPerMillisecond,
                                              durationUs, capCutSubtitleStyle));
        }
        segmentManifest.append(QJsonObject{{QStringLiteral("id"), segment.value(QStringLiteral("id")).toString()},
                                            {QStringLiteral("startMs"), startMs},
                                            {QStringLiteral("endMs"), startMs + durationMs},
                                            {QStringLiteral("speakerId"), segment.value(QStringLiteral("speakerId")).toString()},
                                            {QStringLiteral("sourceText"), segment.value(QStringLiteral("sourceText")).toString()},
                                            {QStringLiteral("targetText"), segment.value(QStringLiteral("targetText")).toString()},
                                            {QStringLiteral("clipAsset"), relativeAsset},
                                            {QStringLiteral("volume"), segment.value(QStringLiteral("volume"), 1.0).toDouble()},
                                            {QStringLiteral("rippleOriginalStartMs"), segment.value(QStringLiteral("rippleOriginalStartMs")).toLongLong()},
                                            {QStringLiteral("rippleOriginalEndMs"), segment.value(QStringLiteral("rippleOriginalEndMs")).toLongLong()},
                                            {QStringLiteral("rippleOffsetMs"), segment.value(QStringLiteral("rippleOffsetMs")).toLongLong()},
                                            {QStringLiteral("intentionalOverlap"), segment.value(QStringLiteral("intentionalOverlap")).toBool()},
                                            {QStringLiteral("subtitleMaterialId"), subtitleMaterialId}});
    }
    if (textMaterials.isEmpty()) {
        fail(error, QStringLiteral("Every editable CapCut draft needs at least one source or translated subtitle."));
        return abort();
    }
    clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Generated voice clips"), perSegmentTrack));
    clipSegments.append(track(QStringLiteral("text"), QStringLiteral("Editable subtitles"), subtitleTrack));

    QJsonArray videoMaterials;
    if (sourceIsVideo) {
        const QString videoId = newId();
        videoMaterials.append(videoMaterial(videoId, publishedAssetPath(copiedSource),
                                            QStringLiteral("Original media"), timelineDurationUs));
        QJsonArray sourceVideoTrack;
        sourceVideoTrack.append(mediaSegment(videoId, 0, timelineDurationUs, true, &speedMaterials, 0.0));
        clipSegments.prepend(track(QStringLiteral("video"), QStringLiteral("Original video (muted)"), sourceVideoTrack));
    } else {
        const QString sourceAudioId = newId();
        audioMaterials.append(audioMaterial(sourceAudioId, publishedAssetPath(copiedSource),
                                            QStringLiteral("Original source audio (reference)"), timelineDurationUs));
        QJsonArray sourceAudioTrack;
        sourceAudioTrack.append(mediaSegment(sourceAudioId, 0, timelineDurationUs, false, &speedMaterials, 0.0));
        clipSegments.prepend(track(QStringLiteral("audio"), QStringLiteral("Original source audio (muted)"),
                                  sourceAudioTrack));
    }

    QJsonObject materials;
    const QStringList materialKinds{
        QStringLiteral("ai_translates"), QStringLiteral("audio_balances"), QStringLiteral("audio_effects"),
        QStringLiteral("audio_fades"), QStringLiteral("audio_track_indexes"), QStringLiteral("audios"),
        QStringLiteral("beats"), QStringLiteral("canvases"), QStringLiteral("chromas"),
        QStringLiteral("color_curves"), QStringLiteral("common_mask"), QStringLiteral("digital_human_model_dressing"),
        QStringLiteral("digital_humans"), QStringLiteral("drafts"), QStringLiteral("effects"), QStringLiteral("flowers"),
        QStringLiteral("green_screens"), QStringLiteral("handwrites"), QStringLiteral("hsl"), QStringLiteral("images"),
        QStringLiteral("log_color_wheels"), QStringLiteral("loudnesses"), QStringLiteral("manual_beautys"),
        QStringLiteral("manual_deformations"), QStringLiteral("material_animations"), QStringLiteral("material_colors"),
        QStringLiteral("multi_language_refs"), QStringLiteral("placeholder_infos"), QStringLiteral("placeholders"),
        QStringLiteral("plugin_effects"), QStringLiteral("primary_color_wheels"), QStringLiteral("realtime_denoises"),
        QStringLiteral("shapes"), QStringLiteral("smart_crops"), QStringLiteral("smart_relights"),
        QStringLiteral("sound_channel_mappings"), QStringLiteral("speeds"), QStringLiteral("stickers"),
        QStringLiteral("tail_leaders"), QStringLiteral("text_templates"), QStringLiteral("texts"),
        QStringLiteral("time_marks"), QStringLiteral("transitions"), QStringLiteral("video_effects"),
        QStringLiteral("video_trackings"), QStringLiteral("videos"), QStringLiteral("vocal_beautifys"),
        QStringLiteral("vocal_separations")};
    for (const QString &kind : materialKinds) materials.insert(kind, QJsonArray{});
    materials.insert(QStringLiteral("videos"), videoMaterials);
    materials.insert(QStringLiteral("audios"), audioMaterials);
    materials.insert(QStringLiteral("texts"), textMaterials);
    materials.insert(QStringLiteral("speeds"), speedMaterials);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QJsonObject content{{QStringLiteral("canvas_config"),
                               QJsonObject{{QStringLiteral("background"), QJsonValue::Null},
                                           {QStringLiteral("height"), 1080},
                                           {QStringLiteral("ratio"), QStringLiteral("original")},
                                           {QStringLiteral("width"), 1920}}},
                              {QStringLiteral("color_space"), 0},
                              {QStringLiteral("config"), QJsonObject{{QStringLiteral("lyrics_sync"), true},
                                                                     {QStringLiteral("subtitle_sync"), true},
                                                                     {QStringLiteral("video_mute"), false}}},
                              {QStringLiteral("create_time"), now.toMSecsSinceEpoch()},
                              {QStringLiteral("duration"), timelineDurationUs},
                              {QStringLiteral("fps"), 30.0},
                              {QStringLiteral("id"), newId()},
                              {QStringLiteral("keyframe_graph_list"), QJsonArray{}},
                              {QStringLiteral("keyframes"), QJsonObject{}},
                              {QStringLiteral("materials"), materials},
                              {QStringLiteral("name"), draftName},
                              {QStringLiteral("new_version"), QStringLiteral("140.0.0")},
                              {QStringLiteral("path"), finalPath},
                              {QStringLiteral("platform"), QJsonObject{{QStringLiteral("app_id"), 359289},
                                                                        {QStringLiteral("app_source"), QStringLiteral("cc")},
                                                                        {QStringLiteral("app_version"), QStringLiteral("6.7.0")},
                                                                        {QStringLiteral("os"), QStringLiteral("windows")}}},
                              {QStringLiteral("relationships"), QJsonArray{}},
                              {QStringLiteral("source"), QStringLiteral("default")},
                              {QStringLiteral("tracks"), clipSegments},
                              {QStringLiteral("update_time"), now.toMSecsSinceEpoch()},
                              {QStringLiteral("version"), 360000}};
    const QJsonObject meta{{QStringLiteral("cloud_draft_cover"), false},
                           {QStringLiteral("cloud_draft_sync"), false},
                           {QStringLiteral("draft_cover"), QString()},
                           {QStringLiteral("draft_fold_path"), finalPath},
                           {QStringLiteral("draft_id"), QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper()},
                           {QStringLiteral("draft_materials"), QJsonArray{}},
                           {QStringLiteral("draft_name"), draftName},
                           {QStringLiteral("draft_root_path"), parent.absolutePath()},
                           {QStringLiteral("draft_timeline_materials_size_"), 0},
                           {QStringLiteral("tm_duration"), timelineDurationUs}};
    const auto relativeDraftPath = [&stagingPath](const QString &path) {
        return path.isEmpty() ? QString() : QDir(stagingPath).relativeFilePath(path);
    };
    QVariantMap manifestSubtitleStyle = subtitleStyle;
    if (copiedSubtitleFont.isEmpty()) {
        manifestSubtitleStyle.remove(QStringLiteral("fontFile"));
    } else {
        manifestSubtitleStyle.insert(QStringLiteral("fontFile"), relativeDraftPath(copiedSubtitleFont));
    }
    const QVariantMap safeTimingConfiguration{
        {QStringLiteral("mode"), timingConfiguration.value(QStringLiteral("mode"), QStringLiteral("keep")).toString()},
        {QStringLiteral("minimumGapMs"), qBound(0,
            timingConfiguration.value(QStringLiteral("minimumGapMs"), 80).toInt(), 5000)}};
    QJsonArray editableTracks{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("original-media")},
                    {QStringLiteral("type"), sourceIsVideo ? QStringLiteral("video") : QStringLiteral("audio")},
                    {QStringLiteral("initialVolume"), 0.0}}};
    if (!copiedMaster.isEmpty()) {
        editableTracks.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("original-audio")},
                                          {QStringLiteral("type"), QStringLiteral("audio")},
                                          {QStringLiteral("initialVolume"), 0.0}});
    }
    if (!copiedVocals.isEmpty()) {
        editableTracks.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("source-vocals")},
                                          {QStringLiteral("type"), QStringLiteral("audio")},
                                          {QStringLiteral("initialVolume"), 0.0}});
    }
    if (!copiedBackground.isEmpty()) {
        editableTracks.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("background")},
                                          {QStringLiteral("type"), QStringLiteral("audio")},
                                          {QStringLiteral("initialVolume"), 1.0}});
    }
    editableTracks.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("rendered-dubbing-mix")},
                                      {QStringLiteral("type"), QStringLiteral("audio")},
                                      {QStringLiteral("initialVolume"), 0.0}});
    editableTracks.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("generated-voice-clips")},
                                      {QStringLiteral("type"), QStringLiteral("audio")},
                                      {QStringLiteral("segmentCount"), perSegmentTrack.size()}});
    editableTracks.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("editable-subtitles")},
                                      {QStringLiteral("type"), QStringLiteral("text")},
                                      {QStringLiteral("segmentCount"), subtitleTrack.size()}});
    const QJsonObject editableManifest{
        {QStringLiteral("format"), QStringLiteral("la-studio-editable-capcut-draft")},
        {QStringLiteral("version"), 2},
        {QStringLiteral("capCutImportStatus"), QStringLiteral("structurally-validated-manual-import-pending")},
        {QStringLiteral("timelineDurationMs"), timelineDurationMs},
        {QStringLiteral("assets"), QJsonObject{
            {QStringLiteral("originalMedia"), relativeDraftPath(copiedSource)},
            {QStringLiteral("originalAudio"), relativeDraftPath(copiedMaster)},
            {QStringLiteral("sourceVocals"), relativeDraftPath(copiedVocals)},
            {QStringLiteral("background"), relativeDraftPath(copiedBackground)},
            {QStringLiteral("renderedDubbingMix"), relativeDraftPath(copiedMix)},
            {QStringLiteral("generatedClipDirectory"), QStringLiteral("assets/clips")}}},
        {QStringLiteral("tracks"), editableTracks},
        {QStringLiteral("subtitle"), QJsonObject{
            {QStringLiteral("source"), subtitleConfiguration.value(QStringLiteral("source"),
                                                                       QStringLiteral("segments")).toString()},
            {QStringLiteral("style"), QJsonObject::fromVariantMap(manifestSubtitleStyle)},
            {QStringLiteral("editableTextSegmentCount"), subtitleTrack.size()},
            {QStringLiteral("sidecars"), QJsonArray{QStringLiteral("subtitles/original.srt"),
                                                      QStringLiteral("subtitles/dubbed.srt")}}}},
        {QStringLiteral("timing"), QJsonObject::fromVariantMap(safeTimingConfiguration)},
        {QStringLiteral("segments"), segmentManifest},
        {QStringLiteral("effects"), QJsonArray{}}
    };
    if (!writeJson(QDir(stagingPath).filePath(QStringLiteral("draft_content.json")), content, error)
        || !writeJson(QDir(stagingPath).filePath(QStringLiteral("draft_meta_info.json")), meta, error)
        || !writeJson(QDir(stagingPath).filePath(QStringLiteral("LA_STUDIO_EDITABLE_MANIFEST.json")),
                      editableManifest, error)
        || !writeJson(QDir(stagingPath).filePath(QStringLiteral("segments.json")),
                      QJsonObject{{QStringLiteral("format"), QStringLiteral("la-studio-capcut-segments")},
                                  {QStringLiteral("version"), 2},
                                  {QStringLiteral("subtitleStyle"), QJsonObject::fromVariantMap(subtitleStyle)},
                                  {QStringLiteral("timingConfiguration"), QJsonObject::fromVariantMap(safeTimingConfiguration)},
                                  {QStringLiteral("segments"), segmentManifest}}, error)
        || !writeText(QDir(stagingPath).filePath(QStringLiteral("subtitles/original.srt")),
                      subtitles(segments, false), error)
        || !writeText(QDir(stagingPath).filePath(QStringLiteral("subtitles/dubbed.srt")),
                      subtitles(segments, true), error)
        || !writeText(QDir(stagingPath).filePath(QStringLiteral("LA_STUDIO_IMPORT_STATUS.md")),
                      QStringLiteral("# CapCut draft export status\n\n"
                                     "This folder contains a self-contained CapCut draft schema and copied local assets. "
                                     "Original media/audio, optional source vocals/background, a muted rendered mix, each "
                                     "generated voice clip, and editable subtitle text segments remain separate. It was "
                                     "structurally validated by LA Studio, but manual import into CapCut has not been "
                                     "verified on this machine. Do not treat import compatibility as confirmed until that "
                                     "manual check succeeds.\n"), error)
        || !validateDraft(stagingPath, finalPath, error)) {
        return abort();
    }
    if (!parent.rename(stagingName, draftName)) {
        fail(error, QStringLiteral("Cannot atomically publish the CapCut draft folder."));
        return abort();
    }
    if (draftDirectory) *draftDirectory = finalPath;
    if (warning) {
        *warning = QStringLiteral("CapCut draft was structurally validated, but manual CapCut import is not yet verified.");
    }
    return true;
}

} // namespace LAStudio
