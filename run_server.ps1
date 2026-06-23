# run_server.ps1
# Script to compile and run the Management Panel Server in Docker

Write-Host "Checking Docker status..." -ForegroundColor Cyan
& docker ps > $null 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "HATA: Docker aktif değil! Lütfen önce Docker Desktop uygulamasını başlatın." -ForegroundColor Red
    Exit
}

Write-Host "1. Docker imajı derleniyor (management_server)..." -ForegroundColor Cyan
& docker build -t management_server .

if ($LASTEXITCODE -ne 0) {
    Write-Host "HATA: Derleme başarısız oldu!" -ForegroundColor Red
    Exit
}

Write-Host "2. Eski konteyner temizleniyor..." -ForegroundColor Cyan
& docker stop management_server > $null 2>&1
& docker rm management_server > $null 2>&1

Write-Host "3. Konteyner başlatılıyor..." -ForegroundColor Cyan
& docker run -d `
  --name management_server `
  -p 7777:7777/udp `
  -p 8080:8080/tcp `
  management_server

if ($LASTEXITCODE -eq 0) {
    Write-Host "BAŞARILI: Sunucu başarıyla başlatıldı!" -ForegroundColor Green
    Write-Host "Logları izlemek için: docker logs -f management_server" -ForegroundColor Yellow
} else {
    Write-Host "HATA: Konteyner başlatılamadı!" -ForegroundColor Red
}
