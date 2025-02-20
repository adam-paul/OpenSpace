#!/usr/bin/env python3

import os
import sys
import json
import logging
import numpy as np
import whisper
from pathlib import Path

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('/tmp/openspace_voice/voice_service.log'),
        logging.StreamHandler(sys.stderr)  # Use stderr for logs to keep stdout clean for output
    ]
)
logger = logging.getLogger('VoiceService')

def output_json(data):
    """Helper function to output JSON in a consistent format."""
    logger.info(f"Outputting JSON response: {data}")
    # Ensure we're writing to stdout
    sys.stdout.write('\n')  # Start with a newline to separate from any previous output
    json_str = json.dumps(data)
    sys.stdout.write(json_str)
    sys.stdout.write('\n')  # End with a newline
    sys.stdout.flush()
    logger.info("JSON response written and flushed")

def transcribe_audio(audio_path, model_name="base"):
    """Transcribe audio file using Whisper."""
    try:
        # Load the model
        logger.info(f"Loading Whisper model: {model_name}")
        model = whisper.load_model(model_name)
        logger.info("Model loaded successfully")

        # Load raw PCM float32 data
        audio_data = np.fromfile(audio_path, dtype=np.float32)
        
        # Log audio data statistics
        logger.info(f"Audio data shape: {audio_data.shape}")
        logger.info(f"Audio data range: [{audio_data.min()}, {audio_data.max()}]")
        logger.info(f"Audio data mean: {audio_data.mean()}")
        
        # Normalize audio if needed (ensure it's in [-1, 1] range)
        max_abs = np.abs(audio_data).max()
        if max_abs > 1.0:
            logger.info(f"Normalizing audio data (max_abs={max_abs})")
            audio_data = audio_data / max_abs

        # Whisper expects audio data to be float32 in [-1, 1] range
        # It also expects mono audio at 16kHz
        if len(audio_data.shape) > 1:
            logger.warning("Audio has multiple channels, converting to mono")
            audio_data = audio_data.mean(axis=1)
        
        # Log final audio shape and stats before transcription
        logger.info("Final audio data stats:")
        logger.info(f"  Shape: {audio_data.shape}")
        logger.info(f"  Range: [{audio_data.min()}, {audio_data.max()}]")
        logger.info(f"  Mean: {audio_data.mean()}")
        logger.info(f"  Non-zero values: {np.count_nonzero(audio_data)}")
        
        # Transcribe
        logger.info(f"Transcribing audio from {audio_path}")
        try:
            result = model.transcribe(audio_data)
            logger.info("Transcription completed successfully")
            logger.info(f"Transcribed text: {result['text']}")
        except Exception as e:
            logger.error(f"Whisper transcription failed: {str(e)}")
            logger.error(f"Error type: {type(e)}")
            raise
        
        # Return result as JSON
        result_json = {
            'text': result["text"].strip(),
            'error': ''
        }
        output_json(result_json)
        return 0

    except Exception as e:
        logger.error(f"Transcription error: {str(e)}")
        logger.error(f"Error type: {type(e)}")
        import traceback
        logger.error(f"Traceback:\n{traceback.format_exc()}")
        error_json = {
            'text': '',
            'error': str(e)
        }
        output_json(error_json)
        return 1

if __name__ == "__main__":
    if len(sys.argv) != 2:
        error_json = {
            'text': '',
            'error': 'Usage: voice_service.py <audio_file_path>'
        }
        output_json(error_json)
        sys.exit(1)
    
    audio_path = sys.argv[1]
    if not os.path.exists(audio_path):
        error_json = {
            'text': '',
            'error': f'Audio file not found: {audio_path}'
        }
        output_json(error_json)
        sys.exit(1)
    
    sys.exit(transcribe_audio(audio_path)) 