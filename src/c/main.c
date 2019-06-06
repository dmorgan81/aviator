#include <pebble.h>
#include <pebble-events/pebble-events.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/ffont.h>
#include <pebble-fctx/fpath.h>
#include <pebble-connection-vibes/connection-vibes.h>
#include <pebble-hourly-vibes/hourly-vibes.h>
#include "enamel.h"
#include "logging.h"

static const GPathInfo PATH_SECOND = {
  2,
  (GPoint []) {
    {0, -4}, {0, -79},
  }
};

static const GPathInfo PATH_MINUTE = {
  7,
  (GPoint []) {
    {-2, 0}, {-2, -13}, {-6, -50}, {0, -70}, {6, -50}, {2, -13}, {2, 0}
  }
};

static const GPathInfo PATH_HOUR = {
  7,
  (GPoint []) {
    {-3, 0}, {-3, -13}, {-7, -42}, {0, -52}, {7, -42}, {3, -13}, {3, 0}
  }
};

static GPath *s_path_second;
static GPath *s_path_minute;
static GPath *s_path_hour;

static Window *s_window;
static Layer *s_logo_layer;
static TextLayer *s_name_layer;
static Layer *s_outer_tick_layer;
static Layer *s_inner_tick_layer;
static Layer *s_date_layer;
static Layer *s_battery_layer;
static Layer *s_hands_layer;

static FFont *s_font;
static FPath *s_logo;
static GDrawCommandImage *s_battery_pdc;

static EventHandle s_settings_event_handle;
static EventHandle s_tick_timer_event_handle;
static EventHandle s_battery_event_handle;

static uint8_t s_hour_multiplier;
static char s_date[3];

#define fpoint_from_polar(bounds, angle) g2fpoint(gpoint_from_polar((bounds), GOvalScaleModeFitCircle, (angle)))

static inline void fctx_draw_line(FContext *fctx, uint32_t rotation, FPoint offset, FPoint scale) {
    fctx_begin_fill(fctx);

    fctx_set_rotation(fctx, rotation);
    fctx_set_offset(fctx, offset);
    fctx_set_scale(fctx, FPointOne, scale);

    fctx_move_to(fctx, FPoint(-1, 1));
    fctx_line_to(fctx, FPoint(-1, -1));
    fctx_line_to(fctx, FPoint(1, -1));
    fctx_line_to(fctx, FPoint(1, 1));
    fctx_close_path(fctx);

    fctx_end_fill(fctx);
}

static void prv_logo_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  FContext fctx;
  fctx_init_context(&fctx, ctx);

  fctx_set_scale(&fctx, FPoint(2, 2), FPointOne);
  fctx_set_fill_color(&fctx, gcolor_legible_over(enamel_get_BACKGROUND_COLOR()));
  fctx_set_offset(&fctx, FPointI(62, 55));

  fctx_begin_fill(&fctx);
  fctx_draw_commands(&fctx, FPointZero, s_logo->data, s_logo->size);
  fctx_end_fill(&fctx);

  fctx_deinit_context(&fctx);
}

static void prv_date_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect frame = layer_get_frame(layer);

  graphics_context_set_stroke_color(ctx, gcolor_legible_over(enamel_get_BACKGROUND_COLOR()));
  graphics_draw_rect(ctx, bounds);

  graphics_context_set_fill_color(ctx, gcolor_legible_over(enamel_get_BACKGROUND_COLOR()));
  graphics_fill_rect(ctx, grect_crop(bounds, 2), 0, GCornerNone);

  FContext fctx;
  fctx_init_context(&fctx, ctx);

  FPoint offset = FPointI(frame.origin.x + bounds.size.w - 3, frame.origin.y + 3);
  fctx_set_offset(&fctx, offset);

  fctx_set_fill_color(&fctx, enamel_get_BACKGROUND_COLOR());
  fctx_set_text_em_height(&fctx, s_font, 15);

  fctx_begin_fill(&fctx);
  fctx_draw_string(&fctx, s_date, s_font, GTextAlignmentRight, FTextAnchorTop);
  fctx_end_fill(&fctx);

  fctx_deinit_context(&fctx);
}

static void prv_battery_layer_update_proc(Layer *layer, GContext *ctx) {
  gdraw_command_image_draw(ctx, s_battery_pdc, GPointZero);
}

static void prv_outer_tick_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = grect_crop(layer_get_bounds(layer), 3);

  FContext fctx;
  fctx_init_context(&fctx, ctx);

  fctx_set_fill_color(&fctx, GColorLightGray);
  FPoint scale = FPointI(1, 8);
  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0) continue;
    int32_t angle = TRIG_MAX_ANGLE * i / 60;
    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_draw_line(&fctx, angle, offset, scale);
  }

  fctx_set_fill_color(&fctx, gcolor_legible_over(enamel_get_BACKGROUND_COLOR()));
  fctx_set_text_em_height(&fctx, s_font, 10);
  for (int i = 1; i < 12; i++) {
    fctx_begin_fill(&fctx);

    int32_t angle = TRIG_MAX_ANGLE * i / 12;
    int32_t rotation = (i > 3 && i < 9) ? (TRIG_MAX_ANGLE / 2) : 0;
    fctx_set_rotation(&fctx, angle + rotation);

    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_set_offset(&fctx, offset);

    char s[3];
    snprintf(s, sizeof(s), "%02d", i * 5);
    fctx_draw_string(&fctx, s, s_font, GTextAlignmentCenter, (i > 3 && i < 9) ? FTextAnchorBaseline : FTextAnchorTop);

    fctx_end_fill(&fctx);
  }

  fctx_deinit_context(&fctx);
}

static void prv_inner_tick_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  FContext fctx;
  fctx_init_context(&fctx, ctx);

  FPoint offset = FPointI(bounds.origin.x + (bounds.size.w / 2) - 1, bounds.origin.y);
  fctx_set_offset(&fctx, offset);
  fctx_set_scale(&fctx, FPointOne, FPointI(12, 16));
  fctx_set_fill_color(&fctx, enamel_get_BACKGROUND_COLOR());

  fctx_begin_fill(&fctx);
  fctx_move_to(&fctx, FPointZero);
  fctx_line_to(&fctx, FPoint(-1, 1));
  fctx_line_to(&fctx, FPoint(1, 1));
  fctx_close_path(&fctx);
  fctx_end_fill(&fctx);

  offset.y += FIX1;
  fctx_set_offset(&fctx, offset);
  fctx_set_scale(&fctx, FPointOne, FPointI(10, 14));
  fctx_set_fill_color(&fctx, enamel_get_HOUR_HAND_COLOR());

  fctx_begin_fill(&fctx);
  fctx_move_to(&fctx, FPointZero);
  fctx_line_to(&fctx, FPoint(-1, 1));
  fctx_line_to(&fctx, FPoint(1, 1));
  fctx_close_path(&fctx);
  fctx_end_fill(&fctx);

  bounds = grect_crop(layer_get_bounds(layer), 14);
  fctx_set_fill_color(&fctx, GColorLightGray);
  FPoint scale = FPointI(3, 3);
  for (int i = 5; i < 60; i += 5) {
    int32_t angle = TRIG_MAX_ANGLE * i / 60;
    offset = fpoint_from_polar(bounds, angle);
    if (i == 5 || i == 15 || i == 25) {
      offset.x -= FIX1;
    } else if (i == 35 || i == 45 || i == 55) {
      offset.x += FIX1;
    }
    if (i == 5 || i == 55) {
      offset.y += FIX1;
    } else if (i == 25 || i == 35) {
      offset.y -= FIX1;
    } else if (i == 30) {
      offset.y -= INT_TO_FIXED(2);
    }
    fctx_draw_line(&fctx, angle, offset, scale);
  }

  bounds = grect_crop(layer_get_bounds(layer), 30);
  scale = FPointI(1, 12);
  for (int i = 5; i < 120; i += 10) {
    int32_t angle = TRIG_MAX_ANGLE * i / 120;
    offset = fpoint_from_polar(bounds, angle);
    fctx_draw_line(&fctx, angle, offset, scale);
  }

  fctx_set_rotation(&fctx, 0);
  fctx_set_fill_color(&fctx, gcolor_legible_over(enamel_get_BACKGROUND_COLOR()));
  fctx_set_text_em_height(&fctx, s_font, 18);
  for (int i = 1; i <= 12; i++) {
    fctx_begin_fill(&fctx);

    int32_t angle = TRIG_MAX_ANGLE * i / 12;
    offset = fpoint_from_polar(bounds, angle);
    fctx_set_offset(&fctx, offset);

    char s[3];
    snprintf(s, sizeof(s), "%02d", i * s_hour_multiplier);
    fctx_draw_string(&fctx, s, s_font, GTextAlignmentCenter, FTextAnchorMiddle);

    fctx_end_fill(&fctx);
  }

  fctx_deinit_context(&fctx);
}

static void prv_hands_layer_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_stroke_color(ctx, enamel_get_BACKGROUND_COLOR());
  graphics_context_set_stroke_width(ctx, 1);

  graphics_context_set_fill_color(ctx, enamel_get_MINUTE_HAND_COLOR());
  gpath_draw_filled(ctx, s_path_minute);
  gpath_draw_outline(ctx, s_path_minute);

  graphics_context_set_fill_color(ctx, enamel_get_HOUR_HAND_COLOR());
  gpath_draw_filled(ctx, s_path_hour);
  gpath_draw_outline(ctx, s_path_hour);

  if (enamel_get_ENABLE_SECONDS()) {
    graphics_context_set_stroke_width(ctx, 2);
    gpath_draw_outline(ctx, s_path_second);
  }

  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  center.x -= 1;
  center.y -= 1;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_circle(ctx, center, 4);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
#ifdef SCREENSHOT
  tick_time->tm_hour = 3;
  tick_time->tm_min = 40;
#endif

  int32_t angle = tick_time->tm_min * TRIG_MAX_ANGLE / 60;
  gpath_rotate_to(s_path_minute, angle);

  s_hour_multiplier = atoi(enamel_get_CLOCK_TYPE());
  angle = (TRIG_MAX_ANGLE * ((tick_time->tm_hour * 6) + (tick_time->tm_min / 10))) / (12 * s_hour_multiplier * 6);
  gpath_rotate_to(s_path_hour, angle);
  
  angle = tick_time->tm_sec * TRIG_MAX_ANGLE / 60;
  gpath_rotate_to(s_path_second, angle);

  layer_mark_dirty(s_hands_layer);

  if (units_changed & DAY_UNIT) {
    snprintf(s_date, sizeof(s_date), "%d", tick_time->tm_mday);
    layer_mark_dirty(s_date_layer);
  }
}

static bool prv_battery_pdc_iterator(GDrawCommand *command, uint32_t index, void *context) {
  GColor *color = (GColor *) context;
  gdraw_command_set_stroke_color(command, *color);
  if (index > 0) {
    gdraw_command_set_fill_color(command, *color);
  }
  return true;
}

static void prv_battery_event_handler(BatteryChargeState state) {
#ifdef SCREENSHOT
  state.charge_percent = 50;
#endif
  GDrawCommandList *list = gdraw_command_image_get_command_list(s_battery_pdc);
  gdraw_command_set_hidden(gdraw_command_list_get_command(list, 2), state.charge_percent < 80);
  gdraw_command_set_hidden(gdraw_command_list_get_command(list, 3), state.charge_percent < 40);
  gdraw_command_set_hidden(gdraw_command_list_get_command(list, 4), state.charge_percent < 10);
  layer_mark_dirty(s_battery_layer);
}

static void prv_settings_received_handler(void *context) {
  connection_vibes_set_state(atoi(enamel_get_CONNECTION_VIBE()));
  hourly_vibes_set_enabled(enamel_get_HOURLY_VIBE());
  connection_vibes_enable_health(enamel_get_ENABLE_HEALTH());
  hourly_vibes_enable_health(enamel_get_ENABLE_HEALTH());

  if (s_tick_timer_event_handle) events_tick_timer_service_unsubscribe(s_tick_timer_event_handle);
  time_t now = time(NULL);
  prv_tick_handler(localtime(&now), DAY_UNIT);
  s_tick_timer_event_handle = events_tick_timer_service_subscribe(enamel_get_ENABLE_SECONDS() ? SECOND_UNIT : MINUTE_UNIT, prv_tick_handler);

  GColor color = gcolor_legible_over(enamel_get_BACKGROUND_COLOR());
  text_layer_set_text_color(s_name_layer, color);
  gdraw_command_list_iterate(gdraw_command_image_get_command_list(s_battery_pdc), prv_battery_pdc_iterator, &color);

  window_set_background_color(s_window, enamel_get_BACKGROUND_COLOR());
}

static void prv_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(root_layer);

/*
  s_logo_layer = bitmap_layer_create(GRect(0, 56, frame.size.w, 14));
  bitmap_layer_set_bitmap(s_logo_layer, s_logo);
  bitmap_layer_set_alignment(s_logo_layer, GAlignTop);
  bitmap_layer_set_compositing_mode(s_logo_layer, GCompOpSet);
  layer_add_child(root_layer, bitmap_layer_get_layer(s_logo_layer));
*/

  s_logo_layer = layer_create(frame);
  layer_set_update_proc(s_logo_layer, prv_logo_layer_update_proc);
  layer_add_child(root_layer, s_logo_layer);

  s_name_layer = text_layer_create(GRect(0, 108, frame.size.w, frame.size.h));
  text_layer_set_background_color(s_name_layer, GColorClear);
  text_layer_set_text(s_name_layer, "AVIATOR");
  text_layer_set_font(s_name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_name_layer, GTextAlignmentCenter);
  layer_add_child(root_layer, text_layer_get_layer(s_name_layer));

  s_date_layer = layer_create(GRect(106, 80, 25, 17));
  layer_set_update_proc(s_date_layer, prv_date_layer_update_proc);
  layer_add_child(root_layer, s_date_layer);

  s_battery_layer = layer_create(GRect(46, 78, frame.size.w, frame.size.h));
  layer_set_update_proc(s_battery_layer, prv_battery_layer_update_proc);
  layer_add_child(root_layer, s_battery_layer);

  s_outer_tick_layer = layer_create(frame);
  layer_set_update_proc(s_outer_tick_layer, prv_outer_tick_layer_update_proc);
  layer_add_child(root_layer, s_outer_tick_layer);

  s_inner_tick_layer = layer_create(frame);
  layer_set_update_proc(s_inner_tick_layer, prv_inner_tick_layer_update_proc);
  layer_add_child(root_layer, s_inner_tick_layer);

  s_hands_layer = layer_create(frame);
  layer_set_update_proc(s_hands_layer, prv_hands_layer_update_proc);
  layer_add_child(root_layer, s_hands_layer);

  GPoint center = grect_center_point(&frame);
  center.x -= 1;
  center.y -= 1;
  gpath_move_to(s_path_second, center);
  gpath_move_to(s_path_minute, center);
  gpath_move_to(s_path_hour, center);

  prv_settings_received_handler(NULL);
  s_settings_event_handle = enamel_settings_received_subscribe(prv_settings_received_handler, NULL);

  prv_battery_event_handler(battery_state_service_peek());
  s_battery_event_handle = events_battery_state_service_subscribe(prv_battery_event_handler);
}

static void prv_window_unload(Window *window) {
  events_battery_state_service_unsubscribe(s_battery_event_handle);
  events_tick_timer_service_unsubscribe(s_tick_timer_event_handle);
  enamel_settings_received_unsubscribe(s_settings_event_handle);

  layer_destroy(s_hands_layer);
  layer_destroy(s_inner_tick_layer);
  layer_destroy(s_outer_tick_layer);
  layer_destroy(s_battery_layer);
  layer_destroy(s_date_layer);
  text_layer_destroy(s_name_layer);
  layer_destroy(s_logo_layer);
}

static void prv_init(void) {
  enamel_init();
  connection_vibes_init();
  hourly_vibes_init();
  uint32_t const pattern[] = { 200 };
  hourly_vibes_set_pattern((VibePattern) {
      .durations = pattern,
      .num_segments = 1
  });

  events_app_message_open();

  s_font = ffont_create_from_resource(RESOURCE_ID_LECO_FFONT);
  s_logo = fpath_create_from_resource(RESOURCE_ID_PEBBLE_LOGO);
  s_battery_pdc = gdraw_command_image_create_with_resource(RESOURCE_ID_PDC_BATTERY);

  s_path_second = gpath_create(&PATH_SECOND);
  s_path_minute = gpath_create(&PATH_MINUTE);
  s_path_hour = gpath_create(&PATH_HOUR);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  window_destroy(s_window);

  gpath_destroy(s_path_hour);
  gpath_destroy(s_path_minute);
  gpath_destroy(s_path_second);

  gdraw_command_image_destroy(s_battery_pdc);
  fpath_destroy(s_logo);
  ffont_destroy(s_font);

  hourly_vibes_deinit();
  connection_vibes_deinit();
  enamel_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
