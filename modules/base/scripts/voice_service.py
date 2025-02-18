#!/usr/bin/env python3

import asyncio
import json
import logging
import os
import signal
import sys
from dataclasses import dataclass
from enum import Enum
from typing import Optional, Callable

import whisper  # OpenAI Whisper for speech recognition

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('VoiceService')

class MessageType(Enum):
    AUDIO_DATA = 0
    TRANSCRIPTION = 1
    LUA_SCRIPT = 2
    ERROR = 3
    STATUS = 4

@dataclass
class IPCMessage:
    type: MessageType
    data: str
    metadata: str = ""

class VoiceService:
    def __init__(self, socket_path: str):
        self.socket_path = socket_path
        self.running = False
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self.whisper_model = None
        self.audio_buffer = bytearray()
        
    async def initialize(self):
        """Initialize the voice service and load models."""
        logger.info("Initializing voice service...")
        try:
            # Load Whisper model (using small model for faster processing)
            self.whisper_model = whisper.load_model("small")
            logger.info("Whisper model loaded successfully")
            return True
        except Exception as e:
            logger.error(f"Failed to initialize voice service: {e}")
            return False

    async def start(self):
        """Start the voice service and listen for connections."""
        try:
            # Remove existing socket file if it exists
            if os.path.exists(self.socket_path):
                os.unlink(self.socket_path)

            # Create Unix Domain Socket server
            server = await asyncio.start_unix_server(
                self.handle_connection,
                path=self.socket_path
            )
            
            # Set socket permissions
            os.chmod(self.socket_path, 0o666)
            
            self.running = True
            logger.info(f"Voice service listening on {self.socket_path}")
            
            async with server:
                await server.serve_forever()
                
        except Exception as e:
            logger.error(f"Failed to start voice service: {e}")
            self.running = False

    async def handle_connection(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """Handle a new connection from OpenSpace."""
        self.reader = reader
        self.writer = writer
        addr = writer.get_extra_info('peername')
        logger.info(f"New connection from {addr}")

        try:
            while self.running:
                # Read message
                data = await reader.read(1024 * 1024)  # 1MB buffer
                if not data:
                    break

                # Parse and handle message
                try:
                    message = json.loads(data.decode())
                    await self.handle_message(message)
                except json.JSONDecodeError as e:
                    logger.error(f"Failed to parse message: {e}")
                    await self.send_error("Invalid message format")

        except Exception as e:
            logger.error(f"Error handling connection: {e}")
        finally:
            writer.close()
            await writer.wait_closed()
            logger.info("Connection closed")

    async def handle_message(self, message: dict):
        """Handle incoming messages from OpenSpace."""
        try:
            msg_type = MessageType(int(message['type']))
            
            if msg_type == MessageType.AUDIO_DATA:
                # Accumulate audio data
                self.audio_buffer.extend(message['data'].encode())
                await self.send_status("Received audio chunk")
                
            elif msg_type == MessageType.TRANSCRIPTION:
                # Process accumulated audio data
                if self.audio_buffer:
                    text = await self.transcribe_audio()
                    await self.send_transcription(text)
                    self.audio_buffer.clear()
                else:
                    await self.send_error("No audio data to transcribe")
                    
        except Exception as e:
            logger.error(f"Error handling message: {e}")
            await self.send_error(str(e))

    async def transcribe_audio(self) -> str:
        """Transcribe audio data using Whisper."""
        try:
            # Save buffer to temporary file (Whisper expects a file)
            temp_path = "/tmp/openspace_voice_temp.wav"
            with open(temp_path, "wb") as f:
                f.write(self.audio_buffer)

            # Transcribe
            result = self.whisper_model.transcribe(temp_path)
            
            # Cleanup
            os.unlink(temp_path)
            
            return result["text"]
            
        except Exception as e:
            logger.error(f"Transcription error: {e}")
            raise

    async def send_message(self, message: IPCMessage):
        """Send a message to OpenSpace."""
        if not self.writer:
            logger.error("No active connection")
            return

        try:
            data = json.dumps({
                "type": message.type.value,
                "data": message.data,
                "metadata": message.metadata
            })
            self.writer.write(data.encode())
            await self.writer.drain()
        except Exception as e:
            logger.error(f"Failed to send message: {e}")

    async def send_status(self, status: str):
        """Send a status update to OpenSpace."""
        await self.send_message(IPCMessage(
            type=MessageType.STATUS,
            data=status
        ))

    async def send_transcription(self, text: str):
        """Send transcribed text to OpenSpace."""
        await self.send_message(IPCMessage(
            type=MessageType.TRANSCRIPTION,
            data=text
        ))

    async def send_error(self, error: str):
        """Send an error message to OpenSpace."""
        await self.send_message(IPCMessage(
            type=MessageType.ERROR,
            data=error
        ))

    def cleanup(self):
        """Cleanup resources on shutdown."""
        self.running = False
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)
        logger.info("Voice service cleaned up")

async def main():
    """Main entry point for the voice service."""
    service = VoiceService("/tmp/openspace_voice.sock")
    
    # Setup signal handlers
    loop = asyncio.get_event_loop()
    for sig in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(sig, service.cleanup)
    
    # Initialize and start service
    if await service.initialize():
        await service.start()
    else:
        logger.error("Failed to initialize voice service")
        sys.exit(1)

if __name__ == "__main__":
    asyncio.run(main()) 