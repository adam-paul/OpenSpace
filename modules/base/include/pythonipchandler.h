#ifndef __OPENSPACE_MODULE_BASE___PYTHONIPCHANDLER___H__
#define __OPENSPACE_MODULE_BASE___PYTHONIPCHANDLER___H__

#include <openspace/properties/propertyowner.h>
#include <ghoul/misc/boolean.h>
#include <atomic>
#include <string>
#include <thread>
#include <functional>

namespace openspace {

/**
 * Handles Inter-Process Communication with the Python voice command service.
 * Uses Unix Domain Sockets for bidirectional communication.
 */
class PythonIPCHandler : public PropertyOwner {
public:
    struct IPCMessage {
        enum class Type {
            AudioData,
            Transcription,
            LuaScript,
            Error,
            Status
        };
        
        Type type;
        std::string data;
        std::string metadata;
    };

    PythonIPCHandler();
    ~PythonIPCHandler();

    bool connect(const std::string& socketPath = "/tmp/openspace_voice.sock");
    void disconnect();
    bool isConnected() const;

    // Message handling
    bool sendMessage(const IPCMessage& message);
    void registerCallback(std::function<void(IPCMessage)> callback);

    // Audio specific methods
    bool sendAudioData(const std::vector<uint8_t>& audioData);
    bool requestTranscription();

private:
    int _socketFd;
    std::thread _receiveThread;
    std::atomic<bool> _running;
    std::function<void(IPCMessage)> _messageCallback;
    std::string _socketPath;

    void receiveLoop();
    bool handleError(const std::string& operation);
    bool initializeSocket();
    void cleanupSocket();
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_BASE___PYTHONIPCHANDLER___H__ 