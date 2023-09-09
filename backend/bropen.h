#ifndef __BROPEN_H
#define __BROPEN_H

/*
   TODO add copyright + info
*/


// #define MOCK_DEVICE

#include <sys/param.h>
#include <jpeglib.h>

#undef  BACKEND_NAME
#define BACKEND_NAME bropen

#define DBG_ERR  1
#define DBG_WARN 2
#define DBG_MSG  3
#define DBG_INFO 4
#define DBG_DBG  5
#define DBG_DBG_MORE 6

#define BROTHER_ID  0x04f9
#define DCP_L2500D  0x0321

#define INTERFACE_INDEX 1
#define USB	1
#define SCSI	2
#define MAX_READ_DATA_SIZE 0x10000
#define WRITE_BUFFER_SIZE 0xff

#define BUTTON_TEXT 2
#define BUTTON_IMAGE 3
#define BUTTON_DOCUMENT 5
#define BUTTON_EMAIL 8


typedef unsigned char u8;
typedef unsigned u32;
typedef unsigned short u16;

/* options */
typedef enum
{
  NUM_OPTS = 0,

  /* General options */
  MODE_GROUP,
  MODE,				/* scanner modes */                     // used
  RESOLUTION,			/* X and Y resolution */            // used

  DUPLEX,			/* Duplex mode */                       // maybe used in the future
  // FEEDER_MODE,			/* Feeder mode, fixed to Continuous */
  // LENGTHCTL,			/* Length control mode */
  // MANUALFEED,			/* Manual feed mode */
  // FEED_TIMEOUT,			/* Feed timeout */
  // DBLFEED,			/* Double feed detection mode */
  // FIT_TO_PAGE,			/* Scanner shrinks image to fit scanned page */

  /* Geometry group */
  GEOMETRY_GROUP,
  PAPER_SIZE,			/* Paper size */          // maybe used in the future
  LANDSCAPE,			/* true if landscape */
  TL_X,				/* upper left X */            // used
  TL_Y,				/* upper left Y */            // used
  BR_X,				/* bottom right X */            // used
  BR_Y,				/* bottom right Y */            // used

  ADVANCED_GROUP,
  BRIGHTNESS,			/* Brightness */            // used
  CONTRAST,			/* Contrast */                // used
  THRESHOLD,			/* Binary threshold */      // TODO should prob. be deleted
  // IMAGE_EMPHASIS,		/* Image emphasis */
  // GAMMA_CORRECTION,		/* Gamma correction */
  // LAMP,				/* Lamp -- color drop out */

  OPT_SENSORS_GROUP,

  OPT_SENSOR_SCAN,
  OPT_SENSOR_EMAIL,
  OPT_SENSOR_MESSAGE,

  /* must come last: */
  NUM_OPTIONS
} KV_OPTION;

#ifndef SANE_OPTION
typedef union
{
  SANE_Bool b;		/**< bool */
  SANE_Word w;		/**< word */
  SANE_Word *wa;	/**< word array */
  SANE_String s;	/**< string */
}
Option_Value;
#define SANE_OPTION 1
#endif


struct window
{
  // u8 reserved[6];
  // u16 window_descriptor_block_length;

  // u8 window_identifier;
  // u8 reserved2;
  u16 x_resolution;
  u16 y_resolution;
  u32 upper_left_x;
  u32 upper_left_y;
  u32 lower_right_x;
  u32 lower_right_y;
  // u8 brightness;
  // u8 threshold;
  // u8 contrast;
  u8 image_composition;
  u8 format_mode;
  size_t bytes_per_line;
  /* The scanner does not always return an image of exactly the requested size. */
  size_t actual_image_width;
  /* The scanner does not always return an image of exactly the requested size. */
  size_t actual_image_height;
  // u16 halftone_pattern;
  // u8 reserved3;
  // u16 bit_ordering;
  // u8 compression_type;
  // u8 compression_argument;
  // u8 reserved4[6];

  // u8 vendor_unique_identifier;
  // u8 nobuf_fstspeed_dfstop;
  // u8 mirror_image;
  // u8 image_emphasis;
  // u8 gamma_correction;
  // u8 mcd_lamp_dfeed_sens;
  // u8 reserved5;
  // u8 document_size;
  // u32 document_width;
  // u32 document_length;
  // u8 ahead_deskew_dfeed_scan_area_fspeed_rshad;
  // u8 continuous_scanning_pages;
  // u8 automatic_threshold_mode;
  // u8 automatic_separation_mode;
  // u8 standard_white_level_mode;
  // u8 b_wnr_noise_reduction;
  // u8 mfeed_toppos_btmpos_dsepa_hsepa_dcont_rstkr;
  // u8 stop_mode;
};

struct JpegDecodeData {
  struct jpeg_decompress_struct *cinfo;
};

typedef enum {
  SCAN_FORMAT_NONE, /* uncompressed */
  SCAN_FORMAT_JPEG /*,
  MODE_RLENGTH */ /* run-length encoding, not available for RGB */
} SCAN_FORMAT;

static const SANE_String_Const format_internal[] = {
  "NONE",
  "JPEG",
  "RLENGTH"
};

// This list must match the other list in "..._opt.c"
typedef enum {
  COLOR_MODE_RGB,
  COLOR_MODE_GRAYSCALE,
  COLOR_MODE_DITHER,
  COLOR_MODE_BW /*,
  COLOR_MODE_C256 */
} COLOR_MODE;

static const SANE_String_Const scan_modes_internal[] = {
    "CGRAY",
    "GRAY64",
    "ERRDIF",
    "TEXT" /*,
     "C256" low quality color scan, not really useful */
};

/*
The raw mode for RGB outputs the data in some weird color space I was unable to decode.
The image quality also does not look great. Therefore, we set the format to JPEG for RGB
and to uncompressed for grayscale / BW.
*/
static const size_t format_map[] = { SCAN_FORMAT_JPEG, SCAN_FORMAT_NONE, SCAN_FORMAT_NONE, SCAN_FORMAT_NONE };

struct scanner
{
  SANE_Int device_id;
  SANE_Bool volatile cancelled;
  // int page;
  // int side;
  SANE_Int file;
  SANE_Option_Descriptor opt[NUM_OPTIONS];
  Option_Value val[NUM_OPTIONS];
  SANE_Parameters params;
  struct window window;
  struct JpegDecodeData jpeg;

  FILE *temp_file;

  /* How much actual data the buffer contains */
  size_t usb_buffer_size;
  size_t usb_buffer_read_head;
  SANE_Byte *usb_read_buffer;
  /* The number of unprocessed bytes in the scanline buffer */
  size_t scanline_buffer_bytes_left;
  SANE_Byte *scanline_buffer;
  size_t total_lines_written;
  SANE_Bool read_from_device_complete;
  /* This value should be cleared to zero once it has been read in sane_control_option(). */
  SANE_Byte last_button_pressed;
};

static inline u16
swap_bytes16 (u16 x)
{
  return x << 8 | x >> 8;
}
static inline u32
swap_bytes32 (u32 x)
{
  return x << 24 | x >> 24 |
    (x & (u32) 0x0000ff00UL) << 8 | (x & (u32) 0x00ff0000UL) >> 8;
}

static inline void
copy16 (u8 * p, u16 x)
{
  memcpy (p, (u8 *) &x, sizeof (x));
}

#if __BYTE_ORDER == __BIG_ENDIAN
static inline void
set24 (u8 * p, u32 x)
{
  p[2] = x >> 16;
  p[1] = x >> 8;
  p[0] = x >> 0;
}

#define cpu2be16(x) (x)
#define cpu2be32(x) (x)
#define cpu2le16(x) swap_bytes16(x)
#define cpu2le32(x) swap_bytes32(x)
#define le2cpu16(x) swap_bytes16(x)
#define le2cpu32(x) swap_bytes32(x)
#define be2cpu16(x) (x)
#define be2cpu32(x) (x)
#define BIT_ORDERING 0
#elif __BYTE_ORDER == __LITTLE_ENDIAN
static inline void
set24 (u8 * p, u32 x)
{
  p[0] = x >> 16;
  p[1] = x >> 8;
  p[2] = x >> 0;
}

#define cpu2le16(x) (x)
#define cpu2le32(x) (x)
#define cpu2be16(x) swap_bytes16(x)
#define cpu2be32(x) swap_bytes32(x)
#define le2cpu16(x) (x)
#define le2cpu32(x) (x)
#define be2cpu16(x) swap_bytes16(x)
#define be2cpu32(x) swap_bytes32(x)
#define BIT_ORDERING 1
#else
#error __BYTE_ORDER not defined
#endif

#endif /*__BROPEN_H*/
