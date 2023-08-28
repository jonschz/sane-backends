#ifndef __BROPEN_CMD_H
#define __BROPEN_CMD_H

/*
   TODO add copyright + info
*/

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

/* My new stuff */


struct ScanInquiryResponse {
    int dpi_x;
    int dpi_y;
    int unknown_2;
    int scanner_width_in_mm_i_guess;
    int max_pixels_x;
    int scanner_height_in_mm_i_guess;
    int max_pixels_y;
};


  
struct DataHeader {
  SANE_Byte packet_type;
  u32 row;
  u16 packet_size;
};

static const size_t DATA_HEADER_SIZE = 12;

SANE_Bool parseScanInquiryResponse(unsigned char* buffer, struct ScanInquiryResponse *params);
size_t hex_print_buffer_to_str(char* text_buffer, size_t text_buffer_len, unsigned char* buffer, size_t buffer_len);
SANE_Status activate_scanner(struct scanner *s);
SANE_Status deactivate_scanner(struct scanner *s);
SANE_Status send_format_inquiry(struct scanner *s);
SANE_Status parse_data_header(SANE_Byte *buffer, struct DataHeader *output);
void clamp_coordinate(u32 *value, u32 max);

#endif /*__BROPEN_CMD_H*/
