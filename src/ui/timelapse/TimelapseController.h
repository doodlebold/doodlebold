#pragma once

#include "core/ffmpeg/Ffmpeg.h"
#include "ui/timelapse/TimelapseDialogs.h"

#include <QObject>
#include <QPointer>

#include <optional>

class QWidget;

namespace doodlebold {

class Document;

// The questions go through protected virtuals, so tests can answer without dialogs.
class TimelapseController : public QObject {
    Q_OBJECT
public:
    explicit TimelapseController(QWidget* dialogParent, QObject* parent = nullptr);

    // FFmpeg, located once per session (the configured path, Doodlebold's tools folder, the PATH). When it is missing and `ask`
    // is true the user is asked to install or locate it; nullopt if there is none in the end.
    std::optional<FfmpegTools> tools(bool ask = true);
    static QString configuredPath();
    static void setConfiguredPath(const QString& path);

    bool exportTimelapse(Document* doc);
    void startRecording(Document* doc);
    void setPaused(Document* doc, bool paused);
    bool deleteRecording(Document* doc);
    bool restartRecording(Document* doc);

    static QString deleteQuestion(const QString& name, int steps);
    static QString restartQuestion(const QString& name, int steps);

signals:
    void message(const QString& text);
    void refused(const QString& why);

protected:
    virtual std::optional<TimelapseExportChoice> askExport(std::shared_ptr<const Timelapse> recording, const QString& canvasName,
                                                           const QString& encoderNote);
    virtual QString askExportPath(const QString& suggested);
    virtual bool confirmDelete(const QString& name, int steps);
    virtual bool confirmRestart(const QString& name, int steps);
    virtual void showError(const QString& title, const QString& text);

private:
    QPointer<QWidget> m_parent;
    std::optional<FfmpegTools> m_tools;
};

}
