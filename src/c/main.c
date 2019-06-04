#include <pebble.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/ffont.h>
#include "logging.h"

static Window *s_window;
static Layer *s_outer_tick_layer;

static FFont *s_font;

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
  /*
  GRect crop = grect_crop(bounds, 12);

  graphics_context_set_stroke_color(ctx, GColorBlack);

  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0 && i != 0) continue;
    int32_t angle = TRIG_MAX_ANGLE * i / 60;
    GPoint p1 = gpoint_from_polar(bounds, GOvalScaleModeFitCircle, angle);
    GPoint p2 = gpoint_from_polar(crop, GOvalScaleModeFitCircle, angle);

    graphics_draw_line(ctx, p1, p2);
  }
  */

  FContext fctx;
  fctx_init_context(&fctx, ctx);
  fctx_set_fill_color(&fctx, GColorBlack);

  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0 && i != 0) continue;
    int32_t angle = TRIG_MAX_ANGLE * i / 60;
    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_draw_line(&fctx, angle, offset, FPointI(1, 8));
  }

  fctx_set_text_em_height(&fctx, s_font, 10);
  for (int i = 1; i < 12; i++) {
    fctx_begin_fill(&fctx);

    int32_t angle = TRIG_MAX_ANGLE * i / 12;
    // TODO adjust rotation for i > 3 && i < 9
    fctx_set_rotation(&fctx, angle);

    FPoint offset = fpoint_from_polar(bounds, angle);
    fctx_set_offset(&fctx, offset);

    char s[3];
    snprintf(s, sizeof(s), "%d", i * 5);
    fctx_draw_string(&fctx, s, s_font, GTextAlignmentCenter, FTextAnchorTop);

    fctx_end_fill(&fctx);
  }

  fctx_deinit_context(&fctx);
}

static void prv_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(root_layer);

  s_outer_tick_layer = layer_create(frame);
  layer_set_update_proc(s_outer_tick_layer, prv_outer_tick_layer_update_proc);
  layer_add_child(root_layer, s_outer_tick_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_outer_tick_layer);
}

static void prv_init(void) {
  s_font = ffont_create_from_resource(RESOURCE_ID_LECO_FFONT);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  window_destroy(s_window);

  ffont_destroy(s_font);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
