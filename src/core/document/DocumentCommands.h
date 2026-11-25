#pragma once

#include "core/document/Document.h"

#include <QUndoCommand>

namespace doodlebold {

// Undo step for structural and property edits: the whole document state before and after (tiles are shared, so this costs
// pointers). The first redo() is a no-op because the edit was already applied when it was pushed.
class StateCommand : public QUndoCommand {
public:
    StateCommand(Document* doc, DocumentState before, DocumentState after, const QString& text, int mergeKey, LayerId mergeLayer);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeKey > 0 ? m_mergeKey : -1; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    Document* m_doc;
    DocumentState m_before;
    DocumentState m_after;
    int m_mergeKey;
    LayerId m_mergeLayer;
    bool m_firstRedo = true;
};

class TileEditCommand : public QUndoCommand {
public:
    TileEditCommand(Document* doc, LayerId layer, TileMap before, TileMap after, const QString& text);
    void undo() override;
    void redo() override;
    // Only the tiles it puts back: its after tiles are the layer's own, or the next step's before, and would be counted twice.
    std::int64_t heldBytes() const;
    void letPixelsGo();
    bool pixelsLetGo() const { return m_letGo; }

private:
    Document* m_doc;
    LayerId m_layer;
    TileMap m_before;
    TileMap m_after;
    bool m_firstRedo = true;
    bool m_letGo = false;
};

}
