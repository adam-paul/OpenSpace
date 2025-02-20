/*****************************************************************************************
 *                                                                                *
 * OpenSpace                                                                     *
 *                                                                                *
 * Copyright (c) 2014-2024                                                       *
 *                                                                                *
 * Permission is hereby granted, free of charge, to any person obtaining a copy  *
 * of this software and associated documentation files (the "Software"), to deal *
 * in the Software without restriction, including without limitation the rights   *
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell     *
 * copies of the Software, and to permit persons to whom the Software is         *
 * furnished to do so, subject to the following conditions:                      *
 *                                                                                *
 * The above copyright notice and this permission notice shall be included in    *
 * all copies or substantial portions of the Software.                           *
 *                                                                                *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR    *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,      *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE   *
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER        *
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, *
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN     *
 * THE SOFTWARE.                                                                 *
 ****************************************************************************************/

#include <openspace/engine/globals.h>
#include <openspace/scripting/lualibrary.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/lua/lua_helper.h>
#include <format>
#include <openspace/interaction/voicecommandhandler.h>

namespace {
    openspace::interaction::VoiceCommandHandler& voiceHandler() {
        ghoul_assert(
            openspace::global::voiceCommandHandler != nullptr,
            "VoiceCommandHandler must not be nullptr"
        );
        return *openspace::global::voiceCommandHandler;
    }
} // namespace

namespace openspace::codegen::lua {

scripting::LuaLibrary::Function StartRecording = {
    "startRecording",
    [](lua_State* L) -> int {
        try {
            bool success = voiceHandler().startRecording();
            if (!success) {
                return ghoul::lua::luaError(
                    L,
                    std::format("Failed to start recording: {}", voiceHandler().error())
                );
            }
            ghoul::lua::push(L, true);
            return 1;
        }
        catch (const ghoul::RuntimeError& e) {
            return ghoul::lua::luaError(
                L,
                std::format("Error starting recording: {}", e.message)
            );
        }
    }
};

scripting::LuaLibrary::Function StopRecording = {
    "stopRecording",
    [](lua_State* L) -> int {
        try {
            bool success = voiceHandler().stopRecording();
            if (!success) {
                return ghoul::lua::luaError(
                    L,
                    std::format("Failed to stop recording: {}", voiceHandler().error())
                );
            }
            ghoul::lua::push(L, true);
            return 1;
        }
        catch (const ghoul::RuntimeError& e) {
            return ghoul::lua::luaError(
                L,
                std::format("Error stopping recording: {}", e.message)
            );
        }
    }
};

scripting::LuaLibrary::Function State = {
    "state",
    [](lua_State* L) -> int {
        const interaction::VoiceCommandHandler::VoiceState state = voiceHandler().state();
        std::string stateStr;
        switch (state) {
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
        ghoul::lua::push(L, stateStr);
        return 1;
    }
};

scripting::LuaLibrary::Function Transcription = {
    "transcription",
    [](lua_State* L) -> int {
        ghoul::lua::push(L, voiceHandler().transcription());
        return 1;
    }
};

scripting::LuaLibrary::Function Error = {
    "error",
    [](lua_State* L) -> int {
        ghoul::lua::push(L, voiceHandler().error());
        return 1;
    }
};

} // namespace openspace::codegen::lua 