#include <pebble.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/ffont.h>
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
    {-1, 0}, {-1, -13}, {-5, -50}, {0, -70}, {5, -50}, {1, -13}, {1, 0}
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
static BitmapLayer *s_logo_layer;
static Layer *s_outer_tick_layer;
static Layer *s_inner_tick_layer;
static Layer *s_hands_layer;

static FFont *s_font;
static GBitmap *s_logo;

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

static void prv_outer_tick_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = grect_crop(layer_get_bounds(layer), 3);

  FContext fctx;
  fctx_init_context(&fctx, ctx);

  fctx_set_fill_color(&fctx, GColorLightGray);
  FPoint scale = FPointI(1, 8);
  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0 && i != 0) continue;
    int32_t angle = TRIG_MAX_ANGLE * i / 60;
    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_draw_line(&fctx, angle, offset, scale);
  }

  fctx_set_fill_color(&fctx, GColorBlack);
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

  fctx_set_offset(&fctx, FPointI(bounds.origin.x + (bounds.size.w / 2), bounds.origin.y));
  fctx_set_scale(&fctx, FPointOne, FPointI(9, 12));
  fctx_set_fill_color(&fctx, GColorBlack);

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
    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_draw_line(&fctx, angle, offset, scale);
  }

  bounds = grect_crop(layer_get_bounds(layer), 30);
  scale = FPointI(1, 12);
  for (int i = 5; i < 120; i += 10) {
    int32_t angle = TRIG_MAX_ANGLE * i / 120;
    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_draw_line(&fctx, angle, offset, scale);
  }

  fctx_set_rotation(&fctx, 0);
  fctx_set_fill_color(&fctx, GColorBlack);
  fctx_set_text_em_height(&fctx, s_font, 18);
  for (int i = 1; i <= 12; i++) {
    fctx_begin_fill(&fctx);

    int32_t angle = TRIG_MAX_ANGLE * i / 12;
    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_set_offset(&fctx, offset);

    char s[3];
    snprintf(s, sizeof(s), "%02d", i * 2);
    fctx_draw_string(&fctx, s, s_font, GTextAlignmentCenter, FTextAnchorMiddle);

    fctx_end_fill(&fctx);
  }

  fctx_deinit_context(&fctx);
}

static void prv_hands_layer_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorLightGray);
  gpath_draw_filled(ctx, s_path_hour);
  gpath_draw_filled(ctx, s_path_minute);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  gpath_draw_outline(ctx, s_path_hour);
  gpath_draw_outline(ctx, s_path_minute);

  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, grect_center_point(&bounds), 8);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  int32_t angle = tick_time->tm_min * TRIG_MAX_ANGLE / 60;
  gpath_rotate_to(s_path_minute, angle);

  angle = (TRIG_MAX_ANGLE * ((tick_time->tm_hour * 6) + (tick_time->tm_min / 10))) / (24 * 6);
  gpath_rotate_to(s_path_hour, angle);

  layer_mark_dirty(s_hands_layer);
}

static void prv_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(root_layer);

  s_logo_layer = bitmap_layer_create(GRect(0, 60, frame.size.w, 21));
  bitmap_layer_set_bitmap(s_logo_layer, s_logo);
  bitmap_layer_set_alignment(s_logo_layer, GAlignTop);
  bitmap_layer_set_compositing_mode(s_logo_layer, GCompOpSet);
  layer_add_child(root_layer, bitmap_layer_get_layer(s_logo_layer));

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
  gpath_move_to(s_path_second, center);
  gpath_move_to(s_path_minute, center);
  gpath_move_to(s_path_hour, center);

  time_t now = time(NULL);
  prv_tick_handler(localtime(&now), MINUTE_UNIT);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
}

static void prv_window_unload(Window *window) {
  tick_timer_service_unsubscribe();

  bitmap_layer_destroy(s_logo_layer);
  layer_destroy(s_hands_layer);
  layer_destroy(s_inner_tick_layer);
  layer_destroy(s_outer_tick_layer);
}

static void prv_init(void) {
  s_font = ffont_create_from_resource(RESOURCE_ID_LECO_FFONT);
  s_logo = gbitmap_create_with_resource(RESOURCE_ID_PEBBLE_LOGO);

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
  gbitmap_destroy(s_logo);
  ffont_destroy(s_font);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
