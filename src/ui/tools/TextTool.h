#pragma once

#include "core/layers/LayerId.h"
#include "core/text/TextContent.h"
#include "ui/tools/CanvasTool.h"

#include <QMetaObject>

#include <vector>

namespace doodlebold {

class TextLayout;

class TextTool : public CanvasTool {
public:
    explicit TextTool(ToolHost& host);
    ~TextTool() override;

    void press(const ToolPointer& p) override;
    void move(const ToolPointer& p) override;
    void release(const ToolPointer& p) override;
    bool keyPress(QKeyEvent* e) override;
    bool wantsKey(const QKeyEvent* e) const override;
    bool isActive() const override { return m_dragging; }
    void cancel() override { m_dragging = false; }
    void paintOverlay(QPainter& p, const QTransform& docToWidget) override;
    Qt::CursorShape cursor(const QPointF&) const override { return Qt::IBeamCursor; }

    LayerId editing() const { return m_layer; }
    int caret() const { return m_caret; }
    int anchor() const { return m_anchor; }

private:
    const TextContent* content() const;
    LayerId textLayerAt(const QPointF& doc) const;
    void commit(const TextContent& edited, bool newStep);
    void replaceSelection(const QString& text);
    void setCaret(int pos, bool extend);
    TextStyle styleFromTools() const;
    void showStyleInPanel();
    void onOptionsChanged();
    void onColourChanged();

    LayerId m_layer;
    int m_caret = 0;
    int m_anchor = 0;
    bool m_dragging = false;
    bool m_syncingPanel = false;  // the tool is showing the caret's style in the panel: not an edit
    TextStyle m_typing;
    int m_mergeKey = 0;           // undo steps of one typing burst merge
    std::vector<QMetaObject::Connection> m_connections;
};

}
