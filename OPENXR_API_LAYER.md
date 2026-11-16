# OpenXR API layer

The project now has a separate API-layer target. The legacy `openxr_loader.dll` proxy remains available as a fallback.

## Build

From the MSYS2 UCRT64 shell:

```sh
export PATH=/ucrt64/bin:/usr/bin:$PATH
cd /c/Users/Aways/source/repos/openxr_loader-dll-proxy
mingw32-make input_layer
```

The output is `obj/InputLayer/openxr_input_layer.dll`.

## Register for testing

The OpenXR loader discovers API-layer manifests through the standard API-layer registry/environment mechanism. For a local, non-administrator test, set `XR_API_LAYER_PATH` to the repository directory containing `openxr_input_layer.json` before starting the game. The manifest uses a repository-relative DLL path, so launch the application with the repository as its working directory or change `library_path` to an absolute path for local testing.

On Windows PowerShell:

```powershell
$env:XR_API_LAYER_PATH = 'C:\Users\Aways\source\repos\openxr_loader-dll-proxy'
```

For a system-wide installation, copy the manifest and DLL to a dedicated layer directory and register the manifest through the OpenXR loader's documented API-layer registry location. Do not replace the game's `openxr_loader.dll` until the API layer has been validated.

## Current milestone

The layer negotiates with the loader and chains to the next layer/runtime. It intercepts and logs:

- `xrCreateAction`
- `xrSuggestInteractionProfileBindings`
- `xrGetActionStateBoolean`
- `xrGetActionStateFloat`
- `xrLocateSpace`

Logs are written to `openxr_api_layer.log` in the process working directory.

The pose correction, GUI, action-aware remapping, and trigger-threshold algorithms are still in the legacy proxy. The next migration step is to move those algorithms behind the layer's per-instance/per-session dispatch state, then validate the layer with Gunman Contracts before retiring the proxy fallback.
