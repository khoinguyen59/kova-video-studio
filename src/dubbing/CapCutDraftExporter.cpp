#include "dubbing/CapCutDraftExporter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
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
                         qint64 targetDurationUs, bool video, QJsonArray *speeds)
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
                       {QStringLiteral("volume"), 1.0},
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
    for (const QString &kind : {QStringLiteral("videos"), QStringLiteral("audios")}) {
        for (const QJsonValue &value : materials.value(kind).toArray()) {
            const QString assetPath = value.toObject().value(QStringLiteral("path")).toString();
            QString validationPath = assetPath;
            const QString relativePath = QDir(publishedDraftPath).relativeFilePath(assetPath);
            if (!relativePath.startsWith(QStringLiteral("..")))
                validationPath = QDir(draftPath).filePath(relativePath);
            if (assetPath.isEmpty() || !QFileInfo(validationPath).isFile())
                return fail(error, QStringLiteral("CapCut draft references a missing %1 asset.").arg(kind));
        }
    }
    return QFileInfo(QDir(draftPath).filePath(QStringLiteral("draft_meta_info.json"))).isFile()
        ? true : fail(error, QStringLiteral("CapCut draft metadata was not written."));
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

    QString copiedMaster;
    if (!masterAudioPath.trimmed().isEmpty() && QFileInfo(masterAudioPath).isFile()) {
        copiedMaster = QDir(assetRoot).filePath(QStringLiteral("source-audio.wav"));
        if (!copyAsset(masterAudioPath, copiedMaster, false, error)) return abort();
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
    QJsonArray speedMaterials;
    QJsonArray segmentManifest;

    const QString mixId = newId();
    audioMaterials.append(audioMaterial(mixId, publishedAssetPath(copiedMix),
                                        QStringLiteral("Dubbed mix"), timelineDurationUs));
    QJsonArray mixTrackSegments;
    mixTrackSegments.append(mediaSegment(mixId, 0, timelineDurationUs, false, &speedMaterials));

    if (!copiedMaster.isEmpty()) {
        const QString id = newId();
        audioMaterials.append(audioMaterial(id, publishedAssetPath(copiedMaster),
                                            QStringLiteral("Original audio"), timelineDurationUs));
        QJsonArray originalTrack;
        originalTrack.append(mediaSegment(id, 0, timelineDurationUs, false, &speedMaterials));
        clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Original audio"), originalTrack));
    }
    if (!copiedBackground.isEmpty()) {
        const QString id = newId();
        audioMaterials.append(audioMaterial(id, publishedAssetPath(copiedBackground),
                                            QStringLiteral("Background"), timelineDurationUs));
        QJsonArray backgroundTrack;
        backgroundTrack.append(mediaSegment(id, 0, timelineDurationUs, false, &speedMaterials));
        clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Background"), backgroundTrack));
    }
    clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Dubbed mix"), mixTrackSegments));

    QJsonArray perSegmentTrack;
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
                                            durationUs, false, &speedMaterials));
        segmentManifest.append(QJsonObject{{QStringLiteral("id"), segment.value(QStringLiteral("id")).toString()},
                                            {QStringLiteral("startMs"), startMs},
                                            {QStringLiteral("endMs"), startMs + durationMs},
                                            {QStringLiteral("speakerId"), segment.value(QStringLiteral("speakerId")).toString()},
                                            {QStringLiteral("sourceText"), segment.value(QStringLiteral("sourceText")).toString()},
                                            {QStringLiteral("targetText"), segment.value(QStringLiteral("targetText")).toString()},
                                            {QStringLiteral("clipAsset"), relativeAsset}});
    }
    clipSegments.append(track(QStringLiteral("audio"), QStringLiteral("Generated clips"), perSegmentTrack));

    QJsonArray videoMaterials;
    if (sourceIsVideo) {
        const QString videoId = newId();
        videoMaterials.append(videoMaterial(videoId, publishedAssetPath(copiedSource),
                                            QStringLiteral("Original media"), timelineDurationUs));
        QJsonArray sourceVideoTrack;
        sourceVideoTrack.append(mediaSegment(videoId, 0, timelineDurationUs, true, &speedMaterials));
        clipSegments.prepend(track(QStringLiteral("video"), QStringLiteral("Original media"), sourceVideoTrack));
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
    if (!writeJson(QDir(stagingPath).filePath(QStringLiteral("draft_content.json")), content, error)
        || !writeJson(QDir(stagingPath).filePath(QStringLiteral("draft_meta_info.json")), meta, error)
        || !writeJson(QDir(stagingPath).filePath(QStringLiteral("segments.json")),
                      QJsonObject{{QStringLiteral("format"), QStringLiteral("la-studio-capcut-segments")},
                                  {QStringLiteral("version"), 1},
                                  {QStringLiteral("segments"), segmentManifest}}, error)
        || !writeText(QDir(stagingPath).filePath(QStringLiteral("subtitles/original.srt")),
                      subtitles(segments, false), error)
        || !writeText(QDir(stagingPath).filePath(QStringLiteral("subtitles/dubbed.srt")),
                      subtitles(segments, true), error)
        || !writeText(QDir(stagingPath).filePath(QStringLiteral("LA_STUDIO_IMPORT_STATUS.md")),
                      QStringLiteral("# CapCut draft export status\n\n"
                                     "This folder contains a self-contained CapCut draft schema and copied local assets. "
                                     "It was structurally validated by LA Studio, but manual import into CapCut has not "
                                     "been verified on this machine. Do not treat import compatibility as confirmed until "
                                     "that manual check succeeds.\n"), error)
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
