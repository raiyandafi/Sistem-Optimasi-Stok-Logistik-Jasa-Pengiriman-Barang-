#define _WIN32_WINNT 0x0600
#include <iostream>
#include <string>
#include <iomanip>
#include <thread>
#include <atomic>
#include <limits>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

// ==========================================
// 1. VARIABEL GLOBAL & KONTROL THREAD
atomic<bool> globalConnected(false);    // Status koneksi ke server (thread-safe)
atomic<bool> programRunning(true);      // Status apakah program masih berjalan
string serverIPGlobal = "";             // Menyimpan IP server yang dituju

double safeStod(const string& s) {
    if (s.empty()) return 0.0;
    try { return stod(s); } catch (...) { return 0.0; }
}

// ==========================================
// 2. FUNGSI VALIDASI INPUT
// Mengamankan input integer dari kesalahan tipe data
int getSafeInt() {
    int val;
    while (!(cin >> val)) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "[ERROR] Input salah! Harap masukkan angka bulat: ";
    }
    return val;
}
// Mengamankan input double/desimal dari kesalahan tipe data
double getSafeDouble() {
    double val;
    while (!(cin >> val)) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "[ERROR] Input salah! Harap masukkan angka desimal/bulat: ";
    }
    return val;
}

// ==========================================
// 3. FUNGSI PARSING JSON MANUAL
// Mengekstrak nilai (value) berdasarkan kunci (key) dari string berformat JSON
string getJsonValue(string json, string key) {
    size_t pos = json.find("\"" + key + "\""); 
    if (pos == string::npos) return "";
    size_t start = json.find(":", pos);
    if (start == string::npos) return "";
    
    start++;
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t')) start++;
    if (start >= json.length()) return "";
    
    if (json[start] == '[') {
        int count = 1; size_t end = start + 1;
        while (end < json.length() && count > 0) {
            if (json[end] == '[') count++;
            else if (json[end] == ']') count--;
            end++;
        }
        return json.substr(start, end - start);
    }
    // Jika value berupa String ""
    if (json[start] == '"') {
        size_t end = json.find("\"", start + 1);
        if (end == string::npos) return "";
        return json.substr(start + 1, end - start - 1);
    }
    // Jika value berupa angka atau boolean
    size_t end = json.find_first_of(",}", start);
    if (end == string::npos) end = json.length();
    string val = json.substr(start, end - start);
    while(!val.empty() && (val.back() == ' ' || val.back() == '\r' || val.back() == '\n')) val.pop_back();
    return val;
}

// ==========================================
// 4. WINSOCK SOCKET (Socket Programming)
class NetworkManager {
private:
    SOCKET sock; char buffer[4096];
    string ip;
    mutex netMutex;
public:
    NetworkManager() : sock(INVALID_SOCKET) { 
        WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~NetworkManager() { if(sock != INVALID_SOCKET) closesocket(sock); WSACleanup(); }

    void setIP(string ipTarget) { ip = ipTarget; }

    bool connectToServer() {
        lock_guard<mutex> lock(netMutex);
        if (sock != INVALID_SOCKET) closesocket(sock);
        sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr; serv_addr.sin_family = AF_INET; serv_addr.sin_port = htons(8888);
        inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);
        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            globalConnected = false;
            return false;
        }
        globalConnected = true;
        return true;
    }

    string sendRequest(string req) {
        lock_guard<mutex> lock(netMutex);
        if (!globalConnected) return "{\"status\":\"fail\",\"message\":\"Disconnected\"}";
        if (send(sock, req.c_str(), req.length(), 0) == SOCKET_ERROR) { globalConnected = false; return "{\"status\":\"fail\"}"; }
        memset(buffer, 0, 4096);
        if (recv(sock, buffer, 4096, 0) <= 0) { globalConnected = false; return "{\"status\":\"fail\"}"; }
        return string(buffer);
    }
};

// ==========================================
// 5. THREAD PENGECEK KONEKSI
//Berjalan di latar belakang untuk auto-reconnect jika koneksi server terputus
void connectionCheckerWorker(NetworkManager* net) {
    bool previouslyConnected = true;
    while (programRunning) {
        this_thread::sleep_for(chrono::seconds(2));
        if (!programRunning) break;

        if (!globalConnected) {
            if (net->connectToServer()) {
                if (!previouslyConnected) {
                    cout << "\n\n==================================================";
                    cout << "\n[INFO] Berhasil terhubung kembali ke server!";
                    cout << "\n==================================================\n> ";
                    previouslyConnected = true;
                }
            } else { previouslyConnected = false; }
        } else {
            string res = net->sendRequest("{\"action\":\"ping\"}");
            if (getJsonValue(res, "status") != "success") {
                globalConnected = false;
                previouslyConnected = false;
                cout << "\n\n==================================================";
                cout << "\n[NOTICE] KONEKSI DENGAN SERVER TERPUTUS!";
                cout << "\n==================================================\n> ";
            }
        }
    }
}

// ==========================================
// 6. BASE CLASS MENU UTAMA
class AppMenu {
protected:
    NetworkManager* net; string role;
public:
    AppMenu(NetworkManager* n, string r) : net(n), role(r) {}
    virtual ~AppMenu() {}
    virtual void tampilkanMenu() = 0; // Pure virtual function

    bool checkConnectionState() {
        if (!globalConnected) {
            cout << "\n[PERINGATAN] Server Terputus. Apakah Anda ingin keluar program? (1. Ya / 0. Tidak): ";
            int conf = getSafeInt();
            if (conf == 1) { programRunning = false; exit(0); }
            return false;
        }
        return true;
    }

    // Menampilkan data logistik dari server ke dalam bentuk tabel
    void tampilkanTabel(string res) {
        string tkRes = net->sendRequest("{\"action\":\"get_tarif\"}");
        double tKg = safeStod(getJsonValue(tkRes, "tarif_kg")), tM3 = safeStod(getJsonValue(tkRes, "tarif_m3"));
        cout << "\n=========================================== DATA LOGISTIK ===========================================\n";
        cout << setw(5) << "ID" << " | " << setw(15) << left << "Nama Barang" << " | " << setw(15) << left << "Pengirim" << " | " << "Berat" << " | " << "Vol" << " | " << "Total Tarif\n";
        cout << "-----------------------------------------------------------------------------------------------------\n";
        string dataArr = getJsonValue(res, "data"); size_t pos = 0; int hitung = 0;
        while ((pos = dataArr.find("{\"id\":", pos)) != string::npos) {
            size_t endPos = dataArr.find("}", pos); string item = dataArr.substr(pos, endPos - pos + 1);
            double b = safeStod(getJsonValue(item, "berat")), v = safeStod(getJsonValue(item, "volume"));
            cout << setw(5) << right << getJsonValue(item, "id") << " | " << setw(15) << left << getJsonValue(item, "nama") << " | " << setw(15) << left << getJsonValue(item, "pengirim") << " | " << setw(5) << b << " | " << setw(3) << v << " | Rp " << fixed << setprecision(0) << (b * tKg) + (v * tM3) << "\n";
            pos = endPos; hitung++;
        }
        if (hitung == 0) cout << "                    [ Data Kosong / Tidak Ditemukan ]\n";
        cout << "====================================================================================================\n";
    }

    // Fungsi Lihat data
    void lihatData() { if(!checkConnectionState()) return; string res = net->sendRequest("{\"action\":\"view\"}"); tampilkanTabel(res); }

    // Fungsi Manajemen Tarif
    void manajemenTarifPengiriman() {
        if(!checkConnectionState()) return;
        int tPil;
        do {
            cout << "\n=== MANAJEMEN TARIF PENGIRIMAN ===\n1. Tampilkan Tarif Saat Ini\n2. Atur Tarif Baru\n0. Kembali ke Menu\nPilih: ";
            tPil = getSafeInt();
            if (tPil == 1) {
                string tkRes = net->sendRequest("{\"action\":\"get_tarif\"}");
                cout << "\n=== TARIF LOGISTIK SAAT INI ===\n";
                cout << "Tarif per Kg : Rp " << fixed << setprecision(0) << safeStod(getJsonValue(tkRes, "tarif_kg")) << "\n";
                cout << "Tarif per m3 : Rp " << fixed << setprecision(0) << safeStod(getJsonValue(tkRes, "tarif_m3")) << "\n";
                cout << "===============================\n";
            } else if (tPil == 2) {
                double tk, tm; 
                cout << "\n-- ATUR TARIF BARU --\nTarif/Kg Baru: "; tk = getSafeDouble();
                cout << "Tarif/m3 Baru: "; tm = getSafeDouble();
                string req = "{\"action\":\"set_tarif\",\"tarif_kg\":" + to_string(tk) + ",\"tarif_m3\":" + to_string(tm) + "}";
                cout << "-> " << getJsonValue(net->sendRequest(req), "message") << "\n";
            }
        } while (tPil != 0);
    }

    // Fungsi Tambah Barang
    void tambahBarang() {
        if(!checkConnectionState()) return;
        string nama, pengirim; double berat, vol;
        string idRes = net->sendRequest("{\"action\":\"get_next_id\"}");
        string nextId = getJsonValue(idRes, "next_id");

        cout << "\n-- INPUT BARANG BARU --\n";
        cout << "ID (Otomatis): " << nextId << "\nNama: "; 
        cin.ignore(numeric_limits<streamsize>::max(), '\n'); 
        getline(cin, nama);
        cout << "Pengirim: "; getline(cin, pengirim); 
        cout << "Berat(Kg): "; berat = getSafeDouble(); 
        cout << "Volume(m3): "; vol = getSafeDouble();

        cout << "\n=== KONFIRMASI INPUT BARANG ===\n";
        cout << "ID       : " << nextId << "\nNama     : " << nama << "\nPengirim : " << pengirim << "\nBerat    : " << berat << " Kg\nVolume   : " << vol << " m3\n";
        cout << "--------------------------------\n1. Simpan Data / Ya\n0. Kembali ke Menu (Batalkan)\nPilih: ";
        int conf = getSafeInt();
        if (conf == 1) {
            string req = "{\"action\":\"add\",\"nama\":\"" + nama + "\",\"pengirim\":\"" + pengirim + "\",\"berat\":" + to_string(berat) + ",\"volume\":" + to_string(vol) + "}";
            cout << "-> " << getJsonValue(net->sendRequest(req), "message") << "\n";
        } else { cout << "-> Input barang dibatalkan.\n"; }
    }

    // Fungsi Hapus Barang
    void hapusBarang() {
        if(!checkConnectionState()) return;
        cout << "\n-- OUTPUT / HAPUS BARANG --\n1. Berdasarkan ID\n2. Berdasarkan Nama\n3. Berdasarkan Pengirim\n0. Kembali ke Menu\nPilih Kriteria Hapus: ";
        int p = getSafeInt(); 
        string by = "";
        if (p == 1) by = "id"; else if (p == 2) by = "nama"; else if (p == 3) by = "pengirim"; else return;

        cout << "Masukkan Kata Kunci: "; 
        cin.ignore(numeric_limits<streamsize>::max(), '\n'); 
        string query; getline(cin, query);
        string res = net->sendRequest("{\"action\":\"search\",\"by\":\"" + by + "\",\"query\":\"" + query + "\"}");
        tampilkanTabel(res);

        string dataArr = getJsonValue(res, "data");
        if (dataArr.find("{\"id\":") == string::npos || dataArr == "[]") { cout << "Barang tidak ditemukan.\n"; return; }

        cout << "\nApakah Anda yakin ingin menghapus data barang di atas?\n1. Ya, Hapus\n0. Kembali ke Menu (Batalkan)\nPilih: ";
        int conf = getSafeInt();
        if (conf == 1) {
            size_t pos = 0;
            while ((pos = dataArr.find("{\"id\":", pos)) != string::npos) {
                size_t endPos = dataArr.find("}", pos); string item = dataArr.substr(pos, endPos - pos + 1);
                string idDel = getJsonValue(item, "id");
                net->sendRequest("{\"action\":\"delete\",\"id\":\"" + idDel + "\"}");
                pos = endPos;
            }
            cout << "-> Proses penghapusan selesai.\n";
        } else { cout << "-> Penghapusan dibatalkan.\n"; }
    }

    // Fungsi Cari Data
    void cariData() {
        if(!checkConnectionState()) return;
        cout << "\n-- MENU PENCARIAN DATA --\n1. Berdasarkan ID Barang\n2. Berdasarkan Nama Barang\n3. Berdasarkan Nama Pengirim\n0. Kembali ke Menu\nPilih: ";
        int p = getSafeInt(); 
        string by = "id", query;
        if (p == 2) by = "nama"; else if (p == 3) by = "pengirim"; else if (p == 0) return;
        cout << "Masukkan Kata Kunci: "; 
        cin.ignore(numeric_limits<streamsize>::max(), '\n'); 
        getline(cin, query);
        tampilkanTabel(net->sendRequest("{\"action\":\"search\",\"by\":\"" + by + "\",\"query\":\"" + query + "\"}"));
    }

    // Fungsi Sortir Data
    void sortirData() {
        if(!checkConnectionState()) return;
        cout << "\n-- MENU SORTIR DATA --\n1. Berdasarkan ID Barang\n2. Berdasarkan Nama Barang\n3. Berdasarkan Nama Pengirim\n4. Berdasarkan Berat (Kg)\n5. Berdasarkan Volume (m3)\n0. Kembali ke Menu\nPilih: ";
        int p = getSafeInt(); 
        string by = "id", order = "asc";
        if (p == 2) by = "nama"; else if (p == 3) by = "pengirim"; else if (p == 4) by = "berat"; else if (p == 5) by = "volume"; else if (p == 0) return;

        cout << "Pilih Jenis Urutan:\n1. Ascending (A-Z / Kecil ke Besar)\n2. Descending (Z-A / Besar ke Kecil)\n0. Kembali ke Menu\nPilih: ";
        int o = getSafeInt();
        if (o == 2) order = "desc"; else if (o == 0) return;

        cout << "-> " << getJsonValue(net->sendRequest("{\"action\":\"sort\",\"by\":\"" + by + "\",\"order\":\"" + order + "\"}"), "message") << "\n";
        lihatData();
    }
};

// ==========================================
// 7. DERIVED CLASS (POLIMORFISME ROLE USER)
// Menu untuk Admin
class AdminMenu : public AppMenu {
public:
    AdminMenu(NetworkManager* n, string r) : AppMenu(n, r) {}
    void tampilkanMenu() override {
        int pil;
        do {
            if(!globalConnected) { if(!checkConnectionState()) break; }
            cout << "\n=== MENU ADMIN ===\n1. Lihat Data\n2. Input Barang\n3. Output Barang\n4. Cari Data (Search)\n5. Sortir Data (Sort)\n6. Manajemen Tarif Pengiriman\n7. Manajemen User\n8. Logout\n9. Exit Program\nPilih: ";
            pil = getSafeInt();
            if (pil == 1) lihatData();
            else if (pil == 2) tambahBarang();
            else if (pil == 3) hapusBarang();
            else if (pil == 4) cariData();
            else if (pil == 5) sortirData();
            else if (pil == 6) manajemenTarifPengiriman();
            else if (pil == 7) {
                int uPil;
                do {
                    if(!globalConnected) break;
                    cout << "\n-- MANAJEMEN USER --\n1. Tambah User\n2. Hapus User\n3. Tampilkan User Saat Ini\n0. Kembali ke Menu\nPilih: ";
                    uPil = getSafeInt();
                    if (uPil == 1) {
                        string u, p, r; cout << "User: "; cin >> u; cout << "Pass: "; cin >> p; cout << "Role (Manajer/Inbound/Outbound): "; cin >> r;
                        cout << "-> " << getJsonValue(net->sendRequest("{\"action\":\"add_user\",\"u\":\"" + u + "\",\"p\":\"" + p + "\",\"r\":\"" + r + "\"}"), "message") << "\n";
                    } else if (uPil == 2) {
                        string u; cout << "User Dihapus: "; cin >> u;
                        cout << "-> " << getJsonValue(net->sendRequest("{\"action\":\"del_user\",\"u\":\"" + u + "\"}"), "message") << "\n";
                    } else if (uPil == 3) {
                        string uRes = net->sendRequest("{\"action\":\"list_users\"}");
                        string uData = getJsonValue(uRes, "data"); size_t uPos = 0;
                        cout << "\n=== DAFTAR USER SISTEM ===\n" << setw(15) << left << "Username" << " | " << setw(15) << left << "Role" << " | Status\n---------------------------------------------\n";
                        while ((uPos = uData.find("{\"username\":", uPos)) != string::npos) {
                            size_t uEnd = uData.find("}", uPos); string uItem = uData.substr(uPos, uEnd - uPos + 1);
                            cout << setw(15) << left << getJsonValue(uItem, "username") << " | " << setw(15) << left << getJsonValue(uItem, "role") << " | " << getJsonValue(uItem, "status") << "\n";
                            uPos = uEnd;
                        }
                        cout << "=============================================\n";
                    }
                } while (uPil != 0);
            } else if (pil == 9) { programRunning = false; exit(0); }
        } while (pil != 8);
    }
};

// Menu untuk Manajer
class ManagerMenu : public AppMenu {
public:
    ManagerMenu(NetworkManager* n, string r) : AppMenu(n, r) {}
    void tampilkanMenu() override {
        int pil;
        do {
            if(!globalConnected) { if(!checkConnectionState()) break; }
            cout << "\n=== MENU MANAJER ===\n1. Lihat Data\n2. Input Barang\n3. Output Barang\n4. Cari Data (Search)\n5. Sortir Data (Sort)\n6. Manajemen Tarif Pengiriman\n7. Logout\n8. Exit Program\nPilih: ";
            pil = getSafeInt();
            if (pil == 1) lihatData();
            else if (pil == 2) tambahBarang();
            else if (pil == 3) hapusBarang();
            else if (pil == 4) cariData();
            else if (pil == 5) sortirData();
            else if (pil == 6) manajemenTarifPengiriman();
            else if (pil == 8) { programRunning = false; exit(0); }
        } while (pil != 7);
    }
};

// Menu untuk Staff
class StaffMenu : public AppMenu {
public:
    StaffMenu(NetworkManager* n, string r) : AppMenu(n, r) {}
    void tampilkanMenu() override {
        int pil;
        do {
            if(!globalConnected) { if(!checkConnectionState()) break; }
            cout << "\n=== MENU STAFF (" << role << ") ===\n1. Lihat Data\n";
            if (role == "Inbound") cout << "2. Input Barang\n";
            if (role == "Outbound") cout << "2. Output Barang\n";
            cout << "3. Logout\n4. Exit Program\nPilih: "; 
            pil = getSafeInt();
            if (pil == 1) lihatData();
            else if (pil == 2 && role == "Inbound") tambahBarang();
            else if (pil == 2 && role == "Outbound") hapusBarang();
            else if (pil == 4) { programRunning = false; exit(0); }
        } while (pil != 3);
    }
};

// ==========================================
// 8. FUNGSI UTAMA
int main() {
    NetworkManager net;
    bool initialConnection = false;
    
    //1. Meminta IP Server dan mencoba terhubung pertama kali
    while (!initialConnection && programRunning) {
        cout << "Masukkan IP Server (ketik 'exit' untuk batal): "; 
        cin >> serverIPGlobal;
        
        if (serverIPGlobal == "exit" || serverIPGlobal == "0") {
            programRunning = false;
            return 0;
        }

        net.setIP(serverIPGlobal);
        if (net.connectToServer()) {
            initialConnection = true;
        } else {
            cout << "[ERROR] Gagal terhubung ke server! Pastikan IP benar dan server sedang menyala.\n\n";
        }
    }

    //2. Menjalankan Background Thread Pengecek Koneksi secara independen
    thread bgChecker(connectionCheckerWorker, &net);
    bgChecker.detach();

    //3. Loop Menu Utama
    while (programRunning) {
        cout << "\n--- MAIN MENU LOGISTIK --- \n1. Login Sistem\n2. Exit Program\nPilih: ";
        int mPil = getSafeInt();
        if (mPil == 2) { programRunning = false; break; }
        if (mPil != 1) continue;

        if (!globalConnected) { 
            cout << "[NOTICE] Saat ini status server terdeteksi offline. Menunggu proses auto-reconnect...\n"; 
            continue; 
        }

        string user, pass;
        cout << "\n--- LOGIN --- \nUsername: "; cin >> user; cout << "Password: "; cin >> pass;
        string res = net.sendRequest("{\"action\":\"login\",\"username\":\"" + user + "\",\"password\":\"" + pass + "\"}");

        //Jika Autentikasi berhasil, maka akan dibuat objek menu yang sesuai dengan role user (Polimorfisme)
        if (getJsonValue(res, "status") == "success") {
            string role = getJsonValue(res, "role");
            cout << "\nBerhasil login sebagai: " << role << "\n";
            AppMenu* menu; 
            if (role == "Admin") menu = new AdminMenu(&net, role);
            else if (role == "Manajer") menu = new ManagerMenu(&net, role);
            else menu = new StaffMenu(&net, role);
            menu->tampilkanMenu();        // Menampilkan menu spesifik role
            delete menu;                  // Membersihkan memory setelah logout
        } else if (globalConnected) {
            cout << "-> [INFO] " << getJsonValue(res, "message") << "\n";
        }
    }
    programRunning = false; return 0;
}
