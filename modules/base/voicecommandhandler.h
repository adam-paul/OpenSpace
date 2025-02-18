#ifndef __OPENSPACE_MODULE_BASE___VOICECOMMANDHANDLER___H__
#define __OPENSPACE_MODULE_BASE___VOICECOMMANDHANDLER___H__

#include <openspace/properties/propertyowner.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <ghoul/misc/boolean.h>
#include <filesystem>
#include <memory>
#include <string>

namespace openspace {

class ModuleEngine;
class ServerModule;

class VoiceCommandHandler : public properties::PropertyOwner {
public:
    BooleanType(IsRecording);

    VoiceCommandHandler();
    ~VoiceCommandHandler() override = default;

    void initialize();
    void deinitialize();

    /**
     * Handles WebSocket messages from the WebGUI frontend
     * @param message The JSON message received from the WebGUI
     */
    void handleWebGuiMessage(const std::string& message);

    /**
     * Starts recording audio from the microphone
     */
    void startRecording();

    /**
     * Stops recording audio and processes the recorded data
     */
    void stopRecording();

    /**
     * Checks if voice recording is currently active
     */
    bool isRecording() const;

    /**
     * Sends a status update to the WebGUI
     * @param status The current status to send
     * @param transcription Optional transcribed text
     * @param error Optional error message
     */
    void sendStatusUpdate(const std::string& status, 
        const std::string& transcription = "", 
        const std::string& error = "");

private:
    /**
     * Ensures the temporary directory for voice command scripts exists
     */
    void ensureTemporaryDirectory();

    /**
     * Processes the recorded audio data through Whisper
     * @return The transcribed text
     */
    std::string processAudioData();

    /**
     * Generates and executes a Lua script based on the transcribed text
     * @param transcription The text to convert to a Lua script
     */
    void generateAndExecuteScript(const std::string& transcription);

    properties::BoolProperty _isRecording;
    std::filesystem::path _tempDirectory;
    static constexpr const char* _tempDirPath = "/tmp/openspace_voice/";

    ServerModule* _serverModule = nullptr;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_BASE___VOICECOMMANDHANDLER___H__ 