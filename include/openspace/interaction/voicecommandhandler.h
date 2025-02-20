/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2025                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#ifndef __OPENSPACE_CORE___VOICECOMMANDHANDLER___H__
#define __OPENSPACE_CORE___VOICECOMMANDHANDLER___H__

#include <openspace/properties/propertyowner.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/scripting/lualibrary.h>
#include <ghoul/misc/boolean.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// Forward declare ma_device to avoid including miniaudio.h in header
struct ma_device;

namespace openspace {

class ModuleEngine;
class ServerModule;

namespace codegen::lua {
    // Forward declarations of Lua functions
    extern scripting::LuaLibrary::Function StartRecording;
    extern scripting::LuaLibrary::Function StopRecording;
    extern scripting::LuaLibrary::Function State;
    extern scripting::LuaLibrary::Function Transcription;
    extern scripting::LuaLibrary::Function Error;
} // namespace codegen::lua

namespace interaction {

class VoiceCommandHandler : public properties::PropertyOwner {
public:
    BooleanType(IsRecording);

    enum class VoiceState {
        Idle = 0,
        Recording,
        Processing,
        GeneratingScript,  // New state for LLM script generation
        Success,          // New state for successful script generation
        Error
    };

    using CallbackHandle = int;
    using StateChangeCallback = std::function<void()>;

    VoiceCommandHandler();
    ~VoiceCommandHandler() override;

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
    bool startRecording();

    /**
     * Stops recording audio and processes the recorded data
     */
    bool stopRecording();

    /**
     * Confirms the current transcription and generates/executes a script
     * @return true if successful, false if no transcription available or in wrong state
     */
    bool confirmTranscription();

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

    /**
     * Returns the current state of the voice command system.
     */
    VoiceState state() const;

    /**
     * Returns the last transcription result, if any.
     */
    std::string transcription() const;

    /**
     * Returns the last error message, if any.
     */
    std::string error() const;

    /**
     * Adds a callback that will be called whenever the state changes.
     * \return A handle that can be used to remove the callback
     */
    CallbackHandle addStateChangeCallback(StateChangeCallback callback);

    /**
     * Removes a previously added callback.
     * \param handle The handle that was returned when adding the callback
     */
    void removeStateChangeCallback(CallbackHandle handle);

    /**
     * Creates the Lua library that will be used to register functions for voice commands.
     */
    static openspace::scripting::LuaLibrary luaLibrary();

    /**
     * Cleans up the temporary audio file if it exists
     */
    void cleanupAudioFile();

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

    /**
     * Updates the current state and notifies callbacks
     */
    void setState(VoiceState state);

    /**
     * Updates the current transcription
     */
    void setTranscription(const std::string& transcription);

    /**
     * Updates the current error message
     */
    void setError(const std::string& error);

    properties::BoolProperty _isRecording;
    std::filesystem::path _tempDirectory;
    static constexpr const char* _tempDirPath = "/tmp/openspace_voice/";

    ServerModule* _serverModule = nullptr;

    // Audio capture members
    ma_device* _audioDevice = nullptr;
    std::vector<float> _capturedAudio;
    static constexpr uint32_t _sampleRate = 16000; // Whisper expects 16kHz
    static constexpr uint32_t _channels = 1;  // Mono audio for speech
    std::string _lastAudioPath;  // Path to the last saved audio file
    bool _needsRetry = false;  // Whether transcription needs to be retried

    // Audio callback function - implementation in cpp file
    static void audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);

    VoiceState _state = VoiceState::Idle;
    std::string _transcription;
    std::string _error;

    int _nextCallbackHandle = 0;
    std::map<CallbackHandle, StateChangeCallback> _stateChangeCallbacks;

    bool saveAudioToTemp();  // Saves captured audio to temporary file
};

} // namespace interaction
} // namespace openspace

#endif // __OPENSPACE_CORE___VOICECOMMANDHANDLER___H__ 