// Ben Minerd
// 4/28/2018
// Based on Pebble SDK examples:
// - Heart rate monitor https://github.com/pebble-examples/watchface-tutorial-hrm
// - Accel data sreaming https://github.com/pebble-examples/accel-data-stream

#include <pebble.h>

#define COMM_SIZE_INBOX          256
#define COMM_SIZE_OUTBOX         COMM_SIZE_INBOX
#define COMM_NUM_PACKET_ELEMENTS 1

static Window *s_main_window;
static Layer *s_window_layer;
static TextLayer *s_time_layer;
static TextLayer *s_hrm_layer;
static TextLayer *s_bat_layer;

static GFont s_time_font;
static GFont s_info_font;

static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;

static const uint8_t s_bat_offset_top_percent = 10;
static const uint8_t s_time_offset_top_percent = 31;
static const uint8_t s_hrm_offset_top_percent = 76;

static bool s_busy = false;

uint8_t relative_pixel(uint8_t percent, uint8_t max) {
  return (max * percent) / 100;
}

static void out_failed_handler(DictionaryIterator *iter, AppMessageResult result, void *context) {
   APP_LOG(APP_LOG_LEVEL_WARNING, "out_failed_handler %d", (int)result);
}

static void outbox_sent_handler(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox_send_handler");
  s_busy = false;
}

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  // A new message has been successfully received
   APP_LOG(APP_LOG_LEVEL_WARNING, "inbox_received_callback");
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  // A message was received, but had to be dropped
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped. Reason: %d", (int)reason);
}

void comm_init() {
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_sent(outbox_sent_handler);
  app_message_register_outbox_failed(out_failed_handler);
  app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED);
  AppMessageResult result = app_message_open(COMM_SIZE_INBOX, COMM_SIZE_OUTBOX);
   APP_LOG(APP_LOG_LEVEL_WARNING, "app_message_open result %u", (int)result);
}

bool comm_is_busy() {
  return s_busy;
}

bool comm_send_data(HealthValue value, uint32_t num_samples) {
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if(result == APP_MSG_OK) {
    DictionaryResult dict_result;
    dict_result = dict_write_int16(out, 0, value);
    if(dict_result != DICT_OK) {
         APP_LOG(APP_LOG_LEVEL_WARNING, "dict_write_int16 failed on item %lu", 0);
    }

    dict_write_end(out);
    
    if(app_message_outbox_send() == APP_MSG_OK) {
      s_busy = true;
      return true;
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage send failed.");
      return false;
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage not ready: %d", (int)result);
    return false;
  }
}

static void prv_update_ui(void) {
  /** Display the Time **/
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  
  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

#if PBL_API_EXISTS(health_service_peek_current_value)
  /** Display the Heart Rate **/
  HealthValue value = health_service_peek_current_value(HealthMetricHeartRateBPM);
  static char s_hrm_buffer[8];
  snprintf(s_hrm_buffer, sizeof(s_hrm_buffer), "%lu BPM", (uint32_t) value);
  text_layer_set_text(s_hrm_layer, s_hrm_buffer);
  layer_set_hidden(text_layer_get_layer(s_hrm_layer), false);
  
  static int counter = 0;
  counter++;
  
  if (counter > 2) {
    comm_send_data(value, 1);
  }
  
#else
  layer_set_hidden(text_layer_get_layer(s_hrm_layer), true);
#endif

  /** Display the Battery **/
  BatteryChargeState battery_info = battery_state_service_peek();

  static char s_bat_buffer[5];
  snprintf(s_bat_buffer, sizeof(s_bat_buffer), "%d%%", battery_info.charge_percent);
  text_layer_set_text(s_bat_layer, s_bat_buffer);
}

static void prv_update_ui_layout(void) {
  // Adapt the layout based on any obstructions
  GRect full_bounds = layer_get_bounds(s_window_layer);
  GRect unobstructed_bounds = layer_get_unobstructed_bounds(s_window_layer);

  if (!grect_equal(&full_bounds, &unobstructed_bounds)) {
    // Screen is obstructed
    layer_set_hidden(bitmap_layer_get_layer(s_background_layer), true);
    text_layer_set_text_color(s_time_layer, GColorWhite);
  } else {
    // Screen is unobstructed
    layer_set_hidden(bitmap_layer_get_layer(s_background_layer), false);
    text_layer_set_text_color(s_time_layer, GColorBlack);
  }

  GRect time_frame = layer_get_frame(text_layer_get_layer(s_time_layer));
  time_frame.origin.y = relative_pixel(s_time_offset_top_percent, unobstructed_bounds.size.h);
  layer_set_frame(text_layer_get_layer(s_time_layer), time_frame);

  GRect hrm_frame = layer_get_frame(text_layer_get_layer(s_hrm_layer));
  hrm_frame.origin.y = relative_pixel(s_hrm_offset_top_percent, unobstructed_bounds.size.h);
  layer_set_frame(text_layer_get_layer(s_hrm_layer), hrm_frame);

  GRect bat_frame = layer_get_frame(text_layer_get_layer(s_bat_layer));
  bat_frame.origin.y = relative_pixel(s_bat_offset_top_percent, unobstructed_bounds.size.h);
  layer_set_frame(text_layer_get_layer(s_bat_layer), bat_frame);

  prv_update_ui();
}

static void prv_initialize_ui(void) {
  GRect bounds = layer_get_bounds(s_window_layer);

  // Create GBitmap
  s_background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);

  // Create BitmapLayer to display the GBitmap
  s_background_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(s_background_layer, s_background_bitmap);
  layer_add_child(s_window_layer, bitmap_layer_get_layer(s_background_layer));

  // Create time TextLayer
  s_time_layer = text_layer_create(GRect(0,
    relative_pixel(s_time_offset_top_percent, bounds.size.h), bounds.size.w, 50));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_text(s_time_layer, "00:00");

  // Create GFont
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_PERFECT_DOS_48));

  // Apply to TextLayer
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_time_layer));

  // Create second custom font, apply it and add to Window
  s_info_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_PERFECT_DOS_20));

  // Create Heart Rate Layer
  s_hrm_layer = text_layer_create(GRect(0,
    relative_pixel(s_hrm_offset_top_percent, bounds.size.h), bounds.size.w, 25));
  text_layer_set_background_color(s_hrm_layer, GColorClear);
  text_layer_set_text_color(s_hrm_layer, GColorWhite);
  text_layer_set_text_alignment(s_hrm_layer, GTextAlignmentCenter);
  text_layer_set_text(s_hrm_layer, "Loading...");
  text_layer_set_font(s_hrm_layer, s_info_font);
  layer_add_child(s_window_layer, text_layer_get_layer(s_hrm_layer));

  // Create Battery Layer
  s_bat_layer = text_layer_create(GRect(0,
    relative_pixel(s_bat_offset_top_percent, bounds.size.h), bounds.size.w, 25));
  text_layer_set_background_color(s_bat_layer, GColorClear);
  text_layer_set_text_color(s_bat_layer, GColorWhite);
  text_layer_set_text_alignment(s_bat_layer, GTextAlignmentCenter);
  text_layer_set_text(s_bat_layer, "__%%");
  text_layer_set_font(s_bat_layer, s_info_font);
  layer_add_child(s_window_layer, text_layer_get_layer(s_bat_layer));

  // Check for obstructions
  prv_update_ui();
}

static void prv_on_health_data(HealthEventType type, void *context) {
  // If the update was from the Heart Rate Monitor, query it
  if (type == HealthEventHeartRateUpdate) {
    HealthValue value = health_service_peek_current_value(HealthMetricHeartRateBPM);
    // Display the heart rate
    
#if PBL_API_EXISTS(health_service_peek_current_value)
    /** Display the Heart Rate **/
    static char s_hrm_buffer[8];
    snprintf(s_hrm_buffer, sizeof(s_hrm_buffer), "%lu BPM", (uint32_t) value);
    text_layer_set_text(s_hrm_layer, s_hrm_buffer);
    layer_set_hidden(text_layer_get_layer(s_hrm_layer), false);
#else
    layer_set_hidden(text_layer_get_layer(s_hrm_layer), true);
 #endif
  }
}

static void prv_destroy_ui(void) {
  gbitmap_destroy(s_background_bitmap);
  bitmap_layer_destroy(s_background_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_hrm_layer);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_ui();
}

static void prv_main_window_load(Window *window) {
  s_window_layer = window_get_root_layer(window);

  // Create the UI elements
  prv_initialize_ui();

  // Make sure the time is displayed from the start
  prv_update_ui_layout();
}

static void prv_main_window_unload(Window *window) {
  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_info_font);

  // Clean up the unused UI elenents
  prv_destroy_ui();
}

static void prv_init() {
  // Create main Window element and assign to pointer
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = prv_main_window_load,
    .unload = prv_main_window_unload
  });
  window_stack_push(s_main_window, true);
  
  // Initialze the AppMessagePush communications
  comm_init();
  
  // Set the sample period to 5s (default is 600s) to activate heart rate monitor
  bool success = health_service_set_heart_rate_sample_period(5);

  // Register with TickTimerService on per-second basis
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);
}

static void prv_deinit() {
  bool success = health_service_set_heart_rate_sample_period(0);
  // Destroy Window
  window_destroy(s_main_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}