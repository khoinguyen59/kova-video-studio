#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace LAStudio {

// Subtitle parsing and serialization lives at the Dubbing boundary so all
// import, preview, sidecar and render paths use the same reviewed timeline.
// This class is value-only: it never accesses a controller, player or model.
class DubbingSubtitleService final
{
public:
    static QVariantMap defaultStyle();
    static bool normalizeStyle(const QVariantMap &candidate, QVariantMap &normalized,
                               QString *error = nullptr);

    // Parses a timed subtitle file.  SRT, VTT and ASS/SSA keep their supplied
    // timing.  TXT/Markdown intentionally return untimed text and require an
    // explicit mapping to an existing, reviewed timeline.
    static bool importFile(const QString &path, QVariantList &segments, bool &hasTiming,
                           QString &format, QString *error = nullptr);
    static bool mapUntimedLines(const QVariantList &untimedSegments,
                                const QVariantList &timelineSegments,
                                QVariantList &mapped, QString *error = nullptr);

    static bool writeSidecar(const QVariantList &segments, const QString &path,
                             bool useTargetText, QString *error = nullptr);
    static bool writeAss(const QVariantList &segments, const QVariantMap &style,
                         const QString &path, bool useTargetText,
                         QString *error = nullptr);
};

} // namespace LAStudio
