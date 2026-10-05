import QtQuick
import QtQuick.Controls
import SimplePlayer

ComboBox {
    id: box
    popup: Popup {
        y: box.height; width: box.width
        height: Math.min(box.count*Theme.controlSize+8,Screen.height-80)
        padding: 4; popupType: Popup.Window
        onAboutToShow: options.forceLayout()
        contentItem: ListView {
            id: options; clip: true
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
        background: Rectangle { color: Theme.panel; border.color: Theme.raised }
    }
    delegate: ItemDelegate {
        width: box.width-8; height: Theme.controlSize
        text: box.textAt(index)
        highlighted: box.highlightedIndex === index
        font: box.font
    }
}
