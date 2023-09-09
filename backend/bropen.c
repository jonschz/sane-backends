/*
   TODO add copyright + info
*/

#define DEBUG_NOT_STATIC
#define BUILD 2

#include "../include/sane/config.h"

#include <string.h>
#include <unistd.h>

#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_scsi.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/lassert.h"

// TODO: Set dependency in autoconfig

#include "../include/sane/sanei_jpeg.h"

#include "bropen.h"
#include "bropen_cmd.h"

struct known_device
{
  const SANE_Int id;
  const SANE_Device scanner;
};

static const struct known_device known_devices[] = {
    {
        DCP_L2500D,
        {"", "BROTHER", "DCP-L2500D", "all-in-one"},
    },
    /*  {
        KV_S2045C,
        { "", "MATSHITA", "KV-S2045C", "sheetfed scanner" },
      },
      {
        KV_S2026C,
        { "", "MATSHITA", "KV-S2026C", "sheetfed scanner" },
      },
      {
        KV_S2046C,
        { "", "MATSHITA", "KV-S2046C", "sheetfed scanner" },
      },
      {
        KV_S2028C,
        { "", "MATSHITA", "KV-S2028C", "sheetfed scanner" },
      },
      {
        KV_S2048C,
        { "", "MATSHITA", "KV-S2048C", "sheetfed scanner" },
      },*/
};

SANE_Status
sane_init(SANE_Int __sane_unused__ *version_code,
          SANE_Auth_Callback __sane_unused__ authorize)
{
  DBG_INIT();
  DBG(DBG_INFO, "Brother Scanner WIP driver\n");

  *version_code = SANE_VERSION_CODE(SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD);

  /* Initialize USB */
  sanei_usb_init();

  return SANE_STATUS_GOOD;
}

/*
 * List of available devices, allocated by sane_get_devices, released
 * by sane_exit()
 */
static SANE_Device **devlist = NULL;
static unsigned curr_scan_dev = 0;

void free_devlist(void) {
  if (devlist)
  {
    int i;
    for (i = 0; devlist[i]; i++)
    {
      free((void *)devlist[i]->name);
      free((void *)devlist[i]);
    }
    free((void *)devlist);
    devlist = NULL;
  }
}


void sane_exit(void)
{
  free_devlist();
}

static SANE_Status
attach(SANE_String_Const devname)
{
  DBG(DBG_DBG, "attach() called with devname `%s'\n", devname);
  int i = 0;
  if (devlist)
  {
    for (; devlist[i]; i++)
      ;
    devlist = realloc(devlist, sizeof(SANE_Device *) * (i + 1));
    if (!devlist)
      return SANE_STATUS_NO_MEM;
  }
  else
  {
    devlist = malloc(sizeof(SANE_Device *) * 2);
    if (!devlist)
      return SANE_STATUS_NO_MEM;
  }
  devlist[i] = malloc(sizeof(SANE_Device));
  if (!devlist[i])
    return SANE_STATUS_NO_MEM;
  memcpy(devlist[i], &known_devices[curr_scan_dev].scanner,
         sizeof(SANE_Device));
  devlist[i]->name = strdup(devname);
  /* terminate device list with NULL entry: */
  devlist[i + 1] = 0;
  DBG(DBG_INFO, "%s device attached\n", devname);
  return SANE_STATUS_GOOD;
}

/* Get device list */
SANE_Status
sane_get_devices(const SANE_Device ***device_list,
                 SANE_Bool __sane_unused__ local_only)
{
  DBG(DBG_INFO, "Listing Brother USB devices\n");
  free_devlist();

#ifdef MOCK_DEVICE
  // Add a dummy device
  devlist = malloc (sizeof (SANE_Device *) * 2);
  devlist[0] = malloc (sizeof (SANE_Device));
  devlist[1] = NULL;

  devlist[0]->model = "Dummy DCP";
  devlist[0]->name = "Some device name";
  devlist[0]->type = "All-in-one";
  devlist[0]->vendor = "Brother";

  *device_list = (const SANE_Device **) devlist;
  
#else

  for (curr_scan_dev = 0;
       curr_scan_dev < sizeof(known_devices) / sizeof(known_devices[0]);
       curr_scan_dev++)
  {
    SANE_Status st = sanei_usb_find_devices(BROTHER_ID,
                           known_devices[curr_scan_dev].id, attach);
    if (st)
      return st;
  }
  if (device_list)
    *device_list = (const SANE_Device **)devlist;

#endif  
  return SANE_STATUS_GOOD;
}

/* Open device, return the device handle */
SANE_Status
sane_open(SANE_String_Const devname, SANE_Handle *handle)
{
  DBG(DBG_DBG, "Call to sane_open() with name `%s'\n", devname);
  unsigned i, j, id = 0;
  struct scanner *s;
  SANE_Int h = 0;
  SANE_Status st;
  #ifndef MOCK_DEVICE

  if (!devlist)
  {
    st = sane_get_devices(NULL, 0);
    if (st)
      return st;
  }
  // docs: devname == "" means 'open the first device'
  if (devname[0]) {
    for (i = 0; devlist[i]; i++)
    {
      if (!strcmp(devlist[i]->name, devname))
        break;
    }
    if (!devlist[i])
      return SANE_STATUS_INVAL;
  } else {
    DBG(DBG_DBG, "No devname provided, choose first connected device (if available)\n");
    if (!devlist || !devlist[0])
      return SANE_STATUS_INVAL;
    i = 0;
  }
  for (j = 0; j < sizeof(known_devices) / sizeof(known_devices[0]); j++)
  {
    if (!strcmp(devlist[i]->model, known_devices[j].scanner.model))
    {
      id = known_devices[j].id;
      break;
    }
  }

  DBG(DBG_DBG, "Trying to open `%s'\n", devlist[i]->name);

  st = sanei_usb_open(devlist[i]->name, &h);
  if (st) {
    DBG(DBG_DBG, "sanei_usb_open() failed with code %d\n", st);
    return st;
  }

  // TODO test: for some odd reason, sanei_usb_open() claims the interface,
  // which we don't want. See if releasing fixes the problems
  st = sanei_usb_release_interface(h, INTERFACE_INDEX);
  if (st) {
    DBG(DBG_DBG, "Failed to release interface after sanei_usb_open() with code %d\n", st);
    sanei_usb_close(h);
  }


  #endif

  s = malloc(sizeof(struct scanner));
  if (!s)
    return SANE_STATUS_NO_MEM;
  memset(s, 0, sizeof(struct scanner));
  s->usb_read_buffer = malloc(MAX_READ_DATA_SIZE);
  if (!s->usb_read_buffer)
    return SANE_STATUS_NO_MEM;
  s->file = h;
  s->device_id = id;

  bropen_init_options(s);

  *handle = s;

  return SANE_STATUS_GOOD;
}

/* Close device */
void sane_close(SANE_Handle handle)
{
  struct scanner *s = (struct scanner *)handle;
  int i;
  sanei_usb_release_interface(s->file, INTERFACE_INDEX);
  sanei_usb_close(s->file);

  // Options (TODO for later)
  for (i = 1; i < NUM_OPTIONS; i++)
  {
    if (s->opt[i].type == SANE_TYPE_STRING && s->val[i].s)
      free(s->val[i].s);
  }
  if (s->scanline_buffer)
    free(s->scanline_buffer);
  if (s->usb_read_buffer)
    free(s->usb_read_buffer);
  free(s);
}

/* Get option descriptor */
const SANE_Option_Descriptor *
sane_get_option_descriptor(SANE_Handle handle, SANE_Int option)
{
  struct scanner *s = handle;

  if ((unsigned)option >= NUM_OPTIONS || option < 0)
    return NULL;
  return s->opt + option;
}

/* Start scanning */
SANE_Status
sane_start(SANE_Handle handle)
{
  struct scanner *s = (struct scanner *)handle;
  SANE_Status st = SANE_STATUS_GOOD;

  s->cancelled = SANE_FALSE;


  DBG(DBG_DBG, "Claiming interface\n");
  st = sanei_usb_claim_interface(s->file, INTERFACE_INDEX);
  if (st)
    goto exit;

  // TODO migrate code here as apropriate
  bropen_init_window(s);

  st = activate_scanner(s);
  if (st)
    goto exit;
  
  DBG(DBG_DBG, "Scanner activation successful.\n");

  st = send_format_inquiry(s);

  read_response_3_attempts(s);

  struct ScanInquiryResponse response;
  if (!parseScanInquiryResponse(s->usb_read_buffer, &response))
    goto exit;

  /* This values appear to always be the same, and I don't know its interpretation. Maybe double/single sided support?  */
  if (response.unknown_2 != 2) {
    log_error_with_hexdump(DBG_WARN, "Warning: Unexpected return values in format description:\n%s\n", s->usb_read_buffer, 255);
  }

  update_window_from_scanner_response(s, &response);

  usleep(200 * 1000); // this delay is taken from the original Windows driver

  send_begin_scan_command(s);

  s->read_from_device_complete = SANE_FALSE;

  // FIXME test if it works with tempfile now

  // s->temp_file = tmpfile();
  s->temp_file = fopen("../dump.bin", "w+");
  s->total_lines_written = 0;

  s->window.bytes_per_line = s->window.lower_right_x - s->window.upper_left_x;
  if (s->window.image_composition == COLOR_MODE_RGB)
    s->window.bytes_per_line *= 3;

  return SANE_STATUS_GOOD;

  SANE_Status st_deactivate;
exit:
  st_deactivate = deactivate_scanner(handle);
  if (st_deactivate)
    DBG(DBG_ERR, "Error handler encountered an additional error while deactivating scanner: %d\n", st);
  return st;
}


/**
 * TODO implement new structure:
 * - Keep reading entire file to disk, though this clearly isn't needed for grayscale
 * - Phases (RGB):
 *   1. Load whole data into file
 *   2. fill scanline buffer (can run multiple times)
 *   3. empty scanline buffer, fill output buffer
 * - Phases (Grayscale):
 *    - 1: fill USB buffer
 *    - 2: empty USB buffer, fill scanline buffer (can run multiple times)
 *    - 3: empty scanline buffer, fill output buffer (can run multiple times)
 * - we skip phase 1 and/or 2 if their respective target buffer is not empty
 */

SANE_Status read_phase1(struct scanner *s) {
  SANE_Status st;
  DBG(DBG_DBG, "Phase 1: Read data into file\n");
  while (!s->read_from_device_complete) {
    while (!s->cancelled) {
      s->usb_buffer_size = MAX_READ_DATA_SIZE;
      st = sanei_usb_read_bulk(s->file, s->usb_read_buffer, &s->usb_buffer_size);
      /* The second case shouldn't happen, we still check to make sure */
      if (st == SANE_STATUS_EOF || st == SANE_STATUS_GOOD) {
        if (s->usb_buffer_size == 0) {
          usleep(5*1000); // wait 5ms until next packet
          continue;
        } else
          break;
      }
      /* If we get here, we must have an error */
      return st;
    }
    if (s->cancelled)
      return SANE_STATUS_CANCELLED;

    /* This code should be unreachable because we should always get an "end of page" packet first,
     we leave it in to be sure */
    if (s->usb_buffer_size == 1) {
      DBG(DBG_ERR, "Final packet arrived before end of page packet\n");
      return SANE_STATUS_IO_ERROR;
    }

    const size_t END_OF_PAGE_HEADER_LEN = 10;
    if (s->usb_buffer_size == END_OF_PAGE_HEADER_LEN) {
      log_error_with_hexdump(DBG_DBG, "Received end of page (hexdump: %s)\n", s->usb_read_buffer, END_OF_PAGE_HEADER_LEN);
      // TODO do something here - especially for button-induced multi-page scans
      while (!s->cancelled) {
        s->usb_buffer_size = MAX_READ_DATA_SIZE;
        st = sanei_usb_read_bulk(s->file, s->usb_read_buffer, &s->usb_buffer_size);
        if (st == SANE_STATUS_EOF) {
          usleep(5000);
          continue;
        }
        DBG(DBG_DBG, "Read final byte\n");
        if (s->usb_read_buffer[0] != 0x80)
          return SANE_STATUS_IO_ERROR;
        else {
          DBG(DBG_DBG, "Read from device complete, returning data to frontend\n");
          s->read_from_device_complete = SANE_TRUE;
          int file_error = fseek(s->temp_file, 0, SEEK_SET);
          if (file_error) {
            DBG(DBG_ERR, "fseek() failed: %d\n", file_error);
            return SANE_STATUS_IO_ERROR;
          }
          return SANE_STATUS_GOOD;
        }
      }
    }
    if (s->cancelled)
      return SANE_STATUS_CANCELLED;

    if (s->usb_buffer_size < DATA_HEADER_SIZE) {
      DBG(DBG_ERR, "Received package of unexpected length: %ld\n", s->usb_buffer_size);
      return SANE_STATUS_IO_ERROR;
    }

    DBG(DBG_DBG, "Received USB package of size %ld\n", s->usb_buffer_size);

    s->usb_buffer_read_head = 0;
    while (s->usb_buffer_read_head < s->usb_buffer_size) {

      struct DataHeader header;
      st = parse_data_header(&s->usb_read_buffer[s->usb_buffer_read_head], &header);
      if (st)
        return st;
      /* The read buffer must be re-parsed on the next occasion, so we only advance the read head
         once we start copying the data */
      s->usb_buffer_read_head += DATA_HEADER_SIZE;
      // DBG(DBG_DBG, "Parsed data block of length %d (type %02x, row number %d)\n", header.packet_size, header.packet_type, header.row);

      if (fwrite(&s->usb_read_buffer[s->usb_buffer_read_head], 1, header.packet_size, s->temp_file) < header.packet_size) {
        DBG(DBG_ERR, "Error writing to temp file: %d\n", ferror(s->temp_file));
        return SANE_STATUS_IO_ERROR;
      }
      s->usb_buffer_read_head += header.packet_size;
      s->window.actual_image_height += 1;
      if (s->window.format_mode == SCAN_FORMAT_NONE) {
        /* In SCAN_FORMAT_NONE every packet contains exactly one scanline */
        s->window.bytes_per_line = header.packet_size;
        s->window.actual_image_width = header.packet_size;
      if (s->window.image_composition == COLOR_MODE_RGB)
        s->window.actual_image_width /= 3;
      }
    }
  }
  return SANE_STATUS_GOOD;
}

SANE_Status check_image_expected_size(struct scanner *s) {
  int32_t expected_width = s->window.lower_right_x - s->window.upper_left_x;
  int32_t expected_height = s->window.lower_right_y - s->window.upper_left_y;
  if ((int32_t)s->window.actual_image_width != expected_width) {
    /* padding / truncating in x does not appear to ever be needed */
    DBG(DBG_ERR, "Invalid size: Expected a width of %d, got %ld",
        expected_width, s->window.actual_image_width);
    return SANE_STATUS_IO_ERROR;
  }
  if ((int32_t)s->window.actual_image_height < expected_height)
    DBG(DBG_WARN, "Requested %d lines, got %ld lines. The last line will be repeated\n",
        expected_height, s->window.actual_image_height);
  else if ((int32_t)s->window.actual_image_height > expected_height)
    DBG(DBG_WARN, "Requested %d lines, got %ld lines. The extra lines will be truncated\n",
        expected_height, s->window.actual_image_height);
  else
    DBG(DBG_DBG, "Requested %d lines, got %ld lines\n",
        expected_height, s->window.actual_image_height);
  return SANE_STATUS_GOOD;
}

SANE_Status prepare_phase_2(struct scanner *s) {
  if (s->window.format_mode == SCAN_FORMAT_JPEG)
  {
    struct jpeg_decompress_struct *cinfo;
    // JPEG setup
    cinfo = malloc(sizeof(struct jpeg_decompress_struct));
    s->jpeg.cinfo = cinfo;
    struct jpeg_error_mgr *jerr = malloc(sizeof(struct jpeg_error_mgr));
    // A cleaner solution would likely define its own error handler
    cinfo->err = jpeg_std_error(jerr); // TODO the error handling must, at the very least, be moved to DBG() instead
    jpeg_create_decompress(cinfo);
    jpeg_stdio_src(cinfo, s->temp_file);
    if (jpeg_read_header(cinfo, TRUE) != JPEG_HEADER_OK)
    {
      DBG(DBG_ERR, "Invalid JPEG header\n");
      return SANE_STATUS_IO_ERROR;
    }
    jpeg_start_decompress(cinfo);
    s->window.actual_image_width = cinfo->image_width;
    s->window.actual_image_height = cinfo->image_height;
  }
  SANE_Status st = check_image_expected_size(s);
  if (st)
    return st;
  s->scanline_buffer = malloc(s->window.bytes_per_line);
  if (!s->scanline_buffer) {
    return SANE_STATUS_NO_MEM;
  }
  return SANE_STATUS_GOOD;
}


SANE_Status read_phase2(struct scanner *s) {
  if (s->total_lines_written >= s->window.actual_image_height)
    /* Do not send EOF because we may have to repeat the last line a few times */
    return SANE_STATUS_GOOD;
  if (s->window.format_mode == SCAN_FORMAT_NONE)
  {
    /* raw image data */
    int data_read = fread(s->scanline_buffer, 1, s->window.bytes_per_line, s->temp_file);
    int file_error = ferror(s->temp_file);
    if (file_error) {
      DBG(DBG_ERR, "Error reading from temp file: %d\n", file_error);
      return SANE_STATUS_IO_ERROR;
    }
    if (data_read < (int)s->window.bytes_per_line) {
      DBG(DBG_ERR, "Incomplete read from temp file\n");
      return SANE_STATUS_IO_ERROR;
    }
  } else {
    struct jpeg_decompress_struct *cinfo  = s->jpeg.cinfo;
    if (cinfo->output_scanline < cinfo->output_height)
    {
      // // super simple solution for now: just return one scanline at a time
      // // TODO size check
      // size_t bytes_per_row = cinfo->output_width * cinfo->actual_number_of_colors;
      // if (max_len < bytes_per_row)
      // {
      //   // TODO this restriction could be lifted quite easily, leave it in for now
      //   DBG(DBG_ERR, "Must provide a buffer at least as long as one scanline\n");
      //   goto io_error;
      // }
      jpeg_read_scanlines(cinfo, &s->scanline_buffer, 1);
    }
  }
  s->scanline_buffer_bytes_left = s->window.bytes_per_line;
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_read(SANE_Handle handle, SANE_Byte *buf,
          SANE_Int max_len, SANE_Int *len)
{
  SANE_Status st = SANE_STATUS_GOOD;
  *len = 0;
  struct scanner *s = (struct scanner *)handle;

  if (!s->read_from_device_complete) {
    st = read_phase1(s);
    if (st)
      goto exit;
    st = prepare_phase_2(s);
    if (st)
      goto exit;
  }

  *len = 0;
  
  while (!s->cancelled
         && (int32_t)s->total_lines_written < (int32_t)s->window.lower_right_y - (int32_t)s->window.upper_left_y) {
    if (s->scanline_buffer_bytes_left == 0) {
      // Fill one scanline
      st = read_phase2(s);
      if (st)
        goto exit;
    }
    SANE_Int target_buffer_space_left =  max_len - *len;
    if ((SANE_Int)s->scanline_buffer_bytes_left > target_buffer_space_left) {
      if (*len > 0) {
        /* At least one full scanline was copied to the buffer */
        DBG(DBG_DBG, "Buffer full at scanline %ld, exiting until next read call\n", s->total_lines_written);
        return SANE_STATUS_GOOD;
      } else {
        /* One scanline contains more data than what fits into buf */
        /* TODO This code is untested - try a fake scan with more than 10k pixels width */
        memcpy(
          &buf[*len],
          &s->scanline_buffer[s->window.bytes_per_line - s->scanline_buffer_bytes_left],
          target_buffer_space_left
        );
        *len = max_len;
        s->scanline_buffer_bytes_left -= target_buffer_space_left;
        DBG(DBG_DBG, "Transferring %d bytes at scanline %ld\n", *len, s->total_lines_written);
        return SANE_STATUS_GOOD;
      }
    } else {
      /* Copy a full (or the leftovers of a full) scanline to buf */
      memcpy(
        &buf[*len],
        &s->scanline_buffer[s->window.bytes_per_line - s->scanline_buffer_bytes_left],
        s->scanline_buffer_bytes_left
      );
      *len += s->scanline_buffer_bytes_left;
      s->scanline_buffer_bytes_left = 0;
      s->total_lines_written += 1;
    }
  }
  if (s->cancelled) {
    st = SANE_STATUS_CANCELLED;
    goto exit;
  }

  if (*len == 0) {
    /* If we get here, all the lines must have been written */
    st = SANE_STATUS_EOF;
    goto exit;
  }

  /* We get here when we write the final scanline to the caller */
  return SANE_STATUS_GOOD;

  SANE_Status st_deactivate;
exit:
  st_deactivate = deactivate_scanner(handle);
  if (st_deactivate)
    DBG(DBG_ERR, "Error handler encountered an additional error while deactivating scanner: %d\n", st_deactivate);

  if (s->temp_file)
  {
    int err = fclose(s->temp_file);
    if (err)
      DBG(DBG_ERR, "Error %d while closing temporary file\n", err);
    s->temp_file = NULL;
  }

  if (s->jpeg.cinfo)
  {
    jpeg_finish_decompress(s->jpeg.cinfo);
    jpeg_destroy_decompress(s->jpeg.cinfo);
    free(s->jpeg.cinfo);
    s->jpeg.cinfo = NULL;
  }

  if (s->scanline_buffer) {
    free(s->scanline_buffer);
    s->scanline_buffer = NULL;
  }

  return st;
}

// Originally, this function is called with NULL to populate some fields of s
SANE_Status sane_get_parameters(SANE_Handle handle, SANE_Parameters *params)
{
  struct scanner *s = (struct scanner *)handle;
  SANE_Parameters *p = &s->params;

  p->last_frame = SANE_TRUE; // only relevant if separate R/G/B pictures are provided
  p->pixels_per_line = s->window.lower_right_x - s->window.upper_left_x;
  p->bytes_per_line = s->window.bytes_per_line;
  if (s->window.image_composition == COLOR_MODE_RGB)
    p->format = SANE_FRAME_RGB;
  else
    p->format = SANE_FRAME_GRAY;
  p->lines = s->window.lower_right_y - s->window.upper_left_y; /* unfortunately, this is not always exact */
  p->depth = 8;

  DBG(DBG_DBG, "get_parameters(): width = %d, height = %d\n", p->pixels_per_line, p->lines);

  if (params)
    memcpy(params, p, sizeof(SANE_Parameters));
  return SANE_STATUS_GOOD;
}

void sane_cancel(SANE_Handle handle)
{
  struct scanner *s = (struct scanner *)handle;
  s->cancelled = SANE_TRUE;
}

SANE_Status
sane_set_io_mode(SANE_Handle __sane_unused__ h, SANE_Bool __sane_unused__ m)
{
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status
sane_get_select_fd(SANE_Handle __sane_unused__ h,
                   SANE_Int __sane_unused__ *fd)
{
  return SANE_STATUS_UNSUPPORTED;
}
