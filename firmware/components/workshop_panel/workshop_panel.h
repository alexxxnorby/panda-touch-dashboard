#pragma once
#include "esphome/core/component.h"
#include "esphome/components/font/font.h"
#include "esphome/core/preferences.h"
#include "esphome/core/application.h"
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/online_image/online_image.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/wifi/wifi_component.h"
#include <array>
#include <map>
#include <functional>
#include <deque>

namespace esphome::workshop_panel {
constexpr int MAP_COUNT = 22;
constexpr int FAN_ICON = 7;
constexpr int SWITCH_IDS[]={0,1,2,3,19,20};
constexpr int SENSOR_IDS[]={4,5,21};
struct Settings {
  uint32_t version{3};
  uint16_t idle_seconds{60};
  uint8_t brightness{80};
  uint8_t camera_seconds{5};
  char entity[MAP_COUNT][112]{};
  char label[MAP_COUNT][32]{};
  uint8_t language{0}, clock_scale{156}, weather_scale{130};
  // Keep all three bytes for saved-settings compatibility; they now share one global value.
  uint8_t switch_opacity{100}, sensor_opacity{100}, panel_opacity{100};
  uint8_t night_enabled{0}, night_start{22}, night_end{7}, night_brightness{20};
  uint8_t text_theme{0}, long_text{0}, weather_visible{1}, date_visible{1}, weather_parts{7};
  uint8_t visible_switches{15}, visible_sensors{3};
  uint8_t switch_order[6]{0,1,2,3,4,5}, sensor_order[3]{0,1,2};
  char wallpaper_url[256]{};
};
// Keep the original byte layout and preference key for non-destructive migration.
struct LegacySettings {
  uint32_t version;
  uint16_t idle_seconds;
  uint8_t brightness, camera_seconds;
  char entity[19][112];
  char label[19][32];
};
static_assert(sizeof(LegacySettings)==2744,"Legacy preference layout must remain unchanged");
struct Notice {int printer; bool error; std::string message;};
// Separate preferences keep the existing display-settings layout untouched.
struct TileSettings {
  uint32_t version{1};
  uint8_t switch_icon[6]{};  // Auto, light bulb, plug, power, none
  uint8_t sensor_icon[3]{};  // Auto, thermometer, humidity, solar, power, none
  uint8_t threshold_enabled{0};
  float threshold[3]{1000,1000,1000};
  char switch_state[6][112]{};
};

class WorkshopPanel : public PollingComponent, public api::CustomAPIDevice, public wifi::WiFiScanResultsListener {
 public:
  void set_german_font(int index,font::Font *p) {const lv_font_t *base[]={&lv_font_montserrat_16,&lv_font_montserrat_20,&lv_font_montserrat_28,&lv_font_montserrat_48};fonts_[index]=*base[index];fonts_[index].fallback=p->get_lv_font();}
  void set_clock(time::RealTimeClock *p) {clock_=p;}
  void set_backlight(light::LightState *p) {backlight_=p;}
  void set_snapshot(online_image::OnlineImage *p) {snapshot_=p;}
  void set_wallpaper(online_image::OnlineImage *p) {wallpaper_=p;}
  void set_background(image::Image *p) {background_=p;}
  float get_setup_priority() const override {return setup_priority::AFTER_WIFI-1;}
  void entity_search_results(std::string request_id, std::string results);
  void benchmark_pages();
  void validate_display_fonts();
  void validate_tile_controls();
  void validate_settings_scroll();
  void setup() override;
  void loop() override;
  void update() override;
  void on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) override;
 private:
  std::array<lv_font_t,4> fonts_{};
  Settings settings_{};
  TileSettings tile_settings_{};
  ESPPreferenceObject tile_pref_;
  std::array<lv_obj_t*,3> threshold_input_{};
  std::array<lv_obj_t*,6> state_input_{};
  bool editor_state_{false};
  void tile_settings_load_();
  void tile_settings_save_();
  void tile_options_(lv_obj_t *body,int &y);
  void tile_refresh_();
  void save_tile_state_(int slot);
  lv_obj_t *tile_icon_(lv_obj_t *parent,int x,int y,int kind);
  int switch_icon_(int slot) const;
  int sensor_icon_(int slot) const;
  void subscriptions_refresh_();
  online_image::OnlineImage *wallpaper_{nullptr};
  lv_obj_t *wallpaper_object_{nullptr}, *background_input_{nullptr}, *display_preview_{nullptr};
  lv_obj_t *preview_clock_{nullptr}, *preview_weather_{nullptr}, *preview_switch_{nullptr}, *preview_sensor_{nullptr};
  bool wallpaper_ready_{false}, wallpaper_loading_{false};
  int applied_brightness_{-1}, drag_kind_{-1}, drag_slot_{-1}, display_scroll_{0};
  lv_obj_t *settings_body_{nullptr};
  int settings_icons_y_{0};
  int page_count_() const;
  std::string translate_(const std::string &text) const;
  void display_settings_();
  void display_preview_refresh_();
  void brightness_refresh_();
  void load_wallpaper_();
  void reorder_(int kind,int from,int to);
  void sanitize_();
  ESPPreferenceObject pref_;
  time::RealTimeClock *clock_{nullptr};
  light::LightState *backlight_{nullptr};
  online_image::OnlineImage *snapshot_{nullptr};
  image::Image *background_{nullptr};
  lv_image_dsc_t background_dsc_{};
  uint8_t *background_pixels_{nullptr};
  struct DashboardPage {
    lv_obj_t *root{nullptr}, *progress{nullptr};
    std::map<std::string,lv_obj_t*> labels;
    std::map<int,lv_obj_t*> toggles;
    std::array<lv_obj_t*,4> spool{}, slots{};
    bool busy{false};
    int step{0};
  };
  std::array<DashboardPage,5> dashboard_pages_{};
  std::map<std::string,lv_obj_t*> chrome_labels_;
  std::array<lv_obj_t*,5> page_marks_{};
  int rendered_view_{-1}, rendered_page_{-1}, content_origin_{0};
  uint32_t frame_started_{0}, navigation_started_{0};
  bool benchmark_active_{false};
  void prepare_background_();
  void dashboard_page_();
  void benchmark_step_(int step);
  struct StateSource {char entity[160]{};char attribute[40]{};};
  // std::map nodes keep these subscription buffers at stable addresses.
  std::map<std::string,StateSource> state_sources_;
  std::map<std::string,std::string> state_;
  std::map<std::string,lv_obj_t*> labels_;
  std::map<int,lv_obj_t*> toggles_;
  std::array<lv_obj_t*,4> spool_color_{};
  std::array<lv_obj_t*,4> slot_box_{};
  std::deque<Notice> notices_;
  lv_obj_t *canvas_{nullptr}, *progress_{nullptr}, *camera_image_{nullptr};
  lv_obj_t *entity_dropdown_{nullptr}, *entity_input_{nullptr}, *label_input_{nullptr}, *keyboard_{nullptr};
  lv_obj_t *settings_message_{nullptr};
  lv_obj_t *editor_{nullptr}, *editor_input_{nullptr}, *editor_target_{nullptr}, *search_status_{nullptr};
  std::array<lv_obj_t*,3> suggestion_buttons_{};
  std::array<lv_obj_t*,3> suggestion_labels_{};
  std::array<std::string,3> suggestion_ids_{};
  uint32_t search_request_{0};
  bool editor_entity_{false};
  void configure_keyboard_();
  void open_editor_(lv_obj_t *target, const std::string &title, bool entity);
  void close_editor_(bool accept);
  void search_entities_();
  lv_obj_t *wifi_ssid_{nullptr}, *wifi_password_{nullptr}, *wifi_dropdown_{nullptr};
  std::vector<std::string> wifi_networks_;
  bool wifi_scanning_{false}, wifi_connecting_{false}, wifi_password_visible_{false};
  uint32_t wifi_attempt_{0}, wifi_scan_started_{0};
  uint32_t state_updates_{0},last_state_at_{0},diagnostic_at_{0},ha_drops_{0},wifi_drops_{0};
  bool last_wifi_connected_{false};
  int page_{0}, view_{0}, camera_printer_{0}, edit_index_{0};
  int step_{1};
  bool rebuild_{false}, ready_{false}, connected_{false}, last_busy_{false};
  bool root_bound_{false};
  bool camera_loading_{false}, snapshot_ready_{false};
  int image_request_printer_{-1};
  std::array<bool,2> homed_{{false,false}};
  std::array<bool,2> homing_requested_{{false,false}};
  bool skip_home_click_{false};
  std::array<std::string,2> previous_status_;
  uint32_t last_activity_{0}, camera_update_{0};
  enum {NORMAL, IDLE, SETTINGS_DISPLAY, SETTINGS_ENTITIES, CAMERA, ALERT, WIFI_SETTINGS};
  void defaults_();
  void subscribe_();
  void watch_(const std::string &entity, const std::string &key, const std::string &attr="");
  void receive_(const std::string &key, const std::string &value);
  std::string get_(const std::string &key) const;
  std::string number_(const std::string &key, const char *unit="") const;
  std::string remaining_(int p) const;
  bool busy_(int p) const;
  bool idle_(int p) const;
  void activity_();
  void route_(int view);
  void render_();
  void normal_();
  void home_();
  void printer_(int p);
  void lasers_();
  void idle_screen_();
  void weather_icon_refresh_();
  lv_obj_t *weather_icon_{nullptr};
  lv_obj_t *finish_banner_{nullptr};
  std::string weather_icon_condition_;
  void settings_screen_(bool entities);
  void wifi_screen_();
  void wifi_scan_();
  void wifi_connect_();
  void wifi_refresh_();
  void camera_screen_();
  void alert_screen_();
  void refresh_();
  void refresh_camera_();
  void toggle_(int map);
  void move_(int printer, const std::string &axis, int distance);
  void extrude_(int printer, bool retract);
  bool movement_allowed_(int p) const;
  void save_();
  void load_fields_();
  void store_fields_();
  void dismiss_();
  lv_obj_t *box_(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=12);
  lv_obj_t *label_(lv_obj_t *parent,int x,int y,int w,const std::string &text,const lv_font_t *font=&lv_font_montserrat_20,uint32_t color=0xEEF3F7);
  lv_obj_t *button_(lv_obj_t *parent,int x,int y,int w,int h,const std::string &text,std::function<void()> action,uint32_t color=0x293744);
  void bind_(lv_obj_t *obj,lv_event_code_t code,std::function<void()> fn);
  void text_(const std::string &key,const std::string &value);
  void service_(const std::string &service,const std::map<std::string,std::string> &data);
};
}  // namespace esphome::workshop_panel
