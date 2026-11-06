$path = "C:\SentinelOS\scripts\build.ps1"
$content = Get-Content -LiteralPath $path
$newContent = $content | ForEach-Object {
    if ($_ -match 'Invoke-Lld @\(' -and $_ -notmatch 'Invoke-Lld \(@\(') {
        $marker = "Invoke-Lld "
        $idx = $_.IndexOf($marker) + $marker.Length
        $newLine = $_.Substring(0, $idx) + "(" + $_.Substring($idx) + ")"
        $newLine
    } else {
        $_
    }
}
Set-Content -LiteralPath $path -Value $newContent
Write-Host "Done fixing Invoke-Lld calls"