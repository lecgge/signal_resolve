$versions = @{8="Python38"; 9="Python39"; 10="Python310"; 12="Python312"; 13="Python313"; 14="Python314"}
foreach ($v in ($versions.Keys | Sort-Object)) {
    $name = $versions[$v]
    $py = "$env:LOCALAPPDATA\Programs\Python\$name\python.exe"
    if (!(Test-Path $py)) {
        Write-Host "SKIP 3.$($v): not found at $py"
        continue
    }
    Write-Host "=== Python 3.$v ==="
    $bd = "D:\work\signal_resolve\build_py$v"
    Remove-Item $bd -Recurse -Force -ErrorAction SilentlyContinue
    $pb = & $py -m pybind11 --cmakedir 2>&1
    cmake -S D:\work\signal_resolve -B $bd -G "Visual Studio 17 2022" -A x64 -DPYTHON_EXECUTABLE="$py" -Dpybind11_DIR="$pb" 2>&1 | Select-Object -Last 1
    cmake --build $bd --config Release --target usde_python 2>&1 | Select-Object -Last 1
    Copy-Item "$bd\Release\usde_python*.pyd" "D:\work\signal_resolve\python\usde\" -Force -ErrorAction SilentlyContinue
}
Write-Host "=== Done ==="
Get-ChildItem "D:\work\signal_resolve\python\usde\*.pyd" | ForEach-Object { "$($_.Name)  $([math]::Round($_.Length/1024))KB" }
