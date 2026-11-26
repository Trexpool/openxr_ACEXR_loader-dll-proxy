param([string]$Path, [string]$Out)
# Read-only literal string scanner (ASCII + UTF-16LE). Reports file, offset, ~80 chars context.
$rx = '(?i)(vr[._][ -~]{0,40}trigger[ -~]{0,40}|trigger[ -~]{0,40}(smoothing|deadzone|sensitivity|curve)[ -~]{0,40}|(smoothing|deadzone|sensitivity|curve)[ -~]{0,40}trigger[ -~]{0,40}|MotionController\w*)'
$fs = [System.IO.File]::OpenRead($Path)
$chunk = 16MB; $overlap = 256
$buf = New-Object byte[] ($chunk + $overlap)
$pos = [int64]0; $carry = 0; $hits = New-Object System.Collections.Generic.List[string]
$latin = [System.Text.Encoding]::GetEncoding(28591)
while ($true) {
  $n = $fs.Read($buf, $carry, $chunk)
  if ($n -le 0) { break }
  $len = $carry + $n
  $base = $pos - $carry
  # ASCII pass
  $s = $latin.GetString($buf, 0, $len)
  foreach ($m in [regex]::Matches($s, $rx)) {
	if ($m.Index + $m.Length -le $carry -and $pos -gt 0) { continue }
	$a = [Math]::Max(0, $m.Index - 30); $b = [Math]::Min($s.Length, $m.Index + $m.Length + 30)
	$ctx = ($s.Substring($a, $b - $a) -replace '[^ -~]', '.')
	$hits.Add(("A`t{0}`t{1}`t{2}" -f $Path, ($base + $m.Index), $ctx))
  }
  # UTF-16LE pass (even and odd alignment)
  foreach ($al in 0, 1) {
	if ($len - $al -lt 2) { continue }
	$cnt = [int][Math]::Floor(($len - $al) / 2) * 2
	$u = [System.Text.Encoding]::Unicode.GetString($buf, $al, $cnt)
	foreach ($m in [regex]::Matches($u, $rx)) {
	  $off = $base + $al + 2 * $m.Index
	  if ($off + 2 * $m.Length -le $pos - 0 -and $off -lt $pos - $carry + 0 -and $pos -gt 0) { continue }
	  $a = [Math]::Max(0, $m.Index - 30); $b = [Math]::Min($u.Length, $m.Index + $m.Length + 30)
	  $ctx = ($u.Substring($a, $b - $a) -replace '[^ -~]', '.')
	  $hits.Add(("W`t{0}`t{1}`t{2}" -f $Path, $off, $ctx))
	}
  }
  $pos += $n
  $carry = [Math]::Min($overlap, $len)
  [Array]::Copy($buf, $len - $carry, $buf, 0, $carry)
}
$fs.Close()
$hits | Sort-Object -Unique | Set-Content -Path $Out -Encoding UTF8
"hits: $($hits.Count)"
