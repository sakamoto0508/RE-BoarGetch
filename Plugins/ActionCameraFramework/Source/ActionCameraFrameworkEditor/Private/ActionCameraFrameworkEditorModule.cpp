#include "Modules/ModuleManager.h"

/**
 * Editor module for Action Camera Framework (README 13 章 / 23 章).
 *
 * It registers nothing: presets and sequences are authored with the standard Data Asset editor and checked by
 * UACFCameraPreset::IsDataValid / UACFCameraSequence::IsDataValid (shared implementation in
 * ACFCameraPresetValidation, runtime module, WITH_EDITOR only; README 21 章). The module holds the tests that need
 * editor-only modules (Data Validation subsystem, Asset Registry scans) and is the place for future editor-only
 * tooling (preset editor, preview, debugger UI; roadmap 7A), so that none of it leaks into the runtime modules.
 */
class FActionCameraFrameworkEditorModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FActionCameraFrameworkEditorModule, ActionCameraFrameworkEditor)
