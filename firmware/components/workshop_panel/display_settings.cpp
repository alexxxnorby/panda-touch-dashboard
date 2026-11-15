#include "workshop_panel.h"
#include <algorithm>
#include <cstring>
#include "esphome/core/log.h"

namespace esphome::workshop_panel {
static constexpr uint32_t PANEL=0x1B2630, MUTED=0xA7B6C3;

// A separate state preserves each widget's normal colour underneath the tint.
static void night_text(lv_obj_t *object,bool night){
  if(lv_obj_has_state(object,LV_STATE_USER_1)!=night){
    if(night){
      lv_obj_set_style_text_color(object,lv_color_hex(0xE86A6A),LV_STATE_USER_1);
      lv_obj_add_state(object,LV_STATE_USER_1);
    }else lv_obj_remove_state(object,LV_STATE_USER_1);
  }
  for(uint32_t i=0;i<lv_obj_get_child_count(object);i++)night_text(lv_obj_get_child(object,i),night);
}

void WorkshopPanel::validate_display_fonts(){
  int missing=0, checked=0;
  const uint32_t extra[]={0x00C4,0x00D6,0x00DC,0x00DF,0x00E4,0x00F6,0x00FC,0x00B0};
  for(int i=0;i<4;i++){
    auto check=[this,i,&missing,&checked](uint32_t codepoint){
      lv_font_glyph_dsc_t glyph{};checked++;
      if(!lv_font_get_glyph_dsc(&fonts_[i],&glyph,codepoint,0)||glyph.is_placeholder){missing++;ESP_LOGE("workshop_panel","Missing glyph U+%04lX at font index %d",(unsigned long)codepoint,i);}
    };
    for(uint32_t codepoint=32;codepoint<=126;codepoint++)check(codepoint);
    for(auto codepoint:extra)check(codepoint);
  }
  ESP_LOGI("workshop_panel","Display font check: %d glyphs checked, %d missing",checked,missing);
}

void WorkshopPanel::sanitize_(){
  settings_.version=3;
  for(int i=0;i<MAP_COUNT;i++){settings_.entity[i][111]=0;settings_.label[i][31]=0;}
  settings_.wallpaper_url[255]=0;
  settings_.idle_seconds=std::min<uint16_t>(settings_.idle_seconds,3600);
  settings_.brightness=std::clamp<int>(settings_.brightness,20,100);
  settings_.camera_seconds=std::clamp<int>(settings_.camera_seconds,3,60);
  settings_.language=std::min<int>(settings_.language,1);
  settings_.clock_scale=std::clamp<int>(settings_.clock_scale,75,200);
  settings_.weather_scale=std::clamp<int>(settings_.weather_scale,75,175);
  for(auto *v:{&settings_.switch_opacity,&settings_.sensor_opacity,&settings_.panel_opacity})*v=std::min<int>(*v,100);
  settings_.sensor_opacity=settings_.panel_opacity=settings_.switch_opacity;
  settings_.night_start%=24;settings_.night_end%=24;settings_.night_brightness=std::clamp<int>(settings_.night_brightness,5,100);
  settings_.visible_switches&=63;settings_.visible_sensors&=7;settings_.weather_parts&=7;settings_.text_theme%=3;settings_.long_text=!!settings_.long_text;
  auto order=[](uint8_t *values,int count){unsigned used=0;for(int i=0;i<count;i++){if(values[i]>=count||(used&(1<<values[i]))){for(int n=0;n<count;n++)values[n]=n;return;}used|=1<<values[i];}};
  order(settings_.switch_order,6);order(settings_.sensor_order,3);
}

void WorkshopPanel::brightness_refresh_(){
  auto now=clock_->now();int value=settings_.brightness;
  bool night=false;
  if(settings_.night_enabled&&now.is_valid()){
    night=settings_.night_start==settings_.night_end||
      (settings_.night_start<settings_.night_end?(now.hour>=settings_.night_start&&now.hour<settings_.night_end):(now.hour>=settings_.night_start||now.hour<settings_.night_end));
    if(night)value=std::min<int>(value,settings_.night_brightness);
  }
  if(value!=applied_brightness_){applied_brightness_=value;backlight_->turn_on().set_brightness(value/100.0f).perform();}
  if(canvas_)night_text(canvas_,night);
}

void WorkshopPanel::load_wallpaper_(){
  if(wallpaper_loading_)return;
  std::string url=settings_.wallpaper_url;
  if(url.empty()){if(wallpaper_object_)lv_image_set_src(wallpaper_object_,&background_dsc_);return;}
  if(url.rfind("/local/",0)==0){std::string base=settings_.entity[16];while(!base.empty()&&base.back()=='/')base.pop_back();url=base+url;}
  wallpaper_loading_=true;wallpaper_->set_url(url);wallpaper_->update();text_("background_status","Loading background...");
}

void WorkshopPanel::display_preview_refresh_(){
  if(!display_preview_)return;
  for(auto pair:{std::make_pair(preview_clock_,settings_.clock_scale),std::make_pair(preview_weather_,settings_.weather_scale)}){
    lv_obj_set_style_transform_scale_x(pair.first,256*pair.second/100,0);lv_obj_set_style_transform_scale_y(pair.first,256*pair.second/100,0);
  }
  lv_obj_set_style_bg_opa(preview_switch_,settings_.panel_opacity*255/100,0);lv_obj_set_style_bg_opa(preview_sensor_,settings_.panel_opacity*255/100,0);
  lv_obj_set_style_bg_opa(display_preview_,settings_.panel_opacity*255/100,0);
  for(auto *o:{preview_clock_,preview_weather_})lv_obj_set_style_text_color(o,lv_color_hex(settings_.text_theme==2?0x17212B:0xFFFFFF),0);
  if(settings_.weather_visible&&(settings_.weather_parts&1))lv_obj_remove_flag(preview_weather_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(preview_weather_,LV_OBJ_FLAG_HIDDEN);
  brightness_refresh_();
}

void WorkshopPanel::reorder_(int kind,int from,int to){
  int count=kind?3:6;if(from<0||to<0||from>=count||to>=count||from==to)return;
  auto *order=kind?settings_.sensor_order:settings_.switch_order;auto value=order[from];
  if(from<to)for(int i=from;i<to;i++)order[i]=order[i+1];else for(int i=from;i>to;i--)order[i]=order[i-1];
  order[to]=value;save_();route_(SETTINGS_DISPLAY);
}

void WorkshopPanel::display_settings_(){
  drag_kind_=drag_slot_=-1;
  auto *body=box_(canvas_,12,239,776,229,PANEL,10);lv_obj_add_flag(body,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(body,LV_DIR_VER);lv_obj_set_scrollbar_mode(body,LV_SCROLLBAR_MODE_ON);
  lv_obj_set_style_bg_color(body,lv_color_hex(0x74DFA5),LV_PART_SCROLLBAR);lv_obj_set_style_bg_opa(body,LV_OPA_COVER,LV_PART_SCROLLBAR);lv_obj_set_style_width(body,5,LV_PART_SCROLLBAR);
  settings_body_=body;
  button_(canvas_,750,250,38,68,LV_SYMBOL_UP,[body](){lv_obj_scroll_by_bounded(body,0,172,LV_ANIM_OFF);});
  button_(canvas_,750,390,38,68,LV_SYMBOL_DOWN,[body](){lv_obj_scroll_by_bounded(body,0,-172,LV_ANIM_OFF);});
  display_preview_=box_(canvas_,24,119,736,110,0x304358);
  preview_clock_=label_(display_preview_,12,8,190,"12:34",&lv_font_montserrat_28);
  preview_weather_=label_(display_preview_,400,8,180,"21 °C",&lv_font_montserrat_28);
  preview_switch_=box_(display_preview_,12,71,340,36,0x293744);label_(preview_switch_,8,5,320,"Switch preview",&lv_font_montserrat_16);
  preview_sensor_=box_(display_preview_,368,71,350,36,PANEL);label_(preview_sensor_,8,5,330,"Sensor preview",&lv_font_montserrat_16);
  int y=12;
  auto slider=[this,body,&y](const char *name,int min,int max,int initial,std::function<void(int)> change,const char *suffix){
    label_(body,12,y,345,name,&lv_font_montserrat_16);auto *value=label_(body,650,y,84,std::to_string(initial)+suffix,&lv_font_montserrat_16);
    auto *control=lv_slider_create(body);lv_obj_set_pos(control,376,y+8);lv_obj_set_size(control,250,16);lv_obj_remove_flag(control,LV_OBJ_FLAG_SCROLL_ON_FOCUS);lv_obj_set_style_pad_top(control,14,LV_PART_KNOB);lv_obj_set_style_pad_bottom(control,14,LV_PART_KNOB);lv_slider_set_range(control,min,max);lv_slider_set_value(control,initial,LV_ANIM_OFF);
    bind_(control,LV_EVENT_VALUE_CHANGED,[this,control,value,change,suffix](){int n=lv_slider_get_value(control);change(n);lv_label_set_text(value,(std::to_string(n)+suffix).c_str());activity_();display_preview_refresh_();});
    bind_(control,LV_EVENT_RELEASED,[this](){save_();});
    bind_(control,LV_EVENT_PRESS_LOST,[this](){save_();});y+=54;
  };
  auto choice=[this,body,&y](const char *name,const std::string &value,std::function<void()> change){
    label_(body,12,y+12,335,name,&lv_font_montserrat_16);button_(body,360,y,374,44,value,[this,change](){change();save_();route_(SETTINGS_DISPLAY);});y+=54;
  };
  choice("Language",settings_.language?"Deutsch":"English",[this](){settings_.language^=1;});
  slider("Clock text size",75,200,settings_.clock_scale,[this](int n){settings_.clock_scale=n;},"%");
  slider("Weather text size",75,175,settings_.weather_scale,[this](int n){settings_.weather_scale=n;},"%");
  slider("Background opacity (global)",0,100,settings_.panel_opacity,[this](int n){settings_.switch_opacity=settings_.sensor_opacity=settings_.panel_opacity=n;},"%");
  choice("Text colour / contrast",settings_.text_theme==0?"Default":settings_.text_theme==1?"White / high contrast":"Dark text",[this](){settings_.text_theme=(settings_.text_theme+1)%3;});
  choice("Long labels",settings_.long_text?"Scroll":"Wrap",[this](){settings_.long_text^=1;});
  choice("Show weather",settings_.weather_visible?"On":"Off",[this](){settings_.weather_visible^=1;});
  choice("Weather temperature",settings_.weather_parts&1?"Show":"Hide",[this](){settings_.weather_parts^=1;});
  choice("Weather condition",settings_.weather_parts&2?"Show":"Hide",[this](){settings_.weather_parts^=2;});
  choice("Weather icon",settings_.weather_parts&4?"Show":"Hide",[this](){settings_.weather_parts^=4;});
  choice("Show date",settings_.date_visible?"On":"Off",[this](){settings_.date_visible^=1;});
  slider("Brightness",20,100,settings_.brightness,[this](int n){settings_.brightness=n;},"%");
  choice("Night dimming",settings_.night_enabled?"On":"Off",[this](){settings_.night_enabled^=1;});
  slider("Night starts at",0,23,settings_.night_start,[this](int n){settings_.night_start=n;},":00");
  slider("Night ends at",0,23,settings_.night_end,[this](int n){settings_.night_end=n;},":00");
  slider("Night brightness",5,100,settings_.night_brightness,[this](int n){settings_.night_brightness=n;},"%");
  choice("Idle screen after",settings_.idle_seconds?std::to_string(settings_.idle_seconds)+" s":"Never",[this](){const int values[]={30,60,120,300,0};for(int i=0;i<5;i++)if(settings_.idle_seconds==values[i]){settings_.idle_seconds=values[(i+1)%5];return;}settings_.idle_seconds=60;});
  slider("Camera snapshot interval",3,60,settings_.camera_seconds,[this](int n){settings_.camera_seconds=n;}," s");
  label_(body,12,y,722,"Background: JPEG URL or /local/backgrounds/image.jpg",&lv_font_montserrat_16);y+=38;
  background_input_=lv_textarea_create(body);lv_obj_set_style_text_font(background_input_,&fonts_[1],0);lv_obj_set_pos(background_input_,12,y);lv_obj_set_size(background_input_,722,44);lv_textarea_set_one_line(background_input_,true);lv_textarea_set_max_length(background_input_,255);lv_textarea_set_text(background_input_,settings_.wallpaper_url);
  bind_(background_input_,LV_EVENT_CLICKED,[this](){open_editor_(background_input_,"Background image URL",false);});y+=54;
  button_(body,12,y,350,44,"Apply background",[this](){
    std::string url=lv_textarea_get_text(background_input_);
    if(wallpaper_loading_){text_("background_status","Please wait for the current image download");return;}
    if(!url.empty()&&url.rfind("http://",0)!=0&&url.rfind("https://",0)!=0&&url.rfind("/local/",0)!=0){text_("background_status","Use http://, https:// or /local/");return;}
    snprintf(settings_.wallpaper_url,sizeof(settings_.wallpaper_url),"%s",url.c_str());wallpaper_ready_=false;save_();load_wallpaper_();
  });
  button_(body,380,y,354,44,"Default background",[this](){if(wallpaper_loading_){text_("background_status","Please wait for the current image download");return;}settings_.wallpaper_url[0]=0;save_();load_wallpaper_();});y+=52;
  labels_["background_status"]=label_(body,12,y,722,wallpaper_loading_?"Loading background...":"JPEG images; leave blank for the default background",&lv_font_montserrat_16);y+=50;
  label_(body,12,y,722,"Hold a row, then drag to reorder. Tap Show / Hide.",&lv_font_montserrat_16);y+=44;
  for(int kind=0;kind<2;kind++){
    int count=kind?3:6;
    for(int slot=0;slot<count;slot++){
      int index=kind?settings_.sensor_order[slot]:settings_.switch_order[slot];int id=kind?SENSOR_IDS[index]:SWITCH_IDS[index];bool visible=(kind?settings_.visible_sensors:settings_.visible_switches)&(1<<index);
      auto *row=box_(body,12,y,560,48,0x293744);lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(row,LV_OBJ_FLAG_PRESS_LOCK);label_(row,12,10,530,std::string(LV_SYMBOL_BARS)+"  "+translate_(settings_.label[id]),&lv_font_montserrat_16);
      bind_(row,LV_EVENT_LONG_PRESSED,[this,body,kind,slot,row](){lv_obj_set_scroll_dir(body,LV_DIR_NONE);drag_kind_=kind;drag_slot_=slot;lv_obj_set_style_outline_width(row,2,0);lv_obj_set_style_outline_color(row,lv_color_hex(0x74DFA5),0);});
      bind_(row,LV_EVENT_RELEASED,[this,body,row,kind,slot,count](){
        if(drag_kind_!=kind||drag_slot_!=slot)return;
        auto *input=lv_indev_active();lv_point_t point{};if(input)lv_indev_get_point(input,&point);lv_area_t coords;lv_obj_get_coords(row,&coords);int delta=point.y-(coords.y1+coords.y2)/2;int to=std::clamp(slot+(delta>=0?(delta+27)/56:(delta-27)/56),0,count-1);drag_kind_=drag_slot_=-1;lv_obj_set_scroll_dir(body,LV_DIR_VER);lv_obj_set_style_outline_width(row,0,0);reorder_(kind,slot,to);
      });
      bind_(row,LV_EVENT_PRESS_LOST,[this,body,row](){lv_obj_set_scroll_dir(body,LV_DIR_VER);drag_kind_=drag_slot_=-1;lv_obj_set_style_outline_width(row,0,0);});
      button_(body,586,y,148,48,visible?"Hide":"Show",[this,kind,index](){if(kind)settings_.visible_sensors^=1<<index;else settings_.visible_switches^=1<<index;save_();route_(SETTINGS_DISPLAY);});y+=56;
    }
  }
  const int icons_y=y;
  settings_icons_y_=icons_y;
  tile_options_(body,y);
  button_(canvas_,510,69,150,40,"Icons",[body,icons_y](){lv_obj_scroll_to_y(body,icons_y,LV_ANIM_OFF);});
  button_(body,12,y+8,722,48,"Reset display defaults",[this](){display_scroll_=0;settings_.language=0;settings_.idle_seconds=60;settings_.brightness=80;settings_.camera_seconds=5;
    // Display preferences follow mappings in the structure; preserve all entity IDs and names.
    settings_.clock_scale=156;settings_.weather_scale=130;settings_.switch_opacity=settings_.sensor_opacity=settings_.panel_opacity=100;settings_.night_enabled=0;settings_.night_start=22;settings_.night_end=7;settings_.night_brightness=20;settings_.text_theme=settings_.long_text=0;settings_.weather_visible=settings_.date_visible=1;settings_.weather_parts=7;settings_.visible_switches=15;settings_.visible_sensors=3;for(int i=0;i<6;i++)settings_.switch_order[i]=i;for(int i=0;i<3;i++)settings_.sensor_order[i]=i;settings_.wallpaper_url[0]=0;for(auto &icon:tile_settings_.switch_icon)icon=0;for(auto &icon:tile_settings_.sensor_icon)icon=0;tile_settings_.threshold_enabled=0;tile_settings_save_();save_();load_wallpaper_();brightness_refresh_();route_(SETTINGS_DISPLAY);
  });
  lv_obj_update_layout(body);lv_obj_scroll_to_y(body,display_scroll_,LV_ANIM_OFF);
  bind_(body,LV_EVENT_SCROLL,[this,body](){display_scroll_=lv_obj_get_scroll_y(body);activity_();});
  display_preview_refresh_();
}

void WorkshopPanel::validate_settings_scroll(){
  if(editor_||benchmark_active_||(view_!=NORMAL&&view_!=SETTINGS_DISPLAY))return;
  int previous_view=view_,previous_scroll=display_scroll_;route_(SETTINGS_DISPLAY);
  set_timeout("settings_scroll_check",300,[this,previous_view,previous_scroll](){
    if(view_!=SETTINGS_DISPLAY||!settings_body_)return;
    auto *body=settings_body_;lv_obj_update_layout(body);int failed=0;
    lv_obj_scroll_to_y(body,0,LV_ANIM_OFF);
    lv_point_t point{40,440};bool touch=lv_indev_search_obj(lv_screen_active(),&point)==body;
    if(!touch)failed++;
    lv_obj_scroll_by_bounded(body,0,-172,LV_ANIM_OFF);bool scroll=lv_obj_get_scroll_y(body)>100;
    if(!scroll)failed++;
    lv_obj_scroll_to_y(body,settings_icons_y_,LV_ANIM_OFF);bool icons=std::abs(lv_obj_get_scroll_y(body)-settings_icons_y_)<3;
    if(!icons)failed++;
    ESP_LOGI("workshop_panel","Settings scroll check: touch=%d scroll=%d icons=%d, %d failed",touch,scroll,icons,failed);
    display_scroll_=previous_scroll;route_(previous_view);
  });
}
std::string WorkshopPanel::translate_(const std::string &text) const {
  if(!settings_.language)return text;
  for(int i=0;i<6;i++)if(text=="Switch "+std::to_string(i+1))return "Schalter "+std::to_string(i+1);
  for(int i=0;i<3;i++)if(text=="Sensor "+std::to_string(i+1))return "Sensor "+std::to_string(i+1);
  for(int i=0;i<MAP_COUNT;i++)if(text==settings_.label[i])return text;
  static const std::pair<const char*,const char*> words[]={
    {"Workshop settings","Werkstatt-Einstellungen"},
    {"Workshop","Werkstatt"},
    {"WORKSHOP","WERKSTATT"},
    {"Back","Zurück"},
    {"Display","Anzeige"},
    {"Entities","Entitäten"},
    {"Language","Sprache"},
    {"Icons and sensor thresholds","Symbole und Sensor-Grenzwerte"},
    {"Icon","Symbol"},
    {"Icons","Symbole"},
    {"Auto","Automatisch"},
    {"Light bulb","Glühbirne"},
    {"Fan","Ventilator"},
    {"Power plug","Steckdose"},
    {"Power bolt","Blitz"},
    {"None","Kein Symbol"},
    {"Thermometer","Thermometer"},
    {"Humidity drop","Wassertropfen"},
    {"Solar panel","Solarpanel"},
    {"Colour threshold","Farb-Grenzwert"},
    {"Amber when value exceeds limit (sensor units)","Gelb bei Überschreitung (Einheit des Sensors)"},
    {"Script state entity (optional)","Skript: Zustandsentität (optional)"},
    {"Use a light, switch, input_boolean or binary_sensor entity","Entität von light, switch, input_boolean oder binary_sensor verwenden"},
    {"Script icons need a state entity to show the device as on.","Skripte benötigen eine Zustandsentität zur Anzeige des Gerätezustands."},
    {"Enter a number; use a dot for decimals","Zahl eingeben; Punkt als Dezimalzeichen verwenden"},
    {"Save","Speichern"},
    {"Clock text size","Uhr: Textgröße"},
    {"Weather text size","Wetter: Textgröße"},
    {"Background opacity (global)","Deckkraft (global)"},
    {"Text colour / contrast","Textfarbe / Kontrast"},
    {"Default","Standard"},
    {"White / high contrast","Weiß / hoher Kontrast"},
    {"Dark text","Dunkler Text"},
    {"Long labels","Lange Beschriftungen"},
    {"Scroll","Laufschrift"},
    {"Wrap","Zeilenumbruch"},
    {"Show weather","Wetter anzeigen"},
    {"Show date","Datum anzeigen"},
    {"Weather temperature","Wetter: Temperatur"},
    {"Weather condition","Wetter: Zustand"},
    {"Weather icon","Wetter: Symbol"},
    {"Type an entity ID or name to filter","Entitäts-ID oder Name zum Filtern eingeben"},
    {"Done keeps your edit; Cancel discards it.","Fertig übernimmt die Eingabe; Abbrechen verwirft sie."},
    {"Brightness","Helligkeit"},
    {"Night dimming","Nachtmodus"},
    {"Night starts at","Nachtbeginn"},
    {"Night ends at","Nachtende"},
    {"Night brightness","Nachthelligkeit"},
    {"Idle screen after","Ruhebildschirm nach"},
    {"Never","Nie"},
    {"Camera snapshot interval","Kamerabild-Intervall"},
    {"Switch preview","Schalter-Vorschau"},
    {"Sensor preview","Sensor-Vorschau"},
    {"Background: JPEG URL or /local/backgrounds/image.jpg","Hintergrund: JPEG-URL oder /local/backgrounds/image.jpg"},
    {"Background image URL","Hintergrundbild-URL"},
    {"Apply background","Hintergrund anwenden"},
    {"Default background","Standardhintergrund"},
    {"Background loaded","Hintergrund geladen"},
    {"Loading background...","Hintergrund wird geladen..."},
    {"Image unavailable; using default background","Bild nicht verfügbar; Standardhintergrund aktiv"},
    {"Please wait for the current image download","Bitte den laufenden Bild-Download abwarten"},
    {"Use http://, https:// or /local/","http://, https:// oder /local/ verwenden"},
    {"JPEG images; leave blank for the default background","JPEG-Bilder; leer lassen für Standardhintergrund"},
    {"Hold a row, then drag to reorder. Tap Show / Hide.","Zeile halten und ziehen. Anzeigen / Ausblenden antippen."},
    {"Hide","Ausblenden"},
    {"Show","Anzeigen"},
    {"Reset display defaults","Anzeige zurücksetzen"},
    {"Lights & energy","Schalter & Sensoren"},
    {"P1S printer","P1S Drucker"},
    {"X2D printer","X2D Drucker"},
    {"More controls","Weitere Bedienelemente"},
    {"On","Ein"},
    {"Off","Aus"},
    {"Unavailable","Nicht verfügbar"},
    {"Send ON command","Einschalten"},
    {"Nozzle","Düse"},
    {"Bed","Druckbett"},
    {"Camera","Kamera"},
    {"Waiting for printer","Warte auf Drucker"},
    {"Ready for the next print","Bereit für nächsten Druck"},
    {"Printer status unavailable","Druckerstatus nicht verfügbar"},
    {"No active job","Kein aktiver Auftrag"},
    {"Time unavailable","Zeit nicht verfügbar"},
    {"AMS & print info","AMS & Druckinfo"},
    {"Empty","Leer"},
    {"No active faults","Keine aktiven Fehler"},
    {"Print error - tap for details","Druckfehler - Details antippen"},
    {"Manual movement","Manuell bewegen"},
    {"Extruder","Extruder"},
    {"Cloud mode: movement unavailable","Cloud-Modus: Bewegung nicht verfügbar"},
    {"Ready - tap to move","Bereit - zum Bewegen antippen"},
    {"Home axes before movement","Achsen zuerst referenzieren"},
    {"Movement unavailable in this connection","Bewegung über diese Verbindung nicht verfügbar"},
    {"Requires LAN Only + Developer Mode","LAN-Only und Entwicklermodus erforderlich"},
    {"Home axes first; then verify at printer","Zuerst referenzieren; am Drucker prüfen"},
    {"Verify homing, then hold Home to confirm","Referenzfahrt prüfen; Home zum Bestätigen halten"},
    {"Homing confirmed by user; ready to move","Referenzfahrt bestätigt; bereit"},
    {"Movement requested","Bewegung angefordert"},
    {"Requires verified homing and local control","Bestätigte Referenzfahrt und lokale Steuerung erforderlich"},
    {"Nozzle must be at least 170 °C","Düse muss mindestens 170 °C haben"},
    {"Power unavailable","Stromversorgung nicht verfügbar"},
    {"Power On","Strom ein"},
    {"Power Off","Strom aus"},
    {"Shared extractor","Gemeinsame Absaugung"},
    {"Shared extractor unavailable","Gemeinsame Absaugung nicht verfügbar"},
    {"Shared extractor On","Gemeinsame Absaugung ein"},
    {"Shared extractor Off","Gemeinsame Absaugung aus"},
    {"Both lasers share this extractor; existing HA automation stays active","Beide Laser nutzen diese Absaugung; HA-Automation bleibt aktiv"},
    {"Waiting for HA time","Warte auf HA-Uhrzeit"},
    {"Weather unavailable","Wetter nicht verfügbar"},
    {"No printers running","Keine Drucker aktiv"},
    {"Touch anywhere to return","Zum Zurückkehren antippen"},
    {"Control to configure","Element auswählen"},
    {"Display name (shown on your buttons)","Anzeigename (auf den Schaltflächen)"},
    {"Home Assistant entity ID","Home Assistant Entitäts-ID"},
    {"Home Assistant entity ID (tap to search)","Home Assistant Entitäts-ID (Suche antippen)"},
    {"Entity prefix (without domain or sensor suffix)","Entitätspräfix (ohne Domain oder Sensor-Suffix)"},
    {"Home Assistant URL (http:// or https://)","Home Assistant URL (http:// oder https://)"},
    {"Bambu integration device ID","Geräte-ID der Bambu-Integration"},
    {"Edit display name","Anzeigename bearbeiten"},
    {"Find Home Assistant entity","Home Assistant Entität suchen"},
    {"Edit connection value","Verbindungswert bearbeiten"},
    {"Save changes","Änderungen speichern"},
    {"Tap a field to edit. Changes apply without restarting.","Feld zum Bearbeiten antippen. Änderungen ohne Neustart."},
    {"Invalid label or entity ID / URL","Ungültiger Name oder Entitäts-ID / URL"},
    {"Saved. Refreshing HA connection; display stays on.","Gespeichert. HA-Verbindung wird aktualisiert."},
    {"Saved. Changes applied.","Gespeichert. Änderungen übernommen."},
    {"Done","Fertig"},
    {"Cancel","Abbrechen"},
    {"Search","Suchen"},
    {"Searching Home Assistant...","Home Assistant wird durchsucht..."},
    {"HA offline - you can still enter an exact entity ID","HA offline - genaue Entitäts-ID kann eingegeben werden"},
    {"Search unavailable - enter the exact ID, then Done","Suche nicht verfügbar - genaue ID eingeben, dann Fertig"},
    {"Tap a match to use it; type more to narrow the first 3 results","Treffer antippen; mehr eingeben für genauere Ergebnisse"},
    {"No matches - try another name or enter an exact ID","Keine Treffer - anderen Namen oder genaue ID eingeben"},
    {"Loading snapshot...","Kamerabild wird geladen..."},
    {"Snapshot updated","Kamerabild aktualisiert"},
    {"Camera unavailable - check HA URL and camera access","Kamera nicht verfügbar - HA-URL und Kamerazugriff prüfen"},
    {"Waiting for camera entity_picture from HA","Warte auf Kamera-Bildpfad von HA"},
    {"Unsupported camera path; use local HA camera proxy","Bildpfad nicht unterstützt; lokalen HA-Kameraproxy nutzen"},
    {"Your print has finished.","Der Druck ist fertig."},
    {"Print failed. Check the printer for details.","Druck fehlgeschlagen. Details am Drucker prüfen."},
    {"Printer reports a print error. Check the fault panel.","Drucker meldet einen Druckfehler. Fehleranzeige prüfen."},
    {"Wi-Fi settings","WLAN-Einstellungen"},
    {"Network name (2.4 GHz)","Netzwerkname (2,4 GHz)"},
    {"Choose network","Netzwerk auswählen"},
    {"Scan","Suchen"},
    {"Wi-Fi password","WLAN-Passwort"},
    {"Enter password (blank for an open network)","Passwort eingeben (leer bei offenem Netzwerk)"},
    {"Save & connect","Speichern & verbinden"},
    {"Reconnect saved network","Gespeichertes Netzwerk verbinden"},
    {"No saved network. Enter its name and password first.","Kein gespeichertes Netzwerk. Name und Passwort eingeben."},
    {"After Wi-Fi connects, add Workshop Panda Touch in Home Assistant.","Nach WLAN-Verbindung Workshop Panda Touch in HA hinzufügen."},
    {"Scanning for 2.4 GHz networks...","Suche nach 2,4-GHz-Netzwerken..."},
    {"No networks found. Type the network name or scan again.","Keine Netzwerke gefunden. Name eingeben oder erneut suchen."},
    {"Scan complete. Choose a network or type its name.","Suche beendet. Netzwerk auswählen oder Name eingeben."},
    {"Enter a network name of 1 to 32 bytes.","Netzwerkname mit 1 bis 32 Bytes eingeben."},
    {"Password must be 8-63 characters (or 64 hex digits).","Passwort: 8-63 Zeichen (oder 64 Hex-Ziffern)."},
    {"Saved. Connecting to your home Wi-Fi...","Gespeichert. WLAN-Verbindung wird hergestellt..."},
    {"Connecting...","Verbindung wird hergestellt..."},
    {"Not connected yet. Check password, signal and 2.4 GHz network; retrying.","Nicht verbunden. Passwort, Signal und 2,4 GHz prüfen; neuer Versuch."},
    {"Scan timed out. Try again or type the network name.","Zeitüberschreitung. Erneut suchen oder Netzwerkname eingeben."},
    {"Not connected. Scan or enter your home network details.","Nicht verbunden. Netzwerk suchen oder Zugangsdaten eingeben."},
    {"sunny","Sonnig"},
    {"clear night","Klare Nacht"},
    {"partlycloudy","Teilweise bewölkt"},
    {"cloudy","Bewölkt"},
    {"rainy","Regen"},
    {"pouring","Starkregen"},
    {"snowy","Schnee"},
    {"snowy rainy","Schneeregen"},
    {"hail","Hagel"},
    {"lightning","Gewitter"},
    {"lightning rainy","Gewitter mit Regen"},
    {"fog","Nebel"},
    {"windy","Windig"},
    {"windy variant","Windig und bewölkt"},
    {"exceptional","Unwetter"},
    {"running","Druck läuft"},
    {"pause","Pausiert"},
    {"prepare","Vorbereitung"},
    {"slicing","Slicing"},
    {"idle","Bereit"},
    {"finish","Fertig"},
    {"failed","Fehlgeschlagen"},
  };
  for(const auto &word:words)if(text==word.first)return word.second;
  // Translate generated UI fragments without altering user labels or HA entity IDs.
  std::string result=text;
  const std::pair<const char*,const char*> fragments[]={
    {"Humidity ","Feuchte "},{"Layer ","Schicht "},{"% left","% übrig"},{"m left","m verbleibend"},{" - Error"," - Fehler"},{" is Done"," ist fertig"},{" camera"," Kamera"},
    {" - running"," - Druck läuft"},{" - pause"," - pausiert"},{" - prepare"," - Vorbereitung"},{" - slicing"," - Slicing"},{" - idle"," - bereit"},{" - finish"," - fertig"},{" - failed"," - fehlgeschlagen"},
    {"Monday","Montag"},{"Tuesday","Dienstag"},{"Wednesday","Mittwoch"},{"Thursday","Donnerstag"},{"Friday","Freitag"},{"Saturday","Samstag"},{"Sunday","Sonntag"},
    {"January","Januar"},{"February","Februar"},{"March","März"},{"May","Mai"},{"June","Juni"},{"July","Juli"},{"October","Oktober"},{"December","Dezember"},
    {"Connected: ","Verbunden: "},{" | HA connected"," | HA verbunden"},{" | Add ESPHome device in HA"," | ESPHome-Gerät in HA hinzufügen"}
  };
  for(const auto &word:fragments){size_t pos=0;while((pos=result.find(word.first,pos))!=std::string::npos){result.replace(pos,strlen(word.first),word.second);pos+=strlen(word.second);}}
  return result;
}
}  // namespace esphome::workshop_panel
