param(
	[Parameter(Mandatory=$false)]
	[string]$RepoRoot = "C:\Users\Aways\source\repos\openxr_loader-dll-proxy",

	[Parameter(Mandatory=$false)]
	[string]$SteamPath = "C:\Program Files (x86)\Steam\steam.exe"
)

if (-not (Test-Path -LiteralPath $SteamPath)) {
	throw "Steam executable not found at: $SteamPath"
}

$env:XR_API_LAYER_PATH = $RepoRoot
$env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_OSSO_openxr_input_layer'
$env:XR_LOADER_DEBUG = 'all'

Write-Host "Launching Steam with OpenXR input layer enabled..."
Write-Host "XR_API_LAYER_PATH=$($env:XR_API_LAYER_PATH)"
Write-Host "XR_ENABLE_API_LAYERS=$($env:XR_ENABLE_API_LAYERS)"

Start-Process -FilePath $SteamPath -WorkingDirectory (Split-Path -Parent $SteamPath)
