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

#include <modules/server/include/topics/voicecommandtopic.h>

#include <modules/server/include/connection.h>
#include <openspace/json.h>
#include <ghoul/logging/logmanager.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <modules/base/basemodule.h>
#include <openspace/interaction/voicecommandhandler.h>

namespace {
    constexpr std::string_view _loggerCat = "VoiceCommandTopic";

    constexpr const char* TypeKey = "type";
    constexpr const char* StatusKey = "status";
    constexpr const char* TranscriptionKey = "transcription";
    constexpr const char* ErrorKey = "error";
    constexpr const char* EventKey = "event";

    // Subscription events
    constexpr std::string_view StartSubscription = "start_subscription";
    constexpr std::string_view StopSubscription = "stop_subscription";
    constexpr std::string_view RefreshSubscription = "refresh";
} // namespace

namespace openspace {

VoiceCommandTopic::VoiceCommandTopic() : _voiceHandler(nullptr) {
    LDEBUG("Starting new VoiceCommand subscription");

    // Get the VoiceCommandHandler instance from globals
    _voiceHandler = global::voiceCommandHandler;

    if (!_voiceHandler) {
        LERROR("Could not find VoiceCommandHandler");
    }
}

VoiceCommandTopic::~VoiceCommandTopic() {
    if (_voiceHandler && _callbackHandle != -1) {
        _voiceHandler->removeStateChangeCallback(_callbackHandle);
    }
}

void VoiceCommandTopic::handleJson(const nlohmann::json& json) {
    if (!json.contains(EventKey)) {
        return;
    }

    const std::string event = json.at(EventKey).get<std::string>();
    
    if (event == StartSubscription) {
        _isSubscribed = true;
        setupStateChangeCallback();
        // Send initial state
        sendStateUpdate();
    }
    else if (event == StopSubscription) {
        if (_voiceHandler && _callbackHandle != -1) {
            _voiceHandler->removeStateChangeCallback(_callbackHandle);
            _callbackHandle = -1;
        }
        _isSubscribed = false;
        _isDone = true;
    }
    else if (event == RefreshSubscription) {
        sendStateUpdate();
    }
}

bool VoiceCommandTopic::isDone() const {
    return !_isSubscribed;
}

void VoiceCommandTopic::setupStateChangeCallback() {
    if (!_voiceHandler) {
        LERROR("Cannot setup callback: VoiceCommandHandler not available");
        return;
    }

    // Remove existing callback if any
    if (_callbackHandle != -1) {
        LDEBUG("Removing existing callback");
        _voiceHandler->removeStateChangeCallback(_callbackHandle);
    }

    // Add new callback
    LDEBUG("Setting up new state change callback");
    _callbackHandle = _voiceHandler->addStateChangeCallback([this]() {
        LDEBUG("State change callback triggered - sending update");
        sendStateUpdate();
    });
    LDEBUG(std::format("Callback registered with handle {}", _callbackHandle));
}

void VoiceCommandTopic::sendStateUpdate() {
    if (!_voiceHandler) {
        LERROR("Cannot send state update: VoiceCommandHandler not available");
        return;
    }

    // Convert state to string
    std::string stateStr;
    switch (_voiceHandler->state()) {
        case interaction::VoiceCommandHandler::VoiceState::Idle:
            stateStr = "idle";
            break;
        case interaction::VoiceCommandHandler::VoiceState::Recording:
            stateStr = "recording";
            break;
        case interaction::VoiceCommandHandler::VoiceState::Processing:
            stateStr = "processing";
            break;
        case interaction::VoiceCommandHandler::VoiceState::Error:
            stateStr = "error";
            break;
        default:
            stateStr = "unknown";
    }

    nlohmann::json stateJson = {
        { TypeKey, "voice_status" },
        { StatusKey, stateStr }
    };

    LDEBUG(std::format(
        "Sending voice state update: {} to client", stateStr
    ));

    // Always include transcription and error fields, even if empty
    const std::string transcription = _voiceHandler->transcription();
    stateJson[TranscriptionKey] = transcription;
    LDEBUG(std::format("Including transcription: {}", transcription));

    const std::string error = _voiceHandler->error();
    stateJson[ErrorKey] = error;
    if (!error.empty()) {
        LDEBUG(std::format("Including error: {}", error));
    }

    LDEBUG("Sending WebSocket message to client");
    LDEBUG(std::format("Full state update: {}", stateJson.dump()));
    _connection->sendJson(wrappedPayload(stateJson));
}

} // namespace openspace 