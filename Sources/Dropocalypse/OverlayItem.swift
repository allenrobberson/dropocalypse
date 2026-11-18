import Foundation

/// One logo or text item. Saved as JSON in the app's support folder (items.json); image items
/// point at a private PNG copy in the same folder.
struct OverlayItem: Codable, Identifiable, Equatable {
    var id = UUID()
    var isText = false
    /// Image file name inside the support folder (images only).
    var file = ""
    /// What the menus show: the original file name, or the text.
    var name = ""
    var text = ""
    /// Index into OverlayLayout.textFontNames / textColors (text only).
    var font = 0
    var color = 0

    init(image file: String, name: String) {
        self.file = file
        self.name = name
    }

    init(text: String, font: Int, color: Int) {
        isText = true
        self.text = text
        name = text
        self.font = font
        self.color = color
    }
}
