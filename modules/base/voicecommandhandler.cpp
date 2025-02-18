#include <modules/base/voicecommandhandler.h>

#include <openspace/json.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <openspace/scripting/scriptengine.h>
#include <openspace/properties/property.h>
#include <modules/server/servermodule.h>
#include <modules/server/include/topics/voicecommandtopic.h>
#include <modules/server/include/connection.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/filesystem/filesystem.h>
#include <filesystem>

namespace {
    constexpr std::string_view _loggerCat = "VoiceCommandHandler";

    constexpr std::string_view MessageType = "voice_command";
    constexpr std::string_view TopicKey = "topic";
    constexpr std::string_view PayloadKey = "payload";
} // namespace

namespace openspace {

VoiceCommandHandler::VoiceCommandHandler()
    : PropertyOwner({ "VoiceCommandHandler" })
    , _isRecording(
        properties::Property::PropertyInfo {
            "isRecording",
            "Is Recording",
            "Indicates whether voice recording is currently active"
        },
        false
    )
{
    addProperty(_isRecording);
    _isRecording.setReadOnly(true);
}

void VoiceCommandHandler::initialize() {
    ensureTemporaryDirectory();

    // Get the Server module instance
    _serverModule = global::moduleEngine->module<ServerModule>();
    if (!_serverModule) {
        LERROR("Could not find Server module");
        return;
    }

    // Register the voice command topic with the connection factory
    if (auto* server = _serverModule->serverInterfaceByIdentifier("WebSocket")) {
        if (auto* connection = server->connection()) {
            connection->registerTopic<VoiceCommandTopic>("voice");
        }
    }
}

void VoiceCommandHandler::deinitialize() {
    if (isRecording()) {
        stopRecording();
    }
}

void VoiceCommandHandler::handleWebGuiMessage(const std::string& message) {
    try {
        const nlohmann::json j = nlohmann::json::parse(message);
        
        if (j.contains("action")) {
            const std::string action = j["action"];
            
            if (action == "toggle_recording") {
                if (isRecording()) {
                    stopRecording();
                }
                else {
                    startRecording();
                }
            }
            else if (action == "start_recording") {
                startRecording();
            }
            else if (action == "stop_recording") {
                stopRecording();
            }
        }
    }
    catch (const nlohmann::json::exception& e) {
        LERROR(std::format(
            "Error parsing WebGui message: {}", e.what()
        ));
        sendStatusUpdate("error", "", "Invalid message format");
    }
}

void VoiceCommandHandler::startRecording() {
    if (isRecording()) {
        return;
    }

    _isRecording = true;
    sendStatusUpdate("recording");

    // TODO: Implement actual audio recording
    // This will be added in Phase 2 with the Python service integration
}

void VoiceCommandHandler::stopRecording() {
    if (!isRecording()) {
        return;
    }

    _isRecording = false;
    sendStatusUpdate("processing");

    // TODO: Implement actual audio processing
    // This will be added in Phase 2 with the Python service integration
    
    // For now, just send a dummy response
    sendStatusUpdate("ready", "Voice command processing not yet implemented");
}

bool VoiceCommandHandler::isRecording() const {
    return _isRecording;
}

void VoiceCommandHandler::sendStatusUpdate(const std::string& status,
                                         const std::string& transcription,
                                         const std::string& error)
{
    if (!_serverModule) {
        LERROR("Cannot send status update: Server module not available");
        return;
    }

    nlohmann::json payload = {
        { "type", MessageType },
        { "status", status }
    };

    if (!transcription.empty()) {
        payload["transcription"] = transcription;
    }
    if (!error.empty()) {
        payload["error"] = error;
    }

    // Create a message that follows the server module's topic format
    nlohmann::json message = {
        { TopicKey, "voice" },
        { PayloadKey, payload }
    };

    // Send the message through the server module's connection
    if (auto* server = _serverModule->serverInterfaceByIdentifier("WebSocket")) {
        if (auto* connection = server->connection()) {
            connection->sendJson(message);
        }
    }
}

void VoiceCommandHandler::ensureTemporaryDirectory() {
    namespace fs = std::filesystem;
    
    _tempDirectory = fs::path(_tempDirPath);
    if (!fs::exists(_tempDirectory)) {
        std::error_code ec;
        if (!fs::create_directories(_tempDirectory, ec)) {
            LERROR(std::format(
                "Failed to create temporary directory {}: {}", 
                _tempDirPath, ec.message()
            ));
        }
    }
}

std::string VoiceCommandHandler::processAudioData() {
    // TODO: Implement Whisper integration in Phase 2
    return "";
}

void VoiceCommandHandler::generateAndExecuteScript(const std::string& transcription) {
    // TODO: Implement script generation and execution in Phase 3
}

} // namespace openspace 