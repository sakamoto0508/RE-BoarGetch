# Capture presentation asset scripts (AKI-164 / AKI-165)

Headless `-run=pythonscript` scripts that (re)generate the capture camera and teleport VFX assets. Close the editor first.

```
UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>/CaptureCameraPresentationAsset.py" -unattended -nosplash -NullRHI
```

| Script | Env | Creates / changes |
|---|---|---|
| `CaptureCameraPresentationAsset.py` | `CCP_STEP=apply` (default) / `verify` | `DA_CaptureCameraPresentation` (rotation + shake + collision + timing in one DataAsset), wires `CameraPresentation` on `BP_BoarPlayerController` / `BP_PC_Lobby` |
| `CaptureTeleportVFX.py` | `CTV_STEP=create` / `configure` / `inspect` | `NS_CaptureTeleport`, `NS_CaptureTeleportImpact`, `M_CaptureTeleport`, `MI_CaptureTeleport_*` (configure runs `CaptureTeleportVFXConfigure.py`) |

Results are written to `Saved/*.json|txt`. Niagara VM compile errors appear in the commandlet log (`LogNiagaraCompiler: Error`); Material shaders are not compiled headless.
