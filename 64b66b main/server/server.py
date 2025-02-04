from datetime import datetime
import requests
from flask import Flask, request, abort

app = Flask(__name__)

TELEGRAM_TOKEN = "735345723:fhyuafskoifjwef"
CHAT_ID = "99999999"

class ScramblerState:
    def __init__(self):
        self.state = (1 << 58) - 1  # Initial LFSR state

    def update(self, scrambled_bit):
        self.state = ((self.state << 1) | scrambled_bit) & ((1 << 58) - 1)

    def get_feedback(self):
        return ((self.state >> 38) & 1) ^ ((self.state >> 57) & 1)

def decode_64b66b(encoded_data):
    """
    Decodes 64B/66B encoded data with scrambling
    Args:
        encoded_data (bytes): The 64B/66B encoded binary data
    Returns:
        bytes: The decoded original data
    """
    decoded = bytearray()
    scrambler = ScramblerState()
    block_size = 9  # 66 bits = 9 bytes per block
    
    for i in range(0, len(encoded_data), block_size):
        block = encoded_data[i:i+block_size]
        if len(block) != block_size:
            break  # Skip incomplete block

        # Extract sync header and verify
        sync_header = (block[0] >> 6) & 0x03
        if sync_header != 0b01:
            continue  # Skip non-data blocks or handle differently

        # Reconstruct 64-bit scrambled payload
        scrambled = (
            ((block[0] & 0x3F) << 58) |
            (block[1] << 50) |
            (block[2] << 42) |
            (block[3] << 34) |
            (block[4] << 26) |
            (block[5] << 18) |
            (block[6] << 10) |
            (block[7] << 2) |
            ((block[8] >> 6) & 0x03)
        )

        # Descramble the data
        original = 0
        for bit_pos in range(63, -1, -1):
            scrambled_bit = (scrambled >> bit_pos) & 0x01
            feedback = scrambler.get_feedback()
            original_bit = scrambled_bit ^ feedback
            original = (original << 1) | original_bit
            scrambler.update(scrambled_bit)

        # Convert to bytes and remove padding
        decoded.extend(original.to_bytes(8, 'big').rstrip(b'\x00'))

    return bytes(decoded)

@app.route('/send_photo', methods=['POST'])
def handle_photo():
    if 'photo' not in request.files:
        abort(400, "No photo uploaded")
    
    photo = request.files['photo']
    try:
        encoded_data = photo.read()
        decoded_data = decode_64b66b(encoded_data)
    except Exception as e:
        abort(400, f"Decoding failed: {str(e)}")

    # Verify decoded image
    if not decoded_data.startswith(b'\xff\xd8') or not decoded_data.endswith(b'\xff\xd9'):
        abort(400, "Invalid JPEG format after decoding")

    # Send to Telegram
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    files = {'photo': ('image.jpg', decoded_data, 'image/jpeg')}
    params = {
        'chat_id': CHAT_ID,
        'caption': f"Photo received at {timestamp} (GMT+3:30)"
    }
    
    response = requests.post(
        f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendPhoto",
        files=files,
        data=params
    )

    return response.text, response.status_code

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)