Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$original = [System.Windows.Forms.Cursor]::Position
$target = New-Object System.Drawing.Point(240, 180)

Write-Host ("Original cursor position: ({0}, {1})" -f $original.X, $original.Y)
Write-Host ("Target cursor position:   ({0}, {1})" -f $target.X, $target.Y)

[System.Windows.Forms.Cursor]::Position = $target
Start-Sleep -Milliseconds 300

$actual = [System.Windows.Forms.Cursor]::Position
Write-Host ("Actual cursor position:   ({0}, {1})" -f $actual.X, $actual.Y)

$passed = ($actual.X -eq $target.X) -and ($actual.Y -eq $target.Y)

if ($passed) {
    Write-Host "RESULT: PASS"
    [System.Windows.Forms.Cursor]::Position = $original
    exit 0
}

Write-Host "RESULT: FAIL"
[System.Windows.Forms.Cursor]::Position = $original
exit 1
