#include <modules/base/include/pythonipchandler.h>

#include <ghoul/logging/logmanager.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/fmt.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

namespace {
    constexpr const char* _loggerCat = "PythonIPCHandler";
    constexpr size_t MaxMessageSize = 1024 * 1024; // 1MB buffer for messages
} // namespace

namespace openspace {

PythonIPCHandler::PythonIPCHandler()
    : PropertyOwner({ "PythonIPCHandler" })
    , _socketFd(-1)
    , _running(false)
{
    LDEBUG("Creating PythonIPCHandler");
}

PythonIPCHandler::~PythonIPCHandler() {
    disconnect();
}

bool PythonIPCHandler::connect(const std::string& socketPath) {
    if (isConnected()) {
        LWARNING("Already connected to Python service");
        return false;
    }

    _socketPath = socketPath;
    if (!initializeSocket()) {
        return false;
    }

    _running = true;
    _receiveThread = std::thread(&PythonIPCHandler::receiveLoop, this);
    
    LINFO(fmt::format("Connected to Python service at {}", socketPath));
    return true;
}

void PythonIPCHandler::disconnect() {
    if (!isConnected()) {
        return;
    }

    _running = false;
    if (_receiveThread.joinable()) {
        _receiveThread.join();
    }

    cleanupSocket();
    LINFO("Disconnected from Python service");
}

bool PythonIPCHandler::isConnected() const {
    return _socketFd != -1;
}

bool PythonIPCHandler::sendMessage(const IPCMessage& message) {
    if (!isConnected()) {
        LERROR("Not connected to Python service");
        return false;
    }

    // Serialize message to JSON format
    std::string jsonMsg = fmt::format(
        "{{\"type\":\"{}\",\"data\":\"{}\",\"metadata\":\"{}\"}}",
        static_cast<int>(message.type),
        message.data,
        message.metadata
    );

    ssize_t bytesSent = send(_socketFd, jsonMsg.c_str(), jsonMsg.length(), 0);
    if (bytesSent == -1) {
        return handleError("Failed to send message");
    }

    return true;
}

bool PythonIPCHandler::sendAudioData(const std::vector<uint8_t>& audioData) {
    IPCMessage message;
    message.type = IPCMessage::Type::AudioData;
    message.data = std::string(audioData.begin(), audioData.end());
    return sendMessage(message);
}

bool PythonIPCHandler::requestTranscription() {
    IPCMessage message;
    message.type = IPCMessage::Type::Transcription;
    message.data = "request";
    return sendMessage(message);
}

void PythonIPCHandler::registerCallback(std::function<void(IPCMessage)> callback) {
    _messageCallback = std::move(callback);
}

void PythonIPCHandler::receiveLoop() {
    std::vector<char> buffer(MaxMessageSize);

    while (_running) {
        ssize_t bytesReceived = recv(_socketFd, buffer.data(), buffer.size(), 0);
        
        if (bytesReceived <= 0) {
            if (_running) {
                handleError("Connection lost");
                disconnect();
            }
            break;
        }

        // Parse received JSON message
        std::string jsonMsg(buffer.data(), bytesReceived);
        try {
            // Basic message parsing (should use proper JSON parser in production)
            IPCMessage message;
            // ... parse message from jsonMsg ...

            if (_messageCallback) {
                _messageCallback(message);
            }
        }
        catch (const std::exception& e) {
            LERROR(fmt::format("Failed to parse message: {}", e.what()));
        }
    }
}

bool PythonIPCHandler::initializeSocket() {
    _socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_socketFd == -1) {
        return handleError("Failed to create socket");
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, _socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(_socketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        return handleError("Failed to connect to Python service");
    }

    return true;
}

void PythonIPCHandler::cleanupSocket() {
    if (_socketFd != -1) {
        close(_socketFd);
        _socketFd = -1;
    }
}

bool PythonIPCHandler::handleError(const std::string& operation) {
    LERROR(fmt::format("{}: {} (errno: {})", operation, strerror(errno), errno));
    return false;
}

} // namespace openspace 