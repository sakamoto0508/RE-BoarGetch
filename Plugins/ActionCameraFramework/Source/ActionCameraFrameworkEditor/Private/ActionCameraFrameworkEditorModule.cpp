#include "Modules/ModuleManager.h"

/**
 * Editor module for Action Camera Framework.
 *
 * Phase 1 intentionally registers nothing: presets are authored through the standard
 * Data Asset editor and validated by UACFCameraPreset::IsDataValid. This module exists so
 * that editor-only tooling (preset editor, preview, debugger, composition guides; see
 * README.md / Phase 6) never leaks into the runtime module.
 */
class FActionCameraFrameworkEditorModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FActionCameraFrameworkEditorModule, ActionCameraFrameworkEditor)
