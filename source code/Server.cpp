#define _WIN32_WINNT 0x0600
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

recursive_mutex dbMutex;
recursive_mutex userMutex;

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
    if (json[start] == '"') {
        size_t end = json.find("\"", start + 1);
        if (end == string::npos) return "";
        return json.substr(start + 1, end - start - 1);
    }
    size_t end = json.find_first_of(",}", start);
    if (end == string::npos) end = json.length();
    string val = json.substr(start, end - start);
    while(!val.empty() && (val.back() == ' ' || val.back() == '\r' || val.back() == '\n')) val.pop_back();
    return val;
}

class Barang {
protected: 
    string id; string nama; string pengirim;
public:
    Barang(string id, string n, string p) : id(id), nama(n), pengirim(p) {}
    virtual ~Barang() {}
    string getId() const { return id; }
    string getNama() const { return nama; }
    string getPengirim() const { return pengirim; }
    virtual double getBerat() const = 0;
    virtual double getVolume() const = 0;
    virtual string toJson() const = 0;
};

class BarangLogistik : public Barang {
private:
    double berat; double volume;
public:
    BarangLogistik(string id, string n, string p, double b, double v) : Barang(id, n, p), berat(b), volume(v) {}
    double getBerat() const override { return berat; }
    double getVolume() const override { return volume; }
    string toJson() const override {
        return "{\"id\":\"" + id + "\",\"nama\":\"" + nama + "\",\"pengirim\":\"" + pengirim + 
               "\",\"berat\":" + to_string(berat) + ",\"volume\":" + to_string(volume) + "}";
    }
};

class UserManager {
private:
    struct UserData { string user, pass, role; };
    vector<UserData> users;
    vector<string> activeUsers;
    const string FILE_USER = "LoginInfo.txt";

    void simpanUserInternal() {
        ofstream file(FILE_USER, ios::trunc);
        for (const auto& usr : users) { file << usr.user << " " << usr.pass << " " << usr.role << "\n"; }
        file.close();
    }

public:
    UserManager() { muatUser(); }
    void muatUser() {
        lock_guard<recursive_mutex> lock(userMutex);
        users.clear(); ifstream file(FILE_USER); string u, p, r;
        if (file.is_open()) {
            while (file >> u >> p >> r) { users.push_back({u, p, r}); }
            file.close();
        }
    }
    string cekLogin(string u, string p) {
        lock_guard<recursive_mutex> lock(userMutex);
        muatUser();
        if (u == "admin" && p == "admin123") return "Admin"; 
        for (const auto& usr : users) { if (usr.user == u && usr.pass == p) return usr.role; }
        return "";
    }
    bool isUserOnline(string u) { 
        lock_guard<recursive_mutex> lock(userMutex);
        return find(activeUsers.begin(), activeUsers.end(), u) != activeUsers.end(); 
    }
    void registerLogin(string u) { 
        lock_guard<recursive_mutex> lock(userMutex);
        activeUsers.push_back(u); 
    }
    void registerLogout(string u) {
        lock_guard<recursive_mutex> lock(userMutex);
        auto it = find(activeUsers.begin(), activeUsers.end(), u);
        if (it != activeUsers.end()) activeUsers.erase(it);
    }
    bool tambahUser(string u, string p, string r) {
        lock_guard<recursive_mutex> lock(userMutex);
        muatUser();
        for (const auto& usr : users) { if (usr.user == u) return false; }
        users.push_back({u, p, r}); simpanUserInternal(); return true;
    }
    bool hapusUser(string u) {
        lock_guard<recursive_mutex> lock(userMutex);
        muatUser();
        if (u == "admin") return false;
        for (auto it = users.begin(); it != users.end(); ++it) {
            if (it->user == u) { users.erase(it); simpanUserInternal(); return true; }
        }
        return false;
    }
    string listUsersJson() {
        lock_guard<recursive_mutex> lock(userMutex);
        muatUser();
        string res = "[{\"username\":\"admin\",\"role\":\"Admin\",\"status\":\"" + string(find(activeUsers.begin(), activeUsers.end(), "admin") != activeUsers.end() ? "Online" : "Offline") + "\"}";
        for (const auto& u : users) {
            bool isOnline = find(activeUsers.begin(), activeUsers.end(), u.user) != activeUsers.end();
            res += ",{\"username\":\"" + u.user + "\",\"role\":\"" + u.role + "\",\"status\":\"" + (isOnline ? "Online" : "Offline") + "\"}";
        }
        res += "]"; return "{\"status\":\"success\",\"data\":" + res + "}";
    }
};

class DatabaseManager {
private:
    vector<Barang*> db;
    double tarifKg = 5000.0, tarifM3 = 20000.0;
    const string FILE_DB = "database.txt";
    const string FILE_TARIF = "tarif_config.txt";

    void simpanDBInternal() {
        ofstream file(FILE_DB, ios::trunc);
        for (const auto& b : db) { file << b->toJson() << "\n"; }
        file.close();
    }

    string toLowerManual(string s) {
        for (char &c : s) {
            if (c >= 'A' && c <= 'Z') c = c + 32;
        }
        return s;
    }

    void bubbleSortInternal(string by, bool asc) {
        int n = db.size();
        for (int i = 0; i < n - 1; i++) {
            for (int j = 0; j < n - i - 1; j++) {
                bool condition = false;
                
                if (by == "nama") {
                    condition = asc ? (db[j]->getNama() > db[j+1]->getNama()) 
                                    : (db[j]->getNama() < db[j+1]->getNama());
                } else if (by == "pengirim") {
                    condition = asc ? (db[j]->getPengirim() > db[j+1]->getPengirim()) 
                                    : (db[j]->getPengirim() < db[j+1]->getPengirim());
                } else if (by == "berat") {
                    condition = asc ? (db[j]->getBerat() > db[j+1]->getBerat()) 
                                    : (db[j]->getBerat() < db[j+1]->getBerat());
                } else if (by == "volume") {
                    condition = asc ? (db[j]->getVolume() > db[j+1]->getVolume()) 
                                    : (db[j]->getVolume() < db[j+1]->getVolume());
                } else {
                    condition = asc ? (stoi(db[j]->getId()) > stoi(db[j+1]->getId())) 
                                    : (stoi(db[j]->getId()) < stoi(db[j+1]->getId()));
                }
                
                if (condition) {
                    Barang* temp = db[j];
                    db[j] = db[j+1];
                    db[j+1] = temp;
                }
            }
        }
    }

public:
    DatabaseManager() { muatDB(); muatTarif(); }
    ~DatabaseManager() { for (auto p : db) delete p; }

    void muatDB() {
        lock_guard<recursive_mutex> lock(dbMutex);
        for (auto p : db) delete p;         // Untuk menghindari memory leak saat me-refresh memori
        db.clear(); 
        ifstream file(FILE_DB); string line;
        if (file.is_open()) {
            while (getline(file, line)) {
                if (line.empty()) continue;
                db.push_back(new BarangLogistik(getJsonValue(line, "id"), getJsonValue(line, "nama"), getJsonValue(line, "pengirim"),
                                                stod(getJsonValue(line, "berat")), stod(getJsonValue(line, "volume"))));
            }
            file.close();
        }
    }
    void muatTarif() {
        lock_guard<recursive_mutex> lock(dbMutex);
        ifstream file(FILE_TARIF); string line;
        if (file.is_open() && getline(file, line) && !line.empty()) {
            tarifKg = stod(getJsonValue(line, "tarif_kg")); tarifM3 = stod(getJsonValue(line, "tarif_m3"));
        }
        file.close();
    }
    void simpanTarif(double tk, double tm) {
        lock_guard<recursive_mutex> lock(dbMutex);
        tarifKg = tk; tarifM3 = tm;
        ofstream file(FILE_TARIF, ios::trunc); file << "{\"tarif_kg\":" << tarifKg << ",\"tarif_m3\":" << tarifM3 << "}\n"; file.close();
    }
    string getTarifJson() { 
        lock_guard<recursive_mutex> lock(dbMutex);
        muatTarif();
        return "{\"status\":\"success\",\"tarif_kg\":" + to_string(tarifKg) + ",\"tarif_m3\":" + to_string(tarifM3) + "}"; 
    }
    string generateIDInternal() {
        int maxId = 0;
        for (const auto& b : db) { if (stoi(b->getId()) > maxId) maxId = stoi(b->getId()); }
        stringstream ss; ss << setfill('0') << setw(3) << (maxId + 1); return ss.str();
    }
    string getNextID() {
        lock_guard<recursive_mutex> lock(dbMutex);
        muatDB();
        return generateIDInternal();
    }
    void tambahBarang(string nama, string pengirim, double berat, double vol) {
        lock_guard<recursive_mutex> lock(dbMutex);
        muatDB();
        db.push_back(new BarangLogistik(generateIDInternal(), nama, pengirim, berat, vol)); simpanDBInternal();
    }
    bool hapusBarang(string id) {
        lock_guard<recursive_mutex> lock(dbMutex);
        muatDB();
        for (auto it = db.begin(); it != db.end(); ++it) {
            if ((*it)->getId() == id) { delete *it; db.erase(it); simpanDBInternal(); return true; }
        }
        return false;
    }
    void sortData(string by, string order) {
        lock_guard<recursive_mutex> lock(dbMutex);
        muatDB();
        bool asc = (order == "asc");
        
        bubbleSortInternal(by, asc);
        
        simpanDBInternal();
    }
    string searchData(string by, string query) {
        lock_guard<recursive_mutex> lock(dbMutex);
        muatDB(); 

        bubbleSortInternal(by, true);
        
        string qLower = toLowerManual(query);
        int low = 0, high = db.size() - 1;
        int foundIdx = -1;
        
        while (low <= high) {
            int mid = low + (high - low) / 2;
            string target = "";
            
            if (by == "id") target = db[mid]->getId();
            else if (by == "nama") target = db[mid]->getNama();
            else if (by == "pengirim") target = db[mid]->getPengirim();
            
            string tLower = toLowerManual(target);
            
            if (tLower == qLower) {
                foundIdx = mid;
                break; 
            } else if (tLower < qLower) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        string res = "[";
        bool first = true;
        
        if (foundIdx != -1) {
            int left = foundIdx;
            while (left >= 0) {
                string target = "";
                if (by == "id") target = db[left]->getId();
                else if (by == "nama") target = db[left]->getNama();
                else if (by == "pengirim") target = db[left]->getPengirim();
                if (toLowerManual(target) != qLower) break;
                left--;
            }
            left++;
        
            int right = foundIdx;
            while (right < db.size()) {
                string target = "";
                if (by == "id") target = db[right]->getId();
                else if (by == "nama") target = db[right]->getNama();
                else if (by == "pengirim") target = db[right]->getPengirim();
                if (toLowerManual(target) != qLower) break;
                right++;
            }
            right--;
            
            for (int i = left; i <= right; i++) {
                if (!first) res += ",";
                res += db[i]->toJson();
                first = false;
            }
        }
        
        res += "]";
        return "{\"status\":\"success\",\"data\":" + res + "}";
    }

    string viewAllJson() {
        lock_guard<recursive_mutex> lock(dbMutex);
        muatDB();
        if (db.empty()) return "{\"status\":\"success\",\"data\":\"[]\"}";
        string res = "[";
        for (size_t i = 0; i < db.size(); i++) { res += db[i]->toJson() + (i < db.size() - 1 ? "," : ""); }
        res += "]"; return "{\"status\":\"success\",\"data\":" + res + "}";
    }
};

UserManager userMgr;
DatabaseManager dbMgr;

string prosesPermintaan(string req, string& role, string& username) {
    string action = getJsonValue(req, "action");

    if (action == "ping") return "{\"status\":\"success\"}";

    if (action == "login") {
        string u = getJsonValue(req, "username"), p = getJsonValue(req, "password");
        if (userMgr.isUserOnline(u)) return "{\"status\":\"fail\",\"message\":\"user ini sedang login di perangkat lain\"}";
        string roleCek = userMgr.cekLogin(u, p);
        if (!roleCek.empty()) { 
            role = roleCek; username = u; userMgr.registerLogin(u); 
            cout << "[LOG LOGIN] User '" << username << "' masuk sebagai [" << role << "].\n";
            return "{\"status\":\"success\",\"role\":\"" + role + "\"}"; 
        }
        return "{\"status\":\"fail\",\"message\":\"Username/Password Salah\"}";
    }
    
    if (action == "view") return dbMgr.viewAllJson();
    if (action == "get_tarif") return dbMgr.getTarifJson();
    if (action == "get_next_id") return "{\"status\":\"success\",\"next_id\":\"" + dbMgr.getNextID() + "\"}";

    if (action == "set_tarif" && (role == "Admin" || role == "Manajer")) {
        dbMgr.simpanTarif(stod(getJsonValue(req, "tarif_kg")), stod(getJsonValue(req, "tarif_m3")));
        return "{\"status\":\"success\",\"message\":\"Tarif Baru Berhasil Disimpan!\"}";
    }
    if (action == "add" && (role == "Admin" || role == "Manajer" || role == "Inbound")) {
        dbMgr.tambahBarang(getJsonValue(req, "nama"), getJsonValue(req, "pengirim"), stod(getJsonValue(req, "berat")), stod(getJsonValue(req, "volume")));
        return "{\"status\":\"success\",\"message\":\"Barang berhasil masuk database!\"}";
    }
    if (action == "delete" && (role == "Admin" || role == "Manajer" || role == "Outbound")) {
        if (dbMgr.hapusBarang(getJsonValue(req, "id"))) return "{\"status\":\"success\",\"message\":\"Barang terhapus!\"}";
        return "{\"status\":\"fail\",\"message\":\"ID tidak ditemukan!\"}";
    }
    if (action == "sort" && (role == "Admin" || role == "Manajer")) {
        dbMgr.sortData(getJsonValue(req, "by"), getJsonValue(req, "order"));
        return "{\"status\":\"success\",\"message\":\"Data berhasil disortir!\"}";
    }
    if (action == "search" && (role == "Admin" || role == "Manajer")) {
        return dbMgr.searchData(getJsonValue(req, "by"), getJsonValue(req, "query"));
    }

    if (action == "add_user" && role == "Admin") {
        if (userMgr.tambahUser(getJsonValue(req, "u"), getJsonValue(req, "p"), getJsonValue(req, "r")))
            return "{\"status\":\"success\",\"message\":\"User berhasil ditambahkan!\"}";
        return "{\"status\":\"fail\",\"message\":\"Username sudah terdaftar!\"}";
    }
    if (action == "del_user" && role == "Admin") {
        if (userMgr.hapusUser(getJsonValue(req, "u"))) return "{\"status\":\"success\",\"message\":\"User berhasil dihapus!\"}";
        return "{\"status\":\"fail\",\"message\":\"Gagal! User tidak ditemukan.\"}";
    }
    if (action == "list_users" && role == "Admin") {
        return userMgr.listUsersJson();
    }

    return "{\"status\":\"fail\",\"message\":\"Akses Ditolak!\"}";
}

void handleClient(SOCKET clientSocket) {
    string role = "Guest", username = "Unknown";
    char buffer[4096] = {0};
    while (true) {
        memset(buffer, 0, 4096);
        int valread = recv(clientSocket, buffer, 4096, 0);
        if (valread <= 0) break; 
        string res = prosesPermintaan(string(buffer), role, username);
        send(clientSocket, res.c_str(), res.length(), 0);
    }
    if (username != "Unknown") {
        cout << "[LOG DISCONNECT] User '" << username << "' terputus.\n";
        userMgr.registerLogout(username); 
    }
    closesocket(clientSocket);
}

int main() {
    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET serverFd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(8888);
    bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)); listen(serverFd, 20);
    cout << "==========================\n";
    cout << "  SERVER SISTEM LOGISTIK \n";
    cout << "==========================\n";
    while (true) {
        int addrlen = sizeof(addr);
        SOCKET newSocket = accept(serverFd, (struct sockaddr*)&addr, &addrlen);
        if (newSocket == INVALID_SOCKET) continue;
        thread t(handleClient, newSocket); t.detach();
    }
    closesocket(serverFd); WSACleanup(); return 0;
}
