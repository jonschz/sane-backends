/*
   TODO add copyright + info
*/

#include "../include/sane/config.h"

#include <string.h>

#define DEBUG_DECLARE_ONLY
#define BACKEND_NAME bropen

#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei.h"
#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"
#include "../include/lassert.h"

#include "bropen.h"
#include "bropen_cmd.h"

#include <stdlib.h>

static size_t
max_string_size(SANE_String_Const strings[])
{
  size_t size, max_size = 0;
  SANE_Int i;

  for (i = 0; strings[i]; ++i)
  {
    size = strlen(strings[i]) + 1;
    if (size > max_size)
      max_size = size;
  }
  return max_size;
}

static SANE_String_Const scan_modes_ui[] = {
    SANE_VALUE_SCAN_MODE_COLOR,
    SANE_VALUE_SCAN_MODE_GRAY,
    SANE_VALUE_SCAN_MODE_HALFTONE,
    SANE_VALUE_SCAN_MODE_LINEART,
    NULL};

static const SANE_Range resolutions_range = {100, 1200, 10};

/* List of paper sizes */
static SANE_String_Const paper_list[] = {
    SANE_I18N("user_def"),
    SANE_I18N("business_card"),
    /*SANE_I18N("Check"), */
    /*SANE_I18N ("A3"), */
    SANE_I18N("A4"),
    SANE_I18N("A5"),
    SANE_I18N("A6"),
    SANE_I18N("Letter"),
    /*SANE_I18N ("Double letter 11x17 in"),
       SANE_I18N ("B4"), */
    SANE_I18N("B5"),
    SANE_I18N("B6"),
    SANE_I18N("Legal"),
    NULL};
static const unsigned paper_val[] = {0, 1, 4, 5, 6, 7, 13, 14, 15};
struct paper_size
{
  int width;
  int height;
};
static const struct paper_size paper_sizes[] = {
    {210, 297},                /* User defined, default=A4 */
    {54, 90},                  /* Business card */
    /*{80, 170},            */ /* Check (China business) */
    /*{297, 420}, */           /* A3 */
    {210, 297},                /* A4 */
    {148, 210},                /* A5 */
    {105, 148},                /* A6 */
    {215, 280},                /* US Letter 8.5 x 11 in */
    /*{280, 432}, */           /* Double Letter 11 x 17 in */
    /*{250, 353}, */           /* B4 */
    {176, 250},                /* B5 */
    {125, 176},                /* B6 */
    {215, 355}                 /* US Legal */
};

// TODO update these
#define MIN_WIDTH 51
#define MAX_WIDTH 215
#define MIN_LENGTH 70
#define MAX_LENGTH 355
static SANE_Range tl_x_range = {0, MAX_WIDTH - MIN_WIDTH, 0};
static SANE_Range tl_y_range = {0, MAX_LENGTH - MIN_LENGTH, 0};
static SANE_Range br_x_range = {MIN_WIDTH, MAX_WIDTH, 0};
static SANE_Range br_y_range = {MIN_LENGTH, MAX_LENGTH, 0};
static SANE_Range byte_value_range = {0, 255, 0};

static SANE_Range param_100_range = {0, 100, 0};


/* Reset the options for that scanner. */
void bropen_init_options(struct scanner *s)
{
  int i;
  SANE_Option_Descriptor *o;
  /* Pre-initialize the options. */
  memset(s->opt, 0, sizeof(s->opt));
  memset(s->val, 0, sizeof(s->val));

  for (i = 0; i < NUM_OPTIONS; i++)
  {
    s->opt[i].size = sizeof(SANE_Word);
    s->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  }

  /* Number of options. */
  o = &s->opt[NUM_OPTS];
  o->name = "";
  o->title = SANE_TITLE_NUM_OPTIONS;
  o->desc = SANE_DESC_NUM_OPTIONS;
  o->type = SANE_TYPE_INT;
  o->cap = SANE_CAP_SOFT_DETECT;
  s->val[NUM_OPTS].w = NUM_OPTIONS;

  /* Mode group */
  o = &s->opt[MODE_GROUP];
  o->title = SANE_I18N("Scan Mode");
  o->desc = ""; /* not valid for a group */
  o->type = SANE_TYPE_GROUP;
  o->cap = 0;
  o->size = 0;
  o->constraint_type = SANE_CONSTRAINT_NONE;

  /* Scanner supported modes */
  o = &s->opt[MODE];
  o->name = SANE_NAME_SCAN_MODE;
  o->title = SANE_TITLE_SCAN_MODE;
  o->desc = SANE_DESC_SCAN_MODE;
  o->type = SANE_TYPE_STRING;
  o->size = max_string_size(scan_modes_ui);
  o->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  o->constraint.string_list = scan_modes_ui;
  s->val[MODE].s = malloc(o->size);
  strcpy(s->val[MODE].s, scan_modes_ui[0]); /* default to RGB */

  /* X and Y resolution */
  o = &s->opt[RESOLUTION];
  o->name = SANE_NAME_SCAN_RESOLUTION;
  o->title = SANE_TITLE_SCAN_RESOLUTION;
  o->desc = SANE_DESC_SCAN_RESOLUTION;
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_DPI;
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &resolutions_range;
  s->val[RESOLUTION].w = 150;

  /* Duplex */
  o = &s->opt[DUPLEX];
  o->name = "duplex";
  o->title = SANE_I18N("Duplex");
  o->desc = SANE_I18N("Enable Duplex (Dual-Sided) Scanning");
  o->type = SANE_TYPE_BOOL;
  o->unit = SANE_UNIT_NONE;
  s->val[DUPLEX].w = SANE_FALSE;

  /*FIXME
     if (!s->support_info.support_duplex)
     o->cap |= SANE_CAP_INACTIVE;
   */

/*
  o = &s->opt[FEEDER_MODE];
  o->name = "feeder-mode";
  o->title = SANE_I18N("Feeder mode");
  o->desc = SANE_I18N("Sets the feeding mode");
  o->type = SANE_TYPE_STRING;
  o->size = max_string_size(feeder_mode_list);
  o->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  o->constraint.string_list = feeder_mode_list;
  s->val[FEEDER_MODE].s = malloc(o->size);
  strcpy(s->val[FEEDER_MODE].s, feeder_mode_list[0]);

  o = &s->opt[LENGTHCTL];
  o->name = "length-control";
  o->title = SANE_I18N("Length control mode");
  o->desc =
      SANE_I18N("Length Control Mode causes the scanner to read the shorter of either the length of the actual"
                " paper or logical document length.");
  o->type = SANE_TYPE_BOOL;
  o->unit = SANE_UNIT_NONE;
  s->val[LENGTHCTL].w = SANE_FALSE;


  o = &s->opt[MANUALFEED];
  o->name = "manual-feed";
  o->title = SANE_I18N("Manual feed mode");
  o->desc = SANE_I18N("Sets the manual feed mode");
  o->type = SANE_TYPE_STRING;
  o->size = max_string_size(manual_feed_list);
  o->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  o->constraint.string_list = manual_feed_list;
  s->val[MANUALFEED].s = malloc(o->size);
  strcpy(s->val[MANUALFEED].s, manual_feed_list[0]);


  o = &s->opt[FEED_TIMEOUT];
  o->name = "feed-timeout";
  o->title = SANE_I18N("Manual feed timeout");
  o->desc = SANE_I18N("Sets the manual feed timeout in seconds");
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_NONE;
  o->size = sizeof(SANE_Int);
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &(byte_value_range);
  o->cap |= SANE_CAP_INACTIVE;
  s->val[FEED_TIMEOUT].w = 30;

  o = &s->opt[DBLFEED];
  o->name = "double-feed";
  o->title = SANE_I18N("Double feed detection");
  o->desc = SANE_I18N("Enable/Disable double feed detection");
  o->type = SANE_TYPE_BOOL;
  o->unit = SANE_UNIT_NONE;
  s->val[DBLFEED].w = SANE_FALSE;

  o = &s->opt[FIT_TO_PAGE];
  o->name = SANE_I18N("fit-to-page");
  o->title = SANE_I18N("Fit to page");
  o->desc = SANE_I18N("Scanner shrinks image to fit scanned page");
  o->type = SANE_TYPE_BOOL;
  o->unit = SANE_UNIT_NONE;
  s->val[FIT_TO_PAGE].w = SANE_FALSE;
*/

  /* Geometry group */
  o = &s->opt[GEOMETRY_GROUP];
  o->title = SANE_I18N("Geometry");
  o->desc = ""; /* not valid for a group */
  o->type = SANE_TYPE_GROUP;
  o->cap = 0;
  o->size = 0;
  o->constraint_type = SANE_CONSTRAINT_NONE;

  /* Paper sizes list */
  o = &s->opt[PAPER_SIZE];
  o->name = "paper-size";
  o->title = SANE_I18N("Paper size");
  o->desc = SANE_I18N("Physical size of the paper in the ADF");
  o->type = SANE_TYPE_STRING;
  o->size = max_string_size(paper_list);
  o->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  o->constraint.string_list = paper_list;
  s->val[PAPER_SIZE].s = malloc(o->size);
  strcpy(s->val[PAPER_SIZE].s, SANE_I18N("A4"));

  /* Landscape */
  o = &s->opt[LANDSCAPE];
  o->name = "landscape";
  o->title = SANE_I18N("Landscape");
  o->desc =
      SANE_I18N("Set paper position : "
                "true for landscape, false for portrait");
  o->type = SANE_TYPE_BOOL;
  o->unit = SANE_UNIT_NONE;
  s->val[LANDSCAPE].w = SANE_FALSE;
  o->cap |= SANE_CAP_INACTIVE;

  /* Upper left X */
  o = &s->opt[TL_X];
  o->name = SANE_NAME_SCAN_TL_X;
  o->title = SANE_TITLE_SCAN_TL_X;
  o->desc = SANE_DESC_SCAN_TL_X;
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_MM;
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &tl_x_range;
  o->cap |= SANE_CAP_INACTIVE;
  s->val[TL_X].w = 0;

  /* Upper left Y */
  o = &s->opt[TL_Y];
  o->name = SANE_NAME_SCAN_TL_Y;
  o->title = SANE_TITLE_SCAN_TL_Y;
  o->desc = SANE_DESC_SCAN_TL_Y;
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_MM;
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &tl_y_range;
  o->cap |= SANE_CAP_INACTIVE;
  s->val[TL_Y].w = 0;

  /* Bottom-right x */
  o = &s->opt[BR_X];
  o->name = SANE_NAME_SCAN_BR_X;
  o->title = SANE_TITLE_SCAN_BR_X;
  o->desc = SANE_DESC_SCAN_BR_X;
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_MM;
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &br_x_range;
  o->cap |= SANE_CAP_INACTIVE;
  s->val[BR_X].w = 210;

  /* Bottom-right y */
  o = &s->opt[BR_Y];
  o->name = SANE_NAME_SCAN_BR_Y;
  o->title = SANE_TITLE_SCAN_BR_Y;
  o->desc = SANE_DESC_SCAN_BR_Y;
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_MM;
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &br_y_range;
  o->cap |= SANE_CAP_INACTIVE;
  s->val[BR_Y].w = 291; // returned height value is 291

  /* Enhancement group */
  o = &s->opt[ADVANCED_GROUP];
  o->title = SANE_I18N("Advanced");
  o->desc = ""; /* not valid for a group */
  o->type = SANE_TYPE_GROUP;
  o->cap = SANE_CAP_ADVANCED;
  o->size = 0;
  o->constraint_type = SANE_CONSTRAINT_NONE;

  /* Brightness */
  o = &s->opt[BRIGHTNESS];
  o->name = SANE_NAME_BRIGHTNESS;
  o->title = SANE_TITLE_BRIGHTNESS;
  o->desc = SANE_DESC_BRIGHTNESS;
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_NONE;
  o->size = sizeof(SANE_Int);
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &(param_100_range);
  s->val[BRIGHTNESS].w = 50;

  /* Contrast */
  o = &s->opt[CONTRAST];
  o->name = SANE_NAME_CONTRAST;
  o->title = SANE_TITLE_CONTRAST;
  o->desc = SANE_DESC_CONTRAST;
  o->type = SANE_TYPE_INT;
  o->unit = SANE_UNIT_NONE;
  o->size = sizeof(SANE_Int);
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &(param_100_range);
  s->val[CONTRAST].w = 50;

  /* threshold */
  o = &s->opt[THRESHOLD];
  o->name = SANE_NAME_THRESHOLD;
  o->title = SANE_TITLE_THRESHOLD;
  o->desc = SANE_DESC_THRESHOLD;
  o->type = SANE_TYPE_INT;
  o->size = sizeof(SANE_Int);
  o->constraint_type = SANE_CONSTRAINT_RANGE;
  o->constraint.range = &(byte_value_range);
  s->val[THRESHOLD].w = 128;

/*
  o = &s->opt[IMAGE_EMPHASIS];
  o->name = "image-emphasis";
  o->title = SANE_I18N("Image emphasis");
  o->desc = SANE_I18N("Sets the image emphasis");
  o->type = SANE_TYPE_STRING;
  o->size = max_string_size(image_emphasis_list);
  o->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  o->constraint.string_list = image_emphasis_list;
  s->val[IMAGE_EMPHASIS].s = malloc(o->size);
  strcpy(s->val[IMAGE_EMPHASIS].s, image_emphasis_list[0]);

  o = &s->opt[GAMMA_CORRECTION];
  o->name = "gamma-cor";
  o->title = SANE_I18N("Gamma correction");
  o->desc = SANE_I18N("Gamma correction");
  o->type = SANE_TYPE_STRING;
  o->size = max_string_size(gamma_list);
  o->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  o->constraint.string_list = gamma_list;
  s->val[GAMMA_CORRECTION].s = malloc(o->size);
  strcpy(s->val[GAMMA_CORRECTION].s, gamma_list[0]);

  o = &s->opt[LAMP];
  o->name = "lamp-color";
  o->title = SANE_I18N("Lamp color");
  o->desc = SANE_I18N("Sets the lamp color (color dropout)");
  o->type = SANE_TYPE_STRING;
  o->size = max_string_size(lamp_list);
  o->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  o->constraint.string_list = lamp_list;
  s->val[LAMP].s = malloc(o->size);
  strcpy(s->val[LAMP].s, lamp_list[0]);
  */

  // TODO WIP: sanebd integration
  o = &s->opt[OPT_SENSORS_GROUP];
  o->name  = SANE_NAME_SENSORS;
  o->title = SANE_TITLE_SENSORS;
  o->type  = SANE_TYPE_GROUP;
  o->desc  = SANE_DESC_SENSORS;
  o->size  = 0;

  // TODO not yet working
  o = &s->opt[OPT_SENSOR_SCAN];
  o->name  = SANE_NAME_SCAN;
  o->title = SANE_TITLE_SCAN;
  o->desc  = SANE_DESC_SCAN;
  // works
  o->type  = SANE_TYPE_STRING;
  o->size = 2;
  // This should work, but doesn't (scanbd bug?)
  // o->type  = SANE_TYPE_BOOL;
  // o->size  = sizeof(SANE_Word);
  o->cap   = SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;

  o = &s->opt[OPT_SENSOR_EMAIL];
  o->name  = SANE_NAME_EMAIL;
  o->title = SANE_TITLE_EMAIL;
  o->desc  = SANE_DESC_EMAIL;
  o->type  = SANE_TYPE_STRING;
  o->cap   = SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  o->size = 8;
}

/* Lookup a string list from one array and return its index. */
static int
str_index(const SANE_String_Const *list, SANE_String_Const name)
{
  int index;
  index = 0;
  while (list[index])
  {
    if (!strcmp(list[index], name))
      return (index);
    index++;
  }
  return (-1); /* not found */
}

/* Control option */
SANE_Status
sane_control_option(SANE_Handle handle, SANE_Int option,
                    SANE_Action action, void *val, SANE_Int *info)
{
  int i;
  SANE_Status status;
  SANE_Word cap;
  struct scanner *s = (struct scanner *)handle;

  if (info)
    *info = 0;

  if (option < 0 || option >= NUM_OPTIONS)
    return SANE_STATUS_UNSUPPORTED;

  cap = s->opt[option].cap;
  if (!SANE_OPTION_IS_ACTIVE(cap))
    return SANE_STATUS_UNSUPPORTED;

  if (action == SANE_ACTION_GET_VALUE)
  {
    // TODO: WIP
    switch (option)
    {
    case OPT_SENSOR_SCAN:
      // FIXME temp code
      update_button_state(s);
      DBG(DBG_DBG_MORE, "reading scan option (last button: %d)\n", s->last_button_pressed);
      if (s->last_button_pressed != 0)
      {
        // TODO handle different values for different buttons
        // This works for some very odd reason when the type is set to STRING
        // It seems like only the length of the string matters; "0" works too
        strcpy(val, "1");
      } else {
        // could probably also just memset one byte to zero
        strcpy(val, "");
      }
      // *(SANE_Word *)val = '1';


      // *(SANE_Bool *)val = SANE_TRUE;
      // SANE_Bool result = SANE_TRUE;
      // memcpy(val, &result, sizeof(result));
      break;
    case OPT_SENSOR_EMAIL:
      // FIXME temp code
      DBG(DBG_DBG_MORE, "reading email option\n");
      // strcpy(val, SANE_NAME_EMAIL);
      strcpy(val, "");
      break;
    
    default:
      if (s->opt[option].type == SANE_TYPE_STRING)
      {
        DBG(DBG_INFO, "sane_control_option: reading opt[%d] =  %s\n",
            option, s->val[option].s);
        strcpy(val, s->val[option].s);
      }
      else
      {
        *(SANE_Word *)val = s->val[option].w;
        DBG(DBG_INFO, "sane_control_option: reading opt[%d] =  %d\n",
            option, s->val[option].w);
      }
    }

    return SANE_STATUS_GOOD;
  }
  else if (action == SANE_ACTION_SET_VALUE)
  {
    if (!SANE_OPTION_IS_SETTABLE(cap))
      return SANE_STATUS_INVAL;

    status = sanei_constrain_value(s->opt + option, val, info);
    if (status != SANE_STATUS_GOOD)
      return status;

    if (s->opt[option].type == SANE_TYPE_STRING)
    {
      if (!strcmp(val, s->val[option].s))
        return SANE_STATUS_GOOD;
      DBG(DBG_INFO, "sane_control_option: writing opt[%d] =  %s\n",
          option, (SANE_String_Const)val);
    }
    else
    {
      if (*(SANE_Word *)val == s->val[option].w)
        return SANE_STATUS_GOOD;
      DBG(DBG_INFO, "sane_control_option: writing opt[%d] =  %d\n",
          option, *(SANE_Word *)val);
    }

    switch (option)
    {
      /* Side-effect options */
    case RESOLUTION:
      s->val[option].w = *(SANE_Word *)val;
      if (info)
        *info |= SANE_INFO_RELOAD_PARAMS;
      return SANE_STATUS_GOOD;

    case TL_Y:
      if ((*(SANE_Word *)val) + MIN_LENGTH <= s->val[BR_Y].w)
      {
        s->val[option].w = *(SANE_Word *)val;
        if (info)
          *info |= SANE_INFO_RELOAD_PARAMS;
      }
      else if (info)
        *info |= SANE_INFO_INEXACT;
      return SANE_STATUS_GOOD;
    case BR_Y:
      if ((*(SANE_Word *)val) >= s->val[TL_Y].w + MIN_LENGTH)
      {
        s->val[option].w = *(SANE_Word *)val;
        if (info)
          *info |= SANE_INFO_RELOAD_PARAMS;
      }
      else if (info)
        *info |= SANE_INFO_INEXACT;
      return SANE_STATUS_GOOD;

    case TL_X:
      if ((*(SANE_Word *)val) + MIN_WIDTH <= s->val[BR_X].w)
      {
        s->val[option].w = *(SANE_Word *)val;
        if (info)
          *info |= SANE_INFO_RELOAD_PARAMS;
      }
      else if (info)
        *info |= SANE_INFO_INEXACT;
      return SANE_STATUS_GOOD;

    case BR_X:
      if (*(SANE_Word *)val >= s->val[TL_X].w + MIN_WIDTH)
      {
        s->val[option].w = *(SANE_Word *)val;
        if (info)
          *info |= SANE_INFO_RELOAD_PARAMS;
      }
      else if (info)
        *info |= SANE_INFO_INEXACT;
      return SANE_STATUS_GOOD;

    case LANDSCAPE:
      s->val[option].w = *(SANE_Word *)val;
      if (info)
        *info |= SANE_INFO_RELOAD_PARAMS;
      return SANE_STATUS_GOOD;

      /* Side-effect free options */
    case CONTRAST:
    case BRIGHTNESS:
    case DUPLEX:
/*    case LENGTHCTL:
    case DBLFEED:
    case FIT_TO_PAGE: */
    case THRESHOLD:
      s->val[option].w = *(SANE_Word *)val;
      return SANE_STATUS_GOOD;

    case MODE:
      // TODO change / remove these as needed
      strcpy(s->val[MODE].s, val);
      if (!strcmp(s->val[MODE].s, SANE_VALUE_SCAN_MODE_LINEART))
      {
        s->opt[THRESHOLD].cap &= ~SANE_CAP_INACTIVE;
        // s->opt[GAMMA_CORRECTION].cap |= SANE_CAP_INACTIVE;
        s->opt[BRIGHTNESS].cap |= SANE_CAP_INACTIVE;
      }
      else
      {
        s->opt[THRESHOLD].cap |= SANE_CAP_INACTIVE;
        // s->opt[GAMMA_CORRECTION].cap &= ~SANE_CAP_INACTIVE;
        s->opt[BRIGHTNESS].cap &= ~SANE_CAP_INACTIVE;
      }
      if (info)
        *info |= SANE_INFO_RELOAD_OPTIONS | SANE_INFO_RELOAD_PARAMS;

      return SANE_STATUS_GOOD;

    /*
    case MANUALFEED:
      strcpy(s->val[option].s, val);
      if (strcmp(s->val[option].s, manual_feed_list[0]) == 0)
        s->opt[FEED_TIMEOUT].cap |= SANE_CAP_INACTIVE;
      else
        s->opt[FEED_TIMEOUT].cap &= ~SANE_CAP_INACTIVE;
      if (info)
        *info |= SANE_INFO_RELOAD_OPTIONS;

      return SANE_STATUS_GOOD;
    */

    case PAPER_SIZE:
      strcpy(s->val[PAPER_SIZE].s, val);
      i = str_index(paper_list, s->val[PAPER_SIZE].s);
      if (i == 0)
      { /*user def */
        s->opt[TL_X].cap &=
            s->opt[TL_Y].cap &=
            s->opt[BR_X].cap &= s->opt[BR_Y].cap &= ~SANE_CAP_INACTIVE;
        s->opt[LANDSCAPE].cap |= SANE_CAP_INACTIVE;
        s->val[LANDSCAPE].w = 0;
      }
      else
      {
        s->opt[TL_X].cap |=
            s->opt[TL_Y].cap |=
            s->opt[BR_X].cap |= s->opt[BR_Y].cap |= SANE_CAP_INACTIVE;
        if (i == 3 || i == 4 || i == 7)
        { /*A5, A6 or B6 */
          s->opt[LANDSCAPE].cap &= ~SANE_CAP_INACTIVE;
        }
        else
        {
          s->opt[LANDSCAPE].cap |= SANE_CAP_INACTIVE;
          s->val[LANDSCAPE].w = 0;
        }
      }
      if (info)
        *info |= SANE_INFO_RELOAD_OPTIONS | SANE_INFO_RELOAD_PARAMS;

      return SANE_STATUS_GOOD;
    }
  }

  return SANE_STATUS_UNSUPPORTED;
}

static inline unsigned
mm2scanner_units(unsigned mm)
{
  return mm * 12000 / 254;
}


// TODO reenable features as far as possible
void
bropen_init_window (struct scanner *s)
{
  struct window *wnd = &s->window;
  // int paper = str_index (paper_list, s->val[PAPER_SIZE].s);
  memset (wnd, 0, sizeof (struct window));

  wnd->x_resolution = s->val[RESOLUTION].w;
  wnd->y_resolution = s->val[RESOLUTION].w;

  wnd->image_composition = str_index (scan_modes_ui, s->val[MODE].s);
  wnd->format_mode = format_map[s->window.image_composition];
}


/*
SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  struct scanner *s = (struct scanner *) handle;
  SANE_Parameters *p = &s->params;

  if (!s->scanning)
    {
      unsigned w, h, res = s->val[RESOLUTION].w;
      unsigned i = str_index (paper_list,
            s->val[PAPER_SIZE].s);
      if (i)
  {
    if (s->val[LANDSCAPE].b)
      {
        w = paper_sizes[i].height;
        h = paper_sizes[i].width;
      }
    else
      {
        w = paper_sizes[i].width;
        h = paper_sizes[i].height;
      }
  }
      else
  {
    w = s->val[BR_X].w - s->val[TL_X].w;
    h = s->val[BR_Y].w - s->val[TL_Y].w;
  }
      p->pixels_per_line = w * res / 25.4;
      p->lines = h * res / 25.4;
    }

  p->format = (!strcmp(s->val[MODE].s,SANE_VALUE_SCAN_MODE_COLOR)) ?
    SANE_FRAME_RGB : SANE_FRAME_GRAY;
  p->last_frame = SANE_TRUE;
  p->depth = bps_val[str_index (mode_list, s->val[MODE].s)];
  p->bytes_per_line = p->depth * p->pixels_per_line / 8;
  if (p->depth > 8)
    p->depth = 8;
  if (params)
    memcpy (params, p, sizeof (SANE_Parameters));
  return SANE_STATUS_GOOD;
}
*/