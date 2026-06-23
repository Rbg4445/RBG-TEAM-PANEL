# Yönetim Paneli – VDS Kurulum Rehberi

Bu repo, **C++ / Vulkan** tabanlı bir yönetim paneli uygulamasının **client** ve **server** bileşenlerini içerir. Sunucu Docker container içinde çalıştırılır ve VDS (Virtual Dedicated Server) üzerine dağıtılabilir.

## Gereksinimler
- VDS – Ubuntu 22.04 (ya da benzer Debian tabanlı) 
- Docker Engine (>= 20.10) ve `docker compose` opsiyonel
- Alan adı (domain) – HTTPS/TLS için önerilir (Let's Encrypt). 
- Açık portlar: `7777/udp` (ENet metin sohbet) ve `8080/tcp` (WebRTC signaling).

## 1. VDS’ye Docker kurulumu
```bash
# Docker kurulum (Ubuntu 22.04)
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg lsb-release
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg

echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] \
  https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io
sudo usermod -aG docker $USER   # oturumu yeniden başlatın
```

## 2. Repoyu VDS’ye çekme
```bash
git clone https://github.com/kullanici/management_panel.git   # kendi repo URL'nizi ekleyin
cd management_panel/scratch/management_panel
```

## 3. Docker imajını oluşturma
```bash
docker build -t management_server .
```
Bu komut `Dockerfile` içerisindeki **builder** aşamasını çalıştırır, `server` ikili dosyasını derler ve **runtime** imajını oluşturur.

## 4. Container’ı çalıştırma
```bash
# Firewall kuralları (ufw) – sadece gerekli portlar açık
sudo ufw allow 7777/udp
sudo ufw allow 8080/tcp
sudo ufw enable

# Container'i başlat (alan adı/HTTPS kullansanız HTTP üzerinden 8080’i reverse‑proxy ile TLS’ye yönlendirebilirsiniz)
docker run -d \
  --name management_server \
  -p 7777:7777/udp -p 8080:8080/tcp \
  -v /srv/management/data:/data   # SQLite dosyası kalıcı olsun
  management_server
```

## 5. HTTPS / TLS (opsiyonel)
Alan adınız varsa, bir **NGINX** reverse‑proxy kurup Let’s Encrypt sertifikası alabilirsiniz.
```bash
sudo apt-get install -y nginx certbot python3-certbot-nginx
sudo certbot --nginx -d mypanel.example.com
# NGINX config örneği ( /etc/nginx/sites‑available/mypanel )
#
# server {
#     listen 443 ssl;
#     server_name mypanel.example.com;
#
#     location / {
#         proxy_pass http://localhost:8080;   # signaling API
#         proxy_set_header Host $host;
#         proxy_set_header X-Real-IP $remote_addr;
#         proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
#     }
# }
```

## 6. İstemci (Windows) kurulumu
1. **Vulkan SDK** (LunarG) ve **Visual Studio 2022** kurun.
2. Repo içindeki `CMakeLists.txt` dosyasını kullanarak `client` projesini derleyin:
```powershell
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```
3. Oluşan `client.exe` dosyasını çalıştırın, sunucu IP/alan adını girerek bağlanın.

---

## Sorun Giderme
- **Docker container hemen kapanıyorsa**: `docker logs management_server` ile logları inceleyin.
- **ENet bağlantısı başarısız**: VDS üzerindeki firewall kurallarını kontrol edin (`ufw status`).
- **WebRTC sesi gelmiyorsa**: STUN sunucusunun ulaşılabilir olduğundan ve `libdatachannel` kütüphanesinin doğru derlendiğinden emin olun.

---

**İyi kodlamalar!** Herhangi bir sorunuz olursa buradan bildirin. 🚀
