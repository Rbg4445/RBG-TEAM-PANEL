# setup_playit.ps1
$playitPath = "$PSScriptRoot\playit.exe"
$url = "https://github.com/playit-cloud/playit-agent/releases/latest/download/playit-windows-x86_64.exe"

if (!(Test-Path $playitPath)) {
    Write-Host "Playit.gg indiriliyor..." -ForegroundColor Cyan
    try {
        Invoke-WebRequest -Uri $url -OutFile $playitPath -UseBasicParsing
        Write-Host "Playit.gg basariyla indirildi!" -ForegroundColor Green
    } catch {
        Write-Error "Indirme hatasi: $_"
        exit
    }
}

Write-Host "Playit.gg baslatiliyor..." -ForegroundColor Green
Write-Host "Lutfen ekrandaki 'claim link' baglantisini tarayicinizda acarak eslestirme yapin." -ForegroundColor Yellow
Start-Process -FilePath $playitPath -NoNewWindow -Wait
