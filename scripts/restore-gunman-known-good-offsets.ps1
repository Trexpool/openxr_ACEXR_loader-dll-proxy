param(
	[Parameter(Mandatory=$false)]
	[string]$ConfigPath = "D:\SteamLibrary\steamapps\common\Gunman Contracts - Stand Alone\GunmanContracts_Data\Plugins\x86_64\openxr_loader_config.txt"
)

if (-not (Test-Path -LiteralPath $ConfigPath)) {
	throw "Config file not found: $ConfigPath"
}

$backup = "$ConfigPath.before-known-good-restore.bak"
Copy-Item -LiteralPath $ConfigPath -Destination $backup -Force

$text = Get-Content -LiteralPath $ConfigPath -Raw

$replacements = @{
	'frame\s*=\s*.*' = 'frame=world'
	'left_hand_rotation_offset_x\s*=\s*.*' = 'left_hand_rotation_offset_x=0.000000'
	'left_hand_rotation_offset_y\s*=\s*.*' = 'left_hand_rotation_offset_y=0.000000'
	'left_hand_rotation_offset_z\s*=\s*.*' = 'left_hand_rotation_offset_z=0.000000'

	'right_hand_rotation_offset_x\s*=\s*.*' = 'right_hand_rotation_offset_x=156.400000'
	'right_hand_rotation_offset_y\s*=\s*.*' = 'right_hand_rotation_offset_y=-19.500000'
	'right_hand_rotation_offset_z\s*=\s*.*' = 'right_hand_rotation_offset_z=170.100000'

	'left_hand_position_offset_x\s*=\s*.*' = 'left_hand_position_offset_x=0.000000'
	'left_hand_position_offset_y\s*=\s*.*' = 'left_hand_position_offset_y=0.000000'
	'left_hand_position_offset_z\s*=\s*.*' = 'left_hand_position_offset_z=0.000000'

	'right_hand_position_offset_x\s*=\s*.*' = 'right_hand_position_offset_x=0.000000'
	'right_hand_position_offset_y\s*=\s*.*' = 'right_hand_position_offset_y=0.000000'
	'right_hand_position_offset_z\s*=\s*.*' = 'right_hand_position_offset_z=0.000000'
}

foreach($pattern in $replacements.Keys){
	$text = [Regex]::Replace($text, $pattern, $replacements[$pattern])
}

Set-Content -LiteralPath $ConfigPath -Value $text -NoNewline
Write-Host "Restored known-good offsets. Backup: $backup"
