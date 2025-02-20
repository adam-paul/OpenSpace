#include <openspace/interaction/voicecommandhandler.h>

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
#include <ghoul/misc/profiling.h>

// Include miniaudio with implementation
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include <modules/audio/ext/soloud/src/backend/miniaudio/miniaudio.h>

// Include Lua function implementations
#include <openspace/interaction/voicecommandhandler_lua.inl>

namespace {
    constexpr std::string_view _loggerCat = "VoiceCommandHandler";

    constexpr std::string_view MessageType = "voice_command";
    constexpr std::string_view TopicKey = "topic";
    constexpr std::string_view PayloadKey = "payload";
} // namespace

namespace openspace::interaction {

VoiceCommandHandler::VoiceCommandHandler()
    : PropertyOwner({ "VoiceCommand", "Voice Command" })
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
    LDEBUG("Creating Voice Command Handler");
}

VoiceCommandHandler::~VoiceCommandHandler() {
    if (_audioDevice) {
        ma_device_uninit(_audioDevice);
        delete _audioDevice;
        _audioDevice = nullptr;
    }
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

void VoiceCommandHandler::audioDataCallback(ma_device* pDevice, void* pOutput, 
                                          const void* pInput, uint32_t frameCount)
{
    VoiceCommandHandler* handler = static_cast<VoiceCommandHandler*>(pDevice->pUserData);
    if (!handler) {
        LERROR("Audio callback received null handler");
        return;
    }

    // pInput contains frameCount frames of audio data
    const float* inputData = static_cast<const float*>(pInput);
    const size_t samplesToCapture = frameCount * handler->_channels;
    
    // Append the captured audio data to our buffer
    handler->_capturedAudio.insert(
        handler->_capturedAudio.end(),
        inputData,
        inputData + samplesToCapture
    );

    // Log every ~1 second of audio
    if (handler->_capturedAudio.size() % (handler->_sampleRate * handler->_channels) < samplesToCapture) {
        LDEBUG(std::format(
            "Captured {} samples of audio so far",
            handler->_capturedAudio.size()
        ));
    }

    // Unused output parameter
    (void)pOutput;
}

bool VoiceCommandHandler::startRecording() {
    if (_state == VoiceState::Recording) {
        LWARNING("Attempted to start recording while already recording");
        setError("Already recording");
        return false;
    }

    // Initialize audio device if not already done
    if (!_audioDevice) {
        _audioDevice = new ma_device;
        
        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_f32;
        config.capture.channels = _channels;
        config.sampleRate = _sampleRate;
        config.dataCallback = audioDataCallback;
        config.pUserData = this;

        if (ma_device_init(nullptr, &config, _audioDevice) != MA_SUCCESS) {
            LERROR("Failed to initialize audio capture device");
            delete _audioDevice;
            _audioDevice = nullptr;
            setError("Failed to initialize audio capture");
            return false;
        }
        LINFO("Successfully initialized audio device");
    }

    // Clear any previously captured audio
    _capturedAudio.clear();
    LINFO("Starting audio capture...");

    // Start the capture device
    if (ma_device_start(_audioDevice) != MA_SUCCESS) {
        LERROR("Failed to start audio capture");
        setError("Failed to start audio capture");
        return false;
    }

    LINFO("Audio capture started successfully");
    setError("");  // Clear any previous errors
    setTranscription("");  // Clear any previous transcription
    setState(VoiceState::Recording);  // This will trigger the state update
    return true;
}

bool VoiceCommandHandler::stopRecording() {
    if (_state != VoiceState::Recording) {
        LWARNING("Attempted to stop recording while not recording");
        setError("Not currently recording");
        return false;
    }

    LINFO("Stopping audio capture...");
    if (_audioDevice) {
        ma_device_stop(_audioDevice);
    }

    setState(VoiceState::Processing);

    const float durationSeconds = static_cast<float>(_capturedAudio.size()) / 
                                (_sampleRate * _channels);
    
    LINFO(std::format(
        "Captured {} samples ({:.2f} seconds) of audio data at {}Hz",
        _capturedAudio.size(), durationSeconds, _sampleRate
    ));
    
    setTranscription("Audio capture complete");
    return true;
}

bool VoiceCommandHandler::isRecording() const {
    return _state == VoiceState::Recording;
}

VoiceCommandHandler::VoiceState VoiceCommandHandler::state() const {
    return _state;
}

std::string VoiceCommandHandler::transcription() const {
    return _transcription;
}

std::string VoiceCommandHandler::error() const {
    return _error;
}

void VoiceCommandHandler::sendStatusUpdate(const std::string& status,
                                         const std::string& transcription,
                                         const std::string& error)
{
    // Just update internal state - the topic will handle sending updates
    if (!error.empty()) {
        setError(error);
    }
    if (!transcription.empty()) {
        setTranscription(transcription);
    }
    
    // Convert status string to state
    if (status == "idle") {
        setState(VoiceState::Idle);
    }
    else if (status == "recording") {
        setState(VoiceState::Recording);
    }
    else if (status == "processing") {
        setState(VoiceState::Processing);
    }
    else if (status == "error") {
        setState(VoiceState::Error);
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

VoiceCommandHandler::CallbackHandle VoiceCommandHandler::addStateChangeCallback(
    StateChangeCallback callback)
{
    const CallbackHandle handle = _nextCallbackHandle++;
    _stateChangeCallbacks[handle] = std::move(callback);
    return handle;
}

void VoiceCommandHandler::removeStateChangeCallback(CallbackHandle handle) {
    _stateChangeCallbacks.erase(handle);
}

scripting::LuaLibrary VoiceCommandHandler::luaLibrary() {
    return {
        "voice",
        {
            codegen::lua::StartRecording,
            codegen::lua::StopRecording,
            codegen::lua::State,
            codegen::lua::Transcription,
            codegen::lua::Error
        }
    };
}

void VoiceCommandHandler::setState(VoiceState state) {
    if (_state != state) {
        LDEBUG(std::format(
            "VoiceCommandHandler state changing from {} to {}",
            static_cast<int>(_state),
            static_cast<int>(state)
        ));
        
        _state = state;
        _isRecording.setValue(state == VoiceState::Recording);
        
        // Notify all callbacks of state change
        LDEBUG(std::format(
            "Notifying {} state change callbacks",
            _stateChangeCallbacks.size()
        ));
        
        for (const auto& [handle, callback] : _stateChangeCallbacks) {
            LDEBUG(std::format("Executing callback {}", handle));
            callback();
        }
    }
}

void VoiceCommandHandler::setTranscription(const std::string& transcription) {
    if (_transcription != transcription) {
        _transcription = transcription;
        setState(VoiceState::Idle); // Transcription complete, return to idle
    }
}

void VoiceCommandHandler::setError(const std::string& error) {
    if (_error != error) {
        _error = error;
        if (!error.empty()) {
            setState(VoiceState::Error);
        }
    }
}

} // namespace openspace::interaction 