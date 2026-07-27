#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace LAStudio {

struct WorkflowRunEvent
{
    quint64 sequence = 0;
    QDateTime timestamp;
    QString eventType;
    QString runId;
    QString nodeRunId;
    QJsonObject payload;

    QJsonObject toJson() const;
    static WorkflowRunEvent fromJson(const QJsonObject &json);
};

// A non-terminal run discovered when the project is opened again.  The journal
// deliberately keeps the run data intact so the caller can offer recovery
// instead of silently replaying work.
struct WorkflowInterruptedRun
{
    QString runId;
    QString workflowId;
    int workflowVersion = 0;
    QString activeNodeId;
    QString lastEventType;
    QDateTime lastUpdated;
};

class WorkflowRunJournal final
{
public:
    explicit WorkflowRunJournal(QString rootPath);

    bool append(WorkflowRunEvent event, QString *error = nullptr) const;
    QList<WorkflowRunEvent> read(const QString &runId, QString *error = nullptr) const;
    QList<WorkflowInterruptedRun> interruptedRuns(QString *error = nullptr) const;
    QString rootPath() const { return m_rootPath; }

private:
    QString pathFor(const QString &runId) const;
    QString m_rootPath;
};

} // namespace LAStudio
