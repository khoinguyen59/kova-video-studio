#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace LAStudio {

// Writes a self-contained CapCut draft folder from a completed dubbing project.
// The schema is based on the locally available pyCapCut implementation. CapCut
// has not been installed in this workspace, so callers must present the result
// as an unverified import until a manual import is recorded.
class CapCutDraftExporter final
{
public:
    static bool exportDraft(const QString &parentDirectory,
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
                            QString *warning = nullptr,
                            QString *error = nullptr);
};

} // namespace LAStudio
