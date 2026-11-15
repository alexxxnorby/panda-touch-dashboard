#include "workshop_panel.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_connection.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace esphome::workshop_panel {
namespace {
enum Icon {NONE, BULB, PLUG, POWER, THERMOMETER, HUMIDITY, SOLAR, FAN=FAN_ICON};
constexpr uint32_t GREEN=0x74DFA5, MUTED=0xA7B6C3, AMBER=0xFFBF47;
bool starts(const std::string &s,const char *prefix){return s.rfind(prefix,0)==0;}
bool unknown(const std::string &s){return s.empty()||s=="unknown"||s=="unavailable"||s=="None"||s=="null";}
bool on(const std::string &s){return s=="on"||s=="true"||s=="True";}
float parse_number(const std::string &s){char *end;float n=strtof(s.c_str(),&end);return end==s.c_str()||*end||!std::isfinite(n)?NAN:n;}
bool exceeds(const std::string &s,float limit){float n=parse_number(s);return std::isfinite(n)&&n>limit;}
int switch_kind(int selection,const std::string &entity){
  const int explicit_kind[]={NONE,BULB,PLUG,POWER,NONE,FAN};
  if(selection>0&&selection<6)return explicit_kind[selection];
  if(starts(entity,"fan."))return FAN;
  if(starts(entity,"light."))return BULB;
  if(starts(entity,"switch.")||starts(entity,"input_boolean."))return PLUG;
  return POWER;
}
int sensor_kind(int selection,const std::string &entity,const std::string &device_class,const std::string &unit){
  const int explicit_kind[]={NONE,THERMOMETER,HUMIDITY,SOLAR,POWER,NONE,FAN};
  if(selection>0&&selection<7)return explicit_kind[selection];
  if(entity.find("solar")!=std::string::npos)return SOLAR;
  if(device_class=="temperature"||unit=="\u00b0C"||unit=="\u00b0F")return THERMOMETER;
  if(device_class=="humidity")return HUMIDITY;
  if(device_class=="power"||device_class=="energy"||unit=="W"||unit=="kW"||unit=="Wh"||unit=="kWh")return POWER;
  return NONE;
}

// Vector icons avoid dependence on special font glyphs or downloaded images.
void draw_icon(lv_event_t *event){
  auto *object=static_cast<lv_obj_t*>(lv_event_get_target(event));
  auto *layer=lv_event_get_layer(event);lv_area_t area;lv_obj_get_coords(object,&area);
  const int kind=int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(object)));
  const auto color=lv_obj_get_style_text_color(object,LV_PART_MAIN);
  const bool glowing=lv_obj_has_state(object,LV_STATE_CHECKED);
  auto rectangle=[&](int x,int y,int w,int h,int radius,bool fill,lv_opa_t opacity=LV_OPA_COVER){
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=radius;d.bg_color=color;d.bg_opa=fill?opacity:lv_opa_t(LV_OPA_TRANSP);d.border_color=color;d.border_width=fill?0:2;d.border_opa=opacity;
    lv_area_t a{area.x1+x,area.y1+y,area.x1+x+w-1,area.y1+y+h-1};lv_draw_rect(layer,&d,&a);
  };
  auto line=[&](int x1,int y1,int x2,int y2,int width=3){
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=color;d.width=width;d.round_start=d.round_end=1;d.p1={lv_value_precise_t(area.x1+x1),lv_value_precise_t(area.y1+y1)};d.p2={lv_value_precise_t(area.x1+x2),lv_value_precise_t(area.y1+y2)};lv_draw_line(layer,&d);
  };
  if(kind==BULB){
    if(glowing)rectangle(3,1,34,34,17,true,LV_OPA_20);
    rectangle(11,7,18,20,10,glowing);line(15,28,25,28);line(16,33,24,33);line(18,37,22,37);
    if(glowing){line(20,0,20,3,2);line(3,16,6,16,2);line(34,16,37,16,2);line(6,3,9,6,2);line(31,6,34,3,2);}
  }else if(kind==PLUG){
    if(glowing)rectangle(5,8,30,28,9,true,LV_OPA_20);
    line(14,3,14,13);line(26,3,26,13);rectangle(9,12,22,17,5,false);line(20,29,20,37);
  }else if(kind==POWER){line(25,2,13,21,4);line(13,21,27,21,4);line(27,21,15,38,4);
  }else if(kind==THERMOMETER){rectangle(15,3,10,27,5,false);rectangle(10,23,20,17,9,true);line(20,14,20,30);line(29,9,33,9,2);line(29,16,33,16,2);
  }else if(kind==HUMIDITY){line(20,3,8,23,3);line(20,3,32,23,3);rectangle(7,19,26,20,12,false);line(13,28,16,33,2);
  }else if(kind==SOLAR){rectangle(2,12,36,21,2,false);line(14,13,14,31,2);line(26,13,26,31,2);line(3,22,37,22,2);line(20,34,20,38,2);line(11,38,29,38,2);rectangle(28,1,8,8,4,true);line(22,4,24,4,2);line(31,0,31,1,2);
  }else if(kind==FAN){
    rectangle(1,1,38,38,19,false);
    for(int blade=0;blade<3;blade++){
      float angle=blade*2.0943951f;
      auto point=[&](float x,float y){return std::make_pair(int(20+x*std::cos(angle)-y*std::sin(angle)),int(20+x*std::sin(angle)+y*std::cos(angle)));};
      auto a=point(3,0),b=point(7,-10),c=point(0,-13);
      line(a.first,a.second,b.first,b.second,6);line(b.first,b.second,c.first,c.second,6);
    }
    rectangle(15,15,10,10,5,true);
  }
}
}  // namespace

void WorkshopPanel::tile_settings_load_(){
  tile_pref_=global_preferences->make_preference<TileSettings>(0x57544C31);
  auto saved=std::make_unique<TileSettings>();if(tile_pref_.load(saved.get())&&saved->version==1)tile_settings_=*saved;
  for(int i=0;i<6;i++){tile_settings_.switch_icon[i]=std::min<int>(tile_settings_.switch_icon[i],5);tile_settings_.switch_state[i][111]=0;}
  for(int i=0;i<3;i++){tile_settings_.sensor_icon[i]=std::min<int>(tile_settings_.sensor_icon[i],6);if(!std::isfinite(tile_settings_.threshold[i]))tile_settings_.threshold[i]=1000;}
  tile_settings_.threshold_enabled&=7;
}
void WorkshopPanel::tile_settings_save_(){tile_pref_.save(&tile_settings_);global_preferences->sync();}
int WorkshopPanel::switch_icon_(int slot)const{
  std::string entity=tile_settings_.switch_state[slot][0]?tile_settings_.switch_state[slot]:settings_.entity[SWITCH_IDS[slot]];
  return switch_kind(tile_settings_.switch_icon[slot],entity);
}
int WorkshopPanel::sensor_icon_(int slot)const{return sensor_kind(tile_settings_.sensor_icon[slot],settings_.entity[SENSOR_IDS[slot]],get_("class"+std::to_string(SENSOR_IDS[slot])),get_("unit"+std::to_string(SENSOR_IDS[slot])));}
lv_obj_t *WorkshopPanel::tile_icon_(lv_obj_t *parent,int x,int y,int kind){
  auto *icon=box_(parent,x,y,40,40,0,0);lv_obj_set_style_bg_opa(icon,LV_OPA_TRANSP,0);lv_obj_set_style_text_color(icon,lv_color_hex(MUTED),0);
  lv_obj_set_user_data(icon,reinterpret_cast<void*>(intptr_t(kind)));lv_obj_add_event_cb(icon,draw_icon,LV_EVENT_DRAW_MAIN,nullptr);return icon;
}

void WorkshopPanel::tile_refresh_(){
  auto set_color=[](lv_obj_t *object,uint32_t rgb){auto color=lv_color_hex(rgb);if(!lv_color_eq(lv_obj_get_style_text_color(object,LV_PART_MAIN),color))lv_obj_set_style_text_color(object,color,0);};
  for(int slot=0;slot<6;slot++){
    int id=SWITCH_IDS[slot];auto button=toggles_.find(id);if(button==toggles_.end())continue;
    const std::string source=settings_.entity[id];const bool script=starts(source,"script.");
    auto value=tile_settings_.switch_state[slot][0]?get_("switch_state"+std::to_string(slot)):script?"":get_("m"+std::to_string(id));
    const bool active=connected_&&on(value);auto color=lv_color_hex(active?0x205D41:0x293744);
    if(!lv_color_eq(lv_obj_get_style_bg_color(button->second,LV_PART_MAIN),color))lv_obj_set_style_bg_color(button->second,color,0);
    const bool unavailable=!connected_||unknown(get_("m"+std::to_string(id)))||(tile_settings_.switch_state[slot][0]&&unknown(value));
    text_("toggle"+std::to_string(id),unavailable?"Unavailable":active?"On":"");
    auto icon=labels_.find("switch_icon"+std::to_string(id));if(icon!=labels_.end()){
      set_color(icon->second,unavailable?0x687985:active?(switch_icon_(slot)==BULB?0xFFD369:GREEN):MUTED);
      if(active&&!lv_obj_has_state(icon->second,LV_STATE_CHECKED))lv_obj_add_state(icon->second,LV_STATE_CHECKED);
      else if(!active&&lv_obj_has_state(icon->second,LV_STATE_CHECKED))lv_obj_remove_state(icon->second,LV_STATE_CHECKED);
    }
  }
  for(int slot=0;slot<3;slot++){
    int id=SENSOR_IDS[slot];auto value=labels_.find("sensor"+std::to_string(id));if(value==labels_.end())continue;
    auto raw=get_("m"+std::to_string(id));bool available=connected_&&std::isfinite(parse_number(raw));
    bool warning=available&&(tile_settings_.threshold_enabled&(1<<slot))&&exceeds(raw,tile_settings_.threshold[slot]);
    uint32_t normal=settings_.text_theme==2?0x17212B:settings_.text_theme==1?0xFFFFFF:0xEEF3F7;
    set_color(value->second,!available?0x687985:warning?AMBER:normal);
    auto icon=labels_.find("sensor_icon"+std::to_string(id));if(icon!=labels_.end()){
      // Device class/units can arrive after page construction. Update icon without rebuilding the page.
      int kind=sensor_icon_(slot);if(int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(icon->second)))!=kind){lv_obj_set_user_data(icon->second,reinterpret_cast<void*>(intptr_t(kind)));lv_obj_invalidate(icon->second);}
      set_color(icon->second,!available?0x687985:warning?AMBER:MUTED);
    }
  }
}

void WorkshopPanel::subscriptions_refresh_(){
  set_timeout("mapping_refresh",0,[this](){
    subscribe_();state_.clear();previous_status_={};homed_={false,false};homing_requested_={false,false};notices_.clear();snapshot_ready_=false;camera_update_=0;
    for(const auto &client:api::global_api_server->active_clients())if(client->is_authenticated()){api::DisconnectRequest request;(void)client->send_message(request);}
  });
}
void WorkshopPanel::save_tile_state_(int slot){
  std::string id=lv_textarea_get_text(state_input_[slot]);
  bool valid=id.empty()||((starts(id,"light.")||starts(id,"switch.")||starts(id,"input_boolean.")||starts(id,"binary_sensor."))&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.")==std::string::npos);
  if(!valid){text_("tile_status","Use a light, switch, input_boolean or binary_sensor entity");return;}
  bool changed=id!=tile_settings_.switch_state[slot];snprintf(tile_settings_.switch_state[slot],112,"%s",id.c_str());tile_settings_save_();
  if(changed)subscriptions_refresh_();
  text_("tile_status","Saved. Changes applied.");
}

void WorkshopPanel::tile_options_(lv_obj_t *body,int &y){
  label_(body,12,y,722,"Icons and sensor thresholds",&lv_font_montserrat_20);y+=42;
  auto choice=[this,body,&y](const std::string &name,const char *value,std::function<void()> action){
    auto *label=label_(body,12,y+5,335,name,&lv_font_montserrat_16);lv_obj_set_height(label,36);button_(body,360,y,374,44,value,[this,action](){action();tile_settings_save_();route_(SETTINGS_DISPLAY);});y+=54;
  };
  const char *switch_names[]={"Auto","Light bulb","Power plug","Power bolt","None","Fan"};
  for(int slot=0;slot<6;slot++){
    choice(translate_(settings_.label[SWITCH_IDS[slot]])+" - "+translate_("Icon"),switch_names[tile_settings_.switch_icon[slot]],[this,slot](){tile_settings_.switch_icon[slot]=(tile_settings_.switch_icon[slot]+1)%6;});
    if(starts(settings_.entity[SWITCH_IDS[slot]],"script.")){
      label_(body,12,y,722,"Script state entity (optional)",&lv_font_montserrat_16);y+=26;
      auto *input=lv_textarea_create(body);state_input_[slot]=input;lv_obj_set_style_text_font(input,&fonts_[1],0);lv_obj_set_pos(input,12,y);lv_obj_set_size(input,550,44);lv_textarea_set_one_line(input,true);lv_textarea_set_max_length(input,111);lv_textarea_set_text(input,tile_settings_.switch_state[slot]);
      bind_(input,LV_EVENT_CLICKED,[this,input](){editor_state_=true;open_editor_(input,"Script state entity (optional)",true);});
      button_(body,576,y,158,44,"Save",[this,slot](){save_tile_state_(slot);});y+=58;
    }
  }
  const char *sensor_names[]={"Auto","Thermometer","Humidity drop","Solar panel","Power bolt","None","Fan"};
  for(int slot=0;slot<3;slot++){
    choice(translate_(settings_.label[SENSOR_IDS[slot]])+" - "+translate_("Icon"),sensor_names[tile_settings_.sensor_icon[slot]],[this,slot](){tile_settings_.sensor_icon[slot]=(tile_settings_.sensor_icon[slot]+1)%7;});
    choice(translate_("Sensor "+std::to_string(slot+1))+" - "+translate_("Colour threshold"),tile_settings_.threshold_enabled&(1<<slot)?"On":"Off",[this,slot](){tile_settings_.threshold_enabled^=1<<slot;});
    label_(body,12,y,722,"Amber when value exceeds limit (sensor units)",&lv_font_montserrat_16);y+=26;
    auto *input=lv_textarea_create(body);threshold_input_[slot]=input;lv_obj_set_style_text_font(input,&fonts_[1],0);lv_obj_set_pos(input,12,y);lv_obj_set_size(input,550,44);lv_textarea_set_one_line(input,true);lv_textarea_set_max_length(input,24);char number[32];snprintf(number,sizeof(number),"%.7g",tile_settings_.threshold[slot]);lv_textarea_set_text(input,number);
    bind_(input,LV_EVENT_CLICKED,[this,input](){open_editor_(input,"Colour threshold",false);});
    button_(body,576,y,158,44,"Save",[this,slot](){float limit=parse_number(lv_textarea_get_text(threshold_input_[slot]));if(!std::isfinite(limit)){text_("tile_status","Enter a number; use a dot for decimals");return;}tile_settings_.threshold[slot]=limit;tile_settings_save_();text_("tile_status","Saved. Changes applied.");});y+=58;
  }
  labels_["tile_status"]=label_(body,12,y,722,"Script icons need a state entity to show the device as on.",&lv_font_montserrat_16);lv_obj_set_height(labels_["tile_status"],40);y+=52;
}

void WorkshopPanel::validate_tile_controls(){
  int checked=0,failed=0;auto test=[&](bool ok){checked++;if(!ok)failed++;};
  test(switch_kind(0,"light.test")==BULB);test(switch_kind(0,"switch.test")==PLUG);test(switch_kind(0,"script.test")==POWER);
  test(switch_kind(1,"script.test")==BULB);test(switch_kind(2,"light.test")==PLUG);test(switch_kind(4,"light.test")==NONE);
  test(switch_kind(5,"switch.test")==FAN);test(switch_kind(0,"fan.test")==FAN);
  test(sensor_kind(6,"sensor.test","power","W")==FAN);
  test(sensor_kind(0,"sensor.test","temperature","")==THERMOMETER);test(sensor_kind(0,"sensor.test","humidity","%")==HUMIDITY);
  test(sensor_kind(0,"sensor.solar","power","W")==SOLAR);test(sensor_kind(0,"sensor.test","power","W")==POWER);test(sensor_kind(5,"sensor.test","power","W")==NONE);
  test(!exceeds("10",10));test(exceeds("10.1",10));test(exceeds("-5",-10));test(!exceeds("unavailable",10));test(!exceeds("nan",10));test(!exceeds("10W",1));
  test(on("on")&&!on("off")&&!on("unavailable"));
  ESP_LOGI("workshop_panel","Tile controls check: %d cases, %d failed",checked,failed);
}
}  // namespace esphome::workshop_panel
