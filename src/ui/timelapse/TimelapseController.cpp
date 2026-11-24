#include "ui/timelapse/TimelapseController.h"

#include "core/document/Document.h"
#include "core/timelapse/TimelapseExport.h"
#include "core/util/AppIdentity.h"
#include "ui/timelapse/RecordingPill.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QProgressDialog>
#include <QStandardPaths>

#include <atomic>
#include <chrono>
#include <thread>

namespace doodlebold {

namespace {

const QString kFfmpegPathKey = QStringLiteral("ffmpeg/path");

class WaitCursor {
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
};

}

TimelapseController::TimelapseController(QWidget* dialogParent, QObject* parent) : QObject(parent), m_parent(dialogParent) {}

QString TimelapseController::configuredPath() {
    return app_identity::settings().value(kFfmpegPathKey).toString();
}

void TimelapseController::setConfiguredPath(const QString& path) {
    QSettings s = app_identity::settings();
    if (path.isEmpty())
        s.remove(kFfmpegPathKey);
    else
        s.setValue(kFfmpegPathKey, path);
}

std::optional<FfmpegTools> TimelapseController::tools(bool ask) {
    if (m_tools)
        return m_tools;
    for (;;) {
        Result<FfmpegTools, FfmpegError> found = Result<FfmpegTools, FfmpegError>::failure(FfmpegError::NotFound);
        {
            WaitCursor wait;
            found = FfmpegLocator::locate(configuredPath(), FfmpegLocator::defaultToolsFolder());
        }
        if (found) {
            m_tools = found.value();
            return m_tools;
        }
        if (!ask)
            return std::nullopt;
        FfmpegMissingDialog dialog(describe(found.error()), m_parent);
        dialog.exec();
        switch (dialog.answer()) {
        case FfmpegMissingDialog::Answer::Cancel: return std::nullopt;
        case FfmpegMissingDialog::Answer::TryAgain: break;
        case FfmpegMissingDialog::Answer::Located: setConfiguredPath(dialog.locatedPath()); break;
        }
    }
}

QString TimelapseController::deleteQuestion(const QString& name, int steps) {
    return tr("Delete the recording of “%1”?\n\nIts %2 steps are removed from the canvas, and the next time you save, from the file. "
              "This cannot be undone.")
        .arg(name, RecordingPill::groupedNumber(steps));
}

QString TimelapseController::restartQuestion(const QString& name, int steps) {
    return tr("Restart the recording of “%1”?\n\nIts %2 steps are deleted, and a new recording starts from the picture as it is now. "
              "This cannot be undone.")
        .arg(name, RecordingPill::groupedNumber(steps));
}

std::optional<TimelapseExportChoice> TimelapseController::askExport(std::shared_ptr<const Timelapse> recording, const QString& canvasName,
                                                                    const QString& encoderNote) {
    ExportTimelapseDialog dialog(std::move(recording), canvasName, encoderNote, m_parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.choice();
}

QString TimelapseController::askExportPath(const QString& suggested) {
    return QFileDialog::getSaveFileName(m_parent, tr("Export Timelapse"), suggested, tr("MP4 video (*.mp4)"));
}

bool TimelapseController::confirmDelete(const QString& name, int steps) {
    QMessageBox box(QMessageBox::Warning, tr("Delete the Recording"), deleteQuestion(name, steps), QMessageBox::NoButton, m_parent);
    QAbstractButton* del = box.addButton(tr("Delete the Recording"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == del;
}

bool TimelapseController::confirmRestart(const QString& name, int steps) {
    QMessageBox box(QMessageBox::Warning, tr("Restart the Recording"), restartQuestion(name, steps), QMessageBox::NoButton, m_parent);
    QAbstractButton* restart = box.addButton(tr("Restart the Recording"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == restart;
}

void TimelapseController::showError(const QString& title, const QString& text) {
    QMessageBox::warning(m_parent, title, text);
}

void TimelapseController::startRecording(Document* doc) {
    if (!doc || doc->isRecording())
        return;
    doc->startRecording();
    emit message(tr("Recording started"));
}

void TimelapseController::setPaused(Document* doc, bool paused) {
    if (!doc || !doc->isRecording())
        return;
    doc->setRecordingPaused(paused);
    emit message(paused ? tr("Recording paused") : tr("Recording resumed"));
}

bool TimelapseController::deleteRecording(Document* doc) {
    if (!doc || !doc->isRecording())
        return false;
    if (!confirmDelete(doc->displayName(), doc->recordedSteps()))
        return false;
    doc->deleteRecording();
    emit message(tr("Recording deleted"));
    return true;
}

bool TimelapseController::restartRecording(Document* doc) {
    if (!doc || !doc->isRecording())
        return false;
    if (!confirmRestart(doc->displayName(), doc->recordedSteps()))
        return false;
    doc->deleteRecording();
    doc->startRecording();
    emit message(tr("Recording restarted"));
    return true;
}

bool TimelapseController::exportTimelapse(Document* doc) {
    if (!doc || !doc->isRecording())
        return false;
    const auto ffmpeg = tools();
    if (!ffmpeg)
        return false;
    const QString encoderName = chooseVideoEncoder(*ffmpeg);
    if (encoderName.isEmpty()) {
        showError(tr("Export Timelapse"), tr("This FFmpeg (%1) has no H.264 or MPEG-4 encoder.").arg(ffmpeg->ffmpeg));
        return false;
    }
    const QString note = encoderName == QLatin1String("libx264")
                             ? tr("H.264 High profile, 4:2:0 colour, fast start. FFmpeg %1.").arg(ffmpeg->version)
                             : tr("Encoded with %1 (libx264 is not in this FFmpeg). FFmpeg %2.").arg(encoderName, ffmpeg->version);
    const std::shared_ptr<const Timelapse> recording = doc->timelapse();  // waits for the worker that is still compressing
    if (!recording || recording->stepCount() < 1) {
        showError(tr("Export Timelapse"), tr("Nothing has been recorded yet. Paint something first."));
        return false;
    }
    const auto answered = askExport(recording, doc->displayName(), note);
    if (!answered)
        return false;
    const TimelapseExportChoice& choice = *answered;
    const QString base = QFileInfo(doc->displayName()).completeBaseName();
    const QString dir = !doc->filePath().isEmpty() ? QFileInfo(doc->filePath()).absolutePath()
                                                   : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString path = askExportPath(QDir(dir.isEmpty() ? QDir::homePath() : dir).filePath(base + QStringLiteral("-timelapse.mp4")));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".mp4");

    const TimelapsePlan plan = planTimelapse(*recording, choice.options);
    EncodeSettings settings;
    settings.output = path;
    settings.crf = choice.crf;
    settings.maxBitrateKbps = choice.maxBitrateKbps;
    settings.audioKbps = choice.audioKbps;
    AudioPlan audio;
    if (!choice.soundFile.isEmpty()) {
        Result<double, QString> length = Result<double, QString>::failure({});
        {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            length = probeSoundDuration(*ffmpeg, choice.soundFile);
            QApplication::restoreOverrideCursor();
        }
        if (!length) {
            showError(tr("Export Timelapse"),
                      tr("“%1” could not be used as the sound.\n\n%2").arg(QFileInfo(choice.soundFile).fileName(), length.error()));
            return false;
        }
        audio = soundFilePlan(choice.soundFile, length.value(), plan.seconds());
    }
    if (choice.maxFileMb > 0.0) {
        const int fit = bitrateForFileSize(choice.maxFileMb, plan.seconds(), audio.isEmpty() ? 0 : choice.audioKbps);
        if (fit < 100) {
            showError(tr("Export Timelapse"),
                      tr("%1 MB is too small for %2 s of video.").arg(choice.maxFileMb).arg(plan.seconds(), 0, 'f', 1));
            return false;
        }
        settings.targetBitrateKbps = choice.maxBitrateKbps > 0 ? std::min(fit, choice.maxBitrateKbps) : fit;
        settings.maxBitrateKbps = settings.targetBitrateKbps;  // the size is a limit: no peak may go over the average either
    }

    // The frames are made and written on a thread of its own (the FFmpeg process lives there).
    std::atomic<int> done{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    TimelapseExportResult result;
    const FfmpegTools& tools = *ffmpeg;
    std::thread worker([&] {
        result = doodlebold::exportTimelapse(tools, *recording, plan, settings, audio, [&](int n, int) {
            done = n;
            return !cancel.load();
        });
        finished = true;
    });
    const int total = static_cast<int>(plan.frames.size());
    QProgressDialog progress(tr("Writing the timelapse …"), tr("Cancel"), 0, total, m_parent);
    progress.setWindowTitle(tr("Export Timelapse"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    while (!finished) {
        progress.setValue(std::min(done.load(), total - 1));
        if (progress.wasCanceled())
            cancel = true;
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    progress.reset();
    if (result.cancelled) {
        emit message(tr("Export cancelled"));
        return false;
    }
    if (!result.ok) {
        showError(tr("Export Timelapse"), tr("The export failed.\n\n%1").arg(result.error));
        return false;
    }
    emit message(tr("Exported “%1”").arg(QFileInfo(path).fileName()));
    return true;
}

}
