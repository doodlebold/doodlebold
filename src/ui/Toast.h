#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

namespace doodlebold {

// One message of a ToastStack. It lets every click and pen stroke through.
class Toast : public QWidget {
    Q_OBJECT
public:
    Toast(QWidget* window, QString text);

    QString text() const { return m_text; }
    bool isLeaving() const { return m_leaving; }
    bool isShaking() const { return m_shake.state() == QAbstractAnimation::Running; }

    void setMaxWidth(int width);
    void placeAt(const QPoint& rest, bool animated);
    void appear(const QPoint& rest, int holdMs);
    // The same message again: it shakes and starts its time again.
    void repeat(int holdMs);
    void leave();

signals:
    void gone();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void fadeTo(double opacity);
    void applyPosition();

    QString m_text;
    QPoint m_rest;
    QVariantAnimation m_fade;
    QVariantAnimation m_move;
    QVariantAnimation m_shake;
    QTimer m_hold;
    double m_opacity = 0;
    int m_shakeOffset = 0;
    bool m_leaving = false;
};

// Short messages at the top right of `anchor`, where the user is looking: why an edit was refused. A message already showing
// shakes when it comes again; another one goes below the ones showing. Each fades away by itself.
class ToastStack : public QObject {
    Q_OBJECT
public:
    ToastStack(QWidget* window, QWidget* anchor);

    void showMessage(const QString& text);
    // Top to bottom.
    const QList<Toast*>& toasts() const { return m_toasts; }
    // How long a message stays before it fades. Negative, as by default: by the length of its text.
    void setHoldTime(int ms) { m_holdOverride = ms; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void arrange(bool animated);
    QPoint restOf(int index) const;
    int holdTimeFor(const QString& text) const;

    QWidget* m_window;
    QWidget* m_anchor;
    QList<Toast*> m_toasts;
    int m_holdOverride = -1;
};

}
