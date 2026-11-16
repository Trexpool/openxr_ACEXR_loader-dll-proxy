param(
	[Parameter(Mandatory=$false)]
	[string]$RepoRoot = "C:\Users\Aways\source\repos\openxr_loader-dll-proxy"
)

$env:XR_API_LAYER_PATH = $RepoRoot
$env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_OSSO_openxr_input_layer'
$env:XR_LOADER_DEBUG = 'all'

Write-Host "XR_API_LAYER_PATH=$($env:XR_API_LAYER_PATH)"
Write-Host "XR_ENABLE_API_LAYERS=$($env:XR_ENABLE_API_LAYERS)"
Write-Host "XR_LOADER_DEBUG=$($env:XR_LOADER_DEBUG)"
Write-Host "OpenXR input layer environment registered for this PowerShell session."
