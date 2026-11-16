Remove-Item Env:XR_API_LAYER_PATH -ErrorAction SilentlyContinue
Remove-Item Env:XR_ENABLE_API_LAYERS -ErrorAction SilentlyContinue
Remove-Item Env:XR_LOADER_DEBUG -ErrorAction SilentlyContinue

Write-Host "OpenXR input layer environment variables removed from this PowerShell session."
