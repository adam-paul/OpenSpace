#!/usr/bin/env python3

import sys
import json
import logging
import os
from pathlib import Path
import openai
from datetime import datetime
from dotenv import load_dotenv

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    stream=sys.stderr  # Send logs to stderr
)
logger = logging.getLogger('LLMService')

# Load environment variables from .env file
load_dotenv(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))), '.env'))

# Constants
TEMP_DIR = "/tmp/openspace_voice"
SCRIPT_DIR = os.path.join(TEMP_DIR, "scripts")

def load_example_commands():
    """
    Load example commands from /tmp/openspace_voice/ directory.
    Each file should be a JSON file containing a voice command and its corresponding Lua script.
    """
    example_dir = Path("/tmp/openspace_voice")
    examples = []
    
    try:
        # Ensure the directory exists
        example_dir.mkdir(parents=True, exist_ok=True)
        
        # Load all .lua files from the directory
        for file_path in example_dir.glob("*.lua"):
            try:
                with open(file_path, 'r') as f:
                    content = json.load(f)
                    if isinstance(content, dict) and "voice" in content and "lua" in content:
                        examples.append(content)
                    else:
                        logger.warning(f"Skipping {file_path}: Invalid format")
            except json.JSONDecodeError:
                logger.warning(f"Skipping {file_path}: Invalid JSON")
            except Exception as e:
                logger.warning(f"Error reading {file_path}: {e}")
    
    except Exception as e:
        logger.error(f"Error accessing example directory: {e}")
    
    # If no examples were loaded, use a default example
    if not examples:
        logger.warning("No example commands found, using default example")
        examples = [{
            "voice": "Show me the sun",
            "lua": """
                -- Focus the camera on the Sun
                openspace.navigation.setNavigationState({
                    Anchor = "Sun",
                    Focus = "Sun",
                    Position = { 0, 0, 50000000000 },  -- 50 million km away
                    Up = { 0, 1, 0 }
                })
                """
        }]
    
    return examples

# Replace the static EXAMPLE_COMMANDS with a function call
EXAMPLE_COMMANDS = load_example_commands()

SYSTEM_PROMPT = """You are a specialized converter that transforms voice commands into Lua scripts for OpenSpace astronomy navigation. Your primary function is to interpret natural language astronomy navigation commands and convert them into executable Lua code.

You will receive transcribed text from voice commands and should output valid Lua code that can be executed in OpenSpace.

When analyzing commands, consider:
- Target celestial objects
- Navigation actions (zoom, rotate, focus)
- Timing and transition specifications
- Additional parameters (speed, distance, orientation)

CONSTRAINTS:
- Only use functions and methods that appear in the examples
- Maintain consistent naming conventions from examples
- Preserve any timing or transition parameters specified in the voice command
- Include error handling for unrecognized commands or objects
- Output ONLY pure Lua code with NO markdown formatting (no ``` or other markers)
- Each line should be valid Lua syntax (NO semicolons at end of lines - Lua doesn't use them)
- Include comments starting with -- to explain the code
- Table entries should use commas, not semicolons
- Function calls and table definitions should NOT end with semicolons

OUTPUT FORMAT:
-- Your comment explaining what the code does
openspace.command1()
openspace.command2()
-- Another comment if needed
openspace.command3()"""

def get_openai_key():
    """Get OpenAI API key from environment variable."""
    api_key = os.getenv('OPENAI_API_KEY')
    if not api_key:
        raise ValueError("OPENAI_API_KEY environment variable not set")
    return api_key

def format_examples_for_prompt():
    """Format the example commands into a string for the prompt."""
    examples_text = "EXAMPLES:\n"
    for example in EXAMPLE_COMMANDS:
        examples_text += f"\nVoice Command: {example['voice']}\nLua Script:\n{example['lua']}\n"
    return examples_text

def ensure_directories():
    """Ensure the temporary and script directories exist."""
    for directory in [TEMP_DIR, SCRIPT_DIR]:
        Path(directory).mkdir(parents=True, exist_ok=True)
        logger.debug(f"Ensured directory exists: {directory}")

def save_script_to_file(lua_script: str) -> str:
    """
    Save the generated Lua script to a temporary file.
    
    Args:
        lua_script: The generated Lua script content
        
    Returns:
        The path to the saved script file
    """
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    script_path = os.path.join(SCRIPT_DIR, f"voice_command_{timestamp}.lua")
    
    try:
        with open(script_path, 'w', encoding='utf-8') as f:
            f.write(lua_script)
        logger.info(f"Saved Lua script to: {script_path}")
        return script_path
    except Exception as e:
        logger.error(f"Failed to save script to file: {e}")
        raise

def clean_lua_script(script: str) -> str:
    """
    Clean the generated Lua script to ensure it's pure Lua code without any markdown.
    
    Args:
        script: The raw script from the LLM
        
    Returns:
        Clean Lua code
    """
    # Remove any markdown code block markers
    script = script.replace('```lua', '').replace('```', '')
    
    # Ensure proper line endings
    script = script.replace('\r\n', '\n').replace('\r', '\n')
    
    # Remove any leading/trailing whitespace while preserving internal formatting
    lines = script.split('\n')
    lines = [line.rstrip() for line in lines]
    
    # Remove any empty lines at start/end while preserving internal empty lines
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
        
    # Join lines back together
    return '\n'.join(lines)

def generate_lua_script(transcription: str) -> str:
    """
    Generate a Lua script from the transcribed text using OpenAI's GPT-4.
    
    Args:
        transcription: The transcribed voice command
        
    Returns:
        A JSON string containing either the generated script info or an error message
    """
    try:
        # Initialize OpenAI client
        client = openai.OpenAI(api_key=get_openai_key())
        
        # Prepare the prompt with examples
        examples = format_examples_for_prompt()
        
        # Create messages for the chat completion
        messages = [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": examples},
            {"role": "user", "content": f"Voice Command: {transcription}"}
        ]
        
        logger.info(f"Sending request to OpenAI for command: {transcription}")
        
        # Call OpenAI API with new syntax
        response = client.chat.completions.create(
            model="gpt-4o",
            messages=messages,
            temperature=0.2,  # Lower temperature for more consistent outputs
            max_tokens=500
        )
        
        # Extract and clean the generated Lua script
        lua_script = response.choices[0].message.content.strip()
        lua_script = clean_lua_script(lua_script)
        
        # Add timestamp comment
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        lua_script = f"-- Generated from voice command: {transcription}\n-- Timestamp: {timestamp}\n\n{lua_script}"
        
        logger.info("Successfully generated Lua script")
        logger.debug(f"Generated script:\n{lua_script}")
        
        # Save the script to a file
        script_path = save_script_to_file(lua_script)
        
        # Return only the JSON response, no debug logs
        result = json.dumps({
            "success": True,
            "script": lua_script,
            "script_path": script_path
        })
        print(result)  # Print only the JSON result
        return ""  # Return empty string since we've already printed
        
    except openai.APIError as e:
        error_msg = json.dumps({
            "success": False,
            "error": f"OpenAI API error: {str(e)}"
        })
        print(error_msg)  # Print only the JSON result
        return ""
    except Exception as e:
        error_msg = json.dumps({
            "success": False,
            "error": str(e)
        })
        print(error_msg)  # Print only the JSON result
        return ""

def main():
    """Main entry point for the script generation service."""
    if len(sys.argv) != 2:
        print(json.dumps({
            "success": False,
            "error": "Usage: llm_service.py <transcription>"
        }))
        sys.exit(1)
        
    transcription = sys.argv[1]
    if not transcription:
        print(json.dumps({
            "success": False,
            "error": "Empty transcription provided"
        }))
        sys.exit(1)
    
    # Ensure directories exist
    ensure_directories()
        
    logger.info(f"Generating script for transcription: {transcription}")
    result = generate_lua_script(transcription)
    print(result)

if __name__ == "__main__":
    main() 