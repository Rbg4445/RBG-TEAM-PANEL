#!/bin/bash
set -e

ARCH=$(dpkg --print-architecture)
echo "Building debian package for architecture: ${ARCH}"

# Bağımlılıkları yükle (Sadece root yetkisi varsa çalıştırılır)
if [ "$EUID" -eq 0 ]; then
  apt-get update && apt-get install -y \
      build-essential \
      cmake \
      ninja-build \
      libvulkan-dev \
      libglfw3-dev \
      libenet-dev \
      libsqlite3-dev \
      libssl-dev \
      pkg-config \
      git
fi

# Derleme dizinini hazırla
mkdir -p build_linux
cd build_linux

# Projeyi yapılandır ve derle
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -G Ninja ..
cmake --build . --parallel $(nproc)

# Paketleme dizini oluştur
PKG_DIR="management-client_${ARCH}"
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/DEBIAN"

# Çıktıları staging alanına yükle
DESTDIR="${PKG_DIR}" cmake --install .

# Gereksiz server binary'sini paket dışı bırak
rm -f "${PKG_DIR}/usr/bin/server"

# Binary adını ve yolunu güncelle
mv "${PKG_DIR}/usr/bin/client" "${PKG_DIR}/usr/bin/management-client"

# Masaüstü kısayolu oluştur (.desktop)
mkdir -p "${PKG_DIR}/usr/share/applications"
cat <<EOT > "${PKG_DIR}/usr/share/applications/management-client.desktop"
[Desktop Entry]
Type=Application
Name=Management Client
Comment=RBG Team Management Client Panel
Exec=/usr/bin/management-client
Icon=management-client
Terminal=false
Categories=Utility;
EOT

# Paket kontrol (control) dosyasını oluştur
cat <<EOT > "${PKG_DIR}/DEBIAN/control"
Package: management-client
Version: 1.0.0
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: RBG Team
Depends: libvulkan1, libglfw3, libenet7, libsqlite3-0, libssl3
Description: RBG Team Management Client Panel
 Client application for managing servers, chat, and voice calls.
EOT

# Masaüstü ikonu kopyala
mkdir -p "${PKG_DIR}/usr/share/pixmaps"
cp ../logo.png "${PKG_DIR}/usr/share/pixmaps/management-client.png"

# .deb paketini oluştur
dpkg-deb --build "${PKG_DIR}"

# Çıktıyı kopyala
mkdir -p ../dist_client
cp "${PKG_DIR}.deb" ../dist_client/

echo "Success! Package built at dist_client/${PKG_DIR}.deb"
