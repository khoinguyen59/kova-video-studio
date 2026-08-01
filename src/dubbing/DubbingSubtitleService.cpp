#include "dubbing/DubbingSubtitleService.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextLayout>
#include <QUuid>

namespace LAStudio {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

qint64 parseTimestamp(const QString &value)
{
    static const QRegularExpression timestamp(
        QStringLiteral("^(?:(\\d+):)?(\\d{1,2}):(\\d{2})[,.](\\d{1,3})$"));
    const QRegularExpressionMatch match = timestamp.match(value.trimmed());
    if (!match.hasMatch()) return -1;
    QString milliseconds = match.captured(4);
    while (milliseconds.size() < 3) milliseconds.append(QLatin1Char('0'));
    return ((match.captured(1).isEmpty() ? 0 : match.captured(1).toLongLong()) * 3600
            + match.captured(2).toLongLong() * 60 + match.captured(3).toLongLong()) * 1000
        + milliseconds.left(3).toLongLong();
}

qint64 parseAssTimestamp(const QString &value)
{
    static const QRegularExpression timestamp(
        QStringLiteral("^(\\d+):(\\d{1,2}):(\\d{2})[.](\\d{1,2})$"));
    const QRegularExpressionMatch match = timestamp.match(value.trimmed());
    if (!match.hasMatch()) return -1;
    QString centiseconds = match.captured(4);
    while (centiseconds.size() < 2) centiseconds.append(QLatin1Char('0'));
    return (match.captured(1).toLongLong() * 3600 + match.captured(2).toLongLong() * 60
            + match.captured(3).toLongLong()) * 1000 + centiseconds.left(2).toLongLong() * 10;
}

QString srtTimestamp(qint64 milliseconds, bool vtt = false)
{
    milliseconds = qMax<qint64>(0, milliseconds);
    const qint64 hours = milliseconds / 3600000;
    milliseconds %= 3600000;
    const qint64 minutes = milliseconds / 60000;
    milliseconds %= 60000;
    const qint64 seconds = milliseconds / 1000;
    milliseconds %= 1000;
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0')).arg(vtt ? QLatin1Char('.') : QLatin1Char(','))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}

QString assTimestamp(qint64 milliseconds)
{
    milliseconds = qMax<qint64>(0, milliseconds);
    const qint64 hours = milliseconds / 3600000;
    milliseconds %= 3600000;
    const qint64 minutes = milliseconds / 60000;
    milliseconds %= 60000;
    const qint64 seconds = milliseconds / 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds / 10, 2, 10, QLatin1Char('0'));
}

QString assColor(const QString &rgba, const QString &fallback, double opacity = -1.0)
{
    const QColor color(rgba);
    QColor value = color.isValid() ? color : QColor(fallback);
    if (opacity >= 0.0) value.setAlphaF(qBound(0.0, opacity, 1.0) * value.alphaF());
    return QStringLiteral("&H%1%2%3%4")
        .arg(255 - value.alpha(), 2, 16, QLatin1Char('0'))
        .arg(value.blue(), 2, 16, QLatin1Char('0'))
        .arg(value.green(), 2, 16, QLatin1Char('0'))
        .arg(value.red(), 2, 16, QLatin1Char('0')).toUpper();
}

QString assEscapedText(QString text)
{
    text.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\N"));
    text.remove(QRegularExpression(QStringLiteral("[\\r\\n]+$")));
    return text;
}

QString plainAssText(QString text)
{
    text.replace(QStringLiteral("\\N"), QStringLiteral("\n"));
    text.remove(QRegularExpression(QStringLiteral("\\{[^}]*\\}")));
    return text.trimmed();
}

QFont subtitleLayoutFont(const QVariantMap &style)
{
    QFont font(style.value(QStringLiteral("fontFamily")).toString());
    font.setPixelSize(style.value(QStringLiteral("fontSize")).toInt());
    font.setWeight(static_cast<QFont::Weight>(
        style.value(QStringLiteral("fontWeight")).toInt()));

    // libass receives the same file through FFmpeg's fontsdir. Register it
    // locally so wrapping measures the selected typeface where possible.
    const QString fontFile = style.value(QStringLiteral("fontFile")).toString().trimmed();
    if (!fontFile.isEmpty() && QFileInfo(fontFile).isFile()) {
        const int fontId = QFontDatabase::addApplicationFont(fontFile);
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) font.setFamily(families.constFirst());
    }
    return font;
}

QStringList wrapSubtitleText(const QString &text, const QFont &font, int maximumWidth)
{
    QStringList wrapped;
    const QStringList paragraphs = text.split(QRegularExpression(QStringLiteral("\\r?\\n")),
                                              Qt::KeepEmptyParts);
    for (const QString &paragraph : paragraphs) {
        if (paragraph.isEmpty()) {
            // libass drops empty dialogue text. A non-breaking space retains
            // an explicitly authored blank line in a multi-line block.
            wrapped.append(QString(QChar::Nbsp));
            continue;
        }
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(maximumWidth);
            const QString visualLine = paragraph.mid(line.textStart(), line.textLength()).trimmed();
            if (!visualLine.isEmpty()) wrapped.append(visualLine);
        }
        layout.endLayout();
    }
    return wrapped;
}

QStringList explicitSubtitleLines(const QString &text)
{
    QStringList lines;
    for (const QString &line : text.split(QRegularExpression(QStringLiteral("\\r?\\n")),
                                          Qt::KeepEmptyParts)) {
        lines.append(line.isEmpty() ? QString(QChar::Nbsp) : line.trimmed());
    }
    return lines;
}

QVariantMap importedSegment(qint64 startMs, qint64 endMs, const QString &text,
                            const QString &source)
{
    return {{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("startMs"), startMs},
            {QStringLiteral("endMs"), endMs},
            {QStringLiteral("sourceText"), text.trimmed()},
            {QStringLiteral("targetText"), QString()},
            {QStringLiteral("speakerId"), QStringLiteral("speaker-1")},
            {QStringLiteral("state"), QStringLiteral("review")},
            {QStringLiteral("timingSource"), source},
            {QStringLiteral("subtitleSource"), source}};
}

bool appendTimedBlocks(const QString &content, const QString &source, QVariantList &segments,
                       QString *error)
{
    QString normalized = content;
    normalized.remove(QChar::ByteOrderMark);
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList blocks = normalized.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")),
                                                 Qt::SkipEmptyParts);
    static const QRegularExpression timing(
        QStringLiteral("^\\s*((?:\\d+:)?\\d{1,2}:\\d{2}[,.]\\d{1,3})\\s*-->\\s*"
                       "((?:\\d+:)?\\d{1,2}:\\d{2}[,.]\\d{1,3})"));
    for (const QString &block : blocks) {
        const QStringList lines = block.split(QLatin1Char('\n'));
        int timingIndex = -1;
        QRegularExpressionMatch match;
        for (int index = 0; index < lines.size(); ++index) {
            match = timing.match(lines.at(index));
            if (match.hasMatch()) { timingIndex = index; break; }
        }
        if (timingIndex < 0) continue;
        const qint64 startMs = parseTimestamp(match.captured(1));
        const qint64 endMs = parseTimestamp(match.captured(2));
        const QString text = lines.mid(timingIndex + 1).join(QLatin1Char('\n')).trimmed();
        if (startMs < 0 || endMs <= startMs || text.isEmpty()) {
            setError(error, QStringLiteral("Subtitle cue has invalid timing or text."));
            return false;
        }
        segments.append(importedSegment(startMs, endMs, text, source));
    }
    if (segments.isEmpty()) {
        setError(error, QStringLiteral("No timed subtitle cues were found."));
        return false;
    }
    return true;
}

QStringList nonEmptyTextLines(const QString &content)
{
    QStringList lines;
    for (QString line : content.split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
        line = line.trimmed();
        line.remove(QRegularExpression(QStringLiteral("^[-*+]\\s+")));
        line.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+")));
        if (!line.isEmpty() && !line.startsWith(QStringLiteral("```"))) lines.append(line);
    }
    return lines;
}

} // namespace

QVariantMap DubbingSubtitleService::defaultStyle()
{
    return {{QStringLiteral("fontFamily"), QStringLiteral("Arial")},
            {QStringLiteral("fontFile"), QString()},
            {QStringLiteral("fontSize"), 42},
            {QStringLiteral("fontWeight"), 600},
            {QStringLiteral("textColor"), QStringLiteral("#FFFFFFFF")},
            {QStringLiteral("outlineColor"), QStringLiteral("#D9000000")},
            {QStringLiteral("outlineWidth"), 2},
            {QStringLiteral("shadowColor"), QStringLiteral("#99000000")},
            {QStringLiteral("shadowOffset"), 2},
            {QStringLiteral("backgroundColor"), QStringLiteral("#00000000")},
            {QStringLiteral("backgroundOpacity"), 0.0},
            {QStringLiteral("alignment"), QStringLiteral("bottom")},
            {QStringLiteral("maxWidth"), 0.82},
            {QStringLiteral("lineSpacing"), 1.0},
            {QStringLiteral("safeMargin"), 0.06},
            {QStringLiteral("positionX"), 0.5},
            {QStringLiteral("positionY"), 0.90}};
}

bool DubbingSubtitleService::normalizeStyle(const QVariantMap &candidate, QVariantMap &normalized,
                                            QString *error)
{
    normalized = defaultStyle();
    for (auto it = candidate.cbegin(); it != candidate.cend(); ++it) {
        if (normalized.contains(it.key())) normalized.insert(it.key(), it.value());
    }
    const QString alignment = normalized.value(QStringLiteral("alignment")).toString().trimmed().toLower();
    if (alignment != QStringLiteral("top") && alignment != QStringLiteral("bottom")
        && alignment != QStringLiteral("custom")) {
        setError(error, QStringLiteral("Subtitle alignment must be top, bottom or custom."));
        return false;
    }
    normalized.insert(QStringLiteral("alignment"), alignment);
    const auto boundedInteger = [&normalized](const QString &key, int lower, int upper) {
        normalized.insert(key, qBound(lower, normalized.value(key).toInt(), upper));
    };
    const auto boundedReal = [&normalized](const QString &key, double lower, double upper) {
        normalized.insert(key, qBound(lower, normalized.value(key).toDouble(), upper));
    };
    boundedInteger(QStringLiteral("fontSize"), 8, 180);
    boundedInteger(QStringLiteral("fontWeight"), 100, 900);
    boundedInteger(QStringLiteral("outlineWidth"), 0, 16);
    boundedInteger(QStringLiteral("shadowOffset"), 0, 24);
    boundedReal(QStringLiteral("backgroundOpacity"), 0.0, 1.0);
    boundedReal(QStringLiteral("maxWidth"), 0.10, 1.0);
    boundedReal(QStringLiteral("lineSpacing"), 0.5, 3.0);
    boundedReal(QStringLiteral("safeMargin"), 0.0, 0.25);
    boundedReal(QStringLiteral("positionX"), 0.0, 1.0);
    boundedReal(QStringLiteral("positionY"), 0.0, 1.0);
    for (const QString &key : {QStringLiteral("textColor"), QStringLiteral("outlineColor"),
                                QStringLiteral("shadowColor"), QStringLiteral("backgroundColor")}) {
        if (!QColor(normalized.value(key).toString()).isValid()) {
            setError(error, QStringLiteral("Subtitle style contains an invalid %1.").arg(key));
            return false;
        }
    }
    return true;
}

bool DubbingSubtitleService::importFile(const QString &path, QVariantList &segments, bool &hasTiming,
                                        QString &format, QString *error)
{
    segments.clear();
    hasTiming = false;
    format = QFileInfo(path).suffix().trimmed().toLower();
    if (format == QStringLiteral("ssa")) format = QStringLiteral("ass");
    if (format != QStringLiteral("srt") && format != QStringLiteral("vtt")
        && format != QStringLiteral("ass") && format != QStringLiteral("txt")
        && format != QStringLiteral("md") && format != QStringLiteral("markdown")) {
        setError(error, QStringLiteral("Supported subtitle imports are SRT, VTT, ASS/SSA, TXT and Markdown."));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open subtitle file: %1").arg(file.errorString()));
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    if (format == QStringLiteral("srt") || format == QStringLiteral("vtt")) {
        hasTiming = appendTimedBlocks(content, format, segments, error);
        return hasTiming;
    }
    if (format == QStringLiteral("ass")) {
        const QStringList lines = content.split(QRegularExpression(QStringLiteral("\\r?\\n")));
        for (const QString &line : lines) {
            if (!line.startsWith(QStringLiteral("Dialogue:"), Qt::CaseInsensitive)) continue;
            const QStringList fields = line.mid(line.indexOf(QLatin1Char(':')) + 1).split(QLatin1Char(','));
            if (fields.size() < 10) {
                setError(error, QStringLiteral("ASS dialogue line has fewer than 10 fields."));
                return false;
            }
            const qint64 startMs = parseAssTimestamp(fields.at(1));
            const qint64 endMs = parseAssTimestamp(fields.at(2));
            const QString text = plainAssText(fields.mid(9).join(QLatin1Char(',')));
            if (startMs < 0 || endMs <= startMs || text.isEmpty()) {
                setError(error, QStringLiteral("ASS dialogue line has invalid timing or text."));
                return false;
            }
            segments.append(importedSegment(startMs, endMs, text, QStringLiteral("ass")));
        }
        hasTiming = !segments.isEmpty();
        if (!hasTiming) setError(error, QStringLiteral("No timed ASS dialogue lines were found."));
        return hasTiming;
    }
    for (const QString &line : nonEmptyTextLines(content)) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("sourceText"), line);
        item.insert(QStringLiteral("subtitleSource"), format == QStringLiteral("md")
                                                     ? QStringLiteral("markdown") : QStringLiteral("txt"));
        segments.append(item);
    }
    if (segments.isEmpty()) {
        setError(error, QStringLiteral("The untimed subtitle file has no usable text lines."));
        return false;
    }
    return true;
}

bool DubbingSubtitleService::mapUntimedLines(const QVariantList &untimedSegments,
                                             const QVariantList &timelineSegments,
                                             QVariantList &mapped, QString *error)
{
    if (untimedSegments.isEmpty() || timelineSegments.isEmpty()
        || untimedSegments.size() != timelineSegments.size()) {
        setError(error, QStringLiteral("Untimed TXT/Markdown must have exactly one non-empty line for every existing reviewed segment. No timestamps were invented."));
        return false;
    }
    mapped.clear();
    for (int index = 0; index < timelineSegments.size(); ++index) {
        QVariantMap segment = timelineSegments.at(index).toMap();
        const QString text = untimedSegments.at(index).toMap().value(QStringLiteral("sourceText")).toString().trimmed();
        const qint64 startMs = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 endMs = segment.value(QStringLiteral("endMs")).toLongLong();
        if (text.isEmpty() || endMs <= startMs) {
            setError(error, QStringLiteral("The existing reviewed timeline is not valid for untimed subtitle mapping."));
            return false;
        }
        segment.insert(QStringLiteral("sourceText"), text);
        segment.insert(QStringLiteral("targetText"), QString());
        segment.insert(QStringLiteral("subtitleSource"), QStringLiteral("untimed-line-map"));
        segment.insert(QStringLiteral("timingSource"), QStringLiteral("existing-reviewed-timeline"));
        segment.insert(QStringLiteral("state"), QStringLiteral("review"));
        mapped.append(segment);
    }
    return true;
}

bool DubbingSubtitleService::writeSidecar(const QVariantList &segments, const QString &path,
                                          bool useTargetText, QString *error)
{
    const bool vtt = QFileInfo(path).suffix().compare(QStringLiteral("vtt"), Qt::CaseInsensitive) == 0;
    QStringList lines;
    if (vtt) lines.append(QStringLiteral("WEBVTT\n"));
    int cue = 1;
    for (const QVariant &entry : segments) {
        const QVariantMap segment = entry.toMap();
        const QString text = segment.value(useTargetText ? QStringLiteral("targetText")
                                                         : QStringLiteral("sourceText")).toString().trimmed();
        const qint64 start = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 end = segment.value(QStringLiteral("endMs")).toLongLong();
        if (text.isEmpty() || end <= start) continue;
        if (!vtt) lines.append(QString::number(cue));
        lines.append(QStringLiteral("%1 --> %2").arg(srtTimestamp(start, vtt), srtTimestamp(end, vtt)));
        lines.append(text);
        lines.append(QString());
        ++cue;
    }
    if (cue == 1) {
        setError(error, useTargetText ? QStringLiteral("No translated subtitle text is available.")
                                      : QStringLiteral("No source subtitle text is available."));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(lines.join(QLatin1Char('\n')).toUtf8()) < 0 || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

bool DubbingSubtitleService::writeAss(const QVariantList &segments, const QVariantMap &candidate,
                                      const QString &path, bool useTargetText, QString *error)
{
    QVariantMap style;
    if (!normalizeStyle(candidate, style, error)) return false;
    const QString alignment = style.value(QStringLiteral("alignment")).toString();
    const int assAlignment = alignment == QStringLiteral("top") ? 8 : 2;
    const QString customPosition = alignment == QStringLiteral("custom")
        ? QStringLiteral("{\\pos(%1,%2)}")
              .arg(qRound(style.value(QStringLiteral("positionX")).toDouble() * 1920.0))
              .arg(qRound(style.value(QStringLiteral("positionY")).toDouble() * 1080.0))
        : QString();
    const int marginH = qRound((1.0 - style.value(QStringLiteral("maxWidth")).toDouble()) * 1920.0 / 2.0);
    const int marginV = qRound(style.value(QStringLiteral("safeMargin")).toDouble() * 1080.0);
    const int maximumWidth = qMax(1, 1920 - marginH * 2);
    const bool canMeasureFont = qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr;
    const QFont layoutFont = canMeasureFont ? subtitleLayoutFont(style) : QFont();
    const int lineHeight = canMeasureFont
        ? qMax(1, qRound(QFontMetricsF(layoutFont).height()))
        : qMax(1, style.value(QStringLiteral("fontSize")).toInt());
    const int lineStep = qMax(1, qRound(lineHeight
                                         * style.value(QStringLiteral("lineSpacing")).toDouble()));
    const QString header = QStringLiteral("[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n"
                                          "\n[V4+ Styles]\nFormat: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding\n"
                                          "Style: LAStudio,%1,%2,%3,&H000000FF,%4,%5,%6,0,0,0,100,100,0,0,1,%7,%8,%9,%10,%10,%11,1\n"
                                          "\n[Events]\nFormat: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text\n")
        .arg(style.value(QStringLiteral("fontFamily")).toString().replace(QLatin1Char(','), QLatin1Char(' ')))
        .arg(style.value(QStringLiteral("fontSize")).toInt())
        .arg(assColor(style.value(QStringLiteral("textColor")).toString(), QStringLiteral("#FFFFFFFF")))
        .arg(assColor(style.value(QStringLiteral("outlineColor")).toString(), QStringLiteral("#D9000000")))
        .arg(assColor(style.value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#00000000"),
                      style.value(QStringLiteral("backgroundOpacity")).toDouble()))
        .arg(style.value(QStringLiteral("fontWeight")).toInt() >= 600 ? -1 : 0)
        .arg(style.value(QStringLiteral("outlineWidth")).toInt())
        .arg(style.value(QStringLiteral("shadowOffset")).toInt())
        .arg(assAlignment).arg(marginH).arg(marginV);
    QStringList lines{header};
    int cue = 0;
    for (const QVariant &entry : segments) {
        const QVariantMap segment = entry.toMap();
        const QString text = segment.value(useTargetText ? QStringLiteral("targetText")
                                                         : QStringLiteral("sourceText")).toString().trimmed();
        const qint64 start = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 end = segment.value(QStringLiteral("endMs")).toLongLong();
        if (text.isEmpty() || end <= start) continue;
        const QStringList visualLines = canMeasureFont
            ? wrapSubtitleText(text, layoutFont, maximumWidth) : explicitSubtitleLines(text);
        if (visualLines.isEmpty()) continue;
        if (visualLines.size() == 1) {
            lines.append(QStringLiteral("Dialogue: 0,%1,%2,LAStudio,,0,0,0,,%3")
                             .arg(assTimestamp(start), assTimestamp(end),
                                  customPosition + assEscapedText(visualLines.constFirst())));
            ++cue;
            continue;
        }

        // ASS/libass has no line-spacing style property: its `Spacing` field
        // is character spacing. Write each wrapped visual line as a timed
        // dialogue and place it at the requested proportional line step.
        const int blockHeight = lineHeight + (visualLines.size() - 1) * lineStep;
        const int centreX = alignment == QStringLiteral("custom")
            ? qRound(style.value(QStringLiteral("positionX")).toDouble() * 1920.0) : 960;
        int topY = marginV;
        if (alignment == QStringLiteral("bottom")) topY = 1080 - marginV - blockHeight;
        else if (alignment == QStringLiteral("custom")) {
            topY = qRound(style.value(QStringLiteral("positionY")).toDouble() * 1080.0)
                - blockHeight / 2;
        }
        topY = qBound(0, topY, qMax(0, 1080 - blockHeight));
        for (int lineIndex = 0; lineIndex < visualLines.size(); ++lineIndex) {
            const QString position = QStringLiteral("{\\an8\\q2\\pos(%1,%2)}")
                .arg(centreX).arg(topY + lineIndex * lineStep);
            lines.append(QStringLiteral("Dialogue: 0,%1,%2,LAStudio,,0,0,0,,%3")
                             .arg(assTimestamp(start), assTimestamp(end),
                                  position + assEscapedText(visualLines.at(lineIndex))));
            ++cue;
        }
    }
    if (cue == 0) {
        setError(error, QStringLiteral("No reviewed subtitle cues are available for burn-in."));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(lines.join(QLatin1Char('\n')).toUtf8()) < 0 || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

} // namespace LAStudio
