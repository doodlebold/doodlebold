#include "core/document/DocumentCommands.h"

namespace doodlebold {

StateCommand::StateCommand(Document* doc, DocumentState before, DocumentState after, const QString& text, int mergeKey, LayerId mergeLayer)
    : QUndoCommand(text),
      m_doc(doc),
      m_before(std::move(before)),
      m_after(std::move(after)),
      m_mergeKey(mergeKey),
      m_mergeLayer(mergeLayer) {}

void StateCommand::undo() {
    m_doc->restoreState(m_before);
}

void StateCommand::redo() {
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_doc->restoreState(m_after);
}

bool StateCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = static_cast<const StateCommand*>(other);
    if (o->m_mergeLayer != m_mergeLayer)
        return false;
    m_after = o->m_after;
    return true;
}

TileEditCommand::TileEditCommand(Document* doc, LayerId layer, TileMap before, TileMap after, const QString& text)
    : QUndoCommand(text), m_doc(doc), m_layer(layer), m_before(std::move(before)), m_after(std::move(after)) {}

void TileEditCommand::undo() {
    m_doc->restoreTiles(m_layer, m_before);
}

void TileEditCommand::redo() {
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_doc->restoreTiles(m_layer, m_after);
}

}
