#include "Modules/ModuleManager.h"

// Phase 6B / B-10: automation tests of Action Camera Framework (README 23 章). The tests register themselves when this
// module loads (Editor, -game with the editor binary, Debug / Development game builds); the module has no runtime role.
IMPLEMENT_MODULE(FDefaultModuleImpl, ActionCameraFrameworkTests)
