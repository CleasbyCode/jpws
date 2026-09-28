# jpws golden-test script (block)
$items = 1..5 | ForEach-Object { "item-$_" }
foreach ($i in $items) {
    Write-Output $i
}
Write-Host "done"
