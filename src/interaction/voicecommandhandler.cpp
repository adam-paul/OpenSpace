#include <openspace/interaction/voicecommandhandler.h>

#include <openspace/json.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <openspace/scripting/scriptengine.h>
#include <openspace/properties/property.h>
#include <modules/server/servermodule.h>
#include <modules/server/include/topics/voicecommandtopic.h>
#include <modules/server/include/connection.h>
#include <modules/base/basemodule.h>
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

    // Save the audio data to a temporary file
    if (!saveAudioToTemp()) {
        setError("Failed to save audio data");
        setState(VoiceState::Error);
        return false;
    }
    
    // Process the audio data through Whisper
    const std::string transcription = processAudioData();
    LDEBUG(std::format("processAudioData returned transcription: '{}'", transcription));
    
    if (transcription.empty()) {
        LERROR("processAudioData returned empty transcription");
        // processAudioData will have set the error message if it failed
        setState(VoiceState::Error);
        return false;
    }

    LINFO(std::format("Setting transcription: '{}'", transcription));
    // Update the transcription (this will also set state to Idle)
    setTranscription(transcription);
    return true;
}

bool VoiceCommandHandler::saveAudioToTemp() {
    namespace fs = std::filesystem;
    
    // Generate a unique filename with timestamp
    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    const fs::path audioPath = _tempDirectory / std::format("audio_{}.raw", timestamp);
    
    try {
        std::ofstream outFile(audioPath, std::ios::binary);
        if (!outFile) {
            LERROR(std::format("Failed to open file for writing: {}", audioPath.string()));
            return false;
        }
        
        // Write raw PCM data
        outFile.write(
            reinterpret_cast<const char*>(_capturedAudio.data()),
            _capturedAudio.size() * sizeof(float)
        );
        
        if (!outFile) {
            LERROR("Failed to write audio data to file");
            return false;
        }
        
        outFile.close();
        LINFO(std::format("Saved audio data to {}", audioPath.string()));
        
        // Store the path for later use by the Python service
        _lastAudioPath = audioPath.string();
        return true;
    }
    catch (const std::exception& e) {
        LERROR(std::format("Exception while saving audio: {}", e.what()));
        return false;
    }
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

void VoiceCommandHandler::cleanupAudioFile() {
    if (!_lastAudioPath.empty()) {
        try {
            std::filesystem::remove(_lastAudioPath);
            LINFO(std::format("Cleaned up audio file: {}", _lastAudioPath));
            _lastAudioPath.clear();
            _needsRetry = false;
        }
        catch (const std::filesystem::filesystem_error& e) {
            LWARNING(std::format("Failed to clean up audio file: {}", e.what()));
        }
    }
}

std::string VoiceCommandHandler::processAudioData() {
    if (_lastAudioPath.empty()) {
        LERROR("No audio file available for processing");
        setError("No audio file available for processing");
        return "";
    }

    // Get the path to the Python script relative to the executable
    const std::filesystem::path scriptPath = 
        absPath("${MODULE_BASE}/scripts/voice/voice_service.py");

    // Build the command to capture both stdout and stderr
    const std::string command = std::format(
        "python3 '{}' '{}' 2>&1",
        scriptPath.string(),
        _lastAudioPath
    );

    LINFO(std::format("Executing command: {}", command));

    // Execute the Python script and capture its output
    std::array<char, 1024> buffer;  // Increased buffer size
    std::string result;
    std::string debug_output;
    FILE* pipe = popen(command.c_str(), "r");
    
    if (!pipe) {
        LERROR("Failed to execute Python script");
        setError("Failed to execute Python script");
        return "";
    }

    try {
        bool found_json = false;
        std::string current_line;
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            current_line = buffer.data();
            
            // Trim whitespace from the start and end of the line
            const size_t start = current_line.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) {
                // Line is all whitespace, skip it
                continue;
            }
            
            const size_t end = current_line.find_last_not_of(" \t\n\r");
            if (end == std::string::npos) {
                // This shouldn't happen if start is valid, but handle it anyway
                continue;
            }
            
            current_line = current_line.substr(start, end - start + 1);
            
            // Skip empty lines
            if (current_line.empty()) {
                continue;
            }
            
            // Check if this line looks like JSON
            if (current_line[0] == '{') {
                result = current_line;
                found_json = true;
                LDEBUG(std::format("Found JSON line: {}", current_line));
                // Don't add this line to debug output
                continue;
            }
            
            debug_output += current_line + "\n";
        }
        
        // Log any debug output
        if (!debug_output.empty()) {
            LINFO(std::format("Python script debug output:\n{}", debug_output));
        }
        
        if (!found_json) {
            LERROR("No JSON output found in Python script output");
            LERROR("Full debug output was:");
            LERROR(debug_output);
            setError("Failed to get transcription result");
            return "";
        }
    }
    catch (const std::exception& e) {
        LERROR(std::format("Error reading Python script output: {}", e.what()));
        pclose(pipe);
        setError(std::format("Error reading Python script output: {}", e.what()));
        return "";
    }

    const int status = pclose(pipe);
    if (status != 0) {
        LERROR(std::format("Python script failed with status: {}", status));
        setError(std::format("Python script failed with status: {}", status));
        return "";
    }

    // Parse the JSON response
    try {
        LDEBUG("BEGIN JSON parsing");
        LDEBUG(std::format("Raw JSON string: '{}'", result));
        
        LDEBUG("Attempting to parse JSON");
        const nlohmann::json response = nlohmann::json::parse(result);
        LDEBUG("JSON parsed successfully");
        
        LDEBUG("Checking error field");
        const std::string error = response["error"].get<std::string>();
        LDEBUG(std::format("Error field value: '{}'", error));
        if (!error.empty()) {
            // Only treat non-empty error fields as errors
            LERROR(std::format("Transcription error from Python: {}", error));
            setError(error);  // Just use the error message directly
            return "";
        }
        LDEBUG("Error field check passed (empty error field = success)");

        LDEBUG("Checking text field exists");
        if (!response.contains("text")) {
            LERROR("JSON response missing 'text' field");
            setError("Invalid transcription response");
            return "";
        }
        LDEBUG("Text field exists");

        LDEBUG("Extracting text field");
        const std::string transcription = response["text"].get<std::string>();
        LDEBUG(std::format("Raw text field value: '{}'", transcription));

        LDEBUG("Checking if text is empty");
        if (transcription.empty()) {
            LERROR("Empty transcription received");
            setError("No speech detected");
            return "";
        }
        LDEBUG(std::format("Text is not empty, length: {}", transcription.length()));

        // Success case - clear error and return transcription
        LINFO(std::format("Transcription successful: '{}'", transcription));
        setError("");  // Clear any previous error
        _needsRetry = false;  // Successful transcription, no retry needed
        LDEBUG(std::format("Returning transcription: '{}'", transcription));
        return transcription;
    }
    catch (const nlohmann::json::exception& e) {
        LERROR(std::format("Failed to parse Python script output: {}", e.what()));
        LERROR(std::format("Raw output was: {}", result));
        setError(std::format("Failed to parse transcription result: {}", e.what()));
        return "";
    }
    catch (const std::exception& e) {
        LERROR(std::format("Unexpected error while processing JSON: {}", e.what()));
        LERROR(std::format("Raw output was: {}", result));
        setError(std::format("Unexpected error: {}", e.what()));
        return "";
    }
}

void VoiceCommandHandler::generateAndExecuteScript([[maybe_unused]] const std::string& transcription) {
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
        if (!transcription.empty()) {
            // Only clean up the audio file if we have a successful transcription
            cleanupAudioFile();
            // Set state to idle only on successful transcription
            setState(VoiceState::Idle);
        }
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