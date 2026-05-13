#pragma once
class QTreeWidget;
namespace s7ui::visual { class Catalog; }
namespace s7ui::ui {
/// Stateless service that builds the node palette tree and registers
/// element libraries.  All methods are static — no state to manage.
///
/// SRP: owns only palette-population and library-bootstrap concerns.
struct PaletteService
{
    /// Call once at application start (or lazily before first populate).
    /// Guard against double-registration is internal.
    static void ensureLibrariesRegistered();
    /// Populate \p paletteTree from the element registry and \p visualCatalog.
    /// Clears the tree before filling it.
    static void populate(QTreeWidget* paletteTree,
                         const s7ui::visual::Catalog* visualCatalog);
};
} // namespace s7ui::ui
