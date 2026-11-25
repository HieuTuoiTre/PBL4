#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

#include "../Shared/Network.h"
#include "../Shared/Crypto.h"
#include "../Shared/Protocol.h"
#include "../Manager/Core/RfbClientSession.h"

// Stubs for RemoteWindow
namespace RemoteWindow {
    static int g_rawFrameCount = 0;
    void UpdateFrame(const Protocol::VideoDeltaHeader& delta, const char* jpegData, int jpegSize) {}
    void UpdateRawFrame(int x, int y, int w, int h, const uint32_t* rawPixels) {
        g_rawFrameCount++;
        std::cout << "  [RemoteWindow Stub] Nhận Raw Frame #" << g_rawFrameCount 
                  << " rect=(" << x << "," << y << ") size=" << w << "x" << h << "\n";
    }
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "  LANREMOTE - MANAGER LIVE TIGHTVNC SERVER VERIFICATION\n";
    std::cout << "============================================================\n";

    Network::Initialize();

    std::cout << "[1/4] Thiet lap che do tuong thich SetCompatibilityMode(true)...\n";
    RfbClient::SetCompatibilityMode(true);
    assert(RfbClient::IsCompatibilityMode() == true);

    std::cout << "[2/4] Ket noi Manager toi TightVNC Server (127.0.0.1:5900)...\n";
    std::string err;
    bool ok = RfbClient::Connect("127.0.0.1", 5900, err);
    if (!ok) {
        std::cerr << "[FAIL] Khong the ket noi: " << err << "\n";
        return 1;
    }
    std::cout << "  -> Ket noi & Bat tay thanh cong! Desktop: " 
              << RfbClient::GetServerWidth() << "x" << RfbClient::GetServerHeight() 
              << " ('" << RfbClient::GetServerDesktopName() << "')\n";

    std::cout << "[3/4] Khoi chay WorkerLoop nhan frame...\n";
    RfbClient::Start();

    // Cho 1.5 giay de xem frame co nhan duoc khong va khong bi do
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    std::cout << "  -> Trang thai ket noi sau 1.5s: " << (RfbClient::IsConnected() ? "STILL CONNECTED (ALIVE!)" : "DISCONNECTED") << "\n";
    assert(RfbClient::IsConnected() == true);
    assert(RemoteWindow::g_rawFrameCount >= 1);

    std::cout << "[4/4] Ngat ket noi an toan...\n";
    RfbClient::Disconnect();
    assert(RfbClient::IsConnected() == false);

    Network::Cleanup();

    std::cout << "\n============================================================\n";
    std::cout << " [SUCCESS] TIGHTVNC COMPATIBILITY MODE 100% VERIFIED!\n";
    std::cout << "============================================================\n";
    return 0;
}
