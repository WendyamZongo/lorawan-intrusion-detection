/**
 * ChirpStack Payload Decoder
 * Device: ATmega328P PIR Motion Node
 * 
 * Payload format (1 byte):
 * Byte 0: 0x01 = Motion Detected, 0x00 = Clear
 */

function decodeUplink(input) {
  var bytes = input.bytes;
  return {
    data: {
      motion: bytes[0] === 1 ? "DETECTED" : "CLEAR"
    }
  };
}

function encodeDownlink(input) {
  return {
    fPort: 1,
    bytes: [0]
  };
}
