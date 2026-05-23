# Sistem Optimasi Stok Logistik (Client-Server)

Aplikasi ini adalah sistem manajemen logistik berbasis **Client-Server** yang dibangun menggunakan bahasa C++. Sistem ini menerapkan pemrograman jaringan (WinSock2), Multi-threading, dan sinkronisasi file (File I/O) untuk database.

##  Persyaratan Sistem
* Sistem Operasi: Windows (membutuhkan library `winsock2.h`).
* Compiler: G++ / MinGW.

---

##  Instruksi Kompilasi & Menjalankan Program

Program ini terdiri dari dua bagian (Server dan Client). Keduanya harus dikompilasi dan dijalankan di dua terminal (Command Prompt / PowerShell) yang **berbeda**.

### 1. Menjalankan Server (Terminal 1)
Buka terminal di folder source code, lalu jalankan perintah kompilasi berikut:
`g++ server.cpp -o server.exe -lws2_32`

Setelah kompilasi selesai, jalankan server dengan perintah:
`.\server.exe`

> **PENTING:** Biarkan terminal ini tetap terbuka agar Server terus berjalan dan bisa menerima koneksi dari Client.

### 2. Menjalankan Client (Terminal 2)
Buka terminal baru di folder yang sama, lalu kompilasi kode client dengan perintah:
`g++ client.cpp -o client.exe -lws2_32`

Setelah kompilasi selesai, jalankan aplikasi client dengan perintah:
`.\client.exe`

---

## Panduan Pengujian Aplikasi

Saat aplikasi client pertama kali dijalankan, program akan meminta IP Server. Ikuti panduan berikut sesuai dengan skenario:

### A. Skenario Pengujian (Input IP Server)
* **Jika diuji di 1 Laptop yang sama:** 
  Ketik **`127.0.0.1`** (Localhost) lalu tekan Enter.
* **Jika diuji di 2 Laptop berbeda (dalam 1 WiFi/LAN):** 
  Ketik **IP Address dari laptop Server** (contoh: `192.168.1.10`) lalu tekan Enter. Pastikan Firewall di laptop Server mengizinkan koneksi masuk ke port `8888`.

### B. Login & Akun Default
1. Di layar utama, ketik angka **1** untuk masuk ke menu Login.
2. Gunakan akun Admin bawaan berikut untuk menguji seluruh fitur (Input, Output, Sortir, Tarif, Manajemen User):
   * **Username :** `admin`
   * **Password :** `admin123`
