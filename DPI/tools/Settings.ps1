function Set-NoDPIValue([string]$Path, [string]$Key, [string]$Value) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191
    $offset = 0
    if ($bom) { $offset = 3 }
    $text = [Text.Encoding]::UTF8.GetString($bytes, $offset, $bytes.Length - $offset)
    $newline = "`n"
    if ($text.Contains("`r`n")) { $newline = "`r`n" }
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in [regex]::Split($text, '\r\n|\n|\r')) { $lines.Add($line) }
    $first = -1
    $last = $lines.Count
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index].Trim() -eq '[NoDPI]') { $first = $index; continue }
        if ($first -ge 0 -and $lines[$index].Trim() -match '^\[.*\]$') { $last = $index; break }
    }
    if ($first -lt 0) { $lines.Add('[NoDPI]'); $first = $lines.Count - 1; $last = $lines.Count }
    $found = $false
    for ($index = $first + 1; $index -lt $last; $index++) {
        if ($lines[$index] -match ('^\s*' + [regex]::Escape($Key) + '\s*=')) {
            $equals = $lines[$index].IndexOf('=')
            $lines[$index] = $lines[$index].Substring(0, $equals + 1) + $Value
            $found = $true
        }
    }
    if (-not $found) { $lines.Insert($last, $Key + '=' + $Value) }
    [IO.File]::WriteAllText($Path, [string]::Join($newline, $lines), [Text.UTF8Encoding]::new($bom))
}
