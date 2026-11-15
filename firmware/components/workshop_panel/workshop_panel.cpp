#include "workshop_panel.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_connection.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include "esp_heap_caps.h"

namespace esphome::workshop_panel {
static constexpr uint32_t BG=0x10171E, PANEL=0x1B2630, MUTED=0xA7B6C3, GREEN=0x74DFA5;
static const char *const TAG="workshop_panel";
static const char *NAMES[]={"P1S","X2D"};
static std::string pk(int p,const char *field) {return "p"+std::to_string(p)+"."+field;}
static bool missing(const std::string &v) {return v.empty()||v=="unknown"||v=="unavailable"||v=="None"||v=="null";}
static bool truth(const std::string &v) {return v=="on"||v=="true"||v=="True";}
static float numeric(const std::string &v) {char *end;float f=strtof(v.c_str(),&end);return end==v.c_str()||*end!='\0'||!std::isfinite(f)?NAN:f;}
static std::string shown(const std::string &s) {return missing(s)?"--":s;}
static void copy(char *to,const std::string &s,size_t n){snprintf(to,n,"%s",s.c_str());}
static void background_color(lv_obj_t *object,uint32_t rgb){
  const auto color=lv_color_hex(rgb);
  if(!lv_color_eq(lv_obj_get_style_bg_color(object,LV_PART_MAIN),color))lv_obj_set_style_bg_color(object,color,0);
}
static void text_color(lv_obj_t *object,uint32_t rgb){
  const auto color=lv_color_hex(rgb);
  if(!lv_color_eq(lv_obj_get_style_text_color(object,LV_PART_MAIN),color))lv_obj_set_style_text_color(object,color,0);
}

void WorkshopPanel::defaults_() {
  const char *ids[]={"switch.workshop_light","switch.workbench_light","light.printer_1_chamber","light.printer_2_chamber","sensor.workshop_power","sensor.solar_power","weather.home","switch.laser_1","switch.laser_2","switch.wall_fan","camera.printer_1","camera.printer_2","printer_1","printer_2","printer_1_ams_1","printer_2_ams_1","http://homeassistant.local:8123","replace_with_printer_1_device_id","replace_with_printer_2_device_id","switch.extra_1","switch.extra_2","sensor.extra"};
  const char *names[]={"Switch 1","Switch 2","Switch 3","Switch 4","Sensor 1","Sensor 2","Weather","Laser 1 power","Laser 2 power","Shared extractor","P1S camera","X2D camera","P1S entity prefix","X2D entity prefix","P1S AMS prefix","X2D AMS prefix","Home Assistant URL","P1S device ID","X2D device ID","Switch 5","Switch 6","Sensor 3"};
  for(int i=0;i<MAP_COUNT;i++){copy(settings_.entity[i],ids[i],112);copy(settings_.label[i],names[i],32);}
}

void WorkshopPanel::setup() {
  defaults_();
  register_service(&WorkshopPanel::entity_search_results,"entity_search_results",{"request_id","results"});
  register_service(&WorkshopPanel::benchmark_pages,"benchmark_pages");
  register_service(&WorkshopPanel::validate_display_fonts,"validate_display_fonts");
  register_service(&WorkshopPanel::validate_tile_controls,"validate_tile_controls");
  register_service(&WorkshopPanel::validate_settings_scroll,"validate_settings_scroll");
  prepare_background_();
  wifi::global_wifi_component->add_scan_results_listener(this);
  pref_=global_preferences->make_preference<Settings>(0x57534B33);
  auto saved=std::make_unique<Settings>();
  if(pref_.load(saved.get())&&saved->version==3)settings_=*saved;
  else {
    auto legacy_pref=global_preferences->make_preference<LegacySettings>(0x57534B31);
    auto legacy=std::make_unique<LegacySettings>();
    if(legacy_pref.load(legacy.get())&&(legacy->version==1||legacy->version==2)){
      settings_.idle_seconds=legacy->idle_seconds;settings_.brightness=legacy->brightness;settings_.camera_seconds=legacy->camera_seconds;
      for(int i=0;i<19;i++){memcpy(settings_.entity[i],legacy->entity[i],112);memcpy(settings_.label[i],legacy->label[i],32);}
      if(legacy->version==1){const char *names[]={"Switch 1","Switch 2","Switch 3","Switch 4","Sensor 1","Sensor 2"};for(int i=0;i<6;i++)copy(settings_.label[i],names[i],32);}
    }
  }
  saved.reset();sanitize_();tile_settings_load_();save_();brightness_refresh_();
  wallpaper_->add_on_finished_callback([this](bool cached){wallpaper_loading_=false;wallpaper_ready_=true;if(wallpaper_object_&&settings_.wallpaper_url[0])lv_image_set_src(wallpaper_object_,wallpaper_->get_lv_image_dsc());text_("background_status","Background loaded");});
  wallpaper_->add_on_error_callback([this](){wallpaper_loading_=false;wallpaper_ready_=false;if(wallpaper_object_)lv_image_set_src(wallpaper_object_,&background_dsc_);text_("background_status","Image unavailable; using default background");});
  set_timeout("wallpaper_load",1500,[this](){load_wallpaper_();});
  subscribe_();
  ESP_LOGI(TAG,"Subscribed to HA: P1S prefix=%s, X2D prefix=%s",settings_.entity[12],settings_.entity[13]);
  snapshot_->add_on_finished_callback([this](bool cached){camera_loading_=false;snapshot_ready_=image_request_printer_==camera_printer_;if(view_==CAMERA&&camera_image_&&snapshot_ready_){lv_image_set_src(camera_image_,snapshot_->get_lv_image_dsc());lv_obj_remove_flag(camera_image_,LV_OBJ_FLAG_HIDDEN);text_("camera_status","Snapshot updated");}else if(view_==CAMERA){camera_update_=0;}});
  snapshot_->add_on_error_callback([this](){camera_loading_=false;/* Retain no stale photo after a failed request. */snapshot_ready_=false;if(view_==CAMERA){if(camera_image_)lv_obj_add_flag(camera_image_,LV_OBJ_FLAG_HIDDEN);text_("camera_status","Camera unavailable - check HA URL and camera access");}});
  set_timeout("start_ui",200,[this](){ready_=true;last_activity_=millis();if(!wifi::global_wifi_component->is_connected()&&wifi::global_wifi_component->get_sta().get_ssid().empty())view_=WIFI_SETTINGS;render_();});
}

void WorkshopPanel::watch_(const std::string &entity,const std::string &key,const std::string &attr) {
  auto [it,created]=state_sources_.try_emplace(key);
  auto &source=it->second;
  copy(source.entity,entity,sizeof(source.entity));copy(source.attribute,attr,sizeof(source.attribute));
  if(created)api::global_api_server->subscribe_home_assistant_state(source.entity,source.attribute,[this,key](StringRef value){receive_(key,value.str());});
}
void WorkshopPanel::subscribe_() {
  for(int i=0;i<MAP_COUNT;i++)if(i<12||i>=19)watch_(settings_.entity[i],"m"+std::to_string(i));
  for(int slot=0;slot<6;slot++)watch_(tile_settings_.switch_state[slot][0]?tile_settings_.switch_state[slot]:settings_.entity[SWITCH_IDS[slot]],"switch_state"+std::to_string(slot));
  for(int i:SENSOR_IDS)watch_(settings_.entity[i],"class"+std::to_string(i),"device_class");
  for(int i:SENSOR_IDS)watch_(settings_.entity[i],"unit"+std::to_string(i),"unit_of_measurement");
  watch_(settings_.entity[6],"weather_temp","temperature");
  watch_(settings_.entity[6],"weather_humidity","humidity");
  for(int p=0;p<2;p++) {
    const std::string prefix=settings_.entity[12+p],ams=settings_.entity[14+p];
    for(const auto *field:{"print_status","print_progress","remaining_time","task_name","nozzle_temperature","bed_temperature","current_layer","total_layer_count","mqtt_connection_mode"})watch_("sensor."+prefix+"_"+field,pk(p,field));
    for(const auto *field:{"print_error","hms_errors","online","developer_lan_mode"})watch_("binary_sensor."+prefix+"_"+field,pk(p,field));
    watch_("binary_sensor."+prefix+"_hms_errors",pk(p,"hms_message"),"1-Error");
    watch_("binary_sensor."+prefix+"_hms_errors",pk(p,"hms_code"),"1-Code");
    watch_("binary_sensor."+prefix+"_hms_errors",pk(p,"hms_severity"),"1-Severity");
    watch_("binary_sensor."+prefix+"_print_error",pk(p,"print_message"),"Error");
    watch_("sensor."+ams+"_humidity",pk(p,"humidity"));
    watch_("sensor."+ams+"_status",pk(p,"ams_status"));
    watch_(settings_.entity[10+p],pk(p,"picture"),"entity_picture");
    for(int s=0;s<4;s++){
      const auto entity="sensor."+ams+"_tray_"+std::to_string(s+1), key=pk(p,("slot"+std::to_string(s)).c_str());
      watch_(entity,key);
      for(const auto *a:{"color","type","remain","active","empty"})watch_(entity,key+"."+a,a);
    }
  }
}
void WorkshopPanel::receive_(const std::string &key,const std::string &value) {
  state_updates_++;last_state_at_=millis();
  if(key==pk(0,"print_status")||key==pk(1,"print_status"))ESP_LOGI(TAG,"Printer state %s=%s",key.c_str(),value.c_str());
  state_[key]=value;
  for(int p=0;p<2;p++) {
    if(key==pk(p,"print_status")&&!missing(value)) {
      const auto old=previous_status_[p];
      // Require a real observed transition; an initial finished state is not a new completion.
      if((old=="running"||old=="pause")&&value=="finish")notices_.push_back({p,false,"Your print has finished."});
      if(!old.empty()&&old!="failed"&&value=="failed")notices_.push_back({p,true,"Print failed. Check the printer for details."});
      previous_status_[p]=value;
      if(!idle_(p))homed_[p]=false;
    }
    if(key==pk(p,"print_error")&&truth(value)&&!truth(state_[pk(p,"error_latched")])){
      state_[pk(p,"error_latched")]=value;
      notices_.push_back({p,true,missing(get_(pk(p,"print_message")))?"Printer reports a print error. Check the fault panel.":get_(pk(p,"print_message"))});
    } else if(key==pk(p,"print_error")&&!truth(value))state_[pk(p,"error_latched")]=value;
  }
}
std::string WorkshopPanel::get_(const std::string &key)const {auto it=state_.find(key);return it==state_.end()?"":it->second;}
std::string WorkshopPanel::number_(const std::string &key,const char *unit)const {const float f=numeric(get_(key));if(!std::isfinite(f))return "--";char b[48];snprintf(b,sizeof(b),"%.0f%s",f,unit);return b;}
std::string WorkshopPanel::remaining_(int p)const {const float hours=numeric(get_(pk(p,"remaining_time")));if(!std::isfinite(hours)||hours<0)return "Time unavailable";int mins=std::lround(hours*60);return std::to_string(mins/60)+"h "+std::to_string(mins%60)+"m left";}
bool WorkshopPanel::busy_(int p)const {const auto s=get_(pk(p,"print_status"));return s=="running"||s=="pause"||s=="prepare"||s=="slicing";}
bool WorkshopPanel::idle_(int p)const {const auto s=get_(pk(p,"print_status"));return (s=="idle"||s=="finish"||s=="failed")&&truth(get_(pk(p,"online")))&&!truth(get_(pk(p,"print_error")));}
void WorkshopPanel::activity_(){last_activity_=millis();}
void WorkshopPanel::route_(int view){view_=view;rebuild_=true;activity_();}
void WorkshopPanel::loop(){if(!ready_)return;if(rebuild_){rebuild_=false;render_();}if(!notices_.empty()&&view_!=ALERT){view_=ALERT;render_();}}
void WorkshopPanel::update(){if(!ready_)return;brightness_refresh_();const bool wifi_now=wifi::global_wifi_component->is_connected();if(last_wifi_connected_&&!wifi_now){wifi_drops_++;ESP_LOGW(TAG,"Wi-Fi connection dropped");}last_wifi_connected_=wifi_now;const bool now=api::global_api_server->is_connected_with_state_subscription();if(connected_&&!now){ha_drops_++;ESP_LOGW(TAG,"HA state connection dropped; Wi-Fi=%s",wifi_now?"connected":"disconnected");previous_status_={};homed_={false,false};state_.clear();}if(!connected_&&now)ESP_LOGI(TAG,"HA state connection established");connected_=now;
  if(millis()-diagnostic_at_>=30000){diagnostic_at_=millis();ESP_LOGI(TAG,"Health: wifi=%d ha=%d updates=%lu wifi_drops=%lu ha_drops=%lu internal_heap=%u largest_block=%u",wifi_now,connected_,(unsigned long)state_updates_,(unsigned long)wifi_drops_,(unsigned long)ha_drops_,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));}
  if(view_==NORMAL&&(page_==1||page_==2)&&last_busy_!=busy_(page_-1)){rebuild_=true;return;}
  if(view_==NORMAL&&settings_.idle_seconds&&millis()-last_activity_>=uint32_t(settings_.idle_seconds)*1000){route_(IDLE);return;}
  if(view_==CAMERA&&millis()-camera_update_>=uint32_t(settings_.camera_seconds)*1000)refresh_camera_();
  refresh_();
}

lv_obj_t *WorkshopPanel::box_(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius){auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y-(parent==canvas_?content_origin_:0));lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,(color==PANEL||color==0x293744||color==0x204C3B||color==0x33414D||color==0x263B32||color==0x1B3029)&&(view_==NORMAL||view_==IDLE)?settings_.panel_opacity*255/100:LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(o,LV_OBJ_FLAG_GESTURE_BUBBLE);return o;}
lv_obj_t *WorkshopPanel::label_(lv_obj_t *parent,int x,int y,int w,const std::string &text,const lv_font_t *font,uint32_t color){const lv_font_t *base[]={&lv_font_montserrat_16,&lv_font_montserrat_20,&lv_font_montserrat_28,&lv_font_montserrat_48};for(int i=0;i<4;i++)if(font==base[i]){font=&fonts_[i];break;}auto *o=lv_label_create(parent);lv_obj_set_pos(o,x,y-(parent==canvas_?content_origin_:0));lv_obj_set_size(o,w,lv_font_get_line_height(font)+2);lv_label_set_text(o,translate_(text).c_str());lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex((view_==NORMAL||view_==IDLE)?(settings_.text_theme==1?0xFFFFFF:settings_.text_theme==2?0x17212B:color):color),0);lv_label_set_long_mode(o,settings_.long_text?LV_LABEL_LONG_SCROLL_CIRCULAR:LV_LABEL_LONG_WRAP);lv_obj_add_flag(o,LV_OBJ_FLAG_GESTURE_BUBBLE);return o;}
struct Action {std::function<void()> fn;lv_event_code_t code;};
void WorkshopPanel::bind_(lv_obj_t *obj,lv_event_code_t code,std::function<void()> fn){auto *a=new Action{std::move(fn),code};lv_obj_add_event_cb(obj,[](lv_event_t *e){auto *a=static_cast<Action*>(lv_event_get_user_data(e));if(lv_event_get_code(e)==LV_EVENT_DELETE){delete a;return;}if(lv_event_get_code(e)==a->code){auto fn=a->fn;fn();}},LV_EVENT_ALL,a);}
lv_obj_t *WorkshopPanel::button_(lv_obj_t *parent,int x,int y,int w,int h,const std::string &text,std::function<void()> action,uint32_t color){auto *b=box_(parent,x,y,w,h,color,10);lv_obj_set_style_bg_opa(b,(view_==NORMAL||view_==IDLE)?settings_.panel_opacity*255/100:LV_OPA_COVER,0);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);auto *l=label_(b,4,0,w-8,text,&lv_font_montserrat_16);lv_obj_set_height(l,std::min(h-8,36));lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(l,LV_ALIGN_CENTER,0,0);
  // A tiny indicator gives feedback without redrawing an entire card on touch-down.
  auto *pressed=box_(b,w-12,5,6,6,GREEN,3);lv_obj_add_flag(pressed,LV_OBJ_FLAG_HIDDEN);
  bind_(b,LV_EVENT_PRESSED,[pressed](){lv_obj_remove_flag(pressed,LV_OBJ_FLAG_HIDDEN);});
  bind_(b,LV_EVENT_RELEASED,[pressed](){lv_obj_add_flag(pressed,LV_OBJ_FLAG_HIDDEN);});
  bind_(b,LV_EVENT_PRESS_LOST,[pressed](){lv_obj_add_flag(pressed,LV_OBJ_FLAG_HIDDEN);});
  bind_(b,LV_EVENT_CLICKED,[this,action](){activity_();action();});return b;}
void WorkshopPanel::text_(const std::string &key,const std::string &value){auto it=labels_.find(key);if(it!=labels_.end()){auto translated=translate_(value);if(std::string(lv_label_get_text(it->second))!=translated)lv_label_set_text(it->second,translated.c_str());}}
void WorkshopPanel::render_(){
  if(view_==NORMAL&&rendered_view_==NORMAL){dashboard_page_();brightness_refresh_();return;}
  wallpaper_object_=display_preview_=preview_clock_=preview_weather_=preview_switch_=preview_sensor_=background_input_=nullptr;
  threshold_input_.fill(nullptr);state_input_.fill(nullptr);editor_state_=false;settings_body_=nullptr;
  dashboard_pages_={};chrome_labels_.clear();page_marks_.fill(nullptr);rendered_page_=-1;content_origin_=0;
  rendered_view_=view_;
  weather_icon_=nullptr;finish_banner_=nullptr;weather_icon_condition_.clear();cancel_timeout("entity_search");cancel_timeout("entity_search_timeout");editor_=editor_input_=editor_target_=search_status_=nullptr;search_request_++;suggestion_buttons_.fill(nullptr);suggestion_labels_.fill(nullptr);labels_.clear();toggles_.clear();spool_color_.fill(nullptr);slot_box_.fill(nullptr);progress_=nullptr;camera_image_=nullptr;entity_dropdown_=entity_input_=label_input_=keyboard_=settings_message_=nullptr;
  auto *screen=lv_screen_active();lv_obj_clean(screen);lv_obj_remove_style_all(screen);lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);canvas_=screen;
  if(!root_bound_){root_bound_=true;
    lv_display_add_event_cb(lv_display_get_default(),[](lv_event_t *e){
      auto *self=static_cast<WorkshopPanel*>(lv_event_get_user_data(e));
      if(lv_event_get_code(e)==LV_EVENT_RENDER_START)self->frame_started_=millis();
      else if(lv_event_get_code(e)==LV_EVENT_REFR_READY&&self->navigation_started_){
        ESP_LOGI(TAG,"Page %d ready: navigation=%lums draw=%lums",self->page_,(unsigned long)(millis()-self->navigation_started_),(unsigned long)(millis()-self->frame_started_));
        self->navigation_started_=0;
      }
    },LV_EVENT_ALL,this);
    for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input)){
      if(lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER)continue;
      lv_indev_set_gesture_min_distance(input,35);
      lv_indev_set_gesture_min_velocity(input,2);
      lv_timer_set_period(lv_indev_get_read_timer(input),16);
    }
    bind_(screen,LV_EVENT_PRESSED,[this](){activity_();});
    bind_(screen,LV_EVENT_CLICKED,[this](){if(view_==IDLE)route_(NORMAL);else if(view_==ALERT)dismiss_();});
    bind_(screen,LV_EVENT_GESTURE,[this](){if(view_!=NORMAL)return;auto *input=lv_indev_active();if(!input)return;const auto direction=lv_indev_get_gesture_dir(input);if(direction==LV_DIR_LEFT)page_=(page_+1)%page_count_();else if(direction==LV_DIR_RIGHT)page_=(page_+page_count_()-1)%page_count_();else return;lv_indev_wait_release(input);rebuild_=true;activity_();});
  }
  // Firmware-resident RGB565 wallpaper: no downloads or full-screen transforms.
  if(view_!=ALERT){
    auto *wallpaper=lv_image_create(canvas_);wallpaper_object_=wallpaper;
    lv_image_set_src(wallpaper,wallpaper_ready_&&settings_.wallpaper_url[0]?wallpaper_->get_lv_image_dsc():&background_dsc_);
    lv_obj_set_pos(wallpaper,0,0);
    lv_obj_remove_flag(wallpaper,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(wallpaper,LV_OBJ_FLAG_GESTURE_BUBBLE);
    if(!background_pixels_){auto *shade=box_(canvas_,0,0,800,480,0x060818,0);lv_obj_set_style_bg_opa(shade,LV_OPA_20,0);}
  }
  wifi_ssid_=wifi_password_=wifi_dropdown_=nullptr;
  switch(view_){case NORMAL:normal_();break;case IDLE:idle_screen_();break;case SETTINGS_DISPLAY:settings_screen_(false);break;case SETTINGS_ENTITIES:settings_screen_(true);break;case CAMERA:camera_screen_();break;case ALERT:alert_screen_();break;case WIFI_SETTINGS:wifi_screen_();break;}
  refresh_();
  brightness_refresh_();
}
void WorkshopPanel::normal_(){auto *title=label_(canvas_,24,19,300,"Workshop",&lv_font_montserrat_28);lv_obj_add_flag(title,LV_OBJ_FLAG_CLICKABLE);bind_(title,LV_EVENT_LONG_PRESSED,[this](){route_(SETTINGS_DISPLAY);});
  labels_["page_title"]=label_(canvas_,24,56,300,"",&lv_font_montserrat_16,MUTED);labels_["wifi_icon"]=label_(canvas_,602,23,42,LV_SYMBOL_WIFI,&lv_font_montserrat_28,MUTED);
  labels_["ha_icon"]=label_(canvas_,660,23,42,LV_SYMBOL_HOME,&lv_font_montserrat_28,MUTED);
  labels_["wifi_cross"]=label_(canvas_,629,41,20,LV_SYMBOL_CLOSE,&lv_font_montserrat_16,0xFF5C69);
  labels_["ha_cross"]=label_(canvas_,687,41,20,LV_SYMBOL_CLOSE,&lv_font_montserrat_16,0xFF5C69);
  lv_obj_add_flag(labels_["wifi_icon"],LV_OBJ_FLAG_CLICKABLE);bind_(labels_["wifi_icon"],LV_EVENT_CLICKED,[this](){route_(WIFI_SETTINGS);});
  lv_obj_add_flag(labels_["ha_icon"],LV_OBJ_FLAG_CLICKABLE);bind_(labels_["ha_icon"],LV_EVENT_CLICKED,[this](){route_(SETTINGS_DISPLAY);});
  auto *gear=button_(canvas_,710,12,70,52,LV_SYMBOL_SETTINGS,[this](){route_(SETTINGS_DISPLAY);});lv_obj_set_style_text_font(lv_obj_get_child(gear,0),&fonts_[2],0);
  for(int i=0;i<page_count_();i++){auto *dot=button_(canvas_,(800-page_count_()*46)/2+i*46,414,44,44,"",[this,i](){if(page_==i)return;page_=i;rebuild_=true;});auto *mark=box_(dot,18,18,8,8,0x566672,4);page_marks_[i]=mark;lv_obj_remove_flag(mark,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_bg_opa(dot,LV_OPA_TRANSP,0);}
  chrome_labels_=labels_;
  // Construct all enabled lightweight pages before the first frame is shown.
  // The first swipe then has the same reuse path as subsequent navigation.
  const int selected=std::min(page_,page_count_()-1);
  page_=std::min(page_,page_count_()-1);for(int i=0;i<page_count_();i++){page_=i;dashboard_page_();}
  page_=selected;
  dashboard_page_();
}

void WorkshopPanel::prepare_background_(){
  background_dsc_=*background_->get_lv_image_dsc();
  background_pixels_=static_cast<uint8_t*>(heap_caps_malloc(background_dsc_.data_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!background_pixels_){ESP_LOGW(TAG,"Wallpaper cache unavailable; using flash image and overlay");return;}
  // Blend the same 20% shade once; keep opaque native RGB565 in PSRAM.
  for(size_t i=0;i<background_dsc_.data_size;i+=2){
    const uint16_t pixel=background_dsc_.data[i]|(uint16_t(background_dsc_.data[i+1])<<8);
    const unsigned r=(((pixel>>11)&31)*204+((0x06>>3)*51)+127)/255;
    const unsigned g=(((pixel>>5)&63)*204+((0x08>>2)*51)+127)/255;
    const unsigned b=((pixel&31)*204+((0x18>>3)*51)+127)/255;
    const uint16_t shaded=(r<<11)|(g<<5)|b;
    background_pixels_[i]=shaded&255;background_pixels_[i+1]=shaded>>8;
  }
  background_dsc_.data=background_pixels_;
  ESP_LOGI(TAG,"Opaque wallpaper cached in PSRAM (%u bytes)",unsigned(background_dsc_.data_size));
}

void WorkshopPanel::dashboard_page_(){
  const auto started=millis();navigation_started_=started;
  auto &cached=dashboard_pages_[page_];
  const bool printer_page=page_==1||page_==2;
  const bool busy=printer_page&&busy_(page_-1);
  const bool stale=cached.root&&printer_page&&(cached.busy!=busy||(!busy&&cached.step!=step_));
  if(rendered_page_>=0&&rendered_page_!=page_)lv_obj_add_flag(dashboard_pages_[rendered_page_].root,LV_OBJ_FLAG_HIDDEN);
  if(stale){lv_obj_delete(cached.root);cached=DashboardPage{};}
  const bool reuse=cached.root!=nullptr;
  if(!reuse){
    labels_=chrome_labels_;toggles_.clear();spool_color_.fill(nullptr);slot_box_.fill(nullptr);progress_=nullptr;
    canvas_=lv_screen_active();
    cached.root=box_(canvas_,0,96,800,308,BG,0);
    lv_obj_set_style_bg_opa(cached.root,LV_OPA_TRANSP,0);
    canvas_=cached.root;content_origin_=96;
    if(page_==0)home_();else if(page_==3)lasers_();else if(page_==4)home_();else printer_(page_-1);
    content_origin_=0;canvas_=lv_screen_active();
    cached.labels=labels_;cached.toggles=toggles_;cached.spool=spool_color_;cached.slots=slot_box_;cached.progress=progress_;cached.busy=busy;cached.step=step_;
  }else{
    labels_=cached.labels;toggles_=cached.toggles;spool_color_=cached.spool;slot_box_=cached.slots;progress_=cached.progress;last_busy_=cached.busy;
    lv_obj_remove_flag(cached.root,LV_OBJ_FLAG_HIDDEN);
  }
  const char *titles[]={"Lights & energy","P1S printer","X2D printer","xTool lasers","More controls"};text_("page_title",titles[page_]);
  for(int i=0;i<page_count_();i++){
    auto *mark=page_marks_[i];lv_obj_set_x(mark,i==page_?8:18);lv_obj_set_width(mark,i==page_?28:8);
    lv_obj_set_style_bg_color(mark,lv_color_hex(i==page_?GREEN:0x566672),0);
  }
  rendered_page_=page_;refresh_();
  ESP_LOGI(TAG,"Page %d widgets: %s %lums",page_,reuse?"reused":"built",(unsigned long)(millis()-started));
}

void WorkshopPanel::benchmark_pages(){
  if(!ready_||benchmark_active_||view_!=NORMAL||editor_)return;
  benchmark_active_=true;benchmark_step_(0);
}
void WorkshopPanel::benchmark_step_(int step){
  if(view_!=NORMAL||step>=page_count_()*2+1){benchmark_active_=false;return;}
  page_=step%page_count_();rebuild_=true;activity_();
  set_timeout("page_benchmark",1200,[this,step](){benchmark_step_(step+1);});
}
int WorkshopPanel::page_count_() const {int sw=0,se=0;for(int i=0;i<6;i++)sw+=(settings_.visible_switches>>i)&1;for(int i=0;i<3;i++)se+=(settings_.visible_sensors>>i)&1;return sw>4||se>2?5:4;}
void WorkshopPanel::home_(){
  int slot=0;for(int n=0;n<6;n++){int order=settings_.switch_order[n];if(!(settings_.visible_switches&(1<<order)))continue;int at=slot++;if(at/4!=(page_==4?1:0))continue;int i=SWITCH_IDS[order];at%=4;
    auto *b=button_(canvas_,24+(at%2)*182,100+(at/2)*136,168,120,"",[this,i](){toggle_(i);});toggles_[i]=b;lv_obj_set_style_bg_opa(b,settings_.panel_opacity*255/100,0);
    auto *name=label_(b,12,14,144,settings_.label[i],&lv_font_montserrat_16);lv_obj_set_height(name,48);labels_["switch_icon"+std::to_string(i)]=tile_icon_(b,12,66,switch_icon_(order));labels_["toggle"+std::to_string(i)]=label_(b,60,76,100,"",&lv_font_montserrat_16,MUTED);lv_obj_set_height(labels_["toggle"+std::to_string(i)],36);
  }
  slot=0;for(int n=0;n<3;n++){int order=settings_.sensor_order[n];if(!(settings_.visible_sensors&(1<<order)))continue;int at=slot++;if(at/2!=(page_==4?1:0))continue;int i=SENSOR_IDS[order];
    auto *panel=box_(canvas_,402,100+(at%2)*136,374,120,PANEL);lv_obj_set_style_bg_opa(panel,settings_.panel_opacity*255/100,0);auto *name=label_(panel,18,16,338,settings_.label[i],&lv_font_montserrat_16,MUTED);lv_obj_set_height(name,36);labels_["sensor_icon"+std::to_string(i)]=tile_icon_(panel,310,62,sensor_icon_(order));labels_["sensor"+std::to_string(i)]=label_(panel,18,62,280,"--",&lv_font_montserrat_28);
  }
}
void WorkshopPanel::printer_(int p){last_busy_=busy_(p);auto *left=box_(canvas_,24,100,366,300,PANEL),*right=box_(canvas_,406,100,370,300,PANEL);
  labels_["printer_status"]=label_(left,16,16,330,"",&lv_font_montserrat_16,MUTED);labels_["task"]=label_(left,16,48,330,"Waiting for printer",&lv_font_montserrat_20);labels_["percent"]=label_(left,16,89,150,"--",&lv_font_montserrat_48);labels_["remaining"]=label_(left,167,115,183,"",&lv_font_montserrat_16);
  progress_=lv_bar_create(left);lv_obj_set_pos(progress_,16,159);lv_obj_set_size(progress_,334,10);lv_obj_set_style_bg_color(progress_,lv_color_hex(0x33414D),LV_PART_MAIN);lv_obj_set_style_bg_color(progress_,lv_color_hex(GREEN),LV_PART_INDICATOR);lv_obj_set_style_border_width(progress_,0,LV_PART_MAIN);
  if(last_busy_){
    auto *track=box_(left,16,154,334,20,0x33414D,5);lv_obj_set_style_bg_opa(track,LV_OPA_TRANSP,0);
    auto *pulse=box_(track,0,0,38,20,GREEN,5);
    lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,pulse);lv_anim_set_values(&animation,-38,334);lv_anim_set_duration(&animation,1800);lv_anim_set_repeat_count(&animation,LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&animation,[](void *obj,int32_t value){lv_obj_set_x(static_cast<lv_obj_t*>(obj),value);lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(obj),90+int(120*(1-std::abs(value-148)/186.0f)),0);});lv_anim_start(&animation);
  }
  label_(left,16,190,142,"Nozzle",&lv_font_montserrat_16,MUTED);label_(left,183,190,142,"Bed",&lv_font_montserrat_16,MUTED);labels_["nozzle"]=label_(left,16,218,145,"--",&lv_font_montserrat_28);labels_["bed"]=label_(left,183,218,145,"--",&lv_font_montserrat_28);
  if(last_busy_){button_(left,240,260,110,32,"Camera",[this,p](){camera_printer_=p;route_(CAMERA);});label_(right,14,15,342,"AMS & print info",&lv_font_montserrat_16,MUTED);
    for(int i=0;i<4;i++){auto *slot=box_(right,14+i*87,48,81,105,0x293744,8);slot_box_[i]=slot;spool_color_[i]=box_(slot,32,13,17,28,0x53616B,2);box_(slot,28,10,5,34,0xF5F6F7,2);box_(slot,49,10,5,34,0xF5F6F7,2);labels_["slot"+std::to_string(i)]=label_(slot,3,58,75,"--",&lv_font_montserrat_16);labels_["remain"+std::to_string(i)]=label_(slot,3,81,75,"--",&lv_font_montserrat_16,MUTED);}
    labels_["humidity"]=label_(right,14,166,342,"Humidity --",&lv_font_montserrat_16,MUTED);labels_["layers"]=label_(right,14,196,342,"Layer --",&lv_font_montserrat_16);labels_["ams"]=label_(right,14,222,342,"AMS --",&lv_font_montserrat_16);auto *fault=box_(right,14,251,342,36,0x263B32,6);labels_["fault"]=label_(fault,8,8,326,"No active faults",&lv_font_montserrat_16);
    lv_obj_add_flag(fault,LV_OBJ_FLAG_CLICKABLE);bind_(fault,LV_EVENT_CLICKED,[this,p](){const auto msg=get_(pk(p,"hms_message"));if(truth(get_(pk(p,"hms_errors")))||truth(get_(pk(p,"print_error")))){notices_.push_back({p,true,shown(msg)+"\n"+shown(get_(pk(p,"hms_code")))});}});
  }else{label_(right,14,15,342,"Manual movement",&lv_font_montserrat_16,MUTED);auto *wheel=box_(right,45,49,174,174,0x33414D,87);
    button_(wheel,58,3,58,45,"Y+",[this,p](){move_(p,"Y",step_);});button_(wheel,58,126,58,45,"Y-",[this,p](){move_(p,"Y",-step_);});button_(wheel,3,65,54,44,"X-",[this,p](){move_(p,"X",-step_);});button_(wheel,117,65,54,44,"X+",[this,p](){move_(p,"X",step_);});
    auto *home=button_(wheel,58,65,58,44,LV_SYMBOL_HOME,[this,p](){if(skip_home_click_){skip_home_click_=false;return;}move_(p,"Home",0);},0x204C3B);
    bind_(home,LV_EVENT_LONG_PRESSED,[this,p](){if(movement_allowed_(p)&&homing_requested_[p]){homed_[p]=true;homing_requested_[p]=false;skip_home_click_=true;text_("motion_note","Homing confirmed by user; ready to move");}});
    button_(right,276,63,68,49,LV_SYMBOL_UP,[this,p](){extrude_(p,false);});label_(right,252,125,108,"Extruder",&lv_font_montserrat_16,MUTED);button_(right,276,155,68,49,LV_SYMBOL_DOWN,[this,p](){extrude_(p,true);});
    for(int i=0;i<4;i++){const int distances[]={-10,-1,1,10};int distance=distances[i];button_(right,14+i*87,232,81,36,(distance<0?std::string(LV_SYMBOL_UP):std::string(LV_SYMBOL_DOWN))+" "+std::to_string(std::abs(distance)),[this,p,distance](){move_(p,"Z",distance);});}
    button_(right,14,48,72,28,std::to_string(step_)+" mm",[this](){step_=step_==1?10:1;rebuild_=true;});labels_["motion_note"]=label_(right,14,276,342,"Cloud mode: movement unavailable",&lv_font_montserrat_16,MUTED);
  }
}
void WorkshopPanel::lasers_(){for(int i=0;i<2;i++){auto *p=box_(canvas_,24+i*384,100,368,185,PANEL);label_(p,16,18,336,"xTool - Laser "+std::to_string(i+1),&lv_font_montserrat_28);auto *b=button_(p,16,82,336,70,"",[this,i](){toggle_(7+i);});toggles_[7+i]=b;labels_["laser"+std::to_string(i)]=label_(b,12,22,310,"Power --",&lv_font_montserrat_20);}
  auto *b=button_(canvas_,24,302,752,82,"",[this](){toggle_(9);});toggles_[9]=b;labels_["extractor_icon"]=tile_icon_(b,18,21,FAN_ICON);label_(b,74,27,434,settings_.label[9],&lv_font_montserrat_20);labels_["extractor"]=label_(b,520,27,214,"--",&lv_font_montserrat_20);lv_obj_set_style_text_align(labels_["extractor"],LV_TEXT_ALIGN_RIGHT,0);
}
void WorkshopPanel::idle_screen_(){
  label_(canvas_,62,80,330,"WORKSHOP",&lv_font_montserrat_20,MUTED);
  labels_["clock"]=label_(canvas_,58,145,280,"--:--",&lv_font_montserrat_48);
  labels_["date"]=label_(canvas_,62,270,355,"Waiting for HA time",&lv_font_montserrat_20,MUTED);
  labels_["weather_temp"]=label_(canvas_,570,145,165,"--",&lv_font_montserrat_48);
  labels_["weather"]=label_(canvas_,452,270,245,"Weather unavailable",&lv_font_montserrat_20,MUTED);
  lv_label_set_long_mode(labels_["weather"],LV_LABEL_LONG_WRAP);lv_obj_set_height(labels_["weather"],48);
  for(const auto *key:{"clock","weather_temp","weather"}){
    lv_obj_set_style_transform_scale_x(labels_[key],333,0);lv_obj_set_style_transform_scale_y(labels_[key],333,0);
  }
  // Existing clock is 333/256; 400/256 adds another 20 percent.
  lv_obj_set_style_transform_scale_x(labels_["clock"],400,0);
  lv_obj_set_style_transform_scale_y(labels_["clock"],400,0);
  lv_obj_set_style_transform_scale_x(labels_["date"],307,0);
  lv_obj_set_style_transform_scale_y(labels_["date"],307,0);
  lv_obj_set_width(labels_["clock"],230);
  lv_obj_set_width(labels_["date"],315);
  weather_icon_=box_(canvas_,452,124,80,80,BG,0);
  lv_obj_set_style_bg_opa(weather_icon_,LV_OPA_TRANSP,0);
  lv_obj_set_style_transform_scale_x(weather_icon_,333,0);lv_obj_set_style_transform_scale_y(weather_icon_,333,0);
  for(const auto *key:{"clock","weather_temp","weather"}){int scale=256*(std::string(key)=="clock"?settings_.clock_scale:settings_.weather_scale)/100;lv_obj_set_style_transform_scale_x(labels_[key],scale,0);lv_obj_set_style_transform_scale_y(labels_[key],scale,0);}
  // Keep transformed text within its half of the screen at the largest setting.
  lv_obj_set_width(labels_["clock"],35000/settings_.clock_scale);lv_obj_set_width(labels_["weather_temp"],19500/settings_.weather_scale);lv_obj_set_width(labels_["weather"],28500/settings_.weather_scale);
  if(!settings_.weather_visible){for(const auto *key:{"weather_temp","weather"})lv_obj_add_flag(labels_[key],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(weather_icon_,LV_OBJ_FLAG_HIDDEN);}
  if(!(settings_.weather_parts&1))lv_obj_add_flag(labels_["weather_temp"],LV_OBJ_FLAG_HIDDEN);
  if(!(settings_.weather_parts&2))lv_obj_add_flag(labels_["weather"],LV_OBJ_FLAG_HIDDEN);
  if(!(settings_.weather_parts&4))lv_obj_add_flag(weather_icon_,LV_OBJ_FLAG_HIDDEN);
  if(!settings_.date_visible)lv_obj_add_flag(labels_["date"],LV_OBJ_FLAG_HIDDEN);
  auto *banner=box_(canvas_,24,368,752,58,0x1B3029);finish_banner_=banner;lv_obj_add_flag(banner,LV_OBJ_FLAG_HIDDEN);labels_["finish_banner"]=label_(banner,18,18,716,"No printers running",&lv_font_montserrat_20);label_(canvas_,285,445,250,"Touch anywhere to return",&lv_font_montserrat_16,MUTED);lv_obj_add_flag(canvas_,LV_OBJ_FLAG_CLICKABLE);
}
void WorkshopPanel::weather_icon_refresh_(){
  if(!weather_icon_)return;
  const auto condition=get_("m6");if(condition==weather_icon_condition_)return;
  weather_icon_condition_=condition;lv_obj_clean(weather_icon_);
  auto circle=[this](int x,int y,int size,uint32_t color){return box_(weather_icon_,x,y,size,size,color,size/2);};
  auto sun=[&](){
    circle(21,19,34,0xFFD369);
    for(int i=0;i<8;i++){auto *ray=box_(weather_icon_,36,2,4,11,0xFFD369,2);lv_obj_set_style_transform_pivot_x(ray,2,0);lv_obj_set_style_transform_pivot_y(ray,34,0);lv_obj_set_style_transform_rotation(ray,i*450,0);}
  };
  const bool known=condition=="sunny"||condition=="clear-night"||condition=="partlycloudy"||condition=="cloudy"||condition=="rainy"||condition=="pouring"||condition=="snowy"||condition=="snowy-rainy"||condition=="hail"||condition=="lightning"||condition=="lightning-rainy"||condition=="fog"||condition=="windy"||condition=="windy-variant"||condition=="exceptional";
  if(!known){label_(weather_icon_,25,18,50,"?",&lv_font_montserrat_48,MUTED);return;}
  if(condition=="sunny"){sun();return;}
  if(condition=="clear-night"){circle(17,10,48,0xD7E4FF);circle(34,3,43,BG);return;}
  if(condition=="partlycloudy")sun();
  if(condition=="exceptional"){label_(weather_icon_,23,20,55,LV_SYMBOL_WARNING,&lv_font_montserrat_48,0xFFD369);return;}
  const uint32_t cloud=condition=="partlycloudy"?0xE4ECF4:0xAFC3D5;
  circle(9,29,29,cloud);circle(25,18,38,cloud);circle(48,32,25,cloud);box_(weather_icon_,17,39,47,18,cloud,6);
  if(condition=="rainy"||condition=="pouring"||condition=="lightning-rainy"||condition=="snowy-rainy"){
    for(int i=0;i<3;i++){auto *drop=box_(weather_icon_,20+i*18,61,4,condition=="pouring"?16:11,0x70BBFF,2);lv_obj_set_style_transform_rotation(drop,200,0);}
  }
  if(condition=="snowy"||condition=="snowy-rainy"||condition=="hail")for(int i=0;i<3;i++)circle(16+i*21,condition=="snowy-rainy"?77:65,5,0xEEF3F7);
  if(condition=="lightning"||condition=="lightning-rainy")label_(weather_icon_,34,46,32,LV_SYMBOL_CHARGE,&lv_font_montserrat_28,0xFFD369);
  if(condition=="fog"||condition=="windy"||condition=="windy-variant")for(int i=0;i<3;i++)box_(weather_icon_,8+i*5,61+i*7,62-i*8,3,0xAFC3D5,1);
}
void WorkshopPanel::settings_screen_(bool entities){button_(canvas_,348,69,150,40,"Wi-Fi",[this](){route_(WIFI_SETTINGS);});label_(canvas_,24,18,620,"Workshop settings",&lv_font_montserrat_28);button_(canvas_,670,16,106,44,"Back",[this](){route_(NORMAL);});button_(canvas_,24,69,150,40,"Display",[this](){route_(SETTINGS_DISPLAY);},entities?PANEL:0x204C3B);button_(canvas_,186,69,150,40,"Entities",[this](){route_(SETTINGS_ENTITIES);},entities?0x204C3B:PANEL);
  if(!entities){display_settings_();}
  else{
    label_(canvas_,24,119,752,"Control to configure",&lv_font_montserrat_16,MUTED);
    entity_dropdown_=lv_dropdown_create(canvas_);lv_obj_set_style_text_font(entity_dropdown_,&fonts_[1],0);lv_obj_set_style_text_font(lv_dropdown_get_list(entity_dropdown_),&fonts_[1],0);lv_obj_set_pos(entity_dropdown_,24,141);lv_obj_set_size(entity_dropdown_,752,44);
    std::string options;for(int i=0;i<MAP_COUNT;i++){if(i)options+='\n';std::string slot=i<4?"Switch "+std::to_string(i+1):i<6?"Sensor "+std::to_string(i-3):i==19?"Switch 5":i==20?"Switch 6":i==21?"Sensor 3":std::string(settings_.label[i]);options+=translate_(slot);}
    lv_dropdown_set_options(entity_dropdown_,options.c_str());lv_dropdown_set_selected(entity_dropdown_,edit_index_);
    bind_(entity_dropdown_,LV_EVENT_VALUE_CHANGED,[this](){edit_index_=lv_dropdown_get_selected(entity_dropdown_);load_fields_();});
    label_(canvas_,24,198,752,"Display name (shown on your buttons)",&lv_font_montserrat_16,MUTED);
    label_input_=lv_textarea_create(canvas_);lv_obj_set_style_text_font(label_input_,&fonts_[1],0);lv_obj_set_pos(label_input_,24,221);lv_obj_set_size(label_input_,752,44);lv_textarea_set_one_line(label_input_,true);lv_textarea_set_max_length(label_input_,31);
    labels_["mapping_field"]=label_(canvas_,24,278,752,"Home Assistant entity ID",&lv_font_montserrat_16,MUTED);
    entity_input_=lv_textarea_create(canvas_);lv_obj_set_style_text_font(entity_input_,&fonts_[1],0);lv_obj_set_pos(entity_input_,24,301);lv_obj_set_size(entity_input_,752,44);lv_textarea_set_one_line(entity_input_,true);lv_textarea_set_max_length(entity_input_,111);
    bind_(label_input_,LV_EVENT_CLICKED,[this](){open_editor_(label_input_,"Edit display name",false);});
    bind_(entity_input_,LV_EVENT_CLICKED,[this](){open_editor_(entity_input_,(edit_index_<12||edit_index_>=19)?"Find Home Assistant entity":"Edit connection value",(edit_index_<12||edit_index_>=19));});
    button_(canvas_,24,366,330,48,"Save changes",[this](){store_fields_();},0x204C3B);
    settings_message_=label_(canvas_,24,432,752,"Tap a field to edit. Changes apply without restarting.",&lv_font_montserrat_16,MUTED);load_fields_();}
}
void WorkshopPanel::load_fields_(){
  lv_textarea_set_text(label_input_,settings_.label[edit_index_]);lv_textarea_set_text(entity_input_,settings_.entity[edit_index_]);
  text_("mapping_field",(edit_index_<12||edit_index_>=19)?"Home Assistant entity ID (tap to search)":edit_index_<16?"Entity prefix (without domain or sensor suffix)":edit_index_==16?"Home Assistant URL (http:// or https://)":"Bambu integration device ID");
}
void WorkshopPanel::configure_keyboard_(){
  // LVGL stores keyboard maps globally: set both languages explicitly so a
  // language change also restores the English layout on the next keyboard.
  static const char *const english_lower[]={
    "1#","q","w","e","r","t","y","u","i","o","p",LV_SYMBOL_BACKSPACE,"\n",
    "ABC","a","s","d","f","g","h","j","k","l",LV_SYMBOL_NEW_LINE,"\n",
    "_","-","z","x","c","v","b","n","m",".",",",":","\n",
    LV_SYMBOL_CLOSE,LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
  static const char *const english_upper[]={
    "1#","Q","W","E","R","T","Y","U","I","O","P",LV_SYMBOL_BACKSPACE,"\n",
    "abc","A","S","D","F","G","H","J","K","L",LV_SYMBOL_NEW_LINE,"\n",
    "_","-","Z","X","C","V","B","N","M",".",",",":","\n",
    LV_SYMBOL_CLOSE,LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
  static const char *const german_lower[]={
    "1#","q","w","e","r","t","z","u","i","o","p","ü","ß",LV_SYMBOL_BACKSPACE,"\n",
    "ABC","a","s","d","f","g","h","j","k","l","ö","ä",LV_SYMBOL_NEW_LINE,"\n",
    "_","-","y","x","c","v","b","n","m",".",",",":","\n",
    LV_SYMBOL_CLOSE,LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
  static const char *const german_upper[]={
    "1#","Q","W","E","R","T","Z","U","I","O","P","Ü","ß",LV_SYMBOL_BACKSPACE,"\n",
    "abc","A","S","D","F","G","H","J","K","L","Ö","Ä",LV_SYMBOL_NEW_LINE,"\n",
    "_","-","Y","X","C","V","B","N","M",".",",",":","\n",
    LV_SYMBOL_CLOSE,LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
  static lv_buttonmatrix_ctrl_t controls[2][2][64]{};
  const char *const *maps[2][2]={{english_lower,english_upper},{german_lower,german_upper}};
  int language=settings_.language?1:0;
  for(int upper=0;upper<2;upper++){
    auto map=maps[language][upper];int button=0;
    for(int i=0;map[i][0];i++){
      if(strcmp(map[i],"\n")==0)continue;
      bool mode=strcmp(map[i],"1#")==0||strcmp(map[i],"ABC")==0||strcmp(map[i],"abc")==0;
      bool action=mode||strcmp(map[i],LV_SYMBOL_CLOSE)==0||strcmp(map[i],LV_SYMBOL_OK)==0;
      int width=strcmp(map[i]," ")==0?6:action?2:1;
      controls[language][upper][button++]=lv_buttonmatrix_ctrl_t(width|(action?LV_KEYBOARD_CTRL_BUTTON_FLAGS:0));
    }
    lv_keyboard_set_map(keyboard_,upper?LV_KEYBOARD_MODE_TEXT_UPPER:LV_KEYBOARD_MODE_TEXT_LOWER,map,controls[language][upper]);
  }
  lv_obj_set_style_text_font(keyboard_,&fonts_[1],LV_PART_ITEMS);
  lv_keyboard_set_mode(keyboard_,LV_KEYBOARD_MODE_TEXT_LOWER);
  // 219 / 190 = 1.153: larger key hit areas within the full screen width.
  lv_obj_set_size(keyboard_,800,219);
  bind_(keyboard_,LV_EVENT_PRESSED,[](){
    auto *input=lv_indev_active();if(input){lv_indev_set_long_press_time(input,450);lv_indev_set_long_press_repeat_time(input,80);}
  });
  auto restore=[](){auto *input=lv_indev_active();if(input){lv_indev_set_long_press_time(input,2000);lv_indev_set_long_press_repeat_time(input,100);}};
  bind_(keyboard_,LV_EVENT_RELEASED,restore);bind_(keyboard_,LV_EVENT_PRESS_LOST,restore);
}
void WorkshopPanel::open_editor_(lv_obj_t *target,const std::string &title,bool entity){
  if(editor_)return;
  editor_target_=target;editor_entity_=entity;
  editor_=box_(canvas_,0,0,800,480,BG,0);lv_obj_add_flag(editor_,LV_OBJ_FLAG_CLICKABLE);
  label_(editor_,24,17,460,title,&lv_font_montserrat_20);
  button_(editor_,534,8,115,40,"Cancel",[this](){close_editor_(false);});
  button_(editor_,661,8,115,40,"Done",[this](){close_editor_(true);},0x204C3B);
  editor_input_=lv_textarea_create(editor_);lv_obj_set_style_text_font(editor_input_,&fonts_[1],0);lv_obj_set_pos(editor_input_,24,58);lv_obj_set_size(editor_input_,752,44);lv_textarea_set_one_line(editor_input_,true);
  lv_textarea_set_max_length(editor_input_,target==label_input_?31:target==background_input_?255:111);lv_textarea_set_text(editor_input_,lv_textarea_get_text(target));
  lv_textarea_set_cursor_pos(editor_input_,LV_TEXTAREA_CURSOR_LAST);
  search_status_=label_(editor_,24,110,752,entity?"Type an entity ID or name to filter":"Done keeps your edit; Cancel discards it.",&lv_font_montserrat_16,MUTED);
  for(int i=0;i<3;i++){
    suggestion_buttons_[i]=button_(editor_,24,134+i*40,752,36,"",[this,i](){
      if(editor_input_&&!suggestion_ids_[i].empty()){lv_textarea_set_text(editor_input_,suggestion_ids_[i].c_str());close_editor_(true);}
    });
    suggestion_labels_[i]=label_(suggestion_buttons_[i],12,8,728,"",&lv_font_montserrat_16);
    lv_obj_add_flag(suggestion_buttons_[i],LV_OBJ_FLAG_HIDDEN);
  }
  keyboard_=lv_keyboard_create(editor_);configure_keyboard_();lv_obj_align(keyboard_,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_remove_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(keyboard_,editor_input_);
  bind_(keyboard_,LV_EVENT_READY,[this](){close_editor_(true);});bind_(keyboard_,LV_EVENT_CANCEL,[this](){close_editor_(false);});
  bind_(editor_input_,LV_EVENT_VALUE_CHANGED,[this](){activity_();search_entities_();});
  search_entities_();
}
void WorkshopPanel::close_editor_(bool accept){
  if(!editor_)return;
  cancel_timeout("entity_search");cancel_timeout("entity_search_timeout");search_request_++;
  if(accept)lv_textarea_set_text(editor_target_,lv_textarea_get_text(editor_input_));
  lv_keyboard_set_textarea(keyboard_,nullptr);lv_obj_delete_async(editor_);
  editor_=editor_input_=editor_target_=search_status_=keyboard_=nullptr;editor_state_=false;suggestion_buttons_.fill(nullptr);suggestion_labels_.fill(nullptr);
  activity_();
}
void WorkshopPanel::search_entities_(){
  if(!editor_||!editor_entity_)return;
  cancel_timeout("entity_search");cancel_timeout("entity_search_timeout");
  search_request_++;for(auto *b:suggestion_buttons_)lv_obj_add_flag(b,LV_OBJ_FLAG_HIDDEN);suggestion_ids_.fill("");
  if(!connected_){lv_label_set_text(search_status_,translate_("HA offline - you can still enter an exact entity ID").c_str());return;}
  lv_label_set_text(search_status_,translate_("Searching Home Assistant...").c_str());
  const auto request=search_request_;
  set_timeout("entity_search",350,[this,request](){
    if(!editor_||request!=search_request_)return;
    const std::string kind=editor_state_?"control":(edit_index_<4||edit_index_==19||edit_index_==20||(edit_index_>=7&&edit_index_<=9))?"control":(edit_index_<6||edit_index_==21)?"sensor":edit_index_==6?"weather":"camera";
    service_("script.panda_entity_search",{{"query",lv_textarea_get_text(editor_input_)},{"kind",kind},{"request_id",std::to_string(request)}});
    set_timeout("entity_search_timeout",6000,[this,request](){if(editor_&&request==search_request_)lv_label_set_text(search_status_,translate_("Search unavailable - enter the exact ID, then Done").c_str());});
  });
}
void WorkshopPanel::entity_search_results(std::string request_id,std::string results){
  if(!editor_||!editor_entity_||request_id!=std::to_string(search_request_))return;
  cancel_timeout("entity_search_timeout");int count=0;size_t start=0;
  while(start<results.size()&&count<3){auto end=results.find('\n',start);if(end==std::string::npos)end=results.size();auto line=results.substr(start,end-start);auto split=line.find('|');auto id=line.substr(0,split);
    if(!id.empty()&&id.size()<=111&&id.find('.')!=std::string::npos&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.")==std::string::npos){
      suggestion_ids_[count]=id;lv_label_set_text(suggestion_labels_[count],line.c_str());lv_obj_remove_flag(suggestion_buttons_[count],LV_OBJ_FLAG_HIDDEN);count++;
    }start=end+1;
  }
  lv_label_set_text(search_status_,translate_(count?"Tap a match to use it; type more to narrow the first 3 results":"No matches - try another name or enter an exact ID").c_str());
}
void WorkshopPanel::store_fields_(){std::string id=lv_textarea_get_text(entity_input_),name=lv_textarea_get_text(label_input_);bool valid=!id.empty()&&!name.empty();if((edit_index_<12||edit_index_>=19))valid=valid&&id.find('.')!=std::string::npos&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.")==std::string::npos;if(edit_index_>=12&&edit_index_<=15)valid=valid&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos;if(edit_index_==16)valid=valid&&(id.rfind("http://",0)==0||id.rfind("https://",0)==0);if(!valid){lv_label_set_text(settings_message_,translate_("Invalid label or entity ID / URL").c_str());return;}const bool source_changed=id!=settings_.entity[edit_index_];
  copy(settings_.entity[edit_index_],id,112);copy(settings_.label[edit_index_],name,32);save_();
  if(source_changed){
    // Reuse the existing subscription slots; reconnect releases HA's old listeners.
    // Defer until outside the LVGL event callback. The display itself stays running.
    subscriptions_refresh_();
  }
  lv_label_set_text(settings_message_,translate_(source_changed?"Saved. Refreshing HA connection; display stays on.":"Saved. Changes applied.").c_str());}
void WorkshopPanel::save_(){pref_.save(&settings_);global_preferences->sync();}

void WorkshopPanel::camera_screen_(){label_(canvas_,24,18,560,std::string(NAMES[camera_printer_])+" camera",&lv_font_montserrat_28);button_(canvas_,670,16,106,44,"Back",[this](){route_(NORMAL);});camera_image_=lv_image_create(canvas_);lv_obj_align(camera_image_,LV_ALIGN_CENTER,0,7);lv_obj_add_flag(camera_image_,LV_OBJ_FLAG_HIDDEN);labels_["camera_status"]=label_(canvas_,24,434,752,"Loading snapshot...",&lv_font_montserrat_16,MUTED);snapshot_ready_=false;camera_update_=0;refresh_camera_();}
void WorkshopPanel::refresh_camera_(){if(camera_loading_||view_!=CAMERA)return;camera_update_=millis();const auto picture=get_(pk(camera_printer_,"picture"));if(!connected_||missing(picture)){text_("camera_status","Waiting for camera entity_picture from HA");return;}std::string base=settings_.entity[16];while(!base.empty()&&base.back()=='/')base.pop_back();if(picture.rfind("/api/",0)!=0){text_("camera_status","Unsupported camera path; use local HA camera proxy");return;}snapshot_->set_url(base+picture);camera_loading_=true;image_request_printer_=camera_printer_;snapshot_->update();}
void WorkshopPanel::alert_screen_(){if(notices_.empty()){route_(NORMAL);return;}const auto &n=notices_.front();lv_obj_set_style_bg_color(canvas_,lv_color_hex(n.error?0x8E2834:0x126A40),0);auto *name=label_(canvas_,50,132,700,std::string(NAMES[n.printer])+(n.error?" - Error":" is Done"),&lv_font_montserrat_48);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);auto *message=label_(canvas_,65,230,670,n.message,&lv_font_montserrat_20);lv_label_set_long_mode(message,LV_LABEL_LONG_WRAP);lv_obj_set_height(message,130);lv_obj_set_style_text_align(message,LV_TEXT_ALIGN_CENTER,0);label_(canvas_,250,410,340,"Touch anywhere to return",&lv_font_montserrat_16);lv_obj_add_flag(canvas_,LV_OBJ_FLAG_CLICKABLE);}
void WorkshopPanel::dismiss_(){if(!notices_.empty())notices_.pop_front();route_(notices_.empty()?NORMAL:ALERT);}

void WorkshopPanel::refresh_(){if(!ready_)return;if(view_==WIFI_SETTINGS){wifi_refresh_();return;}if(view_==NORMAL){
    const bool wifi=wifi::global_wifi_component->is_connected();
    text_color(labels_["wifi_icon"],wifi?GREEN:0x687985);
    text_color(labels_["ha_icon"],connected_?GREEN:0x687985);
    if(wifi)lv_obj_add_flag(labels_["wifi_cross"],LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(labels_["wifi_cross"],LV_OBJ_FLAG_HIDDEN);
    if(connected_)lv_obj_add_flag(labels_["ha_cross"],LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(labels_["ha_cross"],LV_OBJ_FLAG_HIDDEN);
  }
  if(view_==IDLE){auto now=clock_->now();if(now.is_valid()){text_("clock",now.strftime("%H:%M"));text_("date",now.strftime("%A %d %B"));}weather_icon_refresh_();text_("weather_temp",number_("weather_temp"," °C"));auto weather=shown(get_("m6"));std::replace(weather.begin(),weather.end(),'-',' ');text_("weather",weather);std::string running;for(int p=0;p<2;p++)if(busy_(p)){if(!running.empty())running+="   |   ";running+=std::string(NAMES[p])+" - "+remaining_(p);}text_("finish_banner",running);
    if(finish_banner_){if(connected_&&!running.empty())lv_obj_remove_flag(finish_banner_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(finish_banner_,LV_OBJ_FLAG_HIDDEN);}return;}
  if(view_!=NORMAL)return;
  for(const auto &[i,button]:toggles_){if(i<4||i==19||i==20)continue;const auto value=get_("m"+std::to_string(i));background_color(button,truth(value)?0x204C3B:0x293744);if(i==7||i==8)text_("laser"+std::to_string(i-7),missing(value)?"Power unavailable":truth(value)?"Power On":"Power Off");if(i==9)text_("extractor",missing(value)?"Unavailable":truth(value)?"On":"Off");}
  for(int i:SENSOR_IDS)text_("sensor"+std::to_string(i),number_("m"+std::to_string(i),(missing(get_("unit"+std::to_string(i)))?"":(" "+get_("unit"+std::to_string(i)))).c_str()));
  tile_refresh_();
  if(page_!=1&&page_!=2)return;
  const int p=page_-1;
  text_("printer_status",std::string(NAMES[p])+" - "+shown(get_(pk(p,"print_status"))));text_("task",busy_(p)?shown(get_(pk(p,"task_name"))):idle_(p)?"Ready for the next print":"Printer status unavailable");text_("percent",busy_(p)?number_(pk(p,"print_progress"),"%"):"--");text_("remaining",busy_(p)?remaining_(p):"No active job");text_("nozzle",number_(pk(p,"nozzle_temperature")," °C"));text_("bed",number_(pk(p,"bed_temperature")," °C"));
  if(progress_){const float value=numeric(get_(pk(p,"print_progress")));lv_bar_set_value(progress_,busy_(p)&&std::isfinite(value)?std::clamp<int>(int(value),0,100):0,LV_ANIM_OFF);}
  text_("humidity","Humidity "+number_(pk(p,"humidity"),"%"));text_("layers","Layer "+shown(get_(pk(p,"current_layer")))+" / "+shown(get_(pk(p,"total_layer_count"))));text_("ams","AMS: "+shown(get_(pk(p,"ams_status"))));
  auto fault=truth(get_(pk(p,"print_error")))?"Print error - tap for details":truth(get_(pk(p,"hms_errors")))?shown(get_(pk(p,"hms_message"))):"No active faults";text_("fault",fault);text_("motion_note",movement_allowed_(p)?(homed_[p]?"Ready - tap to move":"Home axes before movement"):"Movement unavailable in this connection");
  for(int i=0;i<4;i++){const auto key=pk(p,("slot"+std::to_string(i)).c_str());const bool empty=truth(get_(key+".empty"));text_("slot"+std::to_string(i),empty?"Empty":shown(get_(key+".type")));const float remain=numeric(get_(key+".remain"));text_("remain"+std::to_string(i),empty||!std::isfinite(remain)||remain<0||remain>100?"--":std::to_string(int(remain))+"% left");const auto color=get_(key+".color");uint32_t rgb=0x53616B;if(color.size()>=7&&color[0]=='#')rgb=strtoul(color.substr(1,6).c_str(),nullptr,16);if(spool_color_[i])background_color(spool_color_[i],rgb);if(slot_box_[i])background_color(slot_box_[i],truth(get_(key+".active"))?0x204C3B:0x293744);}
}
void WorkshopPanel::service_(const std::string &service,const std::map<std::string,std::string> &data){if(!connected_)return;call_homeassistant_service(service,data);}
void WorkshopPanel::toggle_(int map){const std::string id=settings_.entity[map];if(missing(get_("m"+std::to_string(map)))||!connected_)return;const auto dot=id.find('.');if(dot==std::string::npos)return;const auto domain=id.substr(0,dot);if(domain=="script")service_("script.turn_on",{{"entity_id",id}});else if(domain=="light"||domain=="switch"||domain=="input_boolean")service_(domain+".toggle",{{"entity_id",id}});}
bool WorkshopPanel::movement_allowed_(int p)const{return connected_&&idle_(p)&&truth(get_(pk(p,"developer_lan_mode")))&&!missing(settings_.entity[17+p]);}
void WorkshopPanel::move_(int p,const std::string &axis,int distance){if(!movement_allowed_(p)){text_("motion_note","Requires LAN Only + Developer Mode");return;}if(axis!="Home"&&!homed_[p]){text_("motion_note","Home axes first; then verify at printer");return;}service_("bambu_lab.move_axis",{{"device_id",settings_.entity[17+p]},{"axis",axis},{"distance",std::to_string(distance)}});if(axis=="Home"){homed_[p]=false;homing_requested_[p]=true;text_("motion_note","Verify homing, then hold Home to confirm");}else text_("motion_note","Movement requested");}
void WorkshopPanel::extrude_(int p,bool retract){if(!movement_allowed_(p)||!homed_[p]){text_("motion_note","Requires verified homing and local control");return;}const float temp=numeric(get_(pk(p,"nozzle_temperature")));if(!std::isfinite(temp)||temp<170){text_("motion_note","Nozzle must be at least 170 °C");return;}service_("bambu_lab.extrude_retract",{{"device_id",settings_.entity[17+p]},{"type",retract?"Retract":"Extrude"},{"force","false"}});}
void WorkshopPanel::wifi_screen_() {
  label_(canvas_,24,18,570,"Wi-Fi settings",&lv_font_montserrat_28);
  button_(canvas_,670,16,106,44,"Back",[this](){route_(SETTINGS_DISPLAY);});
  label_(canvas_,24,78,490,"Network name (2.4 GHz)",&lv_font_montserrat_16,MUTED);
  wifi_ssid_=lv_textarea_create(canvas_);lv_obj_set_style_text_font(wifi_ssid_,&fonts_[1],0);lv_obj_set_pos(wifi_ssid_,24,103);lv_obj_set_size(wifi_ssid_,420,44);lv_textarea_set_one_line(wifi_ssid_,true);lv_textarea_set_max_length(wifi_ssid_,32);
  const auto saved=wifi::global_wifi_component->get_sta();const auto saved_ssid=saved.get_ssid().str();lv_textarea_set_text(wifi_ssid_,saved_ssid.c_str());
  wifi_dropdown_=lv_dropdown_create(canvas_);lv_obj_set_style_text_font(wifi_dropdown_,&fonts_[1],0);lv_obj_set_style_text_font(lv_dropdown_get_list(wifi_dropdown_),&fonts_[1],0);lv_obj_set_pos(wifi_dropdown_,456,103);lv_obj_set_size(wifi_dropdown_,210,44);lv_dropdown_set_options(wifi_dropdown_,translate_("Choose network").c_str());
  button_(canvas_,678,103,98,44,"Scan",[this](){wifi_scan_();});
  bind_(wifi_dropdown_,LV_EVENT_VALUE_CHANGED,[this](){const unsigned selected=lv_dropdown_get_selected(wifi_dropdown_);if(selected>0&&selected<=wifi_networks_.size())lv_textarea_set_text(wifi_ssid_,wifi_networks_[selected-1].c_str());});
  label_(canvas_,24,162,600,"Wi-Fi password",&lv_font_montserrat_16,MUTED);
  wifi_password_=lv_textarea_create(canvas_);lv_obj_set_style_text_font(wifi_password_,&fonts_[1],0);lv_obj_set_pos(wifi_password_,24,187);lv_obj_set_size(wifi_password_,642,44);lv_textarea_set_one_line(wifi_password_,true);lv_textarea_set_max_length(wifi_password_,64);lv_textarea_set_password_mode(wifi_password_,true);
  // Never populate the input with an existing saved password or send it to logs.
  lv_textarea_set_placeholder_text(wifi_password_,translate_("Enter password (blank for an open network)").c_str());wifi_password_visible_=false;
  button_(canvas_,678,187,98,44,"Show",[this](){wifi_password_visible_=!wifi_password_visible_;lv_textarea_set_password_mode(wifi_password_,!wifi_password_visible_);});
  labels_["wifi_status"]=label_(canvas_,24,249,752,"",&lv_font_montserrat_16);lv_obj_set_height(labels_["wifi_status"],42);lv_label_set_long_mode(labels_["wifi_status"],LV_LABEL_LONG_WRAP);
  button_(canvas_,24,306,355,52,"Save & connect",[this](){wifi_connect_();},0x204C3B);
  button_(canvas_,391,306,385,52,"Reconnect saved network",[this](){auto *wifi=wifi::global_wifi_component;if(wifi->get_sta().get_ssid().empty()){text_("wifi_status","No saved network. Enter its name and password first.");return;}wifi_connecting_=true;wifi_attempt_=millis();wifi->disable();set_timeout("wifi_restart",500,[](){wifi::global_wifi_component->enable();});});
  label_(canvas_,24,382,752,"Fallback: Workshop Setup / your setup password",&lv_font_montserrat_16,MUTED);
  label_(canvas_,24,413,752,"After Wi-Fi connects, add Workshop Panda Touch in Home Assistant.",&lv_font_montserrat_16,MUTED);
  keyboard_=lv_keyboard_create(canvas_);configure_keyboard_();lv_obj_align(keyboard_,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
  for(auto *input:{wifi_ssid_,wifi_password_})bind_(input,LV_EVENT_FOCUSED,[this,input](){lv_keyboard_set_textarea(keyboard_,input);lv_keyboard_set_mode(keyboard_,LV_KEYBOARD_MODE_TEXT_LOWER);lv_obj_remove_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);});
  auto close_keyboard=[this](){lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(keyboard_,nullptr);lv_obj_remove_state(wifi_ssid_,LV_STATE_FOCUSED);lv_obj_remove_state(wifi_password_,LV_STATE_FOCUSED);};
  bind_(keyboard_,LV_EVENT_READY,close_keyboard);bind_(keyboard_,LV_EVENT_CANCEL,close_keyboard);
  wifi_refresh_();
}
void WorkshopPanel::wifi_scan_(){if(wifi_scanning_)return;wifi_scanning_=true;wifi_scan_started_=millis();text_("wifi_status","Scanning for 2.4 GHz networks...");wifi::global_wifi_component->start_scanning();}
void WorkshopPanel::on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results){
  wifi_scanning_=false;wifi_networks_.clear();
  for(const auto &result:results){const auto name=result.get_ssid().str();if(!name.empty()&&name.find('\n')==std::string::npos&&std::find(wifi_networks_.begin(),wifi_networks_.end(),name)==wifi_networks_.end())wifi_networks_.push_back(name);if(wifi_networks_.size()>=20)break;}
  if(view_==WIFI_SETTINGS&&wifi_dropdown_){std::string options=translate_("Choose network");for(const auto &name:wifi_networks_)options+='\n'+name;lv_dropdown_set_options(wifi_dropdown_,options.c_str());text_("wifi_status",wifi_networks_.empty()?"No networks found. Type the network name or scan again.":"Scan complete. Choose a network or type its name.");}
}
void WorkshopPanel::wifi_connect_(){
  if(!wifi_ssid_||!wifi_password_)return;
  const std::string ssid=lv_textarea_get_text(wifi_ssid_),password=lv_textarea_get_text(wifi_password_);
  const bool hex64=password.size()==64&&password.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos;
  if(ssid.empty()||ssid.size()>32){text_("wifi_status","Enter a network name of 1 to 32 bytes.");return;}
  if(!password.empty()&&!(password.size()>=8&&password.size()<=63)&&!hex64){text_("wifi_status","Password must be 8-63 characters (or 64 hex digits).");return;}
  lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(keyboard_,nullptr);lv_textarea_set_password_mode(wifi_password_,true);
  auto *wifi=wifi::global_wifi_component;wifi->save_wifi_sta(ssid,password);wifi->disable();wifi_connecting_=true;wifi_attempt_=millis();
  lv_textarea_set_text(wifi_password_,"");text_("wifi_status","Saved. Connecting to your home Wi-Fi...");
  set_timeout("wifi_restart",500,[](){wifi::global_wifi_component->enable();});
}
void WorkshopPanel::wifi_refresh_(){
  auto *wifi=wifi::global_wifi_component;
  if(wifi->is_connected()){wifi_connecting_=false;char ip[network::IP_ADDRESS_BUFFER_SIZE];const auto addresses=wifi->get_ip_addresses();addresses[0].str_to(ip);char ssid[wifi::SSID_BUFFER_SIZE];wifi->wifi_ssid_to(ssid);text_("wifi_status",std::string("Connected: ")+ssid+" | IP "+ip+(connected_?" | HA connected":" | Add ESPHome device in HA"));return;}
  if(wifi_connecting_){text_("wifi_status",millis()-wifi_attempt_<30000?"Connecting...":"Not connected yet. Check password, signal and 2.4 GHz network; retrying.");return;}
  if(wifi_scanning_){if(millis()-wifi_scan_started_>20000){wifi_scanning_=false;text_("wifi_status","Scan timed out. Try again or type the network name.");}return;}
  if(wifi_networks_.empty())text_("wifi_status","Not connected. Scan or enter your home network details.");
}
}  // namespace esphome::workshop_panel





