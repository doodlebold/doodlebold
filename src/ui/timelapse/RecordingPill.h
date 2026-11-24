#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

class QMenu;

namespace doodlebold {

class Document;

class RecordingPill : public QWidget {
    Q_OBJECT
public:
    enum class Command {
        Start,
        Pause,
        Resume,
        Replay,
        Export,
        Restart,
        Delete
    };

    explicit RecordingPill(Document* doc, QWidget* parent = nullptr);

    Document* document() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    QString text() const;
    QMenu* buildMenu(QWidget* parent);

    static QString groupedNumber(int n);
    static QString infoText(int steps, std::int64_t startedMs, std::int64_t paintedMs, std::int64_t bytes);

signals:
    void changed();
    void commandRequested(doodlebold::RecordingPill::Command command);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void sync();

    QPointer<Document> m_doc;
    bool m_hover = false;
};

}
