# Fast Food Queue System — Backend

Backend REST API sistem antrean restoran menggunakan C++ dan Crow Framework.

> Frontend: https://github.com/auto-doxxer/fast-food-frontend

## Cara Menjalankan

### 1. Download dependensi

Buat folder `include/`, lalu download dua file berikut ke dalamnya:

- **Crow:** https://github.com/CrowCpp/Crow/releases/latest/download/crow_all.h
- **Asio:** https://sourceforge.net/projects/asio/files/latest/download → extract → copy folder `asio/` dan file `asio.hpp` ke dalam folder `include/`

### 2. Compile

**Windows:**
```powershell
g++ main.cpp -o main -I include -std=c++17 -lws2_32 -lmswsock
```

**Linux/macOS:**
```bash
g++ main.cpp -o main -I include -std=c++17 -pthread
```

### 3. Jalankan
```powershell
.\main.exe
```

Server berjalan di **http://localhost:8080**
