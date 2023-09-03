/*
   TODO add copyright + info
*/

#include "../include/sane/config.h"

#include <stdio.h> // for snprintf()
#include <string.h>
/*#include <unistd.h>*/

#define DEBUG_DECLARE_ONLY
#define BACKEND_NAME bropen

#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_scsi.h"
#include "../include/sane/sanei_usb.h"

#include "bropen.h"
#include "bropen_cmd.h"


int parse_next(char **token, int *value) {
    if (!*token)
        return 0;
    *value = atoi(*token);
    *token = strtok(NULL, ",");
    return 1;
}

SANE_Bool parseScanInquiryResponse(unsigned char* buffer, struct ScanInquiryResponse *params) {
  // byte 0: \x00; byte 1: length; byte 2: \x00; byte 3-...: string of comma-separated integers
  if (buffer[0] != 0 || buffer[2] != 0) {
      DBG(DBG_ERR, "Invalid scan inquiry response\n");
      return SANE_FALSE;
  }
  char* text = (char*) &buffer[3];
  // buffer[1] == length (including null terminator)
  if (strlen(text) + 1 != buffer[1]) {
      DBG(DBG_ERR, "Invalid scan response length\n");
      return SANE_FALSE;
  }
  const int NUM_ENTRIES = 7;
  int data[NUM_ENTRIES];
  char *token = strtok(text, ",");
  for (int i = 0; i < NUM_ENTRIES; i++) {
      if (!token)
          return SANE_FALSE;
      if (!parse_next(&token, &data[i])) {
          DBG(DBG_ERR, "Parsing error for scan inquiry response: '%s'", text);
          return SANE_FALSE;
      }
  }
  memcpy(params, data, sizeof(struct ScanInquiryResponse));
  return SANE_TRUE;
}

// Make sure that text_buffer_len >= 3*buffer_len + 1.
size_t hex_print_buffer_to_str(char* text_buffer, size_t text_buffer_len, unsigned char* buffer, size_t buffer_len) {
  size_t position = 0;
  for (size_t i = 0; i < buffer_len; i++)
      position += snprintf(&text_buffer[position], text_buffer_len - position, "%02x ", buffer[i]);
  return position;
}

/* msg must have exactly one %s and no other formatted variables */
void log_error_with_hexdump(int lvl, char *msg, SANE_Byte *data_buffer, size_t buffer_len) {
  const size_t text_len = 3*buffer_len + 1;
  char *text_buffer = malloc(text_len);
  hex_print_buffer_to_str(text_buffer, text_len, data_buffer, buffer_len);
  free(text_buffer);
  DBG(lvl, msg, text_buffer);
}



  /* Header:
  bytes 0-5: fixed
  bytes 6-9: current row (little-endian)
  bytes 10-11: data length (little-endian)
  
  Could use a struct here, but there is no compiler-independent way of forcing it to be packed.
  For gcc, we could use
  struct ActualDataHeader {
    SANE_Byte packet_type;
    SANE_Byte fixed[5];
    u32 row;
    u16 packet_size;
  } __attribute__ ((packed));
  */
SANE_Status parse_data_header(SANE_Byte *buffer, struct DataHeader *output) {
  static const SANE_Byte HEADER_EXPECTED[] = {0x07, 0x00, 0x01, 0x00, 0x84};
  SANE_Byte fixed[5];
  output->packet_type = buffer[0];
  memcpy(fixed, &buffer[1], sizeof(fixed));
  memcpy(&output->row, &buffer[6], sizeof(output->row));
  memcpy(&output->packet_size, &buffer[10], sizeof(output->packet_size));

  if (memcmp(&buffer[1], HEADER_EXPECTED, sizeof(HEADER_EXPECTED)) != 0) {
    log_error_with_hexdump(DBG_ERR, "Received invalid data header:\n%s\n", buffer, 20);
    return SANE_STATUS_IO_ERROR;
  }
  // ensure endianness (in case host is big-endian)
  output->row = le2cpu32(output->row);
  output->packet_size = le2cpu16(output->packet_size);
  return SANE_STATUS_GOOD;
}

// The control signal response when the scanner has been activated
static const char RESPONSE_ACTIVATE_SCAN[] = "\x05\x10\x01\x02\x00";
// The control signal response when the scanner has been deactivated
static const char RESPONSE_DEACTIVATE_SCAN[] = "\x05\x10\x02\x02\x00";

SANE_Status activate_scanner(struct scanner *s) {
  DBG(DBG_DBG, "sending control signal to activate scanner\n");

  memset(s->usb_read_buffer, 0, 255);

  SANE_Status st = sanei_usb_control_msg(
    s->file,
    0xc0,     // bmRequestType = 1 (device-to-host) 10 (vendor) 00000 (device)
    1,        // bRequest
    0x0002,   // bValue
    0,        // wIndex
    5,        // expected length of output
    s->usb_read_buffer // return data buffer
  );

  if (st) {
    DBG(DBG_DBG, "Activation control signal failed\n");
    return st;
  }

  if (memcmp(s->usb_read_buffer, RESPONSE_ACTIVATE_SCAN, sizeof(RESPONSE_ACTIVATE_SCAN) - 1) != 0) {
      log_error_with_hexdump(
        DBG_ERR,
        "Unexpected response to scanner activation control signal:\n%s\n",
        s->usb_read_buffer, 10);
      return SANE_STATUS_IO_ERROR;
  }

  return SANE_STATUS_GOOD;
}


SANE_Status deactivate_scanner(struct scanner *s) {
  memset(s->usb_read_buffer, 0, 10);
  SANE_Status st = sanei_usb_control_msg(
    s->file,
    0xc0, // bmRequestType = 1 (device-to-host) 10 (vendor) 00000 (device)
    2, // bRequest
    0x0002, // bValue
    0, // wIndex
    WRITE_BUFFER_SIZE, // I believe this is the size of the return data buffer
    s->usb_read_buffer // return data buffer
  );
  if (st)
    return st;

  if (memcmp(s->usb_read_buffer, RESPONSE_DEACTIVATE_SCAN, sizeof(RESPONSE_DEACTIVATE_SCAN) - 1) != 0) {
    log_error_with_hexdump(
      DBG_ERR,
      "Unexpected response to scanner deactivation control signal:\n%s\n",
      s->usb_read_buffer, 10);
    return SANE_STATUS_IO_ERROR;
  }

  return SANE_STATUS_GOOD;
}


SANE_Status send_format_inquiry(struct scanner *s) {
  SANE_Byte command_buffer[WRITE_BUFFER_SIZE];
  size_t command_length = 0;

  /**
   * Format:
   * - 0x1b: start marker
   * - I: type of inquiry
   * - R=300,300: requested DPI in x and y
   * - M=CGRAY: color mode
   * - D=SIN: likely simplex/duplex
   * - 0x80: end marker
  */

  command_length = snprintf((char *)command_buffer, WRITE_BUFFER_SIZE,
      "\x1bI\nR=%d,%d\nM=%s\nD=SIN\n\x80",
      s->window.x_resolution,
      s->window.y_resolution,
      scan_modes_internal[s->window.image_composition] // e.g. "CGRAY" or "GRAY64"
    );
  if (command_length > WRITE_BUFFER_SIZE)
    /* This can't really happen unless we have some memory corruption */
    return SANE_STATUS_IO_ERROR;

  size_t length_actually_written = command_length;
  SANE_Status st = sanei_usb_write_bulk(s->file, command_buffer, &length_actually_written);
  if (st)
    return st;

  if (length_actually_written < command_length) {
    DBG(DBG_ERR, "Failed to write the full signal to the device (expecte: %ld, actual: %ld)\n", command_length, length_actually_written);
    return SANE_STATUS_IO_ERROR;
  }

  return SANE_STATUS_GOOD;
}

  /*
  Example (300 dpi):
      00 1d 00 33 30 30 2c 33 30 30 2c 32 2c 32 30 39 2c 32 34 38 30 2c 32 39 31 2c 33 34 33 37 2c 00
      As string: 300,300,2,209,2480,291,3437,
  Example (200 dpi):
      00 1d 00 32 30 30 2c 32 30 30 2c 32 2c 32 30 39 2c 31 36 35 33 2c 32 39 31 2c 32 32 39 31 2c 00
      As string: 200,200,2,209,1653,291,2291,
  Example (1200 dpi):
      00 1f 00 36 30 30 2c 31 32 30 30 2c 32 2c 32 30 39 2c 34 39 36 30 2c 32 39 31 2c 31 33 37 34 38 2c 00
      As string: 600,1200,2,209,4960,291,13748,
      - Interesting: The response is "600,1200", so the x resolution is limited!

  Interpretation:
      0, 2, 31: \x00 spacer
      1: clearly the length of the string
      Text: dpix, dpiy, unknown_2, scanner_width_in_mm_i_guess, max pixels x, scanner_height_in_mm_i_guess, max pixels y
      The next inquiry may contain fewer x pixels, probably to account for paper aspect ratio
  */

void clamp_coordinate(u32 *value, u32 max) {
  if (*value > max)
    *value = max; 
}

SANE_Status read_response_3_attempts(struct scanner *s) {
  memset(s->usb_read_buffer, 0, 255);
  size_t read_size;
  // Make 3 attempts, because this does not always work on the first one
  for (int i = 0; i < 3; i++) {
    read_size = 255;
    usleep(5 * 1000);
    SANE_Status st = sanei_usb_read_bulk(s->file, s->usb_read_buffer, &read_size);
    if (st)
      return st;
    if (read_size > 0)
      break;
  }

  if (read_size == 0) {
    DBG(DBG_ERR, "Error: Scanner did not respond with format parameters\n");
    return SANE_STATUS_IO_ERROR;
  }

  return SANE_STATUS_GOOD;
}



void update_window_from_scanner_response(struct scanner *s, struct ScanInquiryResponse *response) {
  s->window.x_resolution = response->dpi_x;
  s->window.y_resolution = response->dpi_y;

  // the intermediate value likely does not fit into a 16 byte word
  s->window.upper_left_x = (u32)s->val[TL_X].w * response->max_pixels_x / response->scanner_width_in_mm_i_guess;
  s->window.upper_left_y = (u32)s->val[TL_Y].w * response->max_pixels_y / response->scanner_height_in_mm_i_guess;
  s->window.lower_right_x = (u32)s->val[BR_X].w * response->max_pixels_x / response->scanner_width_in_mm_i_guess;
  s->window.lower_right_y = (u32)s->val[BR_Y].w * response->max_pixels_y / response->scanner_height_in_mm_i_guess;

  // clip to valid ranges
  clamp_coordinate(&s->window.upper_left_x, response->max_pixels_x);
  clamp_coordinate(&s->window.lower_right_x, response->max_pixels_x);
  clamp_coordinate(&s->window.upper_left_y, response->max_pixels_y);
  clamp_coordinate(&s->window.lower_right_y, response->max_pixels_y);

  DBG(DBG_DBG, "window coords: %d,%d,%d,%d; max: %d,%d\n",
      s->window.upper_left_x,
      s->window.upper_left_y,
      s->window.lower_right_x, // TODO work out if this is indeed the bottom right corner, or if it is the width of the scan area
      s->window.lower_right_y, // TODO same here
      response->max_pixels_x,
      response->max_pixels_y);
}

SANE_Status send_begin_scan_command(struct scanner *s) {
    SANE_Byte command_buffer[255];
    size_t command_length = snprintf((char *)command_buffer, WRITE_BUFFER_SIZE,
    /**
     * Third message examples:
     * - 1200 dpi: \x1bX\nR=600,1200\nM=CGRAY\nC=JPEG\nJ=MIN\nB=50\nN=50\nA=0,0,4832,13747\nD=SIN\nE=0\nG=0\n\x80
     * -  300 dpi: \x1bX\nR=300,300\nM=CGRAY\nC=JPEG\nJ=MIN\nB=50\nN=50\nA=0,0,2416,3437\nD=SIN\nE=0\nG=0\n\x80
     * 
     * Contents:
     * - \x1b: start marker
     * - X: probably instruction to scan
     * - R=300,300: dpix,dpiy
     * - C=JPEG: result encoding; alternatives: RLENGTH / NONE; RLENGTH only for grayscale/monochrome
     * - J=MIN: no idea
     * - B=50: brightness (0-100)
     * - N=50: contrast (0-100)
     * - A=0,0,2416,3437: most likely the area of pixels we want to scan
     * - D=SIN: probably single/duplex; alternative D=DUP
     * - E=0: no idea
     * - G=0: no idea
     * - \x80: end marker
     */
    "\x1bX\nR=%d,%d\nM=%s\nC=%s\nJ=MIN\nB=%d\nN=%d\nA=%d,%d,%d,%d\nD=SIN\nE=0\nG=0\n\x80",
    s->window.x_resolution,
    s->window.y_resolution,
    scan_modes_internal[s->window.image_composition],
    format_internal[s->window.format_mode],
    s->val[BRIGHTNESS].w,
    s->val[CONTRAST].w,
    s->window.upper_left_x,
    s->window.upper_left_y,
    s->window.lower_right_x, // TODO work out if this is indeed the bottom right corner, or if it is the width of the scan area
    s->window.lower_right_y  // TODO same here
  );

  if (command_length > WRITE_BUFFER_SIZE)
    return SANE_STATUS_IO_ERROR;


  DBG(DBG_DBG_MORE, "Sending command to initiate scan:\n%s\n", &command_buffer[1]);

  size_t length_actually_written = command_length;
  SANE_Status st = sanei_usb_write_bulk(s->file, command_buffer, &length_actually_written);
  if (st)
    return st;
  if (length_actually_written < command_length)
    return SANE_STATUS_IO_ERROR;
  return SANE_STATUS_GOOD;
}
