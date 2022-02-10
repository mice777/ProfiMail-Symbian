#ifdef __SYMBIAN32__
#include <e32std.h>
#endif
#include "..\Main.h"
#include "Main_Email.h"
#include <UI\About.h>
#include <UI\FatalError.h>
#include <UI\TextEntry.h>
#include "..\FileBrowser.h"
#include <C_file.h>
#include <Md5.h>
#include <Xml.h>
#include <Directory.h>
#include <Base64.h>
#include <TinyEncrypt.h>

//----------------------------

const int VERSION_HI = 3, VERSION_LO = 60;
const char VERSION_BUILD = 0;

                              //version of accounts binary file (equals to program version * 100)
const int ACCOUNTS_SAVE_VERSION = 316;

extern const char send_key_name[];

const int MIN_IMG_SIZE = 20, MAX_IMG_SIZE = 100;

const wchar app_name[] = L"ProfiMail";

//----------------------------

                              //string by which password is xor'd when written to file
static const char pass_encryption[] = "38fCka@#3";

//----------------------------

#ifdef _DEBUG
//#define DEBUG_EDIT_ACCOUNT 0
//#define DEBUG_MODE_ADDRESS_BOOK
//#define DEBUG_CONFIG
//#define DEBUG_MODE_FILE_BROWSER

//#define DEBUG_MODE_RULES_BROWSER
//#define DEBUG_MODE_RULE_EDITOR
//#define DEBUG_MODE_EDIT_SIGNATURES
//#define DEBUG_MODE_EDIT_IDENTITIES

//#define DEBUG_MODE_FATAL

//#define DEBUG_REPLY 1

//#define OPEN_ACCOUNT 14
//#define OPEN_FOLDER 1
//#define DEBUG_OPEN_MESSAGE 2
//#define DEBUG_WRITE_MAIL

//#define DEBUG_OPEN_FILE L"D:\\1\\1.jpg"
//#define DEBUG_OPEN_FILE L"D:\\1\\cap.gif"
//#define DEBUG_OPEN_FILE L"D:\\1\\classic.txt"
//#define DEBUG_OPEN_FILE L"D:\\1\\test 8bit stereo 32k.wav"
//#define DEBUG_OPEN_FILE L"D:\\5\\party.gif"
//#define DEBUG_OPEN_FILE L"D:\\5\\c0.gif"

#endif

//----------------------------

const char *S_date_time_x::GetDayName() const{

                              //compute day of week from seconds
   dword sec = GetSeconds();
   int d = sec / (60*60*24);
   d += 1;
   d %= 7;
   switch(d){
   default:
   case 0: return "Mon";
   case 1: return "Tue";
   case 2: return "Wed";
   case 3: return "Thu";
   case 4: return "Fri";
   case 5: return "Sat";
   case 6: return "Sun";
   }
}

//----------------------------

const char *S_date_time_x::GetMonthName() const{

   switch(month){
   default:
   case 0: return "Jan";
   case 1: return "Feb";
   case 2: return "Mar";
   case 3: return "Apr";
   case 4: return "May";
   case 5: return "Jun";
   case 6: return "Jul";
   case 7: return "Aug";
   case 8: return "Sep";
   case 9: return "Oct";
   case 10: return "Nov";
   case 11: return "Dec";
   }
}

//----------------------------

template<class T>
int CompVectorItems(const void *p0, const void *p1, void*){
   const T &e0 = *(T*)p0, &e1 = *(T*)p1;
   if(e0<e1) return -1;
   if(e0>e1) return 1;
   return 0;
}

template<class T>
void SortVector(C_vector<T> &v){
   QuickSort(v.begin(), v.size(), sizeof(T), &CompVectorItems<T>);
}

//----------------------------
//----------------------------

const C_mail_client::S_theme C_mail_client::color_themes[] = {
   //bgnd,       selection,  title,      scrollbar,  text_color;
#if defined USE_SYSTEM_SKIN || defined USE_OWN_SKIN
   { 0xffff0000, 0xff80e080, 0xff0000ff, 0xffe0e0e0, 0xff000000 },   //blank
#endif
                              //green + blue
   { 0xffd0f0d0, 0xff80e080, 0xffc0c0f0, 0xffe0c080, 0xff000000 },
                              //pink
   { 0xfff0d0d0, 0xffe08080, 0xffe0c0ff, 0xffc080e0, 0xff000000 },
                              //brown
   { 0xfff0f0a0, 0xffa0a060, 0xffc0f0c0, 0xffc0c0f0, 0xff000000 },
                              //B&W
   { 0xffe0e0e0, 0xff808080, 0xffa0a0a0, 0xffc0c0c0, 0xff000000 },
                              //B&W contrast
   { 0xffffffff, 0xff4040e0, 0xffffffff, 0xffc0c0c0, 0xff000000 },
                              //blue
   { 0xffd0ddf0, 0xff80ace0, 0xff7994bc, 0xff4c6078, 0xff000000 },
                              //orange
   { 0xfff0e8d0, 0xff68582c, 0xfff0e5c4, 0xff745e1e, 0xff000000 },
                              //fluorescent
   { 0xfff0f0c0, 0xff00ff00, 0xffffff00, 0xff00ff00, 0xff000000 },
                              //tyrkis
   { 0xff80a090, 0xff60c0a0, 0xff709080, 0xffc0e0d0, 0xff000000 },
                              //B&W
   { 0xff000000, 0xff808080, 0xff202020, 0xffe0e0e0, 0xffffffff },
};
const int C_mail_client::NUM_COLOR_THEMES = sizeof(color_themes)/sizeof(*color_themes);

//----------------------------

C_mail_client::S_config_mail::S_config_mail():
   last_online_check_day(0),
   date_format(DATE_DD_DOT_MM_DOT_YY),
   alert_volume(5),
   last_msg_cleanup_day(0),
   auto_check_time(0),
   audio_volume(5),
   work_days(0x1f),
   work_time_beg(60*7+30),
   work_time_end(60*19+30),
   work_times_set(false),
   imap_auto_expunge(true),
   sort_by_threads(true),
   sort_contacts_by_last(false),
   sort_mode(SORT_BY_DATE),
   sort_descending(false),
   imap_idle_startup_connect(IDLE_CONNECT_ASK)
{
   flags |= CONF_SHOW_PREVIEW | CONF_DOWNLOAD_HTML_IMAGES;

#if defined _WIN32_WCE 
   if(!IsWMSmartphone())
      fullscreen = false;
#endif
   tweaks.SetDefaults();
}

//----------------------------

Cstr_w C_mail_client::S_config_mail::GetDefaultAlertSound(){
   Cstr_w ret;
   C_file::GetFullPath(L"Email\\"
#if defined WINDOWS_MOBILE || defined _WINDOWS
      L"Alert.wav",
#else
      L"Alert.mid",
#endif
      ret);
   return ret;
}

//----------------------------

void C_mail_client::S_config_mail::SetDefaultAlertSound(){

   alert_sound = GetDefaultAlertSound();
}

//----------------------------

Cstr_w C_mail_client::S_config_mail::GetDateFormatString(E_DATE_FORMAT tf){

   char buf[64];
   int n = 0;
   const char *d = "DD", *m = "MM", *y = "'YY";
   switch(tf){
   case DATE_MM_SLASH_DD: n += StrCpy(buf, m); n += StrCpy(buf+n, " / "); n += StrCpy(buf+n, d); break;
   case DATE_MM_DASH_DD: n += StrCpy(buf, m); n += StrCpy(buf+n, " - "); n += StrCpy(buf+n, d); break;
   case DATE_DD_SLASH_MM: n += StrCpy(buf, d); n += StrCpy(buf+n, " / "); n += StrCpy(buf+n, m); break;
   case DATE_DD_SLASH_MM_SLASH_YY: n += StrCpy(buf, d); n += StrCpy(buf+n, "/"); n += StrCpy(buf+n, m); n += StrCpy(buf+n, "/"); n += StrCpy(buf+n, y); break;
   case DATE_DD_DASH_MM: n += StrCpy(buf, d); n += StrCpy(buf+n, " - "); n += StrCpy(buf+n, m); break;
   case DATE_DD_DASH_MM_DASH_YY: n += StrCpy(buf, d); n += StrCpy(buf+n, " - "); n += StrCpy(buf+n, m); n += StrCpy(buf+n, " - "); n += StrCpy(buf+n, y); break;
   case DATE_DD_DOT_MM_DOT: n += StrCpy(buf, d); n += StrCpy(buf+n, ". "); n += StrCpy(buf+n, m); n += StrCpy(buf+n, "."); break;
   case DATE_DD_DOT_MM_DOT_YY: n += StrCpy(buf, d); n += StrCpy(buf+n, ". "); n += StrCpy(buf+n, m); n += StrCpy(buf+n, ". "); n += StrCpy(buf+n, y); break;
   case DATE_MM_SLASH_DD_SLASH_YY: n += StrCpy(buf, m); n += StrCpy(buf+n, "/"); n += StrCpy(buf+n, d); n += StrCpy(buf+n, "/"); n += StrCpy(buf+n, y); break;
   case DATE_MM_DASH_DD_DASH_YY: n += StrCpy(buf, m); n += StrCpy(buf+n, "-"); n += StrCpy(buf+n, d); n += StrCpy(buf+n, "-"); n += StrCpy(buf+n, y); break;
   case DATE_YY_DASH_MM_DASH_DD: n += StrCpy(buf, y); n += StrCpy(buf+n, "-"); n += StrCpy(buf+n, m); n += StrCpy(buf+n, "-"); n += StrCpy(buf+n, d); break;
   default:
      assert(0);
   }
   buf[n] = 0;
   Cstr_w s;
   s.Copy(buf);
   return s;
}

//----------------------------

Cstr_w C_mail_client::S_config_mail::_GetPassword() const{

   Cstr_w ret = _app_password;
   for(int i=ret.Length(); i--; )
      ret.At(i) -= 0x156;
   return ret;
}

//----------------------------

bool C_mail_client::S_config_mail::IsWorkingTime() const{

   if(!work_times_set)
      return true;
   S_date_time dt;
   dt.GetCurrent();
   dword sec = dt.sort_value;
   //dt.day = 2; dt.month = dt.NOV; dt.year = 2008; dt.hour = 6; dt.minute = 35; sec = dt.GetSeconds();   //!!!
   dword day = sec/(60*60*24);
   dword min_in_day = (sec-day*(60*60*24))/60;
   if(work_time_beg<=work_time_end){
                              //in same day
      if(min_in_day>=work_time_beg && min_in_day<work_time_end){
                              //time inside
         dword week_day = (day+1)%7;
         if(work_days&(1<<week_day))
            return true;      //enabled in this day
      }
   }else{
                              //span accross 2 days
      if(min_in_day>=work_time_beg || min_in_day<work_time_end){
                              //time inside
         if(min_in_day>=work_time_beg){
                              //end of curr day
            dword week_day = (day+1)%7;
            if(work_days&(1<<week_day))
               return true;      //enabled in this day
         }else{
                              //start of next day, check flag of previous day
            dword week_day = (day+0)%7;
            if(work_days&(1<<week_day))
               return true;      //enabled in this day
         }
      }
   }
   return false;
}

//----------------------------

void C_mail_client::S_config_mail::S_tweaks::SetDefaults(){

   preview_area_percent = 25;
   imap_go_to_inbox = false;
   show_new_mail_notify = true;
   focus_go_to_main_screen = false;
   prefer_plain_text_body = false;
   reply_below_quote = false;
   quote_when_reply = true;
   open_links_by_system = true;
   no_led_flash = false;
   show_minimum_counters = false;
   vibration_length = 500;
   scroll_preview_marks_read = false;
   check_mail_on_startup = false;
   detect_phone_numbers = true;
   show_recent_flags = true;
   pass_ask_timeout = 5;
   always_keep_messages = false;
   ask_to_exit = true;
#if defined __SYMBIAN32__ && defined __SYMBIAN_3RD__
   red_key_close_app = false;
#endif
   show_only_unread_msgs = false;
   exit_in_menus = false;
}

//----------------------------

void C_mail_client::AskNewPassword(){
   class C_text_entry: public C_text_entry_callback{
      C_mail_client &app;
      virtual void TextEntered(const Cstr_w &txt){
         app.config_mail.app_password = txt;
         app.SaveAccounts();
         CreateInfoMessage(app, TXT_ENTER_NEW_PASSWORD, app.GetText(txt.Length() ? TXT_PASWORD_SET : TXT_PASSWORD_CLEARED));
      }
   public:
      C_text_entry(C_mail_client &a): app(a){}
   };
   CreateTextEntryMode(*this, TXT_ENTER_NEW_PASSWORD, new(true) C_text_entry(*this), true, 20, NULL, TXTED_SECRET);
}

//----------------------------

class C_mode_blank_screen: public C_mode_app<C_application_ui>{
   virtual bool WantInactiveTimer() const{ return false; }
public:
   C_mode_blank_screen(C_application_ui &_app):
      C_mode_app<C_application_ui>(_app)
   {
      CreateTimer(1);
      Activate();
   }
   virtual void InitLayout(){}
   virtual void Tick(dword time, bool &redraw){
      Close();
   }
   virtual void Draw() const{
      app.ClearToBackground(S_rect(0, 0, app.ScrnSX(), app.ScrnSY()));
   }
};

//----------------------------

void C_mail_client::AskPasswordWhenFocusGained(bool blank_screen_bgnd){

   class C_text_entry: public C_text_entry_callback{
      C_mail_client &app;
      virtual void TextEntered(const Cstr_w &txt){
         if(txt.Length()){
            if(txt!=app.config_mail.app_password){
#ifndef _DEBUG
               system::Sleep(3000);
#endif
               app.AskPasswordWhenFocusGained(false);
               app.ShowErrorWindow(TXT_ERROR, TXT_INVALID_PASSWORD);
            }

         }else
            Canceled();      
      }
      virtual void Canceled(){
         app.AskPasswordWhenFocusGained(false);
         app.MinMaxApplication(true);
      }
   public:
      C_text_entry(C_mail_client &a): app(a){}
   };
   if(blank_screen_bgnd)
      new(true) C_mode_blank_screen(*this);
   CreateTextEntryMode(*this, TXT_ENTER_PASSWORD, new(true) C_text_entry(*this), true, 500, NULL, TXTED_SECRET, MODE_ID_PASS_ENTER);
}

//----------------------------

Cstr_w C_mail_client::GetConfigFilename() const{

   Cstr_w fn; fn<<mail_data_path <<DATA_PATH_PREFIX <<L"config.txt";
   return fn;
}

//----------------------------

const S_config_store_type C_mail_client::save_config_values[] = {
   { S_config_store_type::TYPE_WORD, OffsetOf(S_config,ui_font_size_delta), "ui_font_size" },
   { S_config_store_type::TYPE_DWORD, OffsetOf(S_config_mail,last_online_check_day), "locd" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,date_format), "date_format" },
   { S_config_store_type::TYPE_DWORD, OffsetOf(S_config_mail,last_msg_cleanup_day), "lmcd" },
   { S_config_store_type::TYPE_DWORD, OffsetOf(S_config_mail,auto_check_time), "auto_check_time" },
   { S_config_store_type::TYPE_WORD, OffsetOf(S_config_mail,alert_volume), "alert_volume" },
   { S_config_store_type::TYPE_CSTR_W, OffsetOf(S_config_mail,alert_sound), "alert_sound1" },
   { S_config_store_type::TYPE_CSTR_W, OffsetOf(S_config_mail,_app_password), "app_password" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,audio_volume), "audio_volume" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,imap_auto_expunge), "imap_auto_purge" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,sort_by_threads), "sort_by_threads" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,sort_contacts_by_last), "sort_contacts_by_last" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,sort_mode), "sort_mode" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,sort_descending), "sort_descending" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,imap_idle_startup_connect), "imap_idle_connect_mode" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,work_times_set), "work_times_enabled" },
   { S_config_store_type::TYPE_WORD, OffsetOf(S_config_mail,work_time_beg), "work_time_start" },
   { S_config_store_type::TYPE_WORD, OffsetOf(S_config_mail,work_time_end), "work_time_end" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,work_days), "work_days" },
                              //tweaks:
   { S_config_store_type::TYPE_WORD, OffsetOf(S_config_mail,tweaks.preview_area_percent), "twk_preview_area_percent" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.imap_go_to_inbox), "twk_imap_go_to_inbox" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.show_new_mail_notify), "twk_new_email_notify" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.focus_go_to_main_screen), "twk_activate_go_to_main_screen" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.prefer_plain_text_body), "twk_prefer_plain_text" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.reply_below_quote), "twk_reply_below_quote" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.quote_when_reply), "twk_quote_when_reply" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.open_links_by_system), "twk_open_links_by_system1" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.no_led_flash), "twk_no_led_flash" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.show_minimum_counters), "twk_minimum_counters" },
   { S_config_store_type::TYPE_WORD, OffsetOf(S_config_mail,tweaks.vibration_length), "twk_vibration_length" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.scroll_preview_marks_read), "twk_scroll_preview_marks_read" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.check_mail_on_startup), "twk_check_mail_on_startup" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.detect_phone_numbers), "twk_detect_phone_numbers" },
   { S_config_store_type::TYPE_CSTR_C, OffsetOf(S_config_mail,tweaks.date_fmt), "twk_date_fmt" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.show_recent_flags), "twk_show_recent_flags" },
   { S_config_store_type::TYPE_WORD, OffsetOf(S_config_mail,tweaks.pass_ask_timeout), "twk_pass_ask_timeout" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.show_only_unread_msgs), "twk_show_only_unread" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.always_keep_messages), "twk_always_keep_messages" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.exit_in_menus), "twk_exit_in_menus" },
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.ask_to_exit), "twk_ask_to_exit" },
#if defined __SYMBIAN32__ && defined __SYMBIAN_3RD__
   { S_config_store_type::TYPE_BYTE, OffsetOf(S_config_mail,tweaks.red_key_close_app), "twk_red_key_close" },
#endif
   { S_config_store_type::TYPE_NEXT_VALS, dword(&save_config_values_base) },
};

//----------------------------

#if defined __SYMBIAN32__ && defined __SYMBIAN_3RD__
bool C_mail_client::RedKeyWantClose() const{
   return(config_mail.tweaks.red_key_close_app && 
      !(config_mail.work_times_set || config_mail.auto_check_time || IsBackgroundConnected()));
}
#endif

//----------------------------

const S_config_item C_mail_client::config_options[] = {
   { CFG_ITEM_NUMBER, TXT_CFG_AUTO_UPDATE_TIME, 4, OffsetOf(S_config_mail, auto_check_time) },
   { CFG_ITEM_WORK_HOURS_SUM, TXT_CFG_WORK_HOURS },
   { CFG_ITEM_CHECKBOX, TXT_CFG_KEEP_GPRS, S_config::CONF_KEEP_CONNECTION, OffsetOf(S_config, flags) },
   { CFG_ITEM_IMAP_CONNECT_MODE, TXT_CFG_CONNECT_IMAP_IDLE, 0, OffsetOf(S_config_mail,imap_idle_startup_connect) },
   { CFG_ITEM_CHECKBOX, TXT_CFG_IMAP_AUTO_EXPUNGE, 0, OffsetOf(S_config_mail,imap_auto_expunge) },
   { CFG_ITEM_FONT_SIZE, TXT_CFG_UI_FONT_SIZE, 3, OffsetOf(S_config, ui_font_size_delta) },
   { CFG_ITEM_CHECKBOX, TXT_CFG_USE_SYSTEM_FONT, 0, OffsetOf(S_config, use_system_font) },
   { CFG_ITEM_TEXT_FONT_SIZE, TXT_CFG_TEXT_FONT_SIZE, 0, OffsetOf(S_config, viewer_font_index) },
   { CFG_ITEM_CHECKBOX, TXT_CFG_FULLSCREEN, 0, OffsetOf(S_config, fullscreen) },
   //{ CFG_ITEM_COUNTER_MODE, TXT_CFG_SHOW_DATA_COUNTER, 0, OffsetOf(S_config, flags) },
   { CFG_ITEM_DATE_FORMAT, TXT_CFG_DATE_FORMAT, 0, OffsetOf(S_config_mail, date_format) },
   { CFG_ITEM_TIME_OUT, TXT_CFG_CONNECTION_TIME_OUT, 0, OffsetOf(S_config, connection_time_out) },
   { CFG_ITEM_CHECKBOX, TXT_CFG_SEND_IMMEDIATELY, S_config_mail::CONF_SEND_MSG_IMMEDIATELY, OffsetOf(S_config, flags) },
   { CFG_ITEM_ALERT_SOUND, TXT_CFG_ALERT_SOUND, 0, OffsetOf(S_config_mail, alert_sound) },
   { CFG_ITEM_ALERT_VOLUME, TXT_CFG_ALERT_VOLUME, 10, OffsetOf(S_config_mail, alert_volume) },
#ifndef USE_SYSTEM_VIBRATE
   { CFG_ITEM_CHECKBOX, TXT_CFG_VIBRATE_ALERT, S_config_mail::CONF_VIBRATE_ALERT, OffsetOf(S_config, flags) },
#endif
   { CFG_ITEM_CHECKBOX, TXT_CFG_DOWNLOAD_HTML_IMAGES, S_config_mail::CONF_DOWNLOAD_HTML_IMAGES, OffsetOf(S_config, flags) },
   //{ CFG_ITEM_IMAGE_SCALE, TXT_CFG_HTML_IMAGES_SCALE },
#if defined __SYMBIAN32__ && !defined __SYMBIAN_3RD__
   { CFG_ITEM_CHECKBOX, TXT_AUTO_START, S_config_mail::CONF_AUTO_START, OffsetOf(S_config, flags) },
#endif
#ifndef USE_ANDROID_TEXTS
   { CFG_ITEM_LANGUAGE, TXT_CFG_LANGUAGE },
#endif
   { CFG_ITEM_APP_PASSWORD, TXT_CFG_PROGRAM_PASSWORD, 0, OffsetOf(S_config_mail,app_password) },
   { CFG_ITEM_COLOR_THEME, TXT_CFG_COLOR_THEME, NUM_COLOR_THEMES },
   { CFG_ITEM_DATA_LOCATION, TXT_DATA_LOCATION },
   { CFG_ITEM_TWEAKS, dword(L"Tweaks\0" L"Various tweaks (in English only, not localized). Not recommended to change anything if you're not ProfiMail expert.") },
#if defined _WINDOWS || defined _WIN32_WCE
   { CFG_ITEM_ACCESS_POINT, TXT_ACC_ACCESS_POINT, 0, OffsetOf(S_config, wce_iap_name) },
#else
   { CFG_ITEM_ACCESS_POINT, TXT_ACC_ACCESS_POINT, 0, OffsetOf(S_config, iap_id) },
#ifdef USE_ALT_IAP
   { CFG_ITEM_ACCESS_POINT, TXT_ACC_ACCESS_POINT_ALT, 1, OffsetOf(S_config, secondary_iap_id) },
#endif
#endif
}, C_mail_client::config_work_hours[] = {
   { CFG_ITEM_CHECKBOX, TXT_CFG_WORK_TIMES_SET, 0, OffsetOf(S_config_mail, work_times_set) },
   { CFG_ITEM_WORK_HOUR, TXT_CFG_WORK_START, 0, OffsetOf(S_config_mail, work_time_beg) },
   { CFG_ITEM_WORK_HOUR, TXT_CFG_WORK_END, 0, OffsetOf(S_config_mail, work_time_end) },
   { CFG_ITEM_CHECKBOX, TXT_DAY_MON, 1, OffsetOf(S_config_mail, work_days), 0, 0, TXT_CFG_WORK_DAY_HLP },
   { CFG_ITEM_CHECKBOX, TXT_DAY_TUE, 2, OffsetOf(S_config_mail, work_days), 0, 0, TXT_CFG_WORK_DAY_HLP },
   { CFG_ITEM_CHECKBOX, TXT_DAY_WED, 4, OffsetOf(S_config_mail, work_days), 0, 0, TXT_CFG_WORK_DAY_HLP },
   { CFG_ITEM_CHECKBOX, TXT_DAY_THU, 8, OffsetOf(S_config_mail, work_days), 0, 0, TXT_CFG_WORK_DAY_HLP },
   { CFG_ITEM_CHECKBOX, TXT_DAY_FRI, 16, OffsetOf(S_config_mail, work_days), 0, 0, TXT_CFG_WORK_DAY_HLP },
   { CFG_ITEM_CHECKBOX, TXT_DAY_SAT, 32, OffsetOf(S_config_mail, work_days), 0, 0, TXT_CFG_WORK_DAY_HLP },
   { CFG_ITEM_CHECKBOX, TXT_DAY_SUN, 64, OffsetOf(S_config_mail, work_days), 0, 0, TXT_CFG_WORK_DAY_HLP },
   /*
}, C_mail_client::config_language = {
   CFG_ITEM_LANGUAGE, TXT_CFG_LANGUAGE
   */
}, C_mail_client::config_tweaks[] = {
   { CFG_ITEM_TWEAKS_RESET, dword(L"Reset to default\0" L"Reset all tweaks to their default values.") },
//#ifndef WINDOWS_MOBILE
   { CFG_ITEM_CHECKBOX, dword(L"Show new message notify\0" L"Show information about new messages when ProfiMail is in background."),
      0, OffsetOf(S_config_mail, tweaks.show_new_mail_notify) },
//#endif
   { CFG_ITEM_WORD_NUMBER, dword(L"Preview height/width (%)\0" L"Size of message preview area, relative to total area height or width available for message list and message preview."), (25<<16)|75, OffsetOf(S_config_mail, tweaks.preview_area_percent), 5 },
   { CFG_ITEM_CHECKBOX, dword(L"Prefer plain text\0" L"If message body has two forms, html and plain text, download plain text, not html."), 0, OffsetOf(S_config_mail, tweaks.prefer_plain_text_body) },
   { CFG_ITEM_CHECKBOX, dword(L"Reply below original\0" L"Reply below original (quoted) message text, not above."), 0, OffsetOf(S_config_mail, tweaks.reply_below_quote) },
   { CFG_ITEM_CHECKBOX, dword(L"Quote message on reply\0" L"When replying or forwarding, include original message in text."), 0, OffsetOf(S_config_mail, tweaks.quote_when_reply) },
   { CFG_ITEM_CHECKBOX, dword(L"Ask to exit\0" L"Confirm exiting from application."), 0, OffsetOf(S_config_mail, tweaks.ask_to_exit) },
   { CFG_ITEM_CHECKBOX, dword(L"Open links by system\0" L"Open links in emails by system viewer. When unchecked, built-in viewer is used by default."), 0, OffsetOf(S_config_mail, tweaks.open_links_by_system) },
   { CFG_ITEM_WORD_NUMBER, dword(L"Vibration time\0" L"Time for how long vibration alert is done, in milliseconds."), (0<<16)|2000, OffsetOf(S_config_mail, tweaks.vibration_length), 50 },
#if defined __SYMBIAN_3RD__ && defined S60
   { CFG_ITEM_CHECKBOX, dword(L"No LED flash\0" L"This disables LED flashing as new mail notify. Applicable only to some phones."), 0, OffsetOf(S_config_mail, tweaks.no_led_flash) },
#endif
   //{ CFG_ITEM_CHECKBOX, dword(L"Use volume keys\0" L"Use volume up/down keys (if present) as cursor up/down keys."), 0, OffsetOf(S_config_mail, tweaks.use_volume_keys) },
   { CFG_ITEM_CHECKBOX, dword(L"Preview scroll marks read\0" L"Scrolling in message preview window marks the message as read."), 0, OffsetOf(S_config_mail, tweaks.scroll_preview_marks_read) },
   { CFG_ITEM_CHECKBOX, dword(L"Check mail on startup\0" L"Check for mail (update mailboxes) when ProfiMail is started."), 0, OffsetOf(S_config_mail, tweaks.check_mail_on_startup) },
   { CFG_ITEM_CHECKBOX, dword(L"Detect phone numbers\0" L"Detect phone numbers in email messages, and allow calling to these numbers."), 0, OffsetOf(S_config_mail, tweaks.detect_phone_numbers) },
   { CFG_ITEM_CHECKBOX, dword(L"Go to Inbox\0" L"When opening account, go directly to Inbox folder. Folder list will be accessible from main screen menu."), 0, OffsetOf(S_config_mail, tweaks.imap_go_to_inbox) },
   { CFG_ITEM_CHECKBOX, dword(L"Show recent flag\0" L"Show flag for \"Recent\" IMAP messages."), 0, OffsetOf(S_config_mail, tweaks.show_recent_flags) },
   { CFG_ITEM_TEXTBOX_CSTR, dword(L"Own date format\0" L"Set date format string. Character expansion: %d = day, %m = month, %M = month name, %y = short year, %Y = long year. Example: %Y-%M-%d"), 20, OffsetOf(S_config_mail, tweaks.date_fmt), 0, false },
   { CFG_ITEM_WORD_NUMBER, dword(L"Ask password time\0" L"Time (minutes) after which program password (if set) will be asked again when ProfiMail runs in background. Set to 0 to not ask password after startup."), (0<<16)|120, OffsetOf(S_config_mail, tweaks.pass_ask_timeout), 1 },
   { CFG_ITEM_CHECKBOX, dword(L"Show only unread counters\0" L"Show mail icons and counters only for unread and unsent messages. Hide counters for read/sent/draft messages."), 0, OffsetOf(S_config_mail, tweaks.show_minimum_counters) },
   { CFG_ITEM_CHECKBOX, dword(L"Show only unread messages\0" L"Show only unread messages. Read messages will be hidden."), 0, OffsetOf(S_config_mail, tweaks.show_only_unread_msgs) },
   { CFG_ITEM_CHECKBOX, dword(L"Keep removed messages\0" L"Keep messages removed from server locally - do not remove them during mailbox update."), 0, OffsetOf(S_config_mail, tweaks.always_keep_messages) },
   { CFG_ITEM_CHECKBOX, dword(L"Exit in Menus\0" L"Display Exit in menus instead of Back, which allows fast closing of ProfiMail from various places."), 0, OffsetOf(S_config_mail, tweaks.exit_in_menus) },
#if defined __SYMBIAN32__ && defined __SYMBIAN_3RD__
   { CFG_ITEM_CHECKBOX, dword(L"Red key close\0" L"Red key will close ProfiMail when pressed (instead of hiding), but only when no mail check is scheduled, and Push accounts are not connected."), 0, OffsetOf(S_config_mail, tweaks.red_key_close_app) },
#endif
   //{ CFG_ITEM_CHECKBOX, dword(L"Show accounts on focus\0" L"When ProfiMail is brought to foreground, it will go to Accounts view if it was in other mode."), 0, OffsetOf(S_config_mail, tweaks.focus_go_to_main_screen) },
};

//----------------------------

static const int time_out_vals[] = {
//#if defined _DEBUG || 0
   1, 2, 5, 10, 20,
//#endif
   30, 60, 90, 120, 180, 240, 300, 360, 420, 480, 540, 600
};
const int NUM_TIME_OUT_VALS = sizeof(time_out_vals)/sizeof(*time_out_vals);
static int FindTimeOutValIndex(int to){
   for(int i=NUM_TIME_OUT_VALS; i--; ){
      if(to==time_out_vals[i])
         return i;
   }
   return -1;
}

//----------------------------

C_mail_client::C_mode_config_mail::C_mode_config_mail(C_mail_client &_app, const S_config_item *opts, dword num_opts, C_configuration_editing_email *ce):
   C_mode_config_client(_app, opts, num_opts, ce)
{
}

//----------------------------

bool C_mail_client::C_mode_config_mail::GetConfigOptionText(const S_config_item &ec, Cstr_w &str, bool &draw_arrow_l, bool &draw_arrow_r) const{

   const S_config_mail &config_mail = App().config_mail;
   switch(ec.ctype){
   case CFG_ITEM_NUMBER:
      if(ec.elem_offset==OffsetOf(S_config_mail, auto_check_time)){
         if(&options[selection] != &ec){
            dword n = config_mail.auto_check_time;
            if(n){
               str<<n <<' ' <<app.GetText(TXT_CFG_MIN);
            }else
               str = app.GetText(TXT_CFG_DISABLED);
         }
      }
      break;

   case CFG_ITEM_WORK_HOURS_SUM:
      if(config_mail.work_times_set){
         S_date_time dt;
         dt.SetFromSeconds(config_mail.work_time_beg*60);
         Cstr_w s, e;
         s = text_utils::GetTimeString(dt, false);
         dt.SetFromSeconds(config_mail.work_time_end*60);
         e = text_utils::GetTimeString(dt, false);

         str.Format(L"% - % [% % % % % % %]") <<s <<e;
         for(int i=0; i<7; i++){
            bool on = (config_mail.work_days&(1<<i));
            const wchar *dn = app.GetText(E_TEXT_ID(TXT_DAY_MON+i));
            str<<wchar(on ? dn[0] : '-');
         }
      }else
         str = app.GetText(TXT_CFG_DISABLED);
      break;

   case CFG_ITEM_WORK_HOUR:
      {
         S_date_time dt;
         word t = *(word*)(((byte*)&config_mail) + ec.elem_offset);
         dt.SetFromSeconds(t*60);
         str = text_utils::GetTimeString(dt, false);
         draw_arrow_l = draw_arrow_r = true;
      }
      break;

   case CFG_ITEM_ENUM:
      {
         byte n = *(((byte*)&config_mail) + ec.elem_offset);
         const char *opts = (char*)ec.param;
         dword num;
         for(num=0; *opts; ++num, opts+=StrLen(opts)+1){
            if(n==num)
               str.Copy(opts);
         }
         draw_arrow_l = (n>0);
         draw_arrow_r = (n<num-1);
      }
      break;

      /*
   case CFG_ITEM_COUNTER_MODE:
      draw_arrow_l = draw_arrow_r = true;
      switch(config_mail.flags&S_config::CONF_DRAW_COUNTER_MASK){
      case S_config::CONF_DRAW_COUNTER_CURRENT:
         str = app.GetText(TXT_CFG_SHOW_CNT_CURRENT);
         break;
      case S_config::CONF_DRAW_COUNTER_TOTAL:
         str = app.GetText(TXT_CFG_SHOW_CNT_TOTAL);
         draw_arrow_r = false;
         break;
      default:
         str = app.GetText(TXT_CFG_SHOW_CNT_NONE);
         draw_arrow_l = false;
      }
      break;
      */

   case CFG_ITEM_TIME_OUT:
      {
         if(config_mail.connection_time_out%60){
            str<<config_mail.connection_time_out <<L' ' <<app.GetText(TXT_CFG_SEC);
         }else{
            str<<(config_mail.connection_time_out/60) <<L' ' <<app.GetText(TXT_CFG_MIN);
         }
         int indx = FindTimeOutValIndex(config_mail.connection_time_out);
         draw_arrow_l = (config_mail.connection_time_out>30);
         draw_arrow_r = (indx!=NUM_TIME_OUT_VALS-1);
      }
      break;

   case CFG_ITEM_ALERT_SOUND:
      str = file_utils::GetFileNameNoPath(config_mail.alert_sound);
      break;

   case CFG_ITEM_ALERT_VOLUME:
      {
         dword n = config_mail.alert_volume;
         str<<n;
         draw_arrow_l = (n!=0);
         draw_arrow_r = (n!=10);
      }
      break;

   case CFG_ITEM_DATE_FORMAT:
      {
         str = S_config_mail::GetDateFormatString(config_mail.date_format);
         draw_arrow_l = (config_mail.date_format!=0);
         draw_arrow_r = (config_mail.date_format!=config_mail.DATE_LAST-1);
      }
      break;

      /*
   case CFG_ITEM_IMAGE_SCALE:
      {
         int perc = (config_mail.img_ratio+(C_fixed::Percent(1)>>1))*C_fixed(100);
         str<<perc <<L" %";
         draw_arrow_l = (config_mail.img_ratio>C_fixed::Percent(MIN_IMG_SIZE));
         draw_arrow_r = (config_mail.img_ratio<C_fixed::Percent(MAX_IMG_SIZE));
      }
      break;
      */

   case CFG_ITEM_IMAP_CONNECT_MODE:
      switch(config_mail.imap_idle_startup_connect){
      case S_config_mail::IDLE_CONNECT_ASK: str = app.GetText(TXT_ASK); break;
      case S_config_mail::IDLE_CONNECT_AUTOMATIC: str = app.GetText(TXT_AUTOMATIC); break;
      case S_config_mail::IDLE_CONNECT_MANUAL: str = app.GetText(TXT_MANUAL); break;
      }
      draw_arrow_l = (config_mail.imap_idle_startup_connect>0);
      draw_arrow_r = (config_mail.imap_idle_startup_connect<2);
      break;

   case CFG_ITEM_DATA_LOCATION:
      {
         const C_configuration_editing_email *ce = (C_configuration_editing_email*)configuration_editing;
         str = ce->data_locations[ce->curr_loc_index].name;
         draw_arrow_l = (ce->curr_loc_index>0);
         draw_arrow_r = (ce->curr_loc_index<ce->data_locations.size()-1);
      }
      break;
   default:
      return C_mode_config_client::GetConfigOptionText(ec, str, draw_arrow_l, draw_arrow_r);
   }
   return true;
}

//----------------------------

class C_find_files_arch_sim: public C_archive_simulator{
   struct S_file{
      Cstr_w display_name;
      Cstr_w full_name;
      dword size;
   };
   C_vector<S_file> files;
   virtual E_TYPE GetArchiveType() const{ return (E_TYPE)1001; }
   virtual const wchar *GetFileName() const{ return L"*FindResults"; }

   virtual bool DeleteFile(const wchar *filename){
      return false;
   }
   virtual void SetChangeObserver(t_Change c, void *context){
   }

//----------------------------

   virtual void *GetFirstEntry(const wchar *&name, dword &length) const{
      if(!files.size())
         return NULL;
      name = files.front().display_name;
      length = files.front().size;
      return (void*)1;
   }

//----------------------------

   virtual void *GetNextEntry(void *handle, const wchar *&name, dword &length) const{
      dword indx = (dword)handle;
      if(indx==NumFiles())
         return NULL;
      name = files[indx].display_name;
      length = files[indx].size;
      ++indx;
      return (void*)indx;
   }

//----------------------------

   virtual dword NumFiles() const{ return files.size(); }

   int FindIndex(const wchar *filename) const{
      for(int i=0; i<files.size(); i++){
         if(files[i].display_name==filename)
            return i;
      }
      return -1;
   }

//----------------------------

   virtual bool GetFileWriteTime(const wchar *filename, S_date_time &td) const{
      return false;
   }
public:
   const wchar *GetRealFileName(const wchar *filename) const{
      int i = FindIndex(filename);
      if(i==-1)
         return NULL;
      return files[i].full_name;
   }

   void Construct(const C_vector<Cstr_w> &fls){
      files.reserve(fls.size());
      for(int i=0; i<fls.size(); i++){
         C_file fl;
         S_file f;
         f.full_name = fls[i];
         if(fl.Open(f.full_name)){
            f.size = fl.GetFileSize();
            f.display_name = file_utils::GetFileNameNoPath(f.full_name);
            if(FindIndex(f.display_name)!=-1){
                              //avoid name duplicates
               int ei = f.display_name.FindReverse('.');
               Cstr_w name = ei==-1 ? f.display_name : f.display_name.Left(ei);
               Cstr_w ext;
               if(ei!=-1)
                  ext = f.display_name.Right(f.display_name.Length()-ei);
               for(int j=1; j<100; j++){
                  Cstr_w n1;
                  n1.Format(L"% (#02%)%") <<name <<j <<ext;
                  if(FindIndex(n1)==-1){
                     f.display_name = n1;
                     break;
                  }
               }
            }
            files.push_back(f);
         }
      }
   }

   virtual C_file *OpenFile(const wchar *filename) const{
      int i = FindIndex(filename);
      if(i!=-1){
         C_file *fp = new(true) C_file;
         if(fp->Open(files[i].full_name))
            return fp;
         delete fp;
      }
      return NULL;
   }
};

//----------------------------

Cstr_w C_mail_client::GetAudioAlertRealFileName(C_client_file_mgr::C_mode_file_browser &mod, const wchar *fn){

   const C_find_files_arch_sim *arch = (C_find_files_arch_sim*)(const C_zip_package*)mod.arch;
   return arch->GetRealFileName(fn);
}

//----------------------------

static void AddFolderFiles(const wchar *dir, C_vector<Cstr_w> &files, const char *exts){

   //LOG_RUN(Cstr_w(dir).ToUtf8());
   C_dir d;
   if(d.ScanBegin(dir)){
      while(true){
         dword attributes;
         const wchar *fn = d.ScanGet(&attributes);
         if(!fn)
            break;
         Cstr_w s;
         if(attributes&C_file::ATT_DIRECTORY){
            s<<dir <<fn <<'\\';
            AddFolderFiles(s, files, exts);
         }else{
            if(C_client_file_mgr::CheckExtensionMatch(fn, exts)){
               s<<dir <<fn;
               files.push_back(s);
            }
         }
      }
   }
}

//----------------------------

void C_mail_client::SelectAudioAlert(C_client_file_mgr::C_mode_file_browser::t_OpenCallback OpenCallback, const Cstr_w &fn_base){

   DrawWashOut();
   UpdateScreen();
   //config.flags |= config.CONF_BROWSER_SHOW_ROM;
   C_client_file_mgr::C_mode_file_browser &mod = C_client_file_mgr::SetModeFileBrowser(this, C_client_file_mgr::C_mode_file_browser::MODE_EXPLORER, false,
      OpenCallback, TXT_CHOOSE_FILE);
   mod.flags = mod.FLG_ACCEPT_FILE | mod.FLG_SELECT_OPTION | mod.FLG_AUTO_COLLAPSE_DIRS | mod.FLG_AUDIO_PREVIEW;

   C_vector<Cstr_w> files;
                        //get sound files on ROM and mem card drives
#ifdef WINDOWS_MOBILE
   AddFolderFiles(L"\\", files, "wav\0");
#elif defined _DEBUG
   {
      Cstr_w fp;
      C_file::GetFullPath(L"", fp);
      AddFolderFiles(fp, files, "wav\0mp3\0");
   }
#else
   dword drvs = C_dir::GetAvailableDrives();
   for(int i=0; i<32; i++){
      if(drvs&(1<<i)){
         Cstr_w dir;
         dir<<char('a'+i) <<L":\\";
         AddFolderFiles(dir, files, "wav\0mp3\0aac\0m4a\0rng\0mxmf\0amr\0mid\0");
      }
   }
#endif
   SortVector(files);

   C_find_files_arch_sim *sim = new(true) C_find_files_arch_sim;
   sim->Construct(files);
   mod.arch.SetPtr(sim);
   sim->Release();

   mod.entries.clear();
   {
      C_client_file_mgr::C_mode_file_browser::S_entry e;
      e.type = e.ARCHIVE;
      e.name = GetText(TXT_CFG_ALERT_SOUND);
      e.level = 0;
      e.size.file_size = 0;
      mod.entries.push_back(e);
      mod.sb.total_space = mod.entry_height;
      mod.selection = 0;
   }
                           //expand top level
   C_client_file_mgr::FileBrowser_ExpandDir(this, mod);
   Cstr_w fn_init = file_utils::GetFileNameNoPath(fn_base);
   for(int i=1; i<mod.entries.size(); i++){
      const C_client_file_mgr::C_mode_file_browser::S_entry &e = mod.entries[i];
      if(fn_init==e.name){
         mod.selection = i;
         break;
      }
   }
   mod.EnsureVisible();
   RedrawScreen();
}

//----------------------------

bool C_mail_client::AlertSoundSelectCallback(const Cstr_w *file, C_vector<Cstr_w> *files){

   if(!file)
      return false;
   C_client_file_mgr::C_mode_file_browser &mod = (C_client_file_mgr::C_mode_file_browser&)*mode;
   config_mail.alert_sound = GetAudioAlertRealFileName(mod, *file);
   SaveConfig();
   PlayNewMailSound();
   C_client_file_mgr::FileBrowser_Close(this, mod);
   return true;
}

//----------------------------

bool C_mail_client::C_mode_config_mail::IsDefaultAlert() const{
   const S_config_mail &config_mail = App().config_mail;
   return (config_mail.alert_sound==config_mail.GetDefaultAlertSound());
}

//----------------------------

dword C_mail_client::C_mode_config_mail::GetLeftSoftKey(const S_config_item &ec) const{
   if(ec.ctype==CFG_ITEM_ALERT_SOUND){
      if(!IsDefaultAlert())
         return TXT_DEFAULT;
         
   }
   return TXT_NULL;
}

//----------------------------

bool C_mail_client::C_mode_config_mail::ChangeConfigOption(const S_config_item &ec, dword key, dword key_bits){

   S_config_mail &config_mail = App().config_mail;
   bool changed = false;
   switch(ec.ctype){
   case CFG_ITEM_DATA_LOCATION:
      if(key==K_CURSORLEFT || key==K_CURSORRIGHT){
         C_configuration_editing_email *ce = (C_configuration_editing_email*)configuration_editing;
         int l = ce->curr_loc_index;
         if(key==K_CURSORLEFT)
            l = Max(0, l-1);
         else
            l = Min(ce->data_locations.size()-1, l+1);
         if(ce->curr_loc_index!=l){
            ce->curr_loc_index = l;
            changed = true;
         }
      }
      break;

   case CFG_ITEM_CHECKBOX:
      if(key==K_ENTER){
         if(
            ec.elem_offset!=OffsetOf(S_config_mail, work_times_set)){
            changed = C_mode_config_client::ChangeConfigOption(ec, key, key_bits);
            break;
         }
                              //special work times setting
         config_mail.work_times_set = !config_mail.work_times_set;
         changed = true;
         C_mail_client &app1 = App();
         app1.CloseMode(*this, false);

         C_mode_config_work_times &mod = *new(true) C_mode_config_work_times(app1, config_work_hours, config_mail.work_times_set ? (sizeof(config_work_hours)/sizeof(*config_work_hours)) : 1, new C_configuration_editing_email(app1));
         mod.InitLayout();
         app1.ActivateMode(mod);
      }
      break;

   case CFG_ITEM_WORK_HOUR:
      if(key==K_CURSORLEFT || key==K_CURSORRIGHT){
         const int CHANGE_MINUTES =
#if defined _DEBUG
            5;
#else
            15;
#endif
         word &t = *(word*)(((byte*)&config_mail) + ec.elem_offset);
         if(key == K_CURSORLEFT)
            t = word(t+24*60-CHANGE_MINUTES);
         else
            t = word(t+CHANGE_MINUTES);
         t %= 24*60;
         t = (t/CHANGE_MINUTES)*CHANGE_MINUTES;
         changed = true;
      }
      break;

   case CFG_ITEM_ENUM:
      if(key==K_CURSORLEFT || key==K_CURSORRIGHT){
         byte &n = *(((byte*)&config_mail) + ec.elem_offset);
         byte nw = n;
         const char *opts = (char*)ec.param;
         dword num;
         for(num=0; *opts; ++num, opts+=StrLen(opts)+1);
         if(key == K_CURSORLEFT){
            if(nw)
               --nw;
         }else{
            if(nw<num-1)
               ++nw;
         }
         if(n!=nw){
            n = nw;
         }
         changed = true;
      }
      break;

      /*
   case CFG_ITEM_COUNTER_MODE:
      if(key==K_CURSORRIGHT || key==K_CURSORLEFT){
         dword &f = config_mail.flags;
         dword cf = f&S_config::CONF_DRAW_COUNTER_MASK;
         if(key==K_CURSORLEFT){
            switch(cf){
            case S_config::CONF_DRAW_COUNTER_TOTAL: f &= ~cf; f |= S_config::CONF_DRAW_COUNTER_CURRENT; changed = true; break;
            case S_config::CONF_DRAW_COUNTER_CURRENT: f &= ~cf; f |= S_config::CONF_DRAW_COUNTER_NO; changed = true; break;
            }
         }else{
            switch(cf){
            case S_config::CONF_DRAW_COUNTER_NO: f &= ~cf; f |= S_config::CONF_DRAW_COUNTER_CURRENT; changed = true; break;
            case S_config::CONF_DRAW_COUNTER_CURRENT: f &= ~cf; f |= S_config::CONF_DRAW_COUNTER_TOTAL; changed = true; break;
            }
         }
      }
      break;
      */

   case CFG_ITEM_TIME_OUT:
      if(key==K_CURSORRIGHT || key==K_CURSORLEFT){
         int i = FindTimeOutValIndex(config_mail.connection_time_out);
         if(i==-1)
            i = 1;
         if(key==K_CURSORLEFT){
            if(i){
               if(config_mail.connection_time_out>30 || (key_bits&GKEY_SHIFT)){
                  config_mail.connection_time_out = time_out_vals[i-1];
                  changed = true;
               }
            }
         }else{
            if(i+1<NUM_TIME_OUT_VALS){
               config_mail.connection_time_out = time_out_vals[i+1];
               changed = true;
            }
         }
      }
      break;

   case CFG_ITEM_ALERT_SOUND:
      switch(key){
      case K_LEFT_SOFT:
         if(!IsDefaultAlert()){
                              //set to default
            config_mail.SetDefaultAlertSound();
            changed = true;
         }
         break;
      }
      break;

   case CFG_ITEM_ALERT_VOLUME:
      if(key==K_CURSORRIGHT || key==K_CURSORLEFT){
         if(key==K_CURSORLEFT){
            if(config_mail.alert_volume){
               --config_mail.alert_volume;
               changed = true;
            }
         }else{
            if(config_mail.alert_volume<10){
               ++config_mail.alert_volume;
               changed = true;
            }
         }
      }
      break;

   case CFG_ITEM_DATE_FORMAT:
      if(key==K_CURSORRIGHT || key==K_CURSORLEFT){
         if(key==K_CURSORLEFT){
            if(config_mail.date_format){
               config_mail.date_format = (S_config_mail::E_DATE_FORMAT)(config_mail.date_format-1);
               changed = true;
            }
         }else{
            if(config_mail.date_format<config_mail.DATE_LAST-1){
               config_mail.date_format = (S_config_mail::E_DATE_FORMAT)(config_mail.date_format+1);
               changed = true;
            }
         }
      }
      break;

      /*
   case CFG_ITEM_IMAGE_SCALE:
      if(key==K_CURSORRIGHT || key==K_CURSORLEFT){
         if(key==K_CURSORRIGHT){
            config_mail.img_ratio += C_fixed::Percent(10);
            if(config_mail.img_ratio > C_fixed::Percent(MAX_IMG_SIZE))
               config_mail.img_ratio = C_fixed::Percent(MAX_IMG_SIZE);
         }else{
            config_mail.img_ratio -= C_fixed::Percent(10);
            if(config_mail.img_ratio < C_fixed::Percent(MIN_IMG_SIZE))
               config_mail.img_ratio = C_fixed::Percent(MIN_IMG_SIZE);
         }
         changed = true;
      }
      break;
      */

   case CFG_ITEM_IMAP_CONNECT_MODE:
      if(key==K_CURSORRIGHT || key==K_CURSORLEFT){
         if(key==K_CURSORRIGHT){
            if(config_mail.imap_idle_startup_connect<2)
               ++((byte*&)config_mail.imap_idle_startup_connect);
         }else{
            if(config_mail.imap_idle_startup_connect)
               --((byte*&)config_mail.imap_idle_startup_connect);
         }
         changed = true;
      }
      break;
   default:
      changed = C_mode_config_client::ChangeConfigOption(ec, key, key_bits);
   }
   return changed;
}

//----------------------------

void C_mail_client::C_configuration_editing_email::OnClose(){
   if(modified)
      App().CloseConfigNotify(data_locations, curr_loc_index, init_loc_index);
   super::OnClose();
}

//----------------------------

void C_mail_client::C_configuration_editing_email::OnConfigItemChanged(const S_config_item &ec){

   S_config_mail &config_mail = App().config_mail;
   switch(ec.ctype){
   case CFG_ITEM_ACCESS_POINT:
      App().CloseAccountsConnections();
      break;
   case CFG_ITEM_CHECKBOX:
      switch(ec.txt_id){
#ifndef USE_SYSTEM_VIBRATE
      case TXT_CFG_VIBRATE_ALERT:
         if(config_mail.flags&config_mail.CONF_VIBRATE_ALERT){
            if(!App().MakeVibration()){
               config_mail.flags &= ~config_mail.CONF_VIBRATE_ALERT;
            }
         }
         break;
#endif
      case TXT_CFG_FULLSCREEN:
         app.UpdateGraphicsFlags(config_mail.fullscreen ? 0 : IG_SCREEN_USE_CLIENT_RECT, IG_SCREEN_USE_CLIENT_RECT);
         break;
      default:
         {
            if(ec.elem_offset==OffsetOf(S_config_mail, tweaks.show_only_unread_msgs)){
               App().MarkToSortAllAccounts();
            }
         }
      }
   case CFG_ITEM_WORD_NUMBER:
      if(ec.elem_offset==OffsetOf(S_config_mail, tweaks.vibration_length)){
         App().MakeVibration();
      }
      break;
   case CFG_ITEM_TIME_OUT:
      app.CloseConnection();
      break;
   case CFG_ITEM_ALERT_VOLUME:
      App().PlayNewMailSound();
      break;
   }
   super::OnConfigItemChanged(ec);
}

//----------------------------

void C_mail_client::C_configuration_editing_email::OnClick(const S_config_item &ec){

   S_config_mail &config_mail = App().config_mail;
   switch(ec.ctype){
   case CFG_ITEM_TWEAKS:
      App().SetConfigTweaks();
      break;
   case CFG_ITEM_TWEAKS_RESET:
      {
         class C_question: public C_question_callback{
            C_mail_client &app;
            virtual void QuestionConfirm(){
               app.config_mail.tweaks.SetDefaults();
               app.SaveConfig();
            }
         public:
            C_question(C_mail_client &a): app(a){}
         };
         CreateQuestion(App(), TXT_Q_ARE_YOU_SURE, L"This will reset all tweaks to default values.", new(true) C_question(App()), true);
      }
      break;
   case CFG_ITEM_WORK_HOURS_SUM:
      {
         C_configuration_editing_work_times *ce = new(true) C_configuration_editing_work_times(App());
         dword num = sizeof(config_work_hours)/sizeof(*config_work_hours);
         C_mode_config_work_times &mod = *new(true) C_mode_config_work_times(App(), config_work_hours, config_mail.work_times_set ? num : 1, ce);
         mod.InitLayout();
         mod.Activate();
      }
      break;
   case CFG_ITEM_APP_PASSWORD:
      {
         const Cstr_w &s = config_mail.app_password;
         if(!s.Length()){
            App().AskNewPassword();
         }else{
            class C_text_entry: public C_text_entry_callback{
               C_mail_client &app;
               virtual void TextEntered(const Cstr_w &txt){
                  if(txt!=app.config_mail.app_password)
                     app.ShowErrorWindow(TXT_ERROR, TXT_INVALID_PASSWORD);
                  else
                     app.AskNewPassword();
               }
            public:
               C_text_entry(C_mail_client &a): app(a){}
            };
            CreateTextEntryMode(app, TXT_ENTER_CURRENT_PASSWORD, new(true) C_text_entry(App()), true, 1000, NULL, TXTED_SECRET);
         }
      }
      break;
   case CFG_ITEM_ALERT_SOUND:
      App().SelectAudioAlert((C_client_file_mgr::C_mode_file_browser::t_OpenCallback)&C_mail_client::AlertSoundSelectCallback, config_mail.alert_sound);
      break;
   }
   super::OnClick(ec);
}

//----------------------------

void C_mail_client::ConfigMailLocationChanged(dword index){

   C_vector<S_data_location> locs;
   GetPossibleDataLocations(locs);
   if(index<(dword)locs.size()){
      Cstr_w sfn; sfn<<temp_path <<L"settings.tmp";
      C_file::MakeSurePathExists(sfn);
      if(ExportSettings(sfn)){
                              //delete old settigns
         Cstr_w rem_dir = GetAccountsFilename();
         C_file::DeleteFile(rem_dir);
         C_file::DeleteFile(GetRulesFilename());
         C_file::DeleteFile(GetSignaturesFilename());
         C_file::DeleteFile(GetConfigFilename());

         Cstr_w save = mail_data_path;
         mail_data_path = locs[index].path;
         if(ImportSettings(sfn)){
            SaveDataPath();

            save<<MAIL_PATH;
            save = save.Left(save.Length()-1);
            C_file::GetFullPath(save, save);
            C_dir::RemoveDirectory(save, true);
                              //remove possibly empty dirs
            C_file::GetFullPath(rem_dir, rem_dir);
            while(rem_dir.Length()){
               rem_dir = rem_dir.Left(rem_dir.FindReverse('\\'));
               if(!C_dir::RemoveDirectory(rem_dir))
                  break;
            }
         }else{
            mail_data_path = save;
            ImportSettings(sfn);
         }
         C_file::DeleteFile(sfn);
      }else{
                              //just switch
         mail_data_path = locs[index].path;
         SaveConfig();
      }
   }else
      assert(0);
}

//----------------------------

void C_mail_client::CloseConfigNotify(const C_vector<S_data_location> &data_locations, int curr_loc_index, int init_loc_index){

   ManageTimer();
                              //reconnect idle
   ConnectEnabledAccountsInBackground();

   if(curr_loc_index!=init_loc_index){
      //LOG_RUN("loc!");
                              //ask to switch location
      Cstr_w q;
      q.Format(GetText(TXT_Q_LOCATION_CHANGE)) <<data_locations[curr_loc_index].name;

      class C_question: public C_question_callback{
         C_mail_client &app;
         dword index;
         virtual void QuestionConfirm(){
            app.ConfigMailLocationChanged(index);
         }
      public:
         C_question(C_mail_client &a, dword i): app(a), index(i){}
      };
      CreateQuestion(*this, TXT_Q_ARE_YOU_SURE, q, new(true) C_question(*this, curr_loc_index), true);
   }
}

//----------------------------

void C_mail_client::SetConfig(){

   C_vector<S_config_item> itms;
   itms.insert(itms.end(), config_options, config_options+sizeof(config_options)/sizeof(*config_options));
#ifdef USE_SYSTEM_PROFILES
                              //if profile tone is supported, don't offer such option
   Cstr_w tmp;
   if(C_phone_profile::GetEmailAlertTone(tmp)){
      for(int i=itms.size(); i--; ){
         if(itms[i].ctype==CFG_ITEM_ALERT_SOUND){
            itms.remove_index(i);
            break;
         }
      }
   }
#endif
   C_configuration_editing_email *ce = new(true) C_configuration_editing_email(*this);
   ce->init_loc_index = GetPossibleDataLocations(ce->data_locations);
   ce->curr_loc_index = Max(0, ce->init_loc_index);
#ifndef USE_ANDROID_TEXTS
   ce->CollectLanguages();
#endif
   C_mode_config_mail &mod = *new(true) C_mode_config_mail(*this, itms.begin(), itms.size(), ce);
   mod.InitLayout();
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::SetConfigTweaks(){

   const dword num = sizeof(config_tweaks)/sizeof(*config_tweaks);
   C_mode_config_mail &mod = *new(true) C_mode_config_mail(*this, config_tweaks, num, new C_configuration_editing_email(*this));
   mod.InitLayout();
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::LoadGraphics(){

   const C_smart_ptr<C_zip_package> dta = CreateDtaFile();

   icons_file = C_image::Create(*this);
   icons_file->Release();
   icons_file->Open(L"icons_file.png", 0, fdb.line_spacing*10/8, dta);

   static const char *const fnames[] = { "opened", "new", "sent", "draft", "to_send", "opened_part", "new_part", "draft_part", "deleted", "scissors", "pr_high", "pr_low", "flag", "reply", "forward", "attach", "recent", "pin" };
   for(int i=0; i<MESSAGE_ICON_SMALL_LAST; i++){
      C_image *img = C_image::Create(*this);
      small_msg_icons[i] = img;
      Cstr_w fn; fn<<L"MailIcons\\" <<fnames[i] <<L".png";
      dword sx=0, sy=0;
      if(!i)
         sy = fds.line_spacing;
      else
         sx = small_msg_icons[0]->SizeX();
      img->Open(fn, sx, sy, dta);
      img->Release();
   }
   for(int i=0; i<MESSAGE_ICON_LAST; i++){
      C_image *img = C_image::Create(*this);
      msg_icons[i] = img;
      Cstr_w fn; fn<<L"MailIcons\\" <<fnames[i] <<L".png";
      dword sx=0, sy=0;
      C_fixed radio = 1;
      if(!i)
         sy = fdb.line_spacing*9/8;
      else if(i<9)
         sx = msg_icons[0]->SizeX();
      else{
         radio = C_fixed(msg_icons[0]->SizeY()) / C_fixed(msg_icons[0]->GetOriginalSize().y);
      }
      img->Open(fn, sx, sy, radio, dta);
      img->Release();
   }
}

//----------------------------

void C_mail_client::LoadSpecialIcons(){
   spec_icons = C_image::Create(*this);
   spec_icons->Release();
   spec_icons->Open(L"special_icons.png", 0, fdb.line_spacing, CreateDtaFile());
}

//----------------------------

int C_mail_client::DrawSpecialIcon(int x, int y, E_SPECIAL_ICON index, bool center_y){

   if(!spec_icons)
      LoadSpecialIcons();

   const int NUM_ICONS = 6;
   int SZ_X = spec_icons->SizeX();
   int SZ_Y = spec_icons->SizeY();
   S_rect rc(0, 0, 0, SZ_Y);
   rc.x = (SZ_X*index+NUM_ICONS/2)/NUM_ICONS;
   rc.sx = (SZ_X*(index+1)+NUM_ICONS/2)/NUM_ICONS;
   rc.sx -= rc.x;
   if(center_y)
      y -= SZ_Y/2;
   spec_icons->DrawSpecial(x, y, &rc);
   return SZ_Y;
}

//----------------------------

int C_mail_client::DrawConnectIconType(int x, int y, S_account::E_UPDATE_STATE state, bool center_y){

   int ii = Min(state, S_account::UPDATE_ERROR);
   return DrawSpecialIcon(x, y, E_SPECIAL_ICON(ii), center_y);
}

//----------------------------

int C_mail_client::DrawImapFolderIcon(int x, int y, int index){

   if(!folder_icons){
      folder_icons = C_image::Create(*this);
      folder_icons->Release();
      folder_icons->Open(L"FolderIcons.png", 0, fdb.line_spacing*10/8, CreateDtaFile());
   }
   int SZ = folder_icons->SizeY();
   S_rect rc(0, 0, 0, SZ);
   if(!index)
      rc.sx = SZ*68/29;
   else{
      rc.x = (SZ*69 + (index-1)*SZ*35)/29;
      rc.sx = SZ*34/29;
   }
   folder_icons->DrawSpecial(x, y, &rc);
   return SZ;
}

//----------------------------

static const wchar base_path_fn[] = L"Email\\BasePath.txt";

//----------------------------

void C_mail_client::LoadDataPath(){
   C_file fl;
   if(fl.Open(base_path_fn))
      mail_data_path = fl.GetLine().FromUtf8();
   if(!mail_data_path.Length()){
      /*
                              //setup location base now
      C_file::GetFullPath(L"", mail_data_path);
#ifndef NO_FILE_SYSTEM_DRIVES
      mail_data_path = mail_data_path.Left(mail_data_path.Find('\\'));
#endif
      */
                              //pick last available path (probably memory card)
      C_vector<S_data_location> paths;
      GetPossibleDataLocations(paths);
      mail_data_path = paths.back().path;
   }
   //LOG_RUN("Base path:"); LOG_RUN(mail_data_path.ToUtf8());
}

//----------------------------

void C_mail_client::SaveDataPath() const{

   C_file fl;
   if(fl.Open(base_path_fn, C_file::FILE_WRITE|C_file::FILE_WRITE_CREATE_PATH))
      fl.WriteString(mail_data_path.ToUtf8());
}

//----------------------------

void C_mail_client::MailBaseConstruct(){

   LoadDataPath();

   C_application_ui::Construct(L"Email\\");

   const C_smart_ptr<C_zip_package> dta = CreateDtaFile();
   bool cfg_ok = 
      BaseConstruct(dta);
   if(cfg_ok){
      if(config_mail.flags&S_config_mail::_CONF_SORT_DESCENDING){
         config_mail.flags &= ~S_config_mail::_CONF_SORT_DESCENDING;
         config_mail.sort_descending = true;
         SaveConfig();
      }
      if(config_mail.flags&S_config_mail::_CONF_SORT_MASK){
         config_mail.sort_mode = (S_config_mail::E_SORT_MODE)(config_mail.flags>>24);
         config_mail.flags &= ~S_config_mail::_CONF_SORT_MASK;
         SaveConfig();
      }
   }
   /*
   if(!cfg_ok){
#ifdef _WIN32_WCE
                           //detect WM high-res
      if(ScrnSX()>=480 || ScrnSY()>=480)
         config.img_ratio = C_fixed::One();
#elif defined S60
                           //detect high-res
      if(ScrnSX()>=320 && ScrnSY()>=320)
         config.img_ratio = C_fixed::One();
#endif
   }
   */

   if(!config_mail.alert_sound.Length()){
#ifdef USE_SYSTEM_PROFILES
      Cstr_w tmp;
      if(!C_phone_profile::GetEmailAlertTone(tmp))
#endif
         config_mail.SetDefaultAlertSound();
   }

#ifdef USE_NOKIA_N97_WIDGET
   hs_widget.Init();
#endif
}

//----------------------------

void C_mail_client::Construct(){

   MailBaseConstruct();
   FinishConstruct();
}

//----------------------------

void C_mail_client::FinishConstruct(){

                              //show license if needed
   if(DisplayLicenseAgreement())
      return;
                              //now load accounts with empty password (will ask for pass inside)
   FinishConstructWithPass(NULL);
}

//----------------------------

void C_mail_client::AskInitPassword(){

   class C_text_entry: public C_text_entry_callback{
      C_mail_client &app;
      virtual void TextEntered(const Cstr_w &txt){
         if(txt.Length())
            app.FinishConstructWithPass(txt);
         else
            Init(app);
      }
      virtual void Canceled(){
      }
   public:
      C_text_entry(C_mail_client &a): app(a){}
      static void Init(C_mail_client &app){
         CreateTextEntryMode(app, TXT_ENTER_PASSWORD, new(true) C_text_entry(app), true, 500, NULL, TXTED_SECRET, MODE_ID_PASS_ENTER);
      }
   };
   LOG_RUN("Asking password");
   if(!pass_enter_attempts_left)
      pass_enter_attempts_left = 3;
   C_text_entry::Init(*this);
}

//----------------------------

void C_mail_client::FinishConstructWithPass(const Cstr_w &password, bool after_import){

   LOG_RUN("FinishConstruct");
   if(!LoadAccounts(password)){
      if(!password.Length()){
         AskInitPassword();
      }else{
                              //invalid password
#ifndef _DEBUG
         system::Sleep(1000);
#endif
         //ShowErrorWindow(TXT_ERROR, TXT_INVALID_PASSWORD);
         class C_q: public C_question_callback{
            C_mail_client &app;
            virtual void QuestionConfirm(){
               if(app.pass_enter_attempts_left)
                  app.AskInitPassword();
               else{
                              //clear account settings
                  app.SaveAccounts();
                  app.FinishConstructWithPass(NULL);
               }
            }
            virtual void QuestionReject(){
               app.Exit();
            }
         public:
            C_q(C_mail_client &_a):
               app(_a)
            {}
         };
         --pass_enter_attempts_left;
         Cstr_w s;
         if(pass_enter_attempts_left){
            s<<GetText(TXT_TRY_AGAIN) <<'\n' <<GetText(TXT_ATTEMPTS_LEFT);
            s<<L' ' <<pass_enter_attempts_left;
         }else{
            s = GetText(TXT_ACCOUNTS_WILL_BE_CLEARED);
         }
         CreateQuestion(*this, TXT_INVALID_PASSWORD, s, new C_q(*this), true, TXT_OK, pass_enter_attempts_left ? TXT_EXIT : TXT_CANCEL);
      }
      return;
   }
   if(!NumAccounts() && !after_import){
                              //try to locate accounts in different locations
      Cstr_w save = mail_data_path;
      C_vector<S_data_location> locs;
      int curr = GetPossibleDataLocations(locs);
      int i;
      for(i=0; i<locs.size(); i++){
         if(i==curr)
            continue;
         mail_data_path = locs[i].path;
         if(!LoadAccounts(password)){
            mail_data_path = save;
            if(password.Length())
               ShowErrorWindow(TXT_ERROR, TXT_INVALID_PASSWORD);
            else
               AskInitPassword();
            return;
         }
         if(NumAccounts()){
            LoadConfig();
#if defined USE_SYSTEM_SKIN || defined USE_OWN_SKIN
            EnableSkinsByConfig();
#endif
            SaveDataPath();
            ScreenReinit(true);
            break;
         }
      }
      if(i==locs.size())
         mail_data_path = save;
   }

   SetModeAccounts();
   if(!NumAccounts()){
      class C_question: public C_question_callback{
         C_mail_client &app;
         virtual void QuestionConfirm(){
            app.CreateNewAccount(NULL);
         }
      public:
         C_question(C_mail_client &a): app(a){}
      };
      CreateQuestion(*this, TXT_WELCOME, GetText(TXT_Q_CREATE_ACCOUNT), new(true) C_question(*this), true);
   }else{
      if(config_mail._app_password.Length()){
                              //2011-05-09: convert old password to new, shall be removed after 2011-11-09
         if(!config_mail.app_password.Length()){
            config_mail.app_password = config_mail._GetPassword();
            SaveAccounts();
         }
         config_mail._app_password.Clear();
         SaveConfig();
      }
      if(after_import){
                              //retain password used to import files
         config_mail.app_password = password;
                        //reset stats
         for(int i=accounts.Size(); i--; ){
            S_account &acc = accounts[i];
            C_folders_iterator it(acc._folders);
            while(!it.IsEnd())
               it.Next()->ResetStats();
         }
         SaveAccounts();
         CleanupMailFiles();
      }else
      if(after_init_compose_mail_data){
         C_compose_mail_data *cdata = (C_compose_mail_data*)(C_unknown*)after_init_compose_mail_data;
         ComposeEmail(*cdata);
         after_init_compose_mail_data = NULL;
      }
      ConnectEnabledAccountsInBackground();
      ManageTimer();
      if(config_mail.tweaks.check_mail_on_startup && mode->Id()==C_mode_accounts::ID){
         SetModeUpdateMailboxes();
      }else
         UpdateScreen();
   }

#ifdef DEBUG_EDIT_ACCOUNT
   SetModeEditAccount(DEBUG_EDIT_ACCOUNT);
#elif defined OPEN_ACCOUNT
   {
      C_mode_accounts &mod_acc = (C_mode_accounts&)*mode;
      mod_acc.selection = OPEN_ACCOUNT;
      OpenMailbox(mod_acc, true);
      if(mode->Id()==C_mode_folders_list::ID){
         C_mode_folders_list &mod_fl = (C_mode_folders_list&)*mode;
#ifdef OPEN_FOLDER
         mod_fl.selection = Min(mod_fl.num_entries-1, OPEN_FOLDER);
#endif
         bool r;
         OpenImapFolder(mod_fl, r);
      }
#ifdef DEBUG_OPEN_MESSAGE

      C_mode_mailbox &mod = (C_mode_mailbox&)*mode;
      if(DEBUG_OPEN_MESSAGE < mod.GetContainer().messages.size()){
         mod.selection = DEBUG_OPEN_MESSAGE;
         OpenMessage(mod);
      }
#endif
#if defined DEBUG_WRITE_MAIL || defined DEBUG_REPLY || defined DEBUG_FOWRARD
      {
         C_mode_mailbox &mod = (C_mode_mailbox&)*mode;
         bool r = false, f = false;
         S_message *msg = NULL;
#ifdef DEBUG_REPLY
         r = true;
         msg = &acc.messages[DEBUG_REPLY];
#elif defined DEBUG_FOWRARD
         f = true;
         msg = &acc.messages[DEBUG_FOWRARD];
#endif
         SetModeWriteMail(&mod.GetContainer(), msg, r, f, false);
      }
#endif
   }
#elif defined DEBUG_MODE_FILE_BROWSER
   C_client_file_mgr::SetModeFileBrowser(this);
#elif defined DEBUG_MODE_ADDRESS_BOOK
   SetModeAddressBook();
#elif defined DEBUG_MODE_REGISTER
   SetModeRegister();
#elif defined DEBUG_MODE_EDIT_SIGNATURES
   SetModeEditSignatures();
#elif defined DEBUG_MODE_EDIT_IDENTITIES
   SetModeEditIdentities(accounts[0]);
#elif defined DEBUG_MODE_PROGRAM_UPDATE
   ProgramUpdateCreate(*this);
#elif defined DEBUG_MODE_FATAL
   SetModeFatal(123456);
#elif defined DEBUG_MODE_RULES_BROWSER
   SetModeRulesBrowser();
#elif defined DEBUG_MODE_RULE_EDITOR
   LoadRules();
   if(rules.Size())
      SetModeRuleEditor(rules.Front());
#elif defined DEBUG_OPEN_FILE
   C_client_viewer::OpenFileForViewing(this, DEBUG_OPEN_FILE, DEBUG_OPEN_FILE);
#else

#ifdef _DEBUG
   //SetModeDataCounters();
   //PerformAutoUpdate();
#endif
#endif
   LOG_RUN(" Constructed");
}

//----------------------------

bool C_mail_client::ConnectAccountInBackground(S_account &acc, bool init_other_accounts, bool allow_imap_idle){

                           //find inbox
   C_message_container *cnt = FindInbox(acc);
   if(!cnt)
      return false;
   acc.use_imap_idle = allow_imap_idle;

   if(mode && mode->Id()==C_mode_connection_auth::ID){
                              //already authenticating by normal connection, schedule to connect later
      C_mode_connection_auth &mod_con = (C_mode_connection_auth&)*mode;
      mod_con.params.imap_finish_init_idle_accounts = true;
      acc.background_processor.state = S_account::UPDATE_INIT;
      acc.background_processor.status_text = GetText(TXT_PROGRESS_INITIALIZING);
      return false;
   }

                              //if any other account is in auth phase, don't connect now and wait for auth to finish
   for(dword i=0; i<NumAccounts(); i++){
      const S_account &a = accounts[i];
      if(a.background_processor.auth_check){
         acc.background_processor.state = S_account::UPDATE_INIT;
         acc.background_processor.status_text = GetText(TXT_PROGRESS_INITIALIZING);
         return false;
      }
   }
   if(acc.socket && mode->Id()==C_mode_connection::ID){
   }else{
      acc.CloseConnection();
      S_connection_params prm;
      prm.auto_update = true;
      prm.imap_finish_init_idle_accounts = init_other_accounts;
      SetModeConnection(acc, cnt, C_mode_connection::ACT_IMAP_IDLE, &prm);
   }
#ifdef AUTO_CONNECTION_BACK_SWITCH
   alt_test_connection_counter = 0;
#endif
   return true;
}

//----------------------------

bool C_mail_client::ConnectEnabledAccountsInBackground(bool ask, bool manual, bool force_all, bool allow_imap_idle){

   bool any_init = false;
   if(!manual && !config_mail.IsWorkingTime()){
                              //do nothing outside of work hours
   }else
   if(manual || config_mail.imap_idle_startup_connect!=config_mail.IDLE_CONNECT_MANUAL){
      for(dword i=0; i<NumAccounts(); i++){
         S_account &acc = accounts[i];
         if((acc.IsImap() && (acc.flags&acc.ACC_USE_IMAP_IDLE)) || ((force_all && acc.flags&acc.ACC_INCLUDE_IN_UPDATE_ALL))){
            if(acc.background_processor.state<=acc.UPDATE_INIT || acc.background_processor.state==acc.UPDATE_ERROR){
               if(config_mail.imap_idle_startup_connect==config_mail.IDLE_CONNECT_ASK && ask
                  ){
                                 //ask to connect
                  class C_question: public C_question_callback{
                     C_mail_client &app;
                     virtual void QuestionConfirm(){
                        app.ConnectEnabledAccountsInBackground(false);
                     }
                  public:
                     C_question(C_mail_client &a): app(a){}
                  };
                  CreateQuestion(*this, TXT_CONNECT, GetText(TXT_Q_CONNECT_IMAP_MAILBOXES), new(true) C_question(*this), true);
                  return false;
               }
               if(ConnectAccountInBackground(acc, true, allow_imap_idle))
                  any_init = true;
            }
         }
      }
      if(any_init){
         ManageTimer();
         RedrawScreen();
         UpdateScreen();
      }
   }
   return any_init;
}

//----------------------------

void C_mail_client::C_home_screen_notify::Activate(){
   active = true;
   {
                              //add existing hdrs to list
      for(dword ai=0; ai<app.NumAccounts(); ai++){
         C_mail_client::S_account &acc = app.accounts[ai];
         C_message_container *cnt = app.FindInbox(acc);
         if(cnt){
            cnt->LoadMessages(app.mail_data_path);
            for(int mi=0; mi<cnt->messages.size(); mi++){
               const S_message_header &msg = cnt->messages[mi];
               if(!msg.IsRead() && !msg.IsHidden() && !msg.IsDeleted()){
                  if(msg.IsRecent() || debug_show_all){
                     S_msg m;
                     m.acc = &acc;
                     m.cnt = cnt;
                     m.hdr = msg;
                     new_hdrs.push_back(m);
                  }
               }
            }
         }
      }
      display_hdr_index = new_hdrs.size()-1;
      if(!mail_notify){
         mail_notify = C_notify_window::Create(app);
         mail_notify->Release();
      }
      mail_notify->display_count = new_hdrs.size();
      app.DrawUnreadMailNotify(mail_notify);
   }

#ifdef USE_NOKIA_N97_WIDGET
   if(app.hs_widget.IsActivated()){
      DrawHsWidgetHeader();
   }
#endif
}

//----------------------------

S_message *C_mail_client::C_home_screen_notify::FindMessage(const S_msg &msg) const{

   app.LoadMessages(*msg.cnt);
   C_vector<S_message> &msgs = msg.cnt->messages;
   for(int i=msgs.size(); i--; ){
      S_message &m = msgs[i];
      if(m==msg.hdr){
         return &m;
      }
   }
   return NULL;
}

//----------------------------

void C_mail_client::C_home_screen_notify::Close(){

   mail_notify = NULL;
   active = false;
   new_hdrs.clear();
}

//----------------------------
#ifdef USE_NOKIA_N97_WIDGET

void C_mail_client::C_home_screen_notify::HsWidgetClickField(int txt_id){

   switch(txt_id){
   case 0:
      if(new_hdrs.size()){
                              //switch to prev hdr
         if(!display_hdr_index)
            display_hdr_index = new_hdrs.size();
         --display_hdr_index;
         DrawHsWidgetHeader();
      }//else
         //app.MinMaxApplication(false);
      break;
   case 1:
   case 2:
      if(new_hdrs.size()){
         const S_msg &msg = new_hdrs[display_hdr_index];
                              //todo: close current mode
         if(app.mode->Id()==C_mode_accounts::ID){
            app.SetModeFoldersList(*msg.acc);
            C_mail_client::C_mode_mailbox &mod_mbox = app.SetModeMailbox(*msg.acc, msg.cnt);
                              //find message index
            const C_vector<S_message> &msgs = mod_mbox.GetMessages();
            for(int i=mod_mbox.num_vis_msgs; i--; ){
               const S_message &m = msgs[i];
               if(m==msg.hdr){
                  app.SetMailboxSelection(mod_mbox, i);
                  break;
               }
            }
         }
         app.MinMaxApplication(false);
      }
      break;
   }
}

//----------------------------

void C_mail_client::C_home_screen_notify::HsWidgetResume(){

   if(new_hdrs.size())
      DrawHsWidgetHeader();
}

//----------------------------

void C_mail_client::C_home_screen_notify::DrawHsWidgetHeader(){

   if(!new_hdrs.size()){
      int num_unr = app.CountUnreadVisibleMessages();
      Cstr_c s;
      if(num_unr){
         Cstr_w ws = app.GetText(TXT_UNREAD);
         s.Format("%: %") <<ws.ToUtf8() <<num_unr;
      }
      app.hs_widget.SetItem(C_hs_widget::ITEM_TEXT_1, Cstr_w(app.GetText(TXT_NO_RECENT_MESSAGES)).ToUtf8());
      app.hs_widget.SetItem(C_hs_widget::ITEM_TEXT_2, s);
      app.hs_widget.SetItem(C_hs_widget::ITEM_TEXT_3, "");
   }else{
      Cstr_c s;
      s<<Cstr_w(app.GetText(TXT_RECENT_MESSAGE)).ToUtf8();
      if(new_hdrs.size()>1)
         s.AppendFormat(" %/%") <<int(display_hdr_index+1) <<new_hdrs.size();
      s<<':';
      app.hs_widget.SetItem(C_hs_widget::ITEM_TEXT_1, s);

      const S_message_header &h = new_hdrs[display_hdr_index].hdr;
      Cstr_c n;
      n<<h.sender.display_name;
      if(h.sender.email.Length())
         n.AppendFormat(" <%>") <<h.sender.email;
      app.hs_widget.SetItem(C_hs_widget::ITEM_TEXT_2, n);
      
      app.hs_widget.SetItem(C_hs_widget::ITEM_TEXT_3, h.subject);
   }
   app.hs_widget.Publish();
}

#endif //USE_NOKIA_N97_WIDGET

//----------------------------

void C_mail_client::C_home_screen_notify::AddNewMailNotify(S_account &acc, C_message_container &cnt, const S_message_header &hdr, bool allow_simple_notify){

   if(!active)
      return;

   display_hdr_index = new_hdrs.size();
   S_msg &msg = new_hdrs.push_back(S_msg());
   msg.hdr = hdr;
   msg.acc = &acc;
   msg.cnt = &cnt;
#ifdef USE_NOKIA_N97_WIDGET
   if(app.hs_widget.IsActivated()){
                              //use widget to display recent message
      DrawHsWidgetHeader();
      return;
   }
#endif
   if(allow_simple_notify){
      if(!mail_notify){
         mail_notify = C_notify_window::Create(app);
         mail_notify->Release();
      }
      //mail_notify->display_count += 1;
      mail_notify->display_count = new_hdrs.size();
      app.DrawUnreadMailNotify(mail_notify);
   }
}

//----------------------------

void C_mail_client::C_home_screen_notify::RemoveMailNotify(S_account &acc, C_message_container &cnt, const S_message_header &hdr){

   if(!active)
      return;
   for(int i=new_hdrs.size(); i--; ){
      const S_msg &msg = new_hdrs[i];
      if(msg.acc==&acc && msg.cnt==&cnt && hdr==msg.hdr){
                              //found, remove it
         new_hdrs.remove_index(i);
         display_hdr_index = Min(display_hdr_index, new_hdrs.size()-1);
#ifdef USE_NOKIA_N97_WIDGET
         if(app.hs_widget.IsActivated())
            DrawHsWidgetHeader();
         else
#endif
         if(!new_hdrs.size()){
                              //remove notification
            mail_notify = NULL;
            app.led_flash_notify = NULL;
         }else{
            if(mail_notify){
               mail_notify->display_count = new_hdrs.size();
               app.DrawUnreadMailNotify(mail_notify);
            }
         }
         break;
      }
   }
}

//----------------------------

void C_mail_client::C_home_screen_notify::InitAfterScreenResize(){

   if(mail_notify && new_hdrs.size()){
      //AddNewMailNotify(0);    //simply add zero mails, it will reposition the window
      //AddNewMailNotify(new_hdrs.back(), true);    //simply add zero mails, it will reposition the window
      app.DrawUnreadMailNotify(mail_notify);
   }
}

//----------------------------

void C_mail_client::FocusChange(bool foreground){

   //LOG_RUN_N("FocusChange", foreground);
   C_client::FocusChange(foreground);
#ifndef _DEBUG_
   if(foreground){
      home_screen_notify.Close();
      led_flash_notify = NULL;
      bool redraw = false;
      if(config_mail.tweaks.focus_go_to_main_screen){
                              //to to accounts view
         if(mode->GetParent()){
            bool ok = true;
            while(ok && mode->GetParent()){
               switch(mode->Id()){
               case C_mode_mailbox::ID:
               case C_mode_folders_list::ID:
               case C_mode_read_mail_base::ID:
                  CloseMode(*mode, false);
                  redraw = true;
                  break;
               default:
                  ok = false;
               }
            }
         }
      }
#ifdef USE_SYSTEM_SKIN
      if(!config.color_theme){
         redraw = true;
      }
#endif
      if(config_mail.app_password.Length() && !FindMode(MODE_ID_PASS_ENTER) && config_mail.tweaks.pass_ask_timeout
#ifndef _DEBUG
         && GetTickTime()-last_focus_loss_time>60*1000*dword(config_mail.tweaks.pass_ask_timeout)
#endif
         ){
         AskPasswordWhenFocusGained(true);
         redraw = false;
      }
      if(redraw)
         RedrawScreen();
      UpdateScreen();
   }else{
      if(!exiting){
         home_screen_notify.Activate();
         StoreDataCounters();
      }
      /*
                              //reset address book
      if(mode)
      switch(mode->Id()){
      case C_mode_address_book::ID:
      case C_mode_connection::ID:
         break;
      default:
         if(address_book)
            address_book->items.Clear();
      }
      */
                              //reconnect all idle mailboxes to inbox
      {
         bool redraw = false;
         for(int i=NumAccounts(); i--; ){
            S_account &acc = accounts[i];
            if(acc.background_processor.IsIdling() && (!acc.selected_folder || !acc.selected_folder->IsInbox())){
               C_mode_connection_imap &mod_idle = *acc.background_processor.GetMode();
               mod_idle.folder = FindInbox(acc);
               acc.selected_folder = NULL;
               AfterImapLogin(mod_idle);
               redraw = true;
            }
         }
         if(redraw)
            RedrawScreen();
      }
      UpdateUnreadMessageNotify();
      last_focus_loss_time = GetTickTime();
   }
#endif
}

//----------------------------

#ifdef _DEBUG

void C_mail_client::OpenDocument(const wchar *fname){
   
                              //send as attachment
   C_compose_mail_data cdata;
   cdata.atts.push_back(fname);
   ComposeEmail(cdata);
}

#endif
//----------------------------

void C_mail_client::ComposeEmail(const C_compose_mail_data &cdata){

   if(FindMode(MODE_ID_PASS_ENTER)){
                              //we're sitting on password entry, just save data, they'll be processed later
      LOG_RUN("ComposeEmail: waiting for password");
      C_compose_mail_data *sd = new(true) C_compose_mail_data;
      *sd = cdata;
      after_init_compose_mail_data = sd;
      sd->Release();
      return;
   }
   LOG_RUN("ComposeEmail");
   LOG_RUN_N("Top mode ID", mode->Id());
   C_mode_write_mail_base *mod_wr;
   if(!SafeReturnToAccountsMode()){
      if(mode->Id()==C_mode_write_mail_base::ID){
         LOG_RUN(" - already writing, just attach");
         mod_wr = (C_mode_write_mail_base*)(C_mode*)mode;
      }else{
         LOG_RUN(" - can't return to accounts mode");
         return;
      }
   }else{
      if(!mode || !NumAccounts()){
         LOG_RUN("No accounts defined");
         return;
      }
      /*
      if(NumAccounts()!=1){
                                 //let user select account?
      }
      */
      C_mode_accounts &mod_acc = (C_mode_accounts&)*mode;
      LOG_RUN("OpenMailbox");
      OpenMailbox(mod_acc, true);
      mod_wr = &SetModeWriteMail(cdata.rcpt[0], cdata.rcpt[1], cdata.rcpt[2], cdata.subj, cdata.body);
   }
   for(int n=0; n<cdata.atts.size(); n++)
      mod_wr->AddAttachment(cdata.atts[n], true);
   RedrawScreen();
}

//----------------------------

C_mail_client::C_mail_client():
   C_client(config_mail),
   exiting(false),
   auto_update_counter(0),
   pass_enter_attempts_left(0),
   timer(NULL),
   work_time_alarm(NULL),
   curr_timer_freq(NULL),
   char_conv(NULL),
   home_screen_notify(*this)
#ifdef USE_NOKIA_N97_WIDGET
   , hs_widget(*this)
#endif
{
   temp_path = L"Email\\Temp\\";
}

//----------------------------

C_mail_client::~C_mail_client(){

   delete timer;
   delete work_time_alarm;
   home_screen_notify.Close();

   if(mode)
   switch(mode->Id()){
   case C_mode_write_mail_base::ID:
      ((C_mode_write_mail_base&)*mode).SaveMessage(true, false);
      break;
   }
                              //save now so that stats are saved
   if(IsMailClientInitialized()){
      SaveAccounts();
                              //save nonsaved containers now
      for(int i=accounts.Size(); i--; ){
         S_account &acc = accounts[i];
         C_folders_iterator it(acc._folders);
         while(!it.IsEnd())
            it.Next()->SaveMessages(mail_data_path);
      }
      if(temp_path){
#ifndef _DEBUG
                                 //check to see if it's time to clean-up files
         S_date_time td;
         td.GetCurrent();
         int d = td.GetSeconds() / (60*60*24);
         if(Abs(config_mail.last_msg_cleanup_day-d) >= 7){
            config_mail.last_msg_cleanup_day = d;
            SaveConfig();
            CleanupMailFiles();
         }
#else
         CleanupMailFiles();
#endif
         Cstr_w tmp = temp_path;
         tmp = tmp.Left(tmp.Length()-1);
         C_file::GetFullPath(tmp, tmp);
         C_dir::RemoveDirectory(tmp, true);
      }
   }
   icons_file = NULL;
   CloseCharConv();
   mode = NULL;
#ifndef _DEBUG
                              //delete log file
   C_file::DeleteFile(NETWORK_LOG_FILE);
#endif
}

//----------------------------

C_application_base *CreateApplication(){
   //Info("!");
   return new(true) C_mail_client;
}

//----------------------------

bool C_message_container::IsEmpty(const Cstr_w &mail_data_path) const{

   if(subfolders.Size())
      return false;
                              //first check stats
   const dword *st = GetMessagesStatistics();
   for(int i=STAT_LAST; i--; ){
      if(st[i])
         return false;
   }
                              //some messages may be hidden, so really check message count
   bool was_loaded = loaded;
   const_cast<C_message_container*>(this)->LoadMessages(mail_data_path);
   bool empty = (!messages.size());
   if(!was_loaded)
      const_cast<C_message_container*>(this)->SaveAndUnloadMessages(mail_data_path);
   return empty;
}

//----------------------------

bool C_message_container::IsInbox() const{
   return (!parent_folder && !text_utils::CompareStringsNoCase(folder_name, "inbox"));
}

//----------------------------
struct S_id{
   dword imap_uid;
   dword index;
   static int Comp(const void *i1, const void *i2, void*){
      dword id1 = ((S_id*)i1)->imap_uid;
      dword id2 = ((S_id*)i2)->imap_uid;
      return id1<id2 ? -1 : id1>id2 ? 1 : 0;
   }
};

//----------------------------

dword C_message_container::GetMaxUid() const{

   dword max_uid = 0;
   for(int i=messages.size(); i--; )
      max_uid = Max(max_uid, messages[i].imap_uid);
   return max_uid;
}

//----------------------------

dword C_message_container::GetTotalMessageSize(const S_message &msg, const Cstr_w &mail_data_path) const{

   dword sz = 0;
                              //determine message data size
   Cstr_w mail_path = GetMailPath(mail_data_path);
   Cstr_w body_fname;
   body_fname<<mail_path <<msg.body_filename.FromUtf8();
   C_file ck;
   if(ck.Open(body_fname))
      sz += ck.GetFileSize();
                              //add size of attachments
   for(int ai=msg.attachments.Size(); ai--; ){
      const S_attachment &att = msg.attachments[ai];
      if(ck.Open(att.filename.FromUtf8()))
         sz += ck.GetFileSize();
   }
   return sz;
}

//----------------------------

const wchar S_account_settings::inbox_folder_name[] = L"INBOX",
   S_account_settings::default_sent_folder_name[] = L"Sent",
   S_account_settings::default_trash_folder_name[] = L"Trash",
   S_account_settings::default_outbox_folder_name[] = L"Outbox",
   S_account_settings::default_draft_folder_name[] = L"Drafts";

//----------------------------

S_account_settings::S_account_settings():
   flags(ACC_INCLUDE_IN_UPDATE_ALL | ACC_USE_SMTP_AUTH | 
      ACC_IMAP_UPDATE_INBOX_ONLY |
      ACC_IMAP_UPLOAD_SENT
      | ACC_USE_IMAP4
      | ACC_USE_IMAP_IDLE),
   secure_in(SECURE_NO),
   secure_out(SECURE_NO),
   port_in(0),
   port_out(0),
   imap_draft_folder(default_draft_folder_name),
   imap_sent_folder(default_sent_folder_name),
   imap_trash_folder(default_trash_folder_name),
   max_kb_to_retrieve(0),
   imap_last_x_days_limit(365),
   imap_idle_ping_time(IMAP_IDLE_DEFAULT_PING_TIME),
   imap_folder_delimiter(0),
   save_sent_messages(true),
   move_to_trash(true)
{
}

//----------------------------

static Cstr_c EncryptString(const Cstr_c &s, const char *enc){

   Cstr_c ret = s;
   dword len = s.Length();
   const char *ep = enc;
   for(dword i=0; i<len; i++){
      ret.At(i) ^= *ep;
      if(!*++ep)
         ep = enc;
   }
   return text_utils::StringToBase64(ret);
}

//----------------------------

static Cstr_c DecryptString(const Cstr_c &s, const char *enc){

   Cstr_c ret = text_utils::Base64ToString(s);
   dword len = ret.Length();
   const char *ep = enc;
   for(dword i=0; i<len; i++){
      ret.At(i) ^= *ep;
      if(!*++ep)
         ep = enc;
   }
   return ret;
}

//----------------------------

static void WriteFolders(C_xml_build::C_element &el, const t_folders &folders, bool is_imap){

   int num_folders = folders.Size();
   el.ReserveChildren(num_folders);
   for(int j=0; j<num_folders; j++){
      const C_message_container &cnt = *folders[j];
      C_xml_build::C_element &elf = el.AddChild("Folder");
      elf.ReserveAttributes(6);
      elf.AddAttributeValueUtf8("name", cnt.folder_name);
      if(cnt.flags) elf.AddAttributeValue("flags", cnt.flags);
      elf.AddAttributeValue("id", cnt.msg_folder_id);
      if(cnt.last_msg_cleanup_day) elf.AddAttributeValue("cleanup", cnt.last_msg_cleanup_day);
      if(is_imap && cnt.imap_uid_validity)
         elf.AddAttributeValue("uidvalidity", cnt.imap_uid_validity);

      for(int si=cnt.STAT_LAST; si--; ){
         if(cnt.stats[si]){
            Cstr_c s;
            for(int i=0; i<cnt.STAT_LAST; i++){
               if(s.Length()) s<<',';
               dword n = cnt.stats[i];
               s<<n;
            }
            elf.AddAttributeValue("stats", s);
            break;
         }
      }
      WriteFolders(elf, cnt.subfolders, is_imap);
   }
}

//----------------------------

void C_mail_client::SaveAccounts() const{

   Cstr_w afn = GetAccountsFilename();
   Cstr_w tmp_fn = afn; tmp_fn<<L".$$$";
   C_file fl;
   if(fl.Open(tmp_fn, C_file::FILE_WRITE|C_file::FILE_WRITE_CREATE_PATH)){
      C_xml_build xml("Accounts");
      xml.root.AddAttributeValue("size", NumAccounts());
      xml.root.AddAttributeValue("version", ACCOUNTS_SAVE_VERSION);
      xml.root.ReserveChildren(NumAccounts());
      for(dword i=0; i<NumAccounts(); i++){
         const S_account &acc = accounts[i];
         C_xml_build::C_element &el = xml.root.AddChild("Account");
         el.AddAttributeValue("name", acc.name);
         el.AddAttributeValue("email", acc.primary_identity.email);
         el.AddAttributeValueUtf8("display_name", acc.primary_identity.display_name);
         el.AddAttributeValue("mail_server", acc.mail_server);
         el.AddAttributeValue("smtp_server", acc.smtp_server);
         el.AddAttributeValue("username", acc.username);
         el.AddAttributeValue("password", EncryptString(acc.password, pass_encryption));
         if(acc.smtp_username.Length()) el.AddAttributeValue("smtp_username", acc.smtp_username);
         if(acc.smtp_password.Length()) el.AddAttributeValue("smtp_password", EncryptString(acc.smtp_password, pass_encryption));
         if(acc.primary_identity.reply_to_email.Length()) el.AddAttributeValue("reply_to", acc.primary_identity.reply_to_email);
         if(acc.port_in) el.AddAttributeValue("port_in", acc.port_in);
         if(acc.port_out) el.AddAttributeValue("port_out", acc.port_out);
         if(acc.imap_draft_folder.Length()) el.AddAttributeValue("imap_draft", acc.imap_draft_folder);
         if(acc.imap_sent_folder.Length()) el.AddAttributeValue("imap_sent", acc.imap_sent_folder);
         if(acc.imap_trash_folder.Length()) el.AddAttributeValue("imap_trash", acc.imap_trash_folder);
         if(acc.imap_root_path.Length()) el.AddAttributeValue("imap_root", acc.imap_root_path);
         if(acc.send_msg_copy_to.Length()) el.AddAttributeValue("send_copy", acc.send_msg_copy_to);
         if(acc.max_kb_to_retrieve) el.AddAttributeValue("max_kb", acc.max_kb_to_retrieve);
         el.AddAttributeValue("max_days", acc.imap_last_x_days_limit);
         el.AddAttributeValue("flags", acc.flags);
         if(acc.secure_in)
            el.AddAttributeValue("ssl_in", acc.secure_in);
         if(acc.secure_out)
            el.AddAttributeValue("ssl_out", acc.secure_out);
         el.AddAttributeValue("save_sent", int(acc.save_sent_messages));
         el.AddAttributeValue("move_to_trash", int(acc.move_to_trash));
         if(acc.imap_folder_delimiter){
            Cstr_c s; s<<acc.imap_folder_delimiter;
            el.AddAttributeValue("folder_delimiter", s);
         }
         el.AddAttributeValue("idle_ping_time", acc.imap_idle_ping_time);
         if(acc.signature_name.Length()) el.AddAttributeValue("signature", acc.signature_name);

         WriteFolders(el, acc._folders, acc.IsImap());
                              //write identities
         for(dword j=0; j<acc.identities.Size(); j++){
            const S_identity &idn = acc.identities[j];
            C_xml_build::C_element &eli = el.AddChild("Identity");
            eli.AddAttributeValueUtf8("name", idn.display_name);
            eli.AddAttributeValueUtf8("email", idn.email);
            if(idn.reply_to_email.Length())
               eli.AddAttributeValueUtf8("reply_to", idn.reply_to_email);
         }
      }
      bool write_ok;
      if(config_mail.app_password.Length()){
         Cstr_c xml_text = xml.BuildText(false, false);
         C_tiny_encrypt te(config_mail.app_password.ToUtf8());
         Cstr_c enc = te.Encrypt(xml_text);
         C_xml_build xml_enc("Encrypted");
         xml_enc.root.SetContent(enc);
         write_ok = xml_enc.Write(fl, false, true);
      }else
         write_ok = xml.Write(fl, false, true);
      fl.Close();
      if(write_ok){
                              //rename temp file
         C_file::DeleteFile(afn);
         C_file::RenameFile(tmp_fn, afn);
      }else{
         LOG_RUN("Can't save Accounts, write failed");
         C_file::DeleteFile(tmp_fn);
      }
   }
}

//----------------------------

bool C_mail_client::IsImapIdleConnected() const{

   for(int i=NumAccounts(); i--; ){
      const S_account &acc = accounts[i];
      if(acc.use_imap_idle)
         return true;
   }
   return false;
}

//----------------------------

bool C_mail_client::IsBackgroundConnected() const{

   for(int i=NumAccounts(); i--; ){
      const S_account &acc = accounts[i];
      if(acc.background_processor.state!=S_account::UPDATE_DISCONNECTED)
         return true;
   }
   return false;
}

//----------------------------

Cstr_w C_mail_client::GetAccountsFilename() const{
   Cstr_w fn; fn<<mail_data_path <<DATA_PATH_PREFIX <<L"Accounts.xml";
   return fn;
}

//----------------------------

void C_mail_client::LoadAccountsXmlContent(S_account &acc, const C_xml &xml, const C_xml::C_element *el, t_folders &folders, bool is_imap,
   C_message_container *parent, int save_version){

   const C_vector<C_xml::C_element> &chlds = el->GetChildren();
   folders.Resize(chlds.size());
   C_vector<S_identity> tmp_identities;
   int num = 0;
   for(int i=0; i<chlds.size(); i++){
      const C_xml::C_element &c = chlds[i];
      if(c=="Folder"){
         C_message_container *cnt = new(true) C_message_container;
         cnt->parent_folder = parent;
         cnt->is_imap = is_imap;
         const C_vector<C_xml::C_element::S_attribute> &atts = c.GetAttributes();
         for(int j=0; j<atts.size(); j++){
            const C_xml::C_element::S_attribute &att = atts[j];
            if(att=="name") cnt->folder_name = xml.DecodeStringToUtf8(att.value);
            else if(att=="flags") cnt->flags = att.IntVal();
            else if(att=="id") cnt->msg_folder_id = att.IntVal();
            else if(att=="cleanup") cnt->last_msg_cleanup_day = att.IntVal();
            else if(att=="uidvalidity") cnt->imap_uid_validity = att.IntVal();
            else if(att=="stats"){
               const char *cp = att.value;
               for(int k=0; k<cnt->STAT_LAST; k++){
                  text_utils::ScanDecimalNumber(cp, (int&)cnt->stats[k]);
                  if(*cp==',')
                     ++cp;
               }
            }//else assert(0);
         }
         folders[num++] = cnt;

         if(save_version<316 && is_imap){
            acc.flags |= acc.ACC_NEED_FOLDER_REFRESH;
            if(acc.imap_folder_delimiter){
                              //convert folder hierarchy
               bool converted = false;
               t_folders *hr = &folders;
               Cstr_c n = cnt->folder_name;
               C_message_container *prnt = NULL;
               while(true){
                  int di = n.Find(acc.imap_folder_delimiter);
                  if(di==-1)
                     break;
                  if(!converted){
                     converted = true;
                     folders[--num] = NULL;
                  }
                  Cstr_c base = n.Left(di);
                  n = n.RightFromPos(di+1);
                  cnt->folder_name = n;
                              //find base on this hierarchy level
                  C_message_container *pfld = NULL;
                  for(int j=hr->Size(); j--; ){
                     C_message_container *f = (*hr)[j];
                     if(f && f->folder_name == base){
                        pfld = f;
                        break;
                     }
                  }
                  if(!pfld){
                              //parent level not existing, create now
                     pfld = new(true) C_message_container;
                     pfld->folder_name = base;
                     pfld->parent_folder = prnt;
                     pfld->flags = C_message_container::FLG_NOSELECT;
                     pfld->is_imap = true;
                     if(hr == &folders)
                        folders[num++] = pfld;
                     else{
                        hr->Resize(hr->Size()+1);
                        hr->Back() = pfld;
                     }
                     pfld->Release();
                  }
                  prnt = pfld;
                  hr = &pfld->subfolders;
               }
               if(prnt){
                              //put cnt to hierarchy
                  cnt->parent_folder = prnt;
                  hr->Resize(hr->Size()+1);
                  hr->Back() = cnt;
               }
            }
         }
         cnt->Release();
         if(c.GetChildren().size()){
            LoadAccountsXmlContent(acc, xml, &c, cnt->subfolders, is_imap, cnt, save_version);
         }
      }else if(c=="Identity"){
         S_identity idn;
         const C_vector<C_xml::C_element::S_attribute> &atts = c.GetAttributes();
         for(int j=0; j<atts.size(); j++){
            const C_xml::C_element::S_attribute &att = atts[j];
            if(att=="name") idn.display_name = xml.DecodeStringToUtf8(att.value);
            else if(att=="email") idn.email = xml.DecodeStringSingle(att.value);
            else if(att=="reply_to") idn.reply_to_email = xml.DecodeStringSingle(att.value);
            else assert(0);
         }
         tmp_identities.push_back(idn);
      }else assert(0);
   }
   folders.Resize(num);
   if(!parent){
                              //root level, assign identities
      acc.identities.Assign(tmp_identities.begin(), tmp_identities.end());
   }
}

//----------------------------

bool C_mail_client::LoadAccounts(const Cstr_w &password){

   C_file fl;
   if(fl.Open(GetAccountsFilename())){
      C_xml xml;
      if(xml.Parse(fl)){
         if(xml.GetRoot()=="Encrypted"){
            if(!password.Length())
               return false;
            C_tiny_encrypt te(password.ToUtf8());
            Cstr_c dec = te.Decrypt(xml.GetRoot().GetContent());
            if(!dec.Length())
               return false;
            xml.Reset();
            if(!xml.Parse(dec))
               return false;
                              //password accepted, save
            config_mail.app_password = password;
         }
         if(xml.GetRoot()=="Accounts"){
            accounts.Resize(0);
            const char *size = xml.GetRoot().FindAttributeValue("size");
            if(size){
               int n;
               if(Cstr_c(size)>>n && n>=0 && n<MAX_ACCOUNTS){
                  accounts.Resize(n);
               }
            }
            int version = 0;
            const char *v_str = xml.GetRoot().FindAttributeValue("version");
            if(v_str)
               text_utils::ScanInt(v_str, version);
            dword num_accounts = 0;
            for(const C_xml::C_element *el=xml.GetFirstChild(); el; el=el->GetNextSibling()){
               if(*el=="Account"){
                  ++num_accounts;
                  if(num_accounts>NumAccounts())
                     accounts.Resize(num_accounts);
                  S_account &acc = accounts[num_accounts-1];
                  const C_vector<C_xml::C_element::S_attribute> &atts = el->GetAttributes();
                  int reserve_folders = 32;
                  for(int i=0; i<atts.size(); i++){
                     const C_xml::C_element::S_attribute &att = atts[i];
                     if(att=="name") acc.name = xml.DecodeString(att.value);
                     else if(att=="email") acc.primary_identity.email = xml.DecodeStringSingle(att.value);
                     else if(att=="display_name") acc.primary_identity.display_name = xml.DecodeStringToUtf8(att.value);
                     else if(att=="mail_server") acc.mail_server = xml.DecodeStringSingle(att.value);
                     else if(att=="smtp_server") acc.smtp_server = xml.DecodeStringSingle(att.value);
                     else if(att=="username") acc.username = xml.DecodeStringSingle(att.value);
                     else if(att=="password") acc.password = DecryptString(att.value, pass_encryption);
                     else if(att=="smtp_username") acc.smtp_username = xml.DecodeStringSingle(att.value);
                     else if(att=="smtp_password") acc.smtp_password = DecryptString(att.value, pass_encryption);
                     else if(att=="reply_to") acc.primary_identity.reply_to_email = xml.DecodeStringSingle(att.value);
                     else if(att=="port_in") acc.port_in = (word)att.IntVal();
                     else if(att=="port_out") acc.port_out = (word)att.IntVal();
                     else if(att=="imap_draft") acc.imap_draft_folder = xml.DecodeString(att.value);
                     else if(att=="imap_sent") acc.imap_sent_folder = xml.DecodeString(att.value);
                     else if(att=="imap_trash") acc.imap_trash_folder = xml.DecodeString(att.value);
                     else if(att=="imap_root") acc.imap_root_path = xml.DecodeString(att.value);
                     else if(att=="send_copy") acc.send_msg_copy_to = xml.DecodeStringSingle(att.value);
                     else if(att=="signature") acc.signature_name = xml.DecodeStringSingle(att.value);
                     else if(att=="max_kb") acc.max_kb_to_retrieve = att.IntVal();
                     else if(att=="max_days") acc.imap_last_x_days_limit = (word)att.IntVal();
                     else if(att=="idle_ping_time") acc.imap_idle_ping_time = (word)att.IntVal();
                     else if(att=="flags"){
                        acc.flags = att.IntVal();
                        if(version<315){
                           //convert SSL flags
                           if(acc.flags&acc._ACC_USE_SSL_IN)
                              acc.secure_in = acc.SECURE_SSL;
                           if(acc.flags&acc._ACC_USE_SSL_OUT)
                              acc.secure_out = (acc.flags&acc._ACC_SMTP_USE_STARTTLS) ? acc.SECURE_STARTTLS : acc.SECURE_SSL;

                           acc.flags &= ~(acc._ACC_USE_SSL_IN|acc._ACC_USE_SSL_OUT|acc._ACC_SMTP_USE_STARTTLS);
                        }
                     }else if(att=="ssl_in") acc.secure_in = (S_account_settings::E_SECURE_CONN)Min(att.IntVal(), S_account_settings::SECURE_LAST-1);
                     else if(att=="ssl_out") acc.secure_out = (S_account_settings::E_SECURE_CONN)Min(att.IntVal(), S_account_settings::SECURE_LAST-1);
                     else if(att=="save_sent") acc.save_sent_messages = (bool)att.IntVal();
                     else if(att=="move_to_trash") acc.move_to_trash = (bool)att.IntVal();
                     else if(att=="folder_delimiter") acc.imap_folder_delimiter = xml.DecodeStringSingle(att.value)[0];
                     else if(att=="num_folders") reserve_folders = att.IntVal();
                     else assert(0);
                  }
                  LoadAccountsXmlContent(acc, xml, el, acc._folders, acc.IsImap(), NULL, version);
               }else assert(0);
            }
            if(version<316){
               //assign missing folder id's
               for(dword i=0; i<NumAccounts(); i++){
                  S_account &acc = accounts[i];
                  C_folders_iterator it(acc._folders);
                  while(!it.IsEnd()){
                     C_message_container *fld = it.Next();
                     if(!fld->msg_folder_id){
                        fld->msg_folder_id = GetMsgContainerFolderId();
                     }
                  }
               }
            }
         }
      }
   }
   UpdateUnreadMessageNotify();
   return true;
}

//----------------------------

dword C_mail_client::CountUnreadVisibleMessages(){

   dword num = 0;
   for(int i=NumAccounts(); i--; ){
      S_account &acc = accounts[i];
      C_folders_iterator it(acc._folders, false);
      while(!it.IsEnd()){
         C_message_container &cnt = *it.Next();
                              //not hidden folders
         //if(!(cnt.flags&cnt.FLG_HIDDEN)){
            if(cnt.stats_dirty)
               cnt.BuildMessagesStatistics();
            num += cnt.stats[cnt.STAT_UNREAD];
         //}
      }
   }
   return num;
}

//----------------------------

void C_mail_client::UpdateUnreadMessageNotify(){

#if defined __SYMBIAN32__ || defined WINDOWS_MOBILE
   dword num = CountUnreadVisibleMessages();
#ifdef __SYMBIAN32__
#ifdef _DEBUG
   return;
#endif
   Cstr_w fn;
   fn<<DATA_PATH_PREFIX <<L"UnreadCount.bin";
#else
   const wchar fn[] =
      L":HKLM\\Software\\Lonely Cat Games\\ProfiMail\\UnreadCount";
#endif
   C_file fl;
   if(fl.Open(fn, fl.FILE_WRITE)){
      fl.WriteDword(num);
   }else
      assert(0);
#endif
}

//----------------------------

void C_mail_client::S_account::DeleteFolder(const Cstr_w &mail_data_path, C_message_container *cnt, bool delete_subfolders){

                              //delete all subfolders
   if(delete_subfolders){
      while(cnt->subfolders.Size())
         DeleteFolder(mail_data_path, cnt->subfolders.Back(), true);
   }
   //cnt->AddRef();
   t_folders &hr = cnt->parent_folder ? cnt->parent_folder->subfolders : _folders;
   int indx;
   for(indx=hr.Size(); indx--; ){
      if(cnt==(const C_message_container*)hr[indx])
         break;
   }
   assert(indx!=-1);
   if(indx==-1)
      return;
   cnt->DeleteContainerFiles(mail_data_path);
                              //shift
   dword i;
   for(i=indx+1; i<hr.Size(); i++)
      hr[i-1] = hr[i];
   hr.Resize(hr.Size()-1);
   /*
   if(delete_subfolders)
   if(!hr.Size() && cnt->parent_folder && (cnt->parent_folder->flags&C_message_container::FLG_NOSELECT)){
                              //remove also parent folder
      DeleteFolder(mail_data_path, cnt->parent_folder, false);
   }
   cnt->Release();
   */
}

//----------------------------

void C_mail_client::S_account::DeleteAllFolders(const Cstr_w &mail_data_path){

   C_folders_iterator it(_folders);
   while(!it.IsEnd()){
      C_message_container *fld = it.Next();
      fld->DeleteContainerFiles(mail_data_path);
   }
   _folders.Clear();
}

//----------------------------
/*

C_mail_client::C_mode_connection_imap *C_mail_client::S_account::C_imap_idle_processor::GetMode(){

   return (C_mode_connection_imap*)(C_mode*)mode;
}

*/
//----------------------------

void C_mail_client::S_account::GetMessagesStatistics(dword stats[C_message_container::STAT_LAST]) const{

   MemSet(stats, 0, C_message_container::STAT_LAST*4);
   C_folders_iterator it(const_cast<t_folders&>(_folders), false);
   while(!it.IsEnd()){
      const C_message_container &fld = *it.Next();
      const dword *fst = fld.GetMessagesStatistics();
      for(int i=C_message_container::STAT_LAST; i--; )
         stats[i] += fst[i];
   }
}

//----------------------------

Cstr_w C_mail_client::S_account::GetFolderNameWithRoot(const Cstr_w &n) const{

   Cstr_w s = n;
   if(IsImap()){
                              //fix Inbox problems, make it upper-case
      if(s.Length()>6 && s[5]=='/' &&
         ToLower(s[0])=='i' &&
         ToLower(s[1])=='n' &&
         ToLower(s[2])=='b' &&
         ToLower(s[3])=='o' &&
         ToLower(s[4])=='x'){
         s.At(0) = 'I';
         s.At(1) = 'N';
         s.At(2) = 'B';
         s.At(3) = 'O';
         s.At(4) = 'X';
      }

      if(imap_folder_delimiter){
         if(imap_root_path.Length()){
                              //prepend root folder name
            if(s.Length() < imap_root_path.Length() || s.Left(imap_root_path.Length())!=imap_root_path){
               s = imap_root_path;
               s<<imap_folder_delimiter <<n;
            }else{
            }
         }
         if(imap_folder_delimiter!='/'){
                              //replace imap folder delimiter, if '/' was entered by user, replace by real delimiter
            for(int i=s.Length(); i--; ){
               wchar &c = s.At(i);
               if(c=='/')
                  c = imap_folder_delimiter;
            }
         }
      }
   }
   return s;
}

//----------------------------

Cstr_w C_mail_client::S_account::GetDraftFolderName() const{
   if(imap_draft_folder.Length())
      return GetFolderNameWithRoot(imap_draft_folder);
   return GetFolderNameWithRoot(default_draft_folder_name);
}

//----------------------------

Cstr_w C_mail_client::S_account::GetSentFolderName() const{
   if(imap_sent_folder.Length())
      return GetFolderNameWithRoot(imap_sent_folder);
   return GetFolderNameWithRoot(default_sent_folder_name);
}

//----------------------------

Cstr_w C_mail_client::S_account::GetFullFolderName(const C_message_container &cnt) const{

   Cstr_w ret = cnt.folder_name.FromUtf8();
   for(const C_message_container*pp = cnt.parent_folder; pp; pp=pp->parent_folder){
      Cstr_w tmp;
      tmp<<pp->folder_name.FromUtf8() <<imap_folder_delimiter <<ret;
      ret = tmp;
   }
   return ret;
}

//----------------------------

dword C_mail_client::S_account::NumFolders() const{
   dword n = 0;
   C_folders_iterator it(const_cast<t_folders&>(_folders));
   while(!it.IsEnd()){
      ++n;
      it.Next();
   }
   return n;
}

//----------------------------
//----------------------------

void C_mail_client::DeleteMessage(C_message_container &cnt, int indx, bool also_files){

   if(also_files)
      cnt.DeleteMessageFiles(mail_data_path, cnt.messages[indx]);
   cnt.messages.remove_index(indx);
}

//----------------------------

void C_mail_client::SetModeAccounts(int sel){

   C_mode_accounts &mod = *new(true) C_mode_accounts(*this);

   mod.InitLayoutAccounts1(sel);
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::C_mode_accounts::InitLayoutAccounts1(int sel){

   if(sel==-1)
      sel = selection;

   const int border = 2;
   const int top = app.GetTitleBarHeight();
   rc = S_rect(border, top, app.ScrnSX()-border*2, app.ScrnSY()-top-app.GetSoftButtonBarHeight()-border);
   int num_accounts = app.NumAccounts();
                           //compute # of visible lines, and resize rectangle to whole lines
   entry_height = app.fdb.line_spacing*2;
   sb.visible_space = (rc.sy / entry_height) * entry_height;
   rc.y += (rc.sy - sb.visible_space)/2;
   rc.sy = sb.visible_space;
   selection = Min(sel, (int)num_accounts-1);
   if(num_accounts && selection==-1)
      ++selection;

                           //init scrollbar
   const int width = app.GetScrollbarWidth();
   sb.rc = S_rect(rc.Right()-width-1, rc.y+1, width, rc.sy-2);
   sb.total_space = num_accounts*entry_height;
   sb.SetVisibleFlag();

   EnsureVisible();
}

//----------------------------

void C_mail_client::DeleteAccount(C_mode_accounts &mod){

   CloseAccountsConnections();
   if(NumAccounts()){
      S_account &acc = accounts[mod.selection];
                              //delete all account's files or folders
      acc.DeleteAllFolders(mail_data_path);

      for(dword i=mod.selection; i<NumAccounts()-1; i++)
         accounts[i] = accounts[i+1];
      accounts.Resize(NumAccounts()-1);
      SaveAccounts();
      SetModeAccounts(mod.selection);
   }
}

//----------------------------

void C_mail_client::CreateNewAccountFromEmail(const Cstr_w &email_address){

   Cstr_c email; email.Copy(email_address);
   int ai = email.Find('@');
   if(!email.Length() || ai<=0){
      CreateNewAccount(email_address);
      return;
   }

   CloseAccountsConnections();
   dword new_i = NumAccounts();
   accounts.Resize(new_i+1);
   SaveAccounts();
   S_account &acc = accounts[new_i];
   acc.primary_identity.email = email;
   acc.username = email;
   acc.name.Copy(email.Left(ai));

                              //find among templates
   Cstr_c domain = email.RightFromPos(ai+1);
   domain.ToLower();
   const C_smart_ptr<C_zip_package> dta = CreateDtaFile();
   C_file_zip fl;
   if(fl.Open(L"email_providers.csv", dta)){
      C_buffer<char> buf;
      buf.Resize(256);
      char *line = buf.Begin();
      fl.GetLine(line, buf.Size());
      C_vector<Cstr_c> columns;
      {
         const char *cp = line;
         while(*cp){
            Cstr_c s;
            text_utils::ReadToken(cp, s, ",");
            if(*cp==',') ++cp;
            columns.push_back(s);
         }
      }
      while(!fl.IsEof()){
         fl.GetLine(line, buf.Size());
         const char *cp = line;
         Cstr_c s;
         text_utils::ReadToken(cp, s, ",");
         if(domain!=s)
            continue;

         acc.flags &= ~(acc.ACC_USE_IMAP4);
         acc.secure_in = S_account_settings::SECURE_NO;
         acc.secure_out = S_account_settings::SECURE_NO;
         for(int i=1; i<columns.size(); i++){
            if(*cp==',') ++cp;
            if(text_utils::ReadToken(cp, s, ",") && s.Length()){
               const Cstr_c &c = columns[i];
               if(c=="imap"){
                  if(s=="1") acc.flags |= acc.ACC_USE_IMAP4;
               }else if(c=="in"){
                  if(s=="*"){
                     s.Clear();
                     s<<((acc.flags&acc.ACC_USE_IMAP4) ? "imap." : "pop.") <<domain;
                  }
                  acc.mail_server = s;
               }else if(c=="in_port"){
                  int port;
                  if(s>>port)
                     acc.port_in = word(port);
               }else if(c=="in_ssl"){
                  if(s=="1") acc.secure_in = acc.SECURE_SSL;
               }else if(c=="out"){
                  if(s=="*"){
                     s.Clear();
                     s<<"smtp." <<domain;
                  }
                  acc.smtp_server = s;
               }else if(c=="out_port"){
                  int port;
                  if(s>>port)
                     acc.port_out = word(port);
               }else if(c=="out_ssl"){
                  if(s=="1") acc.secure_out = acc.SECURE_SSL;;
               }else if(c=="starttls"){
                  if(s=="1") acc.secure_out = acc.SECURE_STARTTLS;
               }else if(c=="sent"){ acc.imap_sent_folder.Copy(s);
               }else if(c=="drafts"){ acc.imap_draft_folder.Copy(s);
               }else if(c=="trash"){
                  if(s.Length()){
                     if(s=="0")
                        acc.imap_trash_folder.Clear();
                     else
                        acc.imap_trash_folder.Copy(s);
                  }
               }else if(c=="crop_username"){
                  if(s=="1")
                     acc.username = email.Left(ai);
               }else if(c=="save_sent"){
                  int ii;
                  if(text_utils::ScanInt(s, ii)){
                     acc.save_sent_messages = bool(ii);
                     if(!acc.save_sent_messages && acc.IsImap())
                        acc.flags &= ~acc.ACC_IMAP_UPLOAD_SENT;
                  }
               }else if(c=="use_trash"){
                  int ii;
                  if(text_utils::ScanInt(s, ii))
                     acc.move_to_trash = bool(ii);
               }
            }
         }
         break;
      }
   }
   if(!acc.mail_server.Length())
      acc.mail_server = domain;
   if(!acc.smtp_server.Length())
      acc.smtp_server = domain;

   if(mode->Id()==C_mode_accounts::ID)
      ((C_mode_accounts&)*mode).InitLayout();
                     //go to edit mode
   SetModeEditAccount(new_i, false, true);
}

//----------------------------

void C_mail_client::CreateNewAccount(const wchar *init_text){

   class C_text_entry: public C_text_entry_callback{
      C_mail_client &app;
      virtual void TextEntered(const Cstr_w &txt){
         app.CreateNewAccountFromEmail(txt);
      }
   public:
      C_text_entry(C_mail_client &a): app(a){}
   };
   CreateTextEntryMode(*this, TXT_ACC_EMAIL, new(true) C_text_entry(*this), true, 100, init_text, TXTED_ANSI_ONLY | TXTED_EMAIL_ADDRESS);
}

//----------------------------

void C_mail_client::CloseAccountsConnections(bool disable_idle){

   for(int i=accounts.Size(); i--; ){
      S_account &acc = accounts[i];
      if(disable_idle)
         acc.CloseIdleConnection();
      else
         acc.CloseConnection();
   }
   ManageTimer();
}

//----------------------------

void C_mail_client::OpenMailbox(C_mode_accounts &mod, bool force_folder_mode){

   S_account &acc = accounts[mod.selection];
   {
                              //if background connection has error, display the error now
      S_account::E_UPDATE_STATE st = acc.background_processor.state;
      switch(st){
      case S_account::UPDATE_ERROR:
      case S_account::UPDATE_FATAL_ERROR:
         ShowErrorWindow(TXT_ERROR, acc.background_processor.status_text);
         acc.background_processor.Close();
         if(st==S_account::UPDATE_ERROR){
            acc.CloseConnection();
                              //reconnect IDLE
            if(acc.use_imap_idle && acc.IsImap() && (acc.flags&acc.ACC_USE_IMAP_IDLE))
               ConnectAccountInBackground(acc);
         }
         return;
      }
   }

   SetModeFoldersList(acc);

   bool open_inbox = false;
   if(!force_folder_mode)
      open_inbox = config_mail.tweaks.imap_go_to_inbox;
   if(!acc.IsImap()){
                              //POP3 go directly to inbox if it is only one folder
      if(!force_folder_mode && acc.NumFolders()==1)
         open_inbox = true;
   }
   if(open_inbox && mode->Id()==C_mode_folders_list::ID)
      SetModeMailbox(acc, FindInbox(acc));
}

//----------------------------

bool C_mail_client::SettingsExportCallback(const Cstr_w *file, const C_vector<Cstr_w> *files){

   Cstr_w fn = *file;
   Cstr_w ext = text_utils::GetExtension(fn);
   ext.ToLower();
   if(ext!=L"profimail")
      fn<<L".profimail";
   if(ExportSettings(fn)){
      CreateInfoMessage(*this, TXT_EXPORT_SETTINGS, GetText(TXT_EXPORT_SETTINGS_OK));
      return true;
   }
   Cstr_w s; s.Format(L"Failed to export settings to file ") <<fn;
   ShowErrorWindow(TXT_ERROR, s);
   return false;
}

//----------------------------

bool C_mail_client::SettingsImportCallback(const Cstr_w *file, const C_vector<Cstr_w> *files){

   C_client_file_mgr::FileBrowser_Close(this, (C_client_file_mgr::C_mode_file_browser&)*mode);
   if(ImportSettings(*file)){
      CreateInfoMessage(*this, TXT_EXPORT_SETTINGS, GetText(TXT_IMPORT_SETTINGS_OK));
      return true;
   }
   Cstr_w s = GetText(TXT_ERR_IMPORT_SETTINGS);
   ShowErrorWindow(TXT_ERROR, s);
   return false;
}

//----------------------------

void C_mail_client::SettingsImportConfirm(){

   C_client_file_mgr::C_mode_file_browser &mod = C_client_file_mgr::SetModeFileBrowser(this, C_client_file_mgr::C_mode_file_browser::MODE_EXPLORER, true,
      (C_client_file_mgr::C_mode_file_browser::t_OpenCallback)&C_mail_client::SettingsImportCallback,
      TXT_IMPORT_SETTINGS, NULL, C_client_file_mgr::GETDIR_DIRECTORIES|C_client_file_mgr::GETDIR_FILES, "profimail\0");
   mod.flags = mod.FLG_ACCEPT_FILE | mod.FLG_AUTO_COLLAPSE_DIRS | mod.FLG_SAVE_LAST_PATH | mod.FLG_SELECT_OPTION;
}

//----------------------------

bool C_mail_client::ProcessKeyCode(dword code){

   switch(code){
   case 123:
   case 124:
      socket_log = socket_log ? SOCKET_LOG_NO : (code==123 ? SOCKET_LOG_YES : SOCKET_LOG_YES_NOSHOW);
      if(socket_log)
         PlayNewMailSound();
      CloseAccountsConnections(true);
      break;
   case 263:                  //CMF
      CleanupMailFiles();
      break;
   case 700:
      {
                                 //simulate new mail icon on idle screen
         MinMaxApplication(true);
         PlayNewMailSound();
#ifdef USE_SYSTEM_VIBRATE
      if(C_phone_profile::IsVibrationEnabled())
#else
      if(config.flags&config_mail.CONF_VIBRATE_ALERT)
#endif
            MakeVibration();
         C_application_ui::FocusChange(false);
         home_screen_notify.Activate();
         S_message_header hdr;
         home_screen_notify.AddNewMailNotify(accounts[0], *FindInbox(accounts[0]), hdr, true);
         FlashNewMailLed();
         UpdateUnreadMessageNotify();
      }
      break;
   case 701:
      home_screen_notify.debug_show_all = true;
      MinMaxApplication(true);
      break;
   case 255:                  //wipe selected account
   case 256:                  //wipe all emails
      {
         C_mode_accounts *ma = (C_mode_accounts*)FindMode(C_mode_accounts::ID);
         for(int i=NumAccounts(); i--; ){
            if(code==255 && i!=ma->selection)
               continue;
            S_account &acc = accounts[i];
            C_folders_iterator it(acc._folders);
            while(!it.IsEnd()){
               C_message_container &cnt = *it.Next();
               cnt.DeleteContainerFiles(mail_data_path);
               cnt.ResetStats();
               cnt.msg_folder_id = GetMsgContainerFolderId();
            }
         }
         SaveAccounts();
      }
      break;
   case 727:
      config_mail.sort_mode = S_config_mail::SORT_BY_RECEIVE_ORDER;
      SaveConfig();
      MarkToSortAllAccounts();
      break;
   default:
      return C_client::ProcessKeyCode(code);
   }
   return true;
}

//----------------------------

void C_mail_client::C_mode_accounts::ProcessMenu(int itm, dword menu_id){

   switch(itm){
   case TXT_NEW:
      app.CreateNewAccount(NULL);
      break;

   case TXT_ACCOUNT:
      {
         menu = CreateMenu();
         bool can_edit_delete = (app.NumAccounts()!=0);
         menu->AddItem(TXT_EDIT, (!can_edit_delete ? C_menu::DISABLED : 0), "[6]", "[E]");
         menu->AddItem(TXT_NEW, (app.NumAccounts()==MAX_ACCOUNTS ? C_menu::DISABLED : 0));
         menu->AddItem(TXT_DELETE, (!can_edit_delete ? C_menu::DISABLED : 0), NULL, NULL, BUT_DELETE);
         if(can_edit_delete){
            const S_account &acc = app.accounts[selection];
            if(app.config_mail.tweaks.imap_go_to_inbox)
               menu->AddItem(TXT_FOLDERS, 0, "[7]", "[O]");
            if(acc.IsImap()){
               menu->AddSeparator();
               bool en = ((acc.flags&acc.ACC_USE_IMAP_IDLE) || acc.use_imap_idle);
               menu->AddItem(TXT_ACC_USE_PUSH_MAIL, en ? C_menu::MARKED : 0);
               bool con = (en && acc.use_imap_idle);
               menu->AddItem(TXT_CONNECT, (en && !acc.use_imap_idle) ? 0 : C_menu::DISABLED, !con ? "[5]" : NULL, !con ? "[I]" : NULL);
               menu->AddItem(TXT_DISCONNECT, (en && acc.use_imap_idle) ? 0 : C_menu::DISABLED, con ? "[5]" : NULL, con ? "[I]" : NULL);
            }
         }
         if(app.NumAccounts()>1){
            menu->AddSeparator();
            menu->AddItem(TXT_MOVE, C_menu::HAS_SUBMENU);
         }
         app.PrepareMenu(menu);
      }
      break;

   case TXT_FOLDERS:
      app.OpenMailbox(*this, true);
      break;

   case TXT_MOVE:
      menu = CreateMenu();
      menu->AddItem(TXT_MOVE_UP, selection ? 0 : C_menu::DISABLED);
      menu->AddItem(TXT_MOVE_DOWN, selection<(int)app.NumAccounts()-1 ? 0 : C_menu::DISABLED);
      app.PrepareMenu(menu);
      break;

   case TXT_MOVE_UP:
   case TXT_MOVE_DOWN:
      {
         S_account &acc = app.accounts[selection];
         if(itm==TXT_MOVE_UP){
            assert(selection);
            Swap(acc, app.accounts[selection-1]);
            --selection;
         }else{
            assert(selection < int(app.NumAccounts())-1);
            Swap(acc, app.accounts[selection+1]);
            ++selection;
         }
         app.SaveAccounts();
      }
      break;

   case TXT_ACC_USE_PUSH_MAIL:
      {
         S_account &acc = app.accounts[selection];
         if(acc.IsImap()){
            acc.flags ^= acc.ACC_USE_IMAP_IDLE;
            acc.CloseIdleConnection();
            app.SaveAccounts();
            acc.CloseConnection();
            app.ManageTimer();
         }
      }
      break;

   case TXT_CONNECT:
      {
         S_account &acc = app.accounts[selection];
         if(acc.IsImap())
            app.ConnectAccountInBackground(acc);
      }
      break;

   case TXT_DISCONNECT:
      {
         S_account &acc = app.accounts[selection];
         if(acc.IsImap()){
            acc.CloseIdleConnection();
            app.ManageTimer();
         }
      }
      break;

   case TXT_DELETE:
      {
         class C_question: public C_question_callback{
            C_mail_client &app;
            C_mode_accounts &mod;
            virtual void QuestionConfirm(){
               app.DeleteAccount(mod);
            }
         public:
            C_question(C_mail_client &a, C_mode_accounts &m): app(a), mod(m){}
         };
         CreateQuestion(app, TXT_Q_DELETE_ACCOUNT, app.GetText(TXT_Q_ARE_YOU_SURE), new(true) C_question(app, *this), true);
      }
      break;

   case TXT_OPEN:
      app.OpenMailbox(*this, false);
      break;

   case TXT_EDIT:
      app.SetModeEditAccount(selection, false, false);
      break;

   case TXT_UPDATE_MAILBOXES:
      app.SetModeUpdateMailboxes();
      break;
   case TXT_ABOUT:
      CreateModeAbout(app, TXT_ABOUT, app_name, L"logo.jpg", VERSION_HI, VERSION_LO, VERSION_BUILD, &app);
      break;

   case TXT_IMAP_IDLE:
      {
         menu = CreateMenu();

         bool connected_all = true, disconnected_all = true;
         for(int i=app.NumAccounts(); i--; ){
            const S_account &acc = app.accounts[i];
            if(acc.IsImap() && (acc.flags&acc.ACC_USE_IMAP_IDLE)){
               if(acc.use_imap_idle)
                  disconnected_all = false;
               else
                  connected_all = false;
            }
         }
         menu->AddItem(TXT_CONNECT_ALL, connected_all ? C_menu::MARKED : 0, "[8]", "[L]");
         menu->AddItem(TXT_DISCONNECT_ALL, disconnected_all ? C_menu::MARKED : 0, "[9]", "[D]");
         app.PrepareMenu(menu);
      }
      break;

   case TXT_CONNECT_ALL:
      app.ConnectEnabledAccountsInBackground(false, true);
      break;

   case TXT_DISCONNECT_ALL:
      app.CloseAccountsConnections(true);
      break;

   case TXT_TOOLS:
      menu = app.CreateMenu(*this, 0, false);
      //if(!menu->touch_mode){
         menu->AddItem(TXT_CONFIGURATION, 0, "[0]", "[C]");
         menu->AddItem(TXT_DATA_COUNTERS);
         menu->AddItem(TXT_ADDRESS_BOOK, 0, "[2]", "[B]", BUT_ADDRESS_BOOK);
         menu->AddItem(TXT_RULES, 0, "[3]", "[R]");
         menu->AddItem(TXT_SIGNATURES);
         menu->AddItem(TXT_FILE_BROWSER, 0, "[4]", "[F]", BUT_FILE_EXPLORER);
         menu->AddItem(TXT_EXPORT_SETTINGS);
         menu->AddItem(TXT_IMPORT_SETTINGS);
         menu->AddItem(TXT_DIAGNOSTIC, C_menu::HAS_SUBMENU);
         /*
      }else{
         menu->AddItem(TXT_CONFIGURATION);
         menu->AddItem(TXT_ADDRESS_BOOK, 0, NULL, NULL, BUT_ADDRESS_BOOK);
         menu->AddItem(TXT_RULES);
         menu->AddItem(TXT_SIGNATURES);
         menu->AddItem(TXT_FILE_BROWSER, 0, NULL, NULL, BUT_FILE_EXPLORER);
      }
      */
      app.PrepareMenu(menu);
      break;

   case TXT_DIAGNOSTIC:
      {
         menu = CreateMenu();

         bool has_log = C_file::Exists(NETWORK_LOG_FILE);
         menu->AddItem(TXT_ENABLE_NET_LOG, app.socket_log ? C_menu::MARKED : 0);
         menu->AddItem(TXT_VIEW_NET_LOG, !has_log ? C_menu::DISABLED : 0);
         //menu->AddItem(TXT_SEND_NET_LOG, !has_log ? C_menu::DISABLED : 0);
         app.PrepareMenu(menu);
      }
      break;

   case TXT_ENABLE_NET_LOG:
      app.socket_log = app.socket_log ? app.SOCKET_LOG_NO : app.SOCKET_LOG_YES_NOSHOW;
      app.CloseAccountsConnections(true);
      break;

   case TXT_VIEW_NET_LOG:
      C_client_viewer::OpenFileForViewing(&app, NETWORK_LOG_FILE, L"Network log:");
      break;

      /*
   case TXT_SEND_NET_LOG:
      {
         C_mail_client::C_compose_mail_data cm;
         cm.subj = L"Network log file - ";
         S_date_time dt; dt.GetCurrent();
         Cstr_w ds;
         app.GetDateString(dt, ds, false, true);
         cm.subj<<ds 
            ;
         cm.body = L"Problem description:\n";
         cm.rcpt[0] = "logs@lonelycatgames.com";
         cm.atts.push_back(NETWORK_LOG_FILE);
         app.ComposeEmail(cm);
                              //and disable further logging
         app.socket_log = app.SOCKET_LOG_NO;
         app.CloseAccountsConnections(true);
      }
      break;
      */

   case TXT_EXPORT_SETTINGS:
      C_client_file_mgr::SetModeFileBrowser_GetSaveFileName(&app, L"Settings.profimail",
         (C_client_file_mgr::C_mode_file_browser::t_OpenCallback)&C_mail_client::SettingsExportCallback, TXT_EXPORT_SETTINGS);
      return;

   case TXT_IMPORT_SETTINGS:
      {
         class C_question: public C_question_callback{
            C_mail_client &app;
            virtual void QuestionConfirm(){
               app.SettingsImportConfirm();
            }
         public:
            C_question(C_mail_client &a): app(a){}
         };
         CreateQuestion(app, TXT_IMPORT_SETTINGS, app.GetText(TXT_IMPORT_SETTINGS_HLP), new(true) C_question(app), true);
      }
      return;

   case TXT_CONFIGURATION:
      app.SetConfig();
      return;
   case TXT_DATA_COUNTERS: app.SetModeDataCounters(); return;
   case TXT_FILE_BROWSER: C_client_file_mgr::SetModeFileBrowser(&app); return;
   case TXT_ADDRESS_BOOK: app.SetModeAddressBook(); return;
   case TXT_RULES: app.SetModeRulesBrowser(); return;
   case TXT_SIGNATURES: app.SetModeEditSignatures(); return;
   case TXT_USER_MANUAL:
      {
         const char *url = "http://www.lonelycatgames.com/?app=profimail&page=symbian/manual";
         system::OpenWebBrowser(app, url);
      }
      break;

   case TXT_EXIT:
      //app.config_mail.auto_check_time = 0;
      app.exiting = true;
      app.CloseAccountsConnections(true);
      app.Exit();
      break;
   }
}

//----------------------------

void C_mail_client::C_mode_accounts::ProcessInput(S_user_input &ui, bool &redraw){

   ProcessInputInList(ui, redraw);
#ifdef USE_MOUSE
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){

      int but = app.TickBottomButtons(ui, redraw);
      if(but!=-1){
         switch(but){
         case 2: C_client_file_mgr::SetModeFileBrowser(&app); break;
         case 3: app.SetModeAddressBook(); break;
         case 1:
            if(!app.ConnectEnabledAccountsInBackground(false, true))
               app.CloseAccountsConnections(true);
            break;
         case 0:
#if 1
            app.SetModeUpdateMailboxes(); break;
#else
            {
               mod.menu = CreateTouchMenu();
               mod.menu->AddItem(TXT_IMAP_IDLE, C_menu::HAS_SUBMENU);
               mod.menu->AddItem(TXT_TOOLS, C_menu::HAS_SUBMENU);
               mod.menu->AddItem(TXT_EXIT);
               mod.menu->AddSeparator();
               mod.menu->AddItem(TXT_UPDATE_MAILBOXES, 0, NULL, NULL, BUT_UPDATE_MAILBOX);
               PrepareTouchMenu(mod.menu, ScrnSX()/2, ScrnSY(), false);
            }
#endif
            break;
         }
         return;
      }
      if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
         if(ui.CheckMouseInRect(rc)){
            if(app.NumAccounts() && selection>=0 && selection < (int)app.NumAccounts()){
               menu = app.CreateTouchMenu();
               menu->AddItem(TXT_EDIT);
               //menu->AddItem(TXT_DELETE, 0, 0, 0, BUT_DELETE);
               if(app.config_mail.tweaks.imap_go_to_inbox)
                  menu->AddItem(TXT_FOLDERS);
               else
                  menu->AddSeparator();
               const S_account &acc = app.accounts[selection];
               if(acc.IsImap()){
                  menu->AddSeparator();
                  bool en = (acc.flags&acc.ACC_USE_IMAP_IDLE);
                  menu->AddItem(TXT_ACC_USE_PUSH_MAIL, en ? C_menu::MARKED : 0);
                  menu->AddItem(!acc.use_imap_idle ? TXT_CONNECT : TXT_DISCONNECT, 0, NULL, NULL, char(!acc.use_imap_idle ? BUT_CONNECT_ALL : BUT_DISCONNECT_ALL));
               }
               app.PrepareTouchMenu(menu, ui);
            }
         }
      }
   }
#endif
   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
#if !defined _DEBUG || defined _WIN32_WCE
      if(app.config_mail.work_times_set || app.config_mail.auto_check_time || app.IsBackgroundConnected()){
         app.MinMaxApplication(true);
         break;
      }
#endif
      if(app.config_mail.tweaks.ask_to_exit){
         class C_question: public C_question_callback{
            C_mail_client &app;
            virtual void QuestionConfirm(){
               app.Exit();
            }
         public:
            C_question(C_mail_client &_a): app(_a){}
         };
         CreateQuestion(app, TXT_EXIT, app.GetText(TXT_Q_ARE_YOU_SURE), new(true) C_question(app), true);
      }else
      {
         app.exiting = true;
         app.Exit();
         //redraw = false;
      }
      return;

   case 'c':
   case '0':
      app.SetConfig();
      return;

   case 'u':
   case K_SEND:
#ifdef UPDATE_IN_BACKGROUND
      app.ConnectEnabledAccountsInBackground(false, true, true, false);
#else
      app.SetModeUpdateMailboxes();
#endif
      return;

   case 'b':
   case '2':
      app.SetModeAddressBook();
      break;

   case 'r':
   case '3':
      app.SetModeRulesBrowser();
      return;

   case 'f':
   case '4':
      C_client_file_mgr::SetModeFileBrowser(&app);
      return;

   case 'o':
   case '7':
      app.OpenMailbox(*this, true);
      break;

   case '5':
   case 'i':
      if(app.NumAccounts()){
         S_account &acc = app.accounts[selection];
#ifdef _DEBUG
         if(ui.key_bits&GKEY_CTRL){
            app.ConnectAccountInBackground(acc);
            break;
         }
#endif
         if(acc.IsImap()){
            if(acc.use_imap_idle){
               acc.CloseIdleConnection();
               app.ManageTimer();
               DrawAccount(selection);
            }else
               app.ConnectAccountInBackground(acc);
         }
      }
      break;

   case 'C':
      app.SetModeDataCounters();
      break;

   case 'e':
   case '6':
      if(app.NumAccounts() && selection < (int)app.NumAccounts())
         app.SetModeEditAccount(selection, false, false);
      break;

   case 'D':
      if(!app.socket_log)
         app.socket_log = app.SOCKET_LOG_YES_NOSHOW;
      else
         C_client_viewer::OpenFileForViewing(&app, NETWORK_LOG_FILE, L"Network log:");
      break;

#ifdef _DEBUG
   case 'k':
      if(ui.key_bits&GKEY_CTRL)
         app.ConnectEnabledAccountsInBackground(false, true, true);
      break;
#endif

   case '8':
   case 'l':
      app.ConnectEnabledAccountsInBackground(false, true, (ui.key_bits&GKEY_CTRL));
      //redraw = true;
      break;

   case '9':
   case 'd':
      app.CloseAccountsConnections(true);
      redraw = true;
      break;

   case K_CURSORRIGHT:
   case K_ENTER:
      if(app.NumAccounts())
         app.OpenMailbox(*this, (ui.key_bits&GKEY_SHIFT));
      return;

   case K_LEFT_SOFT:
   case K_MENU:
      {
         menu = CreateMenu();

         menu->AddItem(TXT_OPEN, (!app.NumAccounts() ? C_menu::DISABLED : 0), ok_key_name);
         menu->AddItem(TXT_ACCOUNT, C_menu::HAS_SUBMENU);
         for(int i=app.NumAccounts(); i--; ){
            const S_account &acc = app.accounts[i];
            if(acc.IsImap() && (acc.flags&acc.ACC_USE_IMAP_IDLE)){
               menu->AddItem(TXT_IMAP_IDLE, C_menu::HAS_SUBMENU);
               break;
            }
         }
         menu->AddItem(TXT_UPDATE_MAILBOXES, (!app.NumAccounts() ? C_menu::DISABLED : 0), send_key_name, "[U]", BUT_UPDATE_MAILBOX);
         menu->AddItem(TXT_TOOLS, C_menu::HAS_SUBMENU);
         menu->AddItem(TXT_ABOUT);
         menu->AddItem(TXT_USER_MANUAL);
         menu->AddSeparator();
         menu->AddItem(TXT_EXIT);
         app.PrepareMenu(menu);
      }
      return;

   case 't':
      app.SetConfigTweaks();
      break;

   case 'n':
   case '1':
                              //new message on active account
      if(app.NumAccounts()){
         app.OpenMailbox(*this, true);
         if(app.mode->Id()==C_mode_folders_list::ID)
            app.SetModeWriteMail(NULL, NULL, false, false, false);
      }
      break;

#ifdef _DEBUG
#ifdef USE_MOUSE
   case 'X':
      /*
      if(HasMouse()){
         menu = CreateTouchMenu();
         menu->AddItem(TXT_UPDATE_MAILBOXES, 0, send_key_name, "[U]", BUT_UPDATE_MAILBOX);
         menu->AddItem(TXT_TOOLS, C_menu::HAS_SUBMENU);
         menu->AddItem(TXT_ACCOUNT, C_menu::HAS_SUBMENU);
         menu->AddItem(TXT_EXIT);
         menu->AddItem(TXT_DELETE_FROM_PHONE, C_menu::MARKED);
         PrepareTouchMenu(menu, ScrnSX()/2, ScrnSY(), false);
      }
      */
      break;
#endif
#endif
   }
}

//----------------------------

void C_mail_client::DrawAccountNameAndIcons(const Cstr_w &name, int x, int y, const S_rect &rc, int max_width, dword fnt_flags, dword col_text,
   const dword stats[C_message_container::STAT_LAST], const wchar *status_text, const int progress_pos_total[2]){

   const C_image *img_read = small_msg_icons[MESSAGE_ICON_OPENED];
   const int text_offs_y = Max(1, int(img_read->SizeY() - fds.cell_size_y));

   const bool show_only_unread_count = config_mail.tweaks.show_minimum_counters;

   const int MIN_TEX_WIDTH = fds.cell_size_x*2;
   const int icon_width = img_read->SizeX();
   const dword num_draft = show_only_unread_count ? 0 : stats[C_message_container::STAT_DRAFTS];
   const dword num_to_send = stats[C_message_container::STAT_TO_SEND];
   const dword num_read = show_only_unread_count ? 0 : stats[C_message_container::STAT_READ];
   const dword num_unread = stats[C_message_container::STAT_UNREAD];
   const dword num_recent = stats[C_message_container::STAT_RECENT];
   const dword num_sent = show_only_unread_count ? 0 : stats[C_message_container::STAT_SENT];

                              //draw icons first, so that we know widths
   if(num_read || num_unread){
      Cstr_w s_r, s_u;
      int wr = 0, wu = 0;
      if(num_read){
         s_r<<num_read;
         wr = Max(GetTextWidth(s_r, UI_FONT_SMALL), MIN_TEX_WIDTH);
      }
      if(num_unread){
         s_u<<num_unread;
         wu = Max(GetTextWidth(s_u, UI_FONT_SMALL), MIN_TEX_WIDTH);
      }
      int xx = x+max_width - Max(wr, wu);
      int xxi = xx-icon_width-fds.cell_size_x/2;
      if(num_unread){
         DrawString(s_u, xx, y + text_offs_y, UI_FONT_SMALL, 0, col_text);
         C_image *img = small_msg_icons[MESSAGE_ICON_NEW];
         img->Draw(xxi, y+(img_read->SizeY()-img->SizeY()));
         if(num_recent && config_mail.tweaks.show_recent_flags){
            const C_image *img1 = msg_icons[MESSAGE_ICON_RECENT];
            img1->Draw(xxi-img1->SizeX()/2, y);
         }
      }
      if(num_read){
         int yy = y+fdb.line_spacing;
         DrawString(s_r, xx, yy + text_offs_y, UI_FONT_SMALL, 0, col_text);
         small_msg_icons[MESSAGE_ICON_OPENED]->Draw(xxi, yy);
      }
      max_width = xxi-x;
   }
   if(num_draft || num_to_send || num_sent){
      E_MESSAGE_ICON su, sd;
      dword n_u, n_d;
      n_u = num_draft; su = MESSAGE_ICON_DRAFT;
      n_d = num_sent; sd = MESSAGE_ICON_SENT;
      if(num_to_send){
         if(!n_d)
            n_d = num_to_send, sd = MESSAGE_ICON_TO_SEND;
         else
         if(!n_u)
            n_u = num_to_send, su = MESSAGE_ICON_TO_SEND;
      }

      Cstr_w s_u, s_d;
      int wu = 0, wd = 0;
      if(n_u){
         s_u<<n_u;
         wu = Max(GetTextWidth(s_u, UI_FONT_SMALL), MIN_TEX_WIDTH);
      }
      if(n_d){
         s_d<<n_d;
         wd = Max(GetTextWidth(s_d, UI_FONT_SMALL), MIN_TEX_WIDTH);
      }
      int xx = x+max_width - Max(wu, wd);
      int xxi = xx-icon_width-fds.cell_size_x/2;
      if(n_u){
         DrawString(s_u, xx, y + text_offs_y, UI_FONT_SMALL, 0, col_text);
         C_image *img = small_msg_icons[su];
         img->Draw(xxi, y+(img_read->SizeY()-img->SizeY()));
      }
      if(n_d){
         int yy = y+fdb.line_spacing;
         DrawString(s_d, xx, yy + text_offs_y, UI_FONT_SMALL, 0, col_text);
         C_image *img = small_msg_icons[sd];
         img->Draw(xxi, yy+(img_read->SizeY()-img->SizeY()));
      }
      max_width = xxi-x;
   }
   int yy = y;
   if(!status_text)
      yy += fdb.line_spacing/2;

   //const int text_offset_x = fdb.cell_size_x;
   {
      //int xx;
      //int w = GetTextWidth(name, UI_FONT_BIG, fnt_flags)+4;
      //xx = Max(x, Min(x+int(text_offset_x), x+max_width-w));
      //int max_w = max_width - (xx-x);
      DrawString(name, x, yy, UI_FONT_BIG, fnt_flags, col_text, -max_width);
   }
   if(status_text){
      yy += fds.line_spacing;
      //x += text_offset_x;
      //max_width -= text_offset_x;
      int tw = DrawString(status_text, x, yy, UI_FONT_SMALL, 0, MulAlpha(col_text, 0x8000), -max_width) + fds.cell_size_x;
      max_width -= fds.cell_size_x/2;
      if(progress_pos_total && (tw+fds.cell_size_x)<max_width){
         C_progress_indicator p;
         p.pos = progress_pos_total[0];
         p.total = progress_pos_total[1];
         p.rc = S_rect(x+max_width, yy+2, 0, fds.cell_size_y);
         p.rc.sx = Min(max_width-tw-fds.cell_size_x, fds.cell_size_x*10);
         p.rc.x -= p.rc.sx;
         DrawProgress(p);
      }
   }
}

//----------------------------

void C_mail_client::C_mode_accounts::DrawAccount(int ai) const{

                              //compute rectangle
   S_rect rc_item = rc;
   rc_item.y += ai*entry_height - sb.pos;
   rc_item.sy = entry_height;
                              //check if on-screen
   if(rc_item.Bottom()<=rc.y || rc_item.y>=rc.Bottom())
      return;

   S_rect crc = app.GetClipRect();
   app.SetClipRect(rc);

   dword col_text;

   const int max_x = GetMaxX();
   //S_rect rc = this->rc;
   //rc.y += entry_height*li;
   int x = rc_item.x;
   int max_width = max_x-x - app.fdb.letter_size_x;

   const S_account &acc = app.accounts[ai];

   S_rect rc_fill(2, rc_item.y, max_x-x, entry_height);
   app.AddDirtyRect(rc_fill);
   if(ai==selection){
      app.DrawSelection(rc_fill, true);
      col_text = app.GetColor(COL_TEXT_HIGHLIGHTED);
   }else{
      app.ClearWorkArea(rc_fill);
      col_text = app.GetColor(COL_TEXT);
   }

                        //draw separator
   {
      int y = rc_item.y+entry_height-1;
      if(y>rc.y && y<rc.Bottom()-1){
         const int OFFS = app.fdb.letter_size_x;
         app.DrawSeparator(x+OFFS, max_x-x-OFFS*2, y);
      }
   }

   int icon_x = x+2;

   E_SPECIAL_ICON spec_icon = SPEC_LAST;
   bool sched_icon = false;
   if(app.config_mail.auto_check_time && (acc.flags&acc.ACC_INCLUDE_IN_UPDATE_ALL)){
      sched_icon = true;
   }
   if(//acc.IsImap() && ((acc.flags&acc.ACC_USE_IMAP_IDLE) || acc.use_imap_idle)
      acc.background_processor.state!=S_account::UPDATE_DISCONNECTED
      ){
      spec_icon = E_SPECIAL_ICON(Min(acc.background_processor.state, S_account::UPDATE_ERROR));
   }else
   if(sched_icon){
      spec_icon = SPEC_SCHEDULED;
      sched_icon = false;
   }
   if(spec_icon!=SPEC_LAST){
      int xx = x+app.fdb.cell_size_x/4;
      int yy = rc_item.y+app.fdb.line_spacing;
      if(sched_icon){
         int szy = app.fdb.line_spacing;//app.spec_icons->SizeY();
                              //draw scheduled icon below IDLE icon
         app.DrawSpecialIcon(xx+szy/2, yy+szy/2, SPEC_SCHEDULED);
      }
      int w = app.DrawSpecialIcon(xx, yy, spec_icon);
      icon_x += w;
      x += w;
      max_width -= w;
   }

   x += app.fdb.cell_size_x;
   max_width -= app.fdb.cell_size_x;

   if(acc.IsImap()){
                              //draw folder icon
      app.DrawImapFolderIcon(icon_x, rc_item.y + app.fdb.line_spacing/4, 0);
   }

   dword stats[C_message_container::STAT_LAST];
   acc.GetMessagesStatistics(stats);

   const wchar *status = NULL;
   const int *progress_pos_total = NULL;
   if(acc.background_processor.state!=S_account::UPDATE_DISCONNECTED){
      if(acc.background_processor.status_text.Length())
         status = acc.background_processor.status_text;
      if(acc.background_processor.progress_total)
         progress_pos_total = &acc.background_processor.progress_pos;
   }
   app.DrawAccountNameAndIcons(acc.name, x, rc_item.y, rc_item, max_width, FF_BOLD, col_text, stats, status, progress_pos_total);

   app.SetClipRect(crc);
}

//----------------------------

void C_mail_client::C_mode_accounts::DrawContents() const{

   S_rect rc_item;
   int item_index = -1;
   while(BeginDrawNextItem(rc_item, item_index))
      DrawAccount(item_index);
   EndDrawItems();
   app.DrawScrollbar(sb, SCROLLBAR_DRAW_BACKGROUND);
                              //fill empty space
   int bot = rc.y+GetNumEntries()*entry_height-sb.pos;
   if(bot<rc.Bottom()){
      S_rect rc_fill(2, bot, app.ScrnSX()-4, rc.Bottom()-bot);
      app.ClearWorkArea(rc_fill);
   }
}

//----------------------------

void C_mail_client::C_mode_accounts::Draw() const{

   app.SetScreenDirty();
   app.DrawTitleBar(app_name, rc.y);
   app.ClearSoftButtonsArea(rc.Bottom() + 2);

   app.DrawEtchedFrame(rc);
   DrawContents();

   app.DrawSoftButtonsBar(*this, TXT_MENU,
#ifdef ANDROID_
      TXT_HIDE);
#else
      (app.config_mail.work_times_set || app.config_mail.auto_check_time || app.IsBackgroundConnected()) ? TXT_HIDE : TXT_EXIT);
#endif
#ifdef USE_MOUSE
   {
      bool connected_all = true, disconnected_all = true, any_push = false;
      for(int i=app.NumAccounts(); i--; ){
         const S_account &acc = app.accounts[i];
         if(acc.IsImap() && (acc.flags&acc.ACC_USE_IMAP_IDLE)){
            any_push = true;
            if(acc.use_imap_idle)
               disconnected_all = false;
            else
               connected_all = false;
         }
      }
      const char but_defs[] = { BUT_UPDATE_MAILBOX, char(!any_push ? BUT_NO : (!connected_all ? char(BUT_CONNECT_ALL) : char(BUT_DISCONNECT_ALL))), BUT_FILE_EXPLORER, BUT_ADDRESS_BOOK };
      const dword tids[] = { TXT_UPDATE_MAILBOX, !any_push ? TXT_NULL : !connected_all ? TXT_CONNECT_ALL : TXT_DISCONNECT_ALL, TXT_FILE_BROWSER, TXT_ADDRESS_BOOK };
      app.DrawBottomButtons(*this, but_defs, tids);
   }
#endif
}

//----------------------------

const S_config_item C_mail_client::ctrls_edit_account[] = {
                              //general settings
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_NAME, 40, OffsetOf(S_account, name), C_text_editor::CASE_CAPITAL, true },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_EMAIL, 100, OffsetOf(S_account, primary_identity.email), C_text_editor::CASE_LOWER },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_DISPLAY_NAME, 100, OffsetOf(S_account, primary_identity.display_name), C_text_editor::CASE_CAPITAL, 2},
   { CFG_ITEM_TEXTBOX_CSTR, TXT_REPLY_TO, 100, OffsetOf(S_account, primary_identity.reply_to_email), C_text_editor::CASE_LOWER },
   { CFG_ITEM_IDENTITIES, TXT_ACC_IDENTITIES },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_USERNAME, 100, OffsetOf(S_account, username), C_text_editor::CASE_LOWER },
   { CFG_ITEM_PASSWORD, TXT_ACC_PASSWORD, 100, OffsetOf(S_account, password), C_text_editor::CASE_LOWER },
   { CFG_ITEM_CHECKBOX, TXT_ACC_INCLUDE_IN_UPDATE_ALL, S_account::ACC_INCLUDE_IN_UPDATE_ALL, OffsetOf(S_account, flags) },
   { CFG_ITEM_MAIL_SERVER_TYPE, TXT_ACC_SERVER_TYPE, S_account::ACC_USE_IMAP4, OffsetOf(S_account, flags) },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_SERVER_MAIL, 100, OffsetOf(S_account, mail_server), C_text_editor::CASE_LOWER, 2 },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_SERVER_SMTP, 100, OffsetOf(S_account, smtp_server), C_text_editor::CASE_LOWER, 2 },
   { CFG_ITEM_CHECKBOX, TXT_ACC_MSG_RETRIEVE_MODE, S_account::ACC_UPDATE_GET_ENTIRE_MSG, OffsetOf(S_account, flags) },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_SEND_BCC_COPY, 100, OffsetOf(S_account, send_msg_copy_to), C_text_editor::CASE_LOWER },
   { CFG_ITEM_ADVANCED, TXT_ACC_ADVANCED },
//----------------------------
}, C_mail_client::ctrls_edit_account_pop[] = {
                              //POP3 settings
   { CFG_ITEM_CHECKBOX, TXT_ACC_USE_APOP, S_account::ACC_USE_APOP, OffsetOf(S_account, flags) },
   { CFG_ITEM_CHECKBOX, TXT_ACC_SMTP_AUTHENTICATION, S_account::ACC_USE_SMTP_AUTH, OffsetOf(S_account, flags) },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_SMTP_USERNAME, 100, OffsetOf(S_account, smtp_username), C_text_editor::CASE_LOWER },
   { CFG_ITEM_PASSWORD, TXT_ACC_SMTP_PASSWORD, 100, OffsetOf(S_account, smtp_password), C_text_editor::CASE_LOWER },
   { CFG_ITEM_NUMBER, TXT_ACC_SMTP_POP3_PORT, 5, OffsetOf(S_account, port_in) },
   { CFG_ITEM_NUMBER, TXT_ACC_SMTP_SMTP_PORT, 5, OffsetOf(S_account, port_out) },
   //{ CFG_ITEM_CHECKBOX, TXT_ACC_USE_SSL_POP3, S_account::ACC_USE_SSL_IN, OffsetOf(S_account, flags) },
   //{ CFG_ITEM_CHECKBOX, TXT_ACC_USE_SSL_OUT, S_account::ACC_USE_SSL_OUT, OffsetOf(S_account, flags) },
   //{ CFG_ITEM_CHECKBOX, TXT_ACC_USE_STARTTLS, S_account::ACC_SMTP_USE_STARTTLS, OffsetOf(S_account, flags) },
   { CFG_ITEM_SECURITY, TXT_ACC_USE_SSL_POP3, 0, OffsetOf(S_account, secure_in) },
   { CFG_ITEM_SECURITY, TXT_ACC_USE_SSL_OUT, 0, OffsetOf(S_account, secure_out) },
   { CFG_ITEM_CHECKBOX, TXT_CFG_SAVE_SENT_MESSAGES, 0, OffsetOf(S_account, save_sent_messages) },
   { CFG_ITEM_NUMBER, TXT_ACC_LIMIT_MSG_KB, 6, OffsetOf(S_account, max_kb_to_retrieve) },
//----------------------------
}, C_mail_client::ctrls_edit_account_imap[] = {
                              //IMAP settings
   { CFG_ITEM_CHECKBOX, TXT_ACC_USE_PUSH_MAIL, S_account::ACC_USE_IMAP_IDLE, OffsetOf(S_account, flags) },
   { CFG_ITEM_WORD_NUMBER, TXT_ACC_IDLE_PING_TIME, (1<<16)|60, OffsetOf(S_account, imap_idle_ping_time) },
   { CFG_ITEM_CHECKBOX, TXT_ACC_SMTP_AUTHENTICATION, S_account::ACC_USE_SMTP_AUTH, OffsetOf(S_account, flags) },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_SMTP_USERNAME, 100, OffsetOf(S_account, smtp_username), C_text_editor::CASE_LOWER },
   { CFG_ITEM_PASSWORD, TXT_ACC_SMTP_PASSWORD, 100, OffsetOf(S_account, smtp_password), C_text_editor::CASE_LOWER },
   { CFG_ITEM_NUMBER, TXT_ACC_SMTP_IMAP_PORT, 5, OffsetOf(S_account, port_in) },
   { CFG_ITEM_NUMBER, TXT_ACC_SMTP_SMTP_PORT, 5, OffsetOf(S_account, port_out) },
   //{ CFG_ITEM_CHECKBOX, TXT_ACC_USE_SSL_IMAP, S_account::ACC_USE_SSL_IN, OffsetOf(S_account, flags) },
   //{ CFG_ITEM_CHECKBOX, TXT_ACC_USE_SSL_OUT, S_account::ACC_USE_SSL_OUT, OffsetOf(S_account, flags) },
   //{ CFG_ITEM_CHECKBOX, TXT_ACC_USE_STARTTLS, S_account::ACC_SMTP_USE_STARTTLS, OffsetOf(S_account, flags) },
   { CFG_ITEM_SECURITY, TXT_ACC_USE_SSL_IMAP, 0, OffsetOf(S_account, secure_in) },
   { CFG_ITEM_SECURITY, TXT_ACC_USE_SSL_OUT, 0, OffsetOf(S_account, secure_out) },
   { CFG_ITEM_CHECKBOX_INV, TXT_CFG_IMAP_CHECK_ALL_FOLDERS, S_account::ACC_IMAP_UPDATE_INBOX_ONLY, OffsetOf(S_account, flags) },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CFG_IMAP_DRAFTS, 100, OffsetOf(S_account, imap_draft_folder), C_text_editor::CASE_CAPITAL, true },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CFG_IMAP_SENT, 100, OffsetOf(S_account, imap_sent_folder), C_text_editor::CASE_CAPITAL, true },
   { CFG_ITEM_CHECKBOX, TXT_CFG_SAVE_SENT_MESSAGES, 0, OffsetOf(S_account, save_sent_messages) },
   { CFG_ITEM_CHECKBOX, TXT_CFG_IMAP_UPLOAD_SENT, S_account::ACC_IMAP_UPLOAD_SENT, OffsetOf(S_account, flags) },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CFG_IMAP_TRASH, 100, OffsetOf(S_account, imap_trash_folder), C_text_editor::CASE_CAPITAL, true },
   { CFG_ITEM_CHECKBOX, TXT_MOVE_DELETED_TO_TRASH, 0, OffsetOf(S_account, move_to_trash) },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CFG_IMAP_ROOT_FOLDER, 100, OffsetOf(S_account, imap_root_path), C_text_editor::CASE_CAPITAL, true },
   { CFG_ITEM_NUMBER, TXT_ACC_MAX_DAYS, 3, OffsetOf(S_account, imap_last_x_days_limit) },
   //{ CFG_ITEM_NUMBER, TXT_ACC_LIMIT_MSG_KB, 6, OffsetOf(S_account, max_kb_to_retrieve) },
   { CFG_ITEM_CHECKBOX, TXT_CFG_DOWNLOAD_ATTACHMENTS, S_account::ACC_IMAP_DOWNLOAD_ATTACHMENTS, OffsetOf(S_account, flags) },
};

//----------------------------

const dword C_mail_client::num_edit_acc_ctrls[3] = {
   sizeof(ctrls_edit_account)/sizeof(S_config_item),
   sizeof(ctrls_edit_account_pop)/sizeof(S_config_item),
   sizeof(ctrls_edit_account_imap)/sizeof(S_config_item),
};

#define GET_ACCOUNT_CONTROLS(adv, is_imap) (!(adv) ? ctrls_edit_account : !(is_imap) ? ctrls_edit_account_pop : ctrls_edit_account_imap)

#define NUM_ACCOUNT_CONTROLS(adv, is_imap) num_edit_acc_ctrls[!(adv) ? 0 : !(is_imap) ? 1 : 2]

//----------------------------

void C_mail_client::SetModeEditAccount(dword acc_i, bool advanced, bool new_account){

   C_mode_edit_account &mod = *new(true) C_mode_edit_account(*this, acc_i, advanced, new_account);
   S_account &acc = accounts[mod.acc_indx];

   mod.init_settings = acc;

   mod.InitLayout();
   dword index = 0;
   if(new_account)
      for(; ctrls_edit_account[index].txt_id!=TXT_ACC_PASSWORD; ++index);
   SetEditAccountSelection(mod, index);
   ActivateMode(mod);
}

//----------------------------

int C_mail_client::C_mode_edit_account::GetNumEntries() const{
   return NUM_ACCOUNT_CONTROLS(advanced, app.accounts[acc_indx].IsImap());
}

//----------------------------

void C_mail_client::C_mode_edit_account::InitLayout(){

   const S_account &acc = app.accounts[acc_indx];
   const int border = 2;
   const int top = app.GetTitleBarHeight();
   rc = S_rect(border, top, app.ScrnSX()-border*2, app.ScrnSY()-top-app.GetSoftButtonBarHeight()-border*3);
                              //space for help
   rc.sy -= app.fds.line_spacing*9/2;
                           //compute # of visible lines, and resize rectangle to whole lines
   entry_height = app.fdb.line_spacing*2 + 2;
   sb.visible_space = rc.sy;// / entry_height;
   //sb.visible_space *= entry_height;
   rc.sy = sb.visible_space;

                           //init scrollbar
   int width = app.GetScrollbarWidth();
   sb.rc = S_rect(rc.Right()-width-1, rc.y+1, width, rc.sy-2);
   sb.total_space = NUM_ACCOUNT_CONTROLS(advanced, acc.IsImap())*entry_height;
   sb.SetVisibleFlag();

   max_textbox_width = sb.rc.x-rc.x - app.fdb.letter_size_x*3;

   EnsureVisible();
   if(text_editor)
      PositionTextEditor();
}

//----------------------------

void C_mail_client::SetEditAccountSelection(C_mode_edit_account &mod, int sel){

   const S_account &acc = accounts[mod.acc_indx];
   mod.selection = Abs(sel);
   mod.text_editor = NULL;
                              //determine field type
   const S_config_item &ec = GET_ACCOUNT_CONTROLS(mod.advanced, acc.IsImap())[mod.selection];
   switch(ec.ctype){
   case CFG_ITEM_TEXTBOX_CSTR:
   case CFG_ITEM_PASSWORD:
   //case CFG_ITEM_TEXT_NUMBER:
      {
         dword flags = 0;
         if(ec.ctype==CFG_ITEM_PASSWORD)
            flags |= TXTED_SECRET;
         if(ec.elem_offset==OffsetOf(S_account, primary_identity.email))
            flags |= TXTED_EMAIL_ADDRESS;
         if(!ec.is_wide)
            flags |= TXTED_ANSI_ONLY;
         mod.text_editor = CreateTextEditor(flags,// | (ec.ctype==CFG_ITEM_TEXT_NUMBER ? TXTED_NUMERIC : 0),
            UI_FONT_BIG, 0, NULL, ec.param);
         mod.text_editor->Release();
         //mod.show_password = false;
         C_text_editor &te = *mod.text_editor;
         switch(ec.is_wide){
         case 0:
         case 2:
            {
               Cstr_c &str = *(Cstr_c*)((byte*)&acc + ec.elem_offset);
               Cstr_w sw;
               if(ec.is_wide)
                  sw.FromUtf8(str);
               else
                  sw.Copy(str);
               te.SetInitText(sw);
            }
            break;
         case 1:
            {
               Cstr_w &str = *(Cstr_w*)((byte*)&acc + ec.elem_offset);
               te.SetInitText(str);
            }
            break;
         }
         te.SetCase(C_text_editor::CASE_ALL, ec.param2);
      }
      break;

   case CFG_ITEM_NUMBER:
      {
         mod.text_editor = CreateTextEditor(TXTED_NUMERIC, UI_FONT_BIG, 0, NULL, ec.param); mod.text_editor->Release();
         C_text_editor &te = *mod.text_editor;
         dword n = *(word*)((byte*)&acc + ec.elem_offset);
         Cstr_w s;
         if(n)
            s<<n;
         te.SetInitText(s);
      }
      break;
   }
   if(sel>=0)
      mod.EnsureVisible();
   if(mod.text_editor){
      C_text_editor &te = *mod.text_editor;
      te.SetCursorPos(te.GetTextLength());
      mod.PositionTextEditor();
      MakeSureCursorIsVisible(te);
   }
}

//----------------------------

void C_mail_client::StoreEditorText(C_text_editor &te, Cstr_w &str){

   str = te.GetText();
}

//----------------------------

template<class T>
void TruncExtraSpace(T &s){
   while(s.Length() && s[0]==' ')
      s = s.Right(s.Length()-1);
   while(s.Length() && s[s.Length()-1]==' ')
      s = s.Left(s.Length()-1);
}

//----------------------------

void C_mail_client::CollectContainerFolderIds(C_vector<dword> &ids) const{

   for(int i=NumAccounts(); i--; ){
      const S_account &acc = accounts[i];
      C_folders_iterator it(const_cast<S_account&>(acc)._folders);
      while(!it.IsEnd()){
         const C_message_container *fld = it.Next();
         if(fld){
            dword id = fld->msg_folder_id;
            if(id)
               ids.push_back(id);
         }
      }
   }
   SortVector(ids);
}

//----------------------------

dword C_mail_client::GetMsgContainerFolderId() const{

   C_vector<dword> ids;
   CollectContainerFolderIds(ids);
   dword min_id = ids.size()+1;
   if(min_id!=1){
      if(ids.front()!=1)
         min_id = 1;
      else
      for(int i=ids.size()-1; i--; ){
         if(ids[i]+1 != ids[i+1])
            min_id = ids[i]+1;
      }
   }
                              //delete possible folder
   Cstr_w fn; fn<<mail_data_path <<MAIL_PATH <<min_id <<'\\' <<L"messages.bin";
   C_file::DeleteFile(fn);
   return min_id;
}

//----------------------------

void C_mail_client::CloseEditAccounts(C_mode_edit_account &mod, bool validate){

   S_account &acc = accounts[mod.acc_indx];
   if(!mod.advanced){
      if(validate){

         E_TEXT_ID err_txt = TXT_NULL;
         int offs = -1;
         TruncExtraSpace(acc.name);
         TruncExtraSpace(acc.primary_identity.display_name);
         TruncExtraSpace(acc.mail_server);
         TruncExtraSpace(acc.smtp_server);
         TruncExtraSpace(acc.primary_identity.email);
         TruncExtraSpace(acc.username);
         TruncExtraSpace(acc.smtp_username);
         if(!acc.name.Length()){
            offs = OffsetOf(S_account, name);
            err_txt = TXT_ERR_ACC_NO_NAME;
         }else
         if(!acc.mail_server.Length()){
            offs = OffsetOf(S_account, mail_server);
            err_txt = TXT_ERR_ACC_NO_SERVER;
         }else{
            C_vector<Cstr_c> adds;
            if(!ParseRecipients(acc.primary_identity.email, adds) || adds.size()!=1){
               offs = OffsetOf(S_account, primary_identity.email);
               err_txt = TXT_ERR_ACC_EMAIL_INVALID;
            }else{
               bool bcc_ok = true;
               if(acc.send_msg_copy_to.Length()){
                  C_vector<Cstr_c> adds1;
                  bcc_ok = (ParseRecipients(acc.send_msg_copy_to, adds1) && adds1.size() <= 1);
               }
               if(!bcc_ok){
                  offs = OffsetOf(S_account, send_msg_copy_to);
                  err_txt = TXT_ERR_ACC_EMAIL_INVALID;
               }            }
         }

         if(offs!=-1){
                                 //display error message related to given field
            const S_config_item *ctrls = GET_ACCOUNT_CONTROLS(mod.advanced, acc.IsImap());
            int i;
            int num_ctrls = NUM_ACCOUNT_CONTROLS(mod.advanced, acc.IsImap());
            for(i=0; i<num_ctrls; i++){
               if(ctrls[i].elem_offset==offs){
                  SetEditAccountSelection(mod, i);
                  break;
               }
            }
            assert(i!=-1);
            ShowErrorWindow(TXT_ERROR, err_txt);
            return;
         }
         /*
                              //fix IMAP Inbox case issues
         if(acc.IsImap()){
            for(int i=0; i<3; i++){
               Cstr_w &n = i==0 ? acc.imap_draft_folder : i==1 ? acc.imap_sent_folder : acc.imap_trash_folder;
               Cstr_w l = n.Left(6); l.ToLower();
               if(l==L"inbox/"){
                  l = n.RightFromPos(5);
                  n.Clear();
                  n<<L"INBOX" <<l;
               }
            }
         }
         */
      }
      //S_account &acc = accounts[mod.acc_indx];
                              //set our (sorted) account index as selection to parent mode
      C_mode_accounts &mod_a = (C_mode_accounts&)*mod.GetParent();
      mod_a.selection = mod.acc_indx;
      mod_a.EnsureVisible();

      if(!mod.new_account && mod.init_settings.IsImap() != acc.IsImap()){
                              //user switched POP3/IMAP account type, erase all messages and folders
         acc.CloseConnection();
         acc.DeleteAllFolders(mail_data_path);
         acc.flags |= acc.ACC_NEED_FOLDER_REFRESH;
      }
      if(mod.new_account || mod.init_settings.IsImap() != acc.IsImap()){
                              //make Inbox folder
         C_message_container *fld = new(true) C_message_container;
         fld->folder_name.Copy(acc.inbox_folder_name);
         acc._folders.Resize(1);
         acc._folders[0] = fld;
         fld->Release();
         if(acc.IsImap()){
            //fld->imap_name = "\"INBOX\"";
            acc.flags |= acc.ACC_NEED_FOLDER_REFRESH;
         }else
            fld->is_imap = false;
         fld->msg_folder_id = GetMsgContainerFolderId();
      }
      if(mod.new_account && NumAccounts()==1)
         CreateDemoMail(*FindInbox(acc));
      SaveAccounts();
      if(acc.IsImap()){
         bool reset = ((acc.flags&acc.ACC_USE_IMAP_IDLE) != (mod.init_settings.flags&acc.ACC_USE_IMAP_IDLE) ||
            acc.imap_last_x_days_limit != mod.init_settings.imap_last_x_days_limit ||
            acc.mail_server != mod.init_settings.mail_server ||
            acc.password != mod.init_settings.password ||
            acc.port_in != mod.init_settings.port_in ||
            acc.username != mod.init_settings.username);

         if(reset)
            acc.CloseConnection();
         if(acc.use_imap_idle)
            ConnectAccountInBackground(acc);
      }
   }else{
      if(!acc.imap_idle_ping_time)
         acc.imap_idle_ping_time = acc.IMAP_IDLE_DEFAULT_PING_TIME;
   }
   CloseMode(mod);
}

//----------------------------

void C_mail_client::C_mode_edit_account::TextEditNotify(bool cursor_moved, bool text_changed, bool &redraw){

   if(text_changed){
      if(EnsureVisible())
         PositionTextEditor();
      S_account &acc = app.accounts[acc_indx];
      const S_config_item &ec = GET_ACCOUNT_CONTROLS(advanced, acc.IsImap())[selection];
      app.ModifySettingsTextEditor(*this, ec, (byte*)&acc);
   }
   redraw = true;
}

//----------------------------

void C_mail_client::EditAccountProcessMenu(C_mode_edit_account &mod, int itm, dword menu_id){

   switch(itm){
   case TXT_EDIT:
      mod.menu = CreateEditCCPSubmenu(mod.text_editor, mod.menu);
      PrepareMenu(mod.menu);
      return;
   case SPECIAL_TEXT_CUT:
   case SPECIAL_TEXT_COPY:
   case SPECIAL_TEXT_PASTE:
      ProcessCCPMenuOption(itm, mod.text_editor);
      break;

   case TXT_DONE:
      CloseEditAccounts(mod, true);
      return;

   case TXT_DELETE_ACCOUNT:
      CloseEditAccounts(mod, false);
      DeleteAccount((C_mode_accounts&)*mode);
      return;
   }
   if(mod.text_editor)
      mod.text_editor->Activate(true);
}

//----------------------------

void C_mail_client::C_mode_edit_account::PositionTextEditor(){

   assert(text_editor);

   //C_application_ui &app = AppForListMode();
   C_text_editor &te = *text_editor;
   te.SetRect(S_rect(rc.x+app.fdb.letter_size_x, rc.y + 1 + app.fdb.line_spacing + selection*entry_height-sb.pos, max_textbox_width, app.fdb.cell_size_y+1));
}

//----------------------------

void C_mail_client::C_mode_edit_account::ProcessInput(S_user_input &ui, bool &redraw){

   S_account &acc = app.accounts[acc_indx];
   ProcessInputInList(ui, redraw);
   const S_config_item &ec = GET_ACCOUNT_CONTROLS(advanced, acc.IsImap())[selection];

#ifdef USE_MOUSE
   if(app.HasMouse())
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){
      if(text_editor && app.ProcessMouseInTextEditor(*text_editor, ui))
         redraw = true;
      else
      if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
         if(ui.key==K_ENTER){
            switch(ec.ctype){
            case CFG_ITEM_CHECKBOX:
            case CFG_ITEM_CHECKBOX_INV:
               //ui.key = K_ENTER;
               break;
            default:
               if(ui.mouse.x<rc.sx/3){
                  ui.key = K_CURSORLEFT;
               }else
               if(ui.mouse.x>=rc.sx*2/3){
                  ui.key = K_CURSORRIGHT;
               }else
                  ui.key = K_ENTER;
            }
         }
      }
   }
#endif
   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      app.CloseEditAccounts(*this, true);
      return;
   case K_LEFT_SOFT:
   case K_MENU:
      {
         bool sep = false;
         menu = CreateMenu();
         if(!advanced && new_account){
            menu->AddItem(TXT_DELETE_ACCOUNT);
            sep = true;
         }
         if(text_editor){
            text_editor->Activate(false);
            app.AddEditSubmenu(menu);
            sep = true;
         }
         if(sep)
            menu->AddSeparator();
         menu->AddItem(TXT_DONE);
         app.PrepareMenu(menu);
      }
      return;

   case K_ENTER:
      switch(ec.ctype){
      case CFG_ITEM_CHECKBOX:
      case CFG_ITEM_CHECKBOX_INV:
         if(ec.param){
            dword &flags = *(dword*)((byte*)&acc + ec.elem_offset);
            flags ^= ec.param;
         }else{
            bool &b = *(bool*)((byte*)&acc + ec.elem_offset);
            b = !b;
         }
         redraw = true;
         break;
      case CFG_ITEM_ADVANCED:
         app.SetModeEditAccount(acc_indx, true, false);
         return;
      case CFG_ITEM_IDENTITIES:
         app.SetModeEditIdentities(acc);
         break;
      }
      break;

   default:
      switch(ec.ctype){
      case CFG_ITEM_ADVANCED:
         if(ui.key==K_CURSORRIGHT || ui.key==K_CURSORLEFT)
            app.SetModeEditAccount(acc_indx, true, false);
         break;
      case CFG_ITEM_IDENTITIES:
         if(ui.key==K_CURSORRIGHT || ui.key==K_CURSORLEFT)
            app.SetModeEditIdentities(acc);
         break;
      case CFG_ITEM_MAIL_SERVER_TYPE:
         {
            dword &flags = *(dword*)((byte*)&acc + ec.elem_offset);
            if(ui.key==K_CURSORRIGHT){
               if(!(flags&S_account::ACC_USE_IMAP4)){
                  flags |= S_account::ACC_USE_IMAP4;
                  redraw = true;
               }
            }else
            if(ui.key==K_CURSORLEFT){
               if(flags&S_account::ACC_USE_IMAP4){
                  flags &= ~S_account::ACC_USE_IMAP4;
                  redraw = true;
               }
            }
         }
         break;
      case CFG_ITEM_WORD_NUMBER:
         {
            word &val = *(word*)((byte*)&acc + ec.elem_offset);
            const int step = ec.param2 ? ec.param2 : 1;
            if(ui.key==K_CURSORLEFT){
               const int min = ec.param>>16;
               if(val>min){
                  val = (word)Max(min, int(val)-step);
                  redraw = true;
               }
            }else
            if(ui.key==K_CURSORRIGHT){
               const int max = ec.param&0xffff;
               if(val<max){
                  val = (word)Min(max, int(val)+step);
                  redraw = true;
               }
            }
         }
         break;
      case CFG_ITEM_SECURITY:
         {
            byte &sc = *(byte*)((byte*)&acc + ec.elem_offset);
            if(ui.key==K_CURSORLEFT){
               if(sc){
                  --sc;
                  redraw = true;
               }
            }else if(ui.key==K_CURSORRIGHT){
               if(sc<S_account_settings::SECURE_LAST-1){
                  ++sc;
                  redraw = true;
               }
            }
         }
         break;
      }
   }
}

//----------------------------

void C_mail_client::DrawSettings(const C_mode_settings &mod, const S_config_item *ctrls, const void *cfg_base, bool draw_help){

   const dword col_text = GetColor(COL_TEXT);

   dword sx = ScrnSX();
   ClearWorkArea(mod.rc);
   ClearSoftButtonsArea(mod.rc.Bottom() + 2);

   const int border = 2;
   S_text_display_info tdi_help;
   tdi_help.rc = S_rect(border, mod.rc.Bottom()+fds.line_spacing/2-1, sx-border*2, fds.line_spacing*9/2);

   DrawEtchedFrame(mod.rc);
   if(draw_help)
      DrawPreviewWindow(tdi_help.rc);
                              //draw entries
   int max_x = mod.GetMaxX();
   int x = mod.rc.x;
   int max_width = max_x-x;

   S_rect rc_item;
   int item_index = -1;
   while(mod.BeginDrawNextItem(rc_item, item_index)){
      const S_config_item &ec = ctrls[item_index];

      dword color = col_text;
      if(item_index==mod.selection){
         DrawSelection(rc_item);
         color = GetColor(COL_TEXT_HIGHLIGHTED);
      }
         
                              //draw separator
      if(rc_item.y > mod.rc.y && (item_index<mod.selection || item_index>mod.selection+1))
         DrawSeparator(x+fdb.letter_size_x*1, max_width-fdb.letter_size_x*2, rc_item.y);

      {
                              //draw name
         int xx = x;
         xx += fdb.letter_size_x;
         DrawString(GetText(E_TEXT_ID(ec.txt_id)), xx, rc_item.y + 2, UI_FONT_BIG, FF_BOLD, color, -max_width);
      }

      bool draw_arrow_l = false, draw_arrow_r = false;
                              //draw additional control info
      int xx = x, yy = rc_item.y + 1 + fdb.line_spacing;
         xx += fdb.letter_size_x;
      switch(ec.ctype){
      case CFG_ITEM_TEXTBOX_CSTR:
      case CFG_ITEM_PASSWORD:
      //case CFG_ITEM_TEXT_NUMBER:
         {
            const C_text_editor &te = *mod.text_editor;
            if(item_index==mod.selection){
               DrawEditedText(te);
            }else{
               Cstr_w sw;
               switch(ec.is_wide){
               case 0:
               case 2:
                  {
                     const Cstr_c &str = *(Cstr_c*)((byte*)cfg_base + ec.elem_offset);
                     if(!ec.is_wide)
                        sw.Copy(str);
                     else
                        sw.FromUtf8(str);
                  }
                  break;
               case 1:
                  sw = *(Cstr_w*)((byte*)cfg_base + ec.elem_offset);
                  break;
               }
               if(ec.ctype==CFG_ITEM_PASSWORD)
                  text_utils::MaskPassword(sw);
               DrawString(sw, xx, yy, UI_FONT_BIG, 0, color, -mod.max_textbox_width);
            }
         }
         break;

      case CFG_ITEM_NUMBER:
         {
            const C_text_editor &te = *mod.text_editor;
            if(item_index==mod.selection){
               DrawEditedText(te);
            }else{
               dword n = *(word*)((byte*)cfg_base + ec.elem_offset);
               if(n){
                  Cstr_w s;
                  s<<n;
                  DrawString(s, xx, yy, UI_FONT_BIG, 0, color, -mod.max_textbox_width);
               }
            }
         }
         break;

      case CFG_ITEM_WORD_NUMBER:
         {
            word val = *(word*)((byte*)cfg_base + ec.elem_offset);
            Cstr_w s;
            s<<dword(val);
            DrawString(s, mod.rc.CenterX(), yy, UI_FONT_BIG, FF_CENTER, color, -mod.max_textbox_width);
            draw_arrow_l = (val > (ec.param>>16));
            draw_arrow_r = (val < (ec.param&0xffff));
         }
         break;

      case CFG_ITEM_CHECKBOX:
      case CFG_ITEM_CHECKBOX_INV:
         {
            int x1 = xx;
            const int frame_size = fdb.cell_size_y;
            x1 += max_width - frame_size - fdb.cell_size_x;
            bool on;
            if(ec.param){
               const dword &flags = *(dword*)((byte*)cfg_base + ec.elem_offset);
               on = (flags&ec.param);
            }else{
               on = *(bool*)((byte*)cfg_base + ec.elem_offset);
            }
            if(ec.ctype==CFG_ITEM_CHECKBOX_INV)
               on = !on;
            DrawCheckbox(x1-frame_size/2, yy, frame_size, on);
            /*
            if(on){
               yy += fdb.line_spacing/2-1;
               DrawCheckbox(x1, yy, frame_size);
            }
            */
         }
         break;

      case CFG_ITEM_MAIL_SERVER_TYPE:
         {
            const wchar *txt = NULL;
            Cstr_w tmp;
            dword flags = *(dword*)((byte*)cfg_base + ec.elem_offset);
            if(flags&S_account::ACC_USE_IMAP4){
               txt = L"IMAP";
               draw_arrow_l = true;
            }else{
               txt = L"POP3";
               draw_arrow_r = true;
            }
            DrawString(txt, x + max_width/2, yy, UI_FONT_BIG, FF_CENTER, color);
         }
         break;

      case CFG_ITEM_SECURITY:
         {
            S_account_settings::E_SECURE_CONN sc = *(S_account_settings::E_SECURE_CONN*)((byte*)cfg_base + ec.elem_offset);
            const wchar *txt;
            switch(sc){ default: assert(0);
            case S_account_settings::SECURE_NO: txt = GetText(TXT_NO); break;
            case S_account_settings::SECURE_SSL: txt = L"SSL/TLS"; break;
            case S_account_settings::SECURE_STARTTLS: txt = L"StartTLS"; break;
            }
            DrawString(txt, x + max_width/2, yy, UI_FONT_BIG, FF_CENTER, color);
            draw_arrow_l = (sc>0);
            draw_arrow_r = (sc<S_account_settings::SECURE_LAST-1);
         }
         break;
      case CFG_ITEM_IDENTITIES:
         {
            const S_account &acc = *(S_account*)cfg_base;
            int n = acc.identities.Size();
            if(n){
               Cstr_w s;
               s<<(n+1);
               DrawString(s, x+fdb.cell_size_x*2, yy, UI_FONT_SMALL, 0, color);
            }
         }
         break;
      }
      const int arrow_size = (fdb.line_spacing/2) | 1;
      if(draw_arrow_l){
         int xx1 = x + fdb.letter_size_x;
         DrawArrowHorizontal(xx1, yy+1, arrow_size, color, false);
      }
      if(draw_arrow_r){
         int xx1 = x + max_width - fdb.letter_size_x - arrow_size;
         DrawArrowHorizontal(xx1, yy+1, arrow_size, color, true);
      }
   }
   DrawScrollbar(mod.sb);
   mod.EndDrawItems();

   if(draw_help){
                              //help
      const S_config_item &ec = ctrls[mod.selection];
      tdi_help.rc.Compact();
      tdi_help.rc.Compact();
      tdi_help.body_w = GetText((E_TEXT_ID)(ec.txt_id+1));
      tdi_help.is_wide = true;
      tdi_help.ts.font_index = UI_FONT_SMALL;
      tdi_help.ts.text_color = GetColor(COL_TEXT_POPUP);
      DrawFormattedText(tdi_help);
   }
   SetScreenDirty();
}

//----------------------------

void C_mail_client::C_mode_edit_account::Draw() const{

   const S_account &acc = app.accounts[acc_indx];
   {
      wchar title[128];
      StrCpy(title, app.GetText(TXT_EDIT_ACCOUNT));
      StrCpy(title+StrLen(title), L" - ");
      StrCpy(title+StrLen(title), acc.name);
      app.DrawTitleBar(title);
   }
   app.DrawSettings(*this, GET_ACCOUNT_CONTROLS(advanced, acc.IsImap()), &acc);
   app.DrawSoftButtonsBar(*this, TXT_MENU, advanced ? TXT_BACK : TXT_DONE, text_editor);
}

//----------------------------
//----------------------------
const int NUM_SEL_LINES = 5;

void C_mail_client::SetModeAccountSelector(const char *rcpt){

   C_mode_account_selector &mod = *new(true) C_mode_account_selector(*this, rcpt);
   mod.InitLayout();
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::C_mode_account_selector::InitLayout(){

   const dword sx = app.ScrnSX();
   const int sz_x = sx - app.fdb.letter_size_x * 4;
   const int title_sy = app.GetDialogTitleHeight();
                              //initial rect (horizontal position is correct)
   rc1 = S_rect((sx-sz_x)/2, 0, sz_x, title_sy + app.fdb.line_spacing * NUM_SEL_LINES + app.fdb.line_spacing/2);
   rc1.y = (app.ScrnSY()-rc1.sy)/2;

   rc = S_rect(rc1.x+app.fdb.letter_size_x, rc1.y+title_sy, rc1.sx-app.fdb.letter_size_x*2, app.fdb.line_spacing * NUM_SEL_LINES + 2);

   sb.visible_space = NUM_SEL_LINES;
   sb.total_space = app.NumAccounts();
   sb.SetVisibleFlag();
   int width = app.GetScrollbarWidth();
   sb.rc = S_rect(rc.Right()-width-1, rc.y+1, width, rc.sy-2);
   draw_bgnd = true;
}

//----------------------------

void C_mail_client::SetModeAccountSelector_SendFiles(const C_client_file_mgr::C_mode_file_browser &mod){

   SetModeAccountSelector();
                              //store filename into temp message, which is part of the account-selector mode
   C_mode_account_selector &mod_as = (C_mode_account_selector&)*mode;
   S_message &msg = mod_as.tmp_msg;
   if(!mod.marked_files.size()){
      msg.attachments.Resize(1);
      S_attachment &att = msg.attachments.Front();
      Cstr_w fn = att.filename.FromUtf8();
      C_client_file_mgr::FileBrowser_GetFullName(mod, fn);
      att.filename = fn.ToUtf8();
      const C_client_file_mgr::C_mode_file_browser::S_entry &e = mod.entries[mod.selection];
      att.suggested_filename = e.name;
   }else{
      msg.attachments.Resize(mod.marked_files.size());
      for(int i=0; i<mod.marked_files.size(); i++){
         S_attachment &att = msg.attachments[i];
         const Cstr_w &fn = mod.marked_files[i];
         att.filename = fn.ToUtf8();
         att.suggested_filename = fn.Right(fn.Length()-fn.FindReverse('\\')-1);
      }
   }
}

//----------------------------

void C_mail_client::C_mode_account_selector::ProcessInput(S_user_input &ui, bool &redraw){

#ifdef USE_MOUSE
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){
      C_scrollbar::E_PROCESS_MOUSE pm = app.ProcessScrollbarMouse(sb, ui);
      switch(pm){
      case C_scrollbar::PM_PROCESSED: redraw = true; break;
      case C_scrollbar::PM_CHANGED:
         //mod.top_line = mod.sb.pos;
         redraw = true;
         break;
      default:
         if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
            if(ui.CheckMouseInRect(rc)){
               int line = (ui.mouse.y - rc.y) / app.fdb.line_spacing;
               line += top_line;
               if(line < (int)app.NumAccounts()){
                  if(selection != line){
                     selection = line;
                     redraw = true;
                  }else
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                     ui.key = K_ENTER;
                  }
               }
            }
         }
      }
   }
#endif

   const int num = app.NumAccounts();

   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      app.CloseMode(*this);
      return;
   case K_LEFT_SOFT:
   case K_ENTER:
      if(num){
                              //switch to selected mailbox, and start writing message in the mailbox
         AddRef();

         app.SetModeAccounts();
         C_mode_accounts &mod_acc = (C_mode_accounts&)*app.mode;
         mod_acc.selection = selection;
         S_account &acc = app.accounts[selection];
         app.SetModeFoldersList(acc);
         if(!acc.IsImap())
            app.SetModeMailbox(acc, app.FindInbox(acc));
         app.SetModeWriteMail(NULL, tmp_msg.attachments.Size() ? &tmp_msg : NULL, false, false, false, force_recipient.Length() ? (const char*)force_recipient : NULL);
         Release();
      }
      return;
   case K_CURSORUP:
      if(num){
         if(!selection)
            selection = num;
         --selection;
         EnsureVisible();
         redraw = true;
      }
      break;
   case K_CURSORDOWN:
      if(num){
         if(++selection==num)
            selection = 0;
         EnsureVisible();
         redraw = true;
      }
      break;
   }
}

//----------------------------

void C_mail_client::C_mode_account_selector::Draw() const{

   if(draw_bgnd){
      DrawParentMode(true);

      app.DrawDialogBase(rc1, true);
      app.DrawDialogTitle(rc1, app.GetText(TXT_USE_ACCOUNT));
   }
   app.DrawDialogBase(rc1, false, &rc);

   dword col_text= app.GetColor(COL_TEXT_POPUP);

   int y = rc.y + 1;
   int x = rc.x + app.fdb.letter_size_x;
   int max_w = sb.rc.x - x - app.fdb.letter_size_x;
   for(int i=0; i<NUM_SEL_LINES; i++){
      int ai = i + top_line;
      if(ai >= int(app.NumAccounts()))
         break;
      dword color = col_text;
      if(ai==selection){
         app.DrawSelection(S_rect(rc.x, y, sb.rc.x-rc.x-3, app.fdb.line_spacing));
         color = app.GetColor(COL_TEXT_HIGHLIGHTED);
      }
      const S_account &acc = app.accounts[ai];
      app.DrawString(acc.name, x, y, UI_FONT_BIG, 0, color, -max_w);

      y += app.fdb.line_spacing;
   }
   app.DrawScrollbar(sb);

   if(draw_bgnd)
      app.DrawSoftButtonsBar(*this, TXT_OK, TXT_CANCEL);
   draw_bgnd = false;
}

//----------------------------

void C_mail_client::GetDateString(const S_date_time &dt, Cstr_w &str, bool force_short, bool force_year) const{

   str.Clear();
   int m = dt.month + 1;
   int d = dt.day + 1;
   int y = dt.year % 100;

   if(config_mail.tweaks.date_fmt.Length()){
      const char *fmt = config_mail.tweaks.date_fmt;
      while(true){
         char c = *fmt++;
         if(!c)
            break;
         if(c=='%' && *fmt){
            c = *fmt++;
            switch(c){
            case 'Y':
               //if(!force_short)
               {
                  str<<dt.year;
                  break;
               }
            case 'y': str.AppendFormat(L"'#02%") <<y; break;
            case 'd': str.AppendFormat(L"#02%") <<d; break;
            case 'm': str.AppendFormat(L"#02%") <<m; break;
            case 'M':
               {
                  Cstr_w s;
                  s.Copy(((S_date_time_x&)dt).GetMonthName());
                  str<<s;
               }
               break;
            default: str<<'%' <<c;
            }
         }else
            str<<c;
      }
      return;
   }

   S_config_mail::E_DATE_FORMAT fmt = config_mail.date_format;
   if(force_year){
      force_short = false;
      switch(fmt){
      case S_config_mail::DATE_MM_SLASH_DD: fmt = S_config_mail::DATE_MM_SLASH_DD_SLASH_YY; break;
      case S_config_mail::DATE_MM_DASH_DD: fmt = S_config_mail::DATE_MM_DASH_DD_DASH_YY; break;
      case S_config_mail::DATE_DD_SLASH_MM: fmt = S_config_mail::DATE_DD_SLASH_MM_SLASH_YY; break;
      case S_config_mail::DATE_DD_DOT_MM_DOT: fmt = S_config_mail::DATE_DD_DOT_MM_DOT_YY; break;
      case S_config_mail::DATE_DD_DASH_MM: fmt = S_config_mail::DATE_DD_DASH_MM_DASH_YY; break;
      }
   }
   switch(fmt){
   case S_config_mail::DATE_MM_SLASH_DD_SLASH_YY:
      if(!force_short){
         str.Format(L"%/%/'#02%") <<(int)m <<(int)d <<(int)y;
         break;
      }
                              //flow...
   case S_config_mail::DATE_MM_SLASH_DD: str<<m <<L'/' <<d; break;

   case S_config_mail::DATE_MM_DASH_DD_DASH_YY:
      if(!force_short){
         str.Format(L"%-%-'#02%") <<(int)m <<(int)d <<(int)y;
         break;
      }
                              //flow...
   case S_config_mail::DATE_MM_DASH_DD: str<<m <<L'-' <<d; break;

   case S_config_mail::DATE_DD_SLASH_MM_SLASH_YY:
      if(!force_short){
         str.Format(L"%/%/'#02%") <<(int)d <<(int)m <<(int)y;
         break;
      }
                              //flow...
   case S_config_mail::DATE_DD_SLASH_MM: str<<d <<L'/' <<m; break;

   case S_config_mail::DATE_DD_DASH_MM_DASH_YY:
      if(!force_short){
         str.Format(L"%-%-'#02%") <<(int)d <<(int)m <<(int)y;
         break;
      }
                              //flow...
   case S_config_mail::DATE_DD_DASH_MM: str<<d <<L'-' <<m; break;

   case S_config_mail::DATE_DD_DOT_MM_DOT_YY:
      if(!force_short){
         str.Format(L"%.%.'#02%") <<(int)d <<(int)m <<(int)y;
         break;
      }
                              //flow...
   case S_config_mail::DATE_DD_DOT_MM_DOT: str<<d <<L'.' <<m <<L'.';
      break;

   case S_config_mail::DATE_YY_DASH_MM_DASH_DD:
      if(!force_short){
         str.Format(L"'#02%-#02%-#02%") <<(int)y <<(int)m <<(int)d;
      }else
         str.Format(L"#02%-#02%") <<(int)m <<(int)d;
      break;
   }
}

//----------------------------

dword C_mail_client::GetNextEnabledWorkTime() const{
   S_date_time dt;
   dt.GetCurrent();
   dword curr_sec = dt.sort_value;

   dword week_day = ((curr_sec/(60*60*24))+1)%7;

   dt.hour = 0; dt.minute = 0; dt.second = 0;
   dword alarm_time = dt.GetSeconds();
   curr_sec -= alarm_time;
   if(curr_sec > dword(config_mail.work_time_beg)*60){
      alarm_time += 24*60*60; //tomorrow
      if(++week_day==7)
         week_day = 0;
   }
   alarm_time += config_mail.work_time_beg*60;
                        //check enabled day
   while(!(config_mail.work_days&(1<<week_day))){
                        //next day
      alarm_time += 24*60*60;
      if(++week_day==7)
         week_day = 0;
   }
   return alarm_time;
}

//----------------------------

void C_mail_client::ManageTimer(){

   bool is_work_hour = config_mail.IsWorkingTime();
   dword need_timer_freq_sec = 0;
   if(simple_snd_plr)
      need_timer_freq_sec = 1;
   else if(config_mail.auto_check_time && is_work_hour){
      need_timer_freq_sec = 10;
      if(config_mail.auto_check_time==9990)
         need_timer_freq_sec = 10;
   }else
   if(IsImapIdleConnected()){
      need_timer_freq_sec =
#ifdef _DEBUG
          5;
#else
         30;
#endif
   }else
   if(
      !(config.flags&config.CONF_KEEP_CONNECTION) &&
      connection){
      need_timer_freq_sec = 30;
   }
   tick_was_working_time = is_work_hour;
   if(!is_work_hour && !exiting){
      if(!work_time_alarm && config_mail.work_times_set && config_mail.work_days){
                              //schedule alarm for starting work hours
         //LOG_RUN("CreateAlarm");
         work_time_alarm = CreateAlarm(GetNextEnabledWorkTime(), NULL);
      }
   }else{
      if(work_time_alarm){
         //LOG_RUN("delete alarm");
         delete work_time_alarm;
         work_time_alarm = NULL;
      }
   }

   //LOG_RUN_N("ManageTimer", need_timer_freq_sec);
   if(curr_timer_freq!=need_timer_freq_sec || exiting){
      curr_timer_freq = need_timer_freq_sec;
      delete timer;
      timer = NULL;
      //LOG_RUN("ManageTimer off");
   }
   if(need_timer_freq_sec && !timer && !exiting){
      //LOG_RUN_N("ManageTimer on", need_timer_freq_sec);
      timer = C_application_base::CreateTimer(1000*need_timer_freq_sec, NULL);
   }
#if defined __SYMBIAN32__
   {
                              //manage "system app" setting
   bool want_service = config_mail.auto_check_time || IsImapIdleConnected() || config_mail.work_times_set;
   system::SetAsSystemApp(want_service);
   }
#endif
}

//----------------------------

void C_mail_client::PerformAutoUpdate(){

                              //if IDLE is authenticating, schedule to update later
   for(int i=NumAccounts(); i--; ){
      S_account &acc = accounts[i];
      if(acc.background_processor.auth_check){
         C_mode_connection_base &mod_con = (C_mode_connection_base&)*acc.background_processor.auth_check;
         mod_con.params.schedule_update_after_auth = true;
         return;
      }
   }
   if(!SafeReturnToAccountsMode())
      return;
   switch(mode->Id()){
   case C_mode_accounts::ID:
      SetModeUpdateMailboxes(true);
      break;
      /*
   case C_mode_folders_list::ID:
      ImapFolders_UpdateFolders((C_mode_folders_list&)*mode, true);
      break;
   case C_mode_mailbox::ID:
      {
         C_mode_mailbox &mod = (C_mode_mailbox&)*mode;
         if(mod.folder && !mod.folder->IsTemp()){
            if(mod.IsImap() || mod.folder->IsInbox()){
               S_connection_params con_params;
               con_params.auto_update = true;
               SetModeConnection(mod.acc, mod.folder, C_mode_connection::ACT_UPDATE_MAILBOX, &con_params);
            }
         }
      }
      break
         */;
   default:
      assert(0);
   }
}

//----------------------------

void C_mail_client::AlarmNotify(C_timer *t, void *context){

   //LOG_RUN("Alarm");
   //Info("alarm"); //!!!
   assert(work_time_alarm);
   delete work_time_alarm; work_time_alarm = NULL;
   if(config_mail.auto_check_time)
      PerformAutoUpdate();
   if(!ConnectEnabledAccountsInBackground(false))
      ManageTimer();
}

//----------------------------

void C_mail_client::TimerTick(C_timer *t, void *context, dword ms){

   if(!context){
      //LOG_RUN("Tick");
      bool manage_timer = false;
      bool is_work_time = config_mail.IsWorkingTime();
      if(connection){
         //LOG_RUN("connection");
         if(IsImapIdleConnected()){
            //LOG_RUN("IsImapIdleConnected");
            bool all_idling = true;
            for(int i=NumAccounts(); i--; ){
               S_account &acc = accounts[i];
               if(acc.use_imap_idle){
                  //LOG_RUN_N(acc.name.ToUtf8(), acc.background_processor.state);
                  if(acc.background_processor.state!=S_account::UPDATE_IDLING)
                     all_idling = false;
                  switch(acc.background_processor.state){
                  case S_account::UPDATE_IDLING:
                     if(is_work_time!=tick_was_working_time){
                        if(!is_work_time){
                              //close push mail
                           ConnectionUpdateState(acc, S_account::UPDATE_DISCONNECTED);
                           acc.CloseIdleConnection();
                           manage_timer = true;
                        }
                     }else{
                              //ping idle
                        const dword PING_TIME = 60000 * acc.imap_idle_ping_time;
                        if((GetTickTime()-acc.background_processor.idle_begin_time) >= PING_TIME){
                                    //ping idle connection
                           ImapIdleUpdateFlags(*acc.background_processor.GetMode(), false);
                        }
                     }
                     break;

                  case S_account::UPDATE_ERROR:
                     {
                                 //try to reconnect account with error after some time
                        //LOG_RUN_N(acc.name.ToUtf8(), acc.background_processor.error_time);
                        //LOG_RUN_N(acc.name.ToUtf8(), GetTickTime());
                        const dword RETRY_TIME = 60000 * 1;
                        if(!acc.background_processor.error_time || (GetTickTime()-acc.background_processor.error_time) >= RETRY_TIME){
                           //if(is_work_time)
                           {
                              LOG_RUN_N("Reconnect account", i);
                              acc.CloseConnection();
                              ConnectAccountInBackground(acc);
                           }//else
                              //manage_timer = true;
                        }
                     }
                     break;
                  }
               }
            }
#ifdef AUTO_CONNECTION_BACK_SWITCH
            alt_test_connection_counter += ms;
            if(alt_test_connection_counter >= 1000*60*10)
            {
               if(all_idling && connection && !alt_test_connection && connection->GetIapIndex()>0){
                  alt_test_connection_counter = 0;
                  alt_test_connection = ::CreateConnection(this, config.iap_id, config.connection_time_out*1000);
                  alt_test_connection->Release();
                  alt_test_socket = CreateSocket(alt_test_connection, (void*)0xffffffff);
                  alt_test_socket->Release();
                  alt_test_socket->Connect("www.lonelycatgames.com", 80, 1000*10);
               }
            }
#endif
         }
         else
         if(!(config.flags&config.CONF_KEEP_CONNECTION)){
            const int disconnect_time = Max(60000 * 3, config.connection_time_out*1000+2000);
            if(connection->GetLastUseTime()+disconnect_time < GetTickTime()){
               //LOG_RUN("CloseConnection");
               CloseConnection();
               manage_timer = true;
            }
         }
      }
      if(simple_snd_plr && simple_snd_plr->IsDone()){
         simple_snd_plr = NULL;
         if(alert_manager.alerts_to_play.size())
            PlayNextAlertSound();
         manage_timer = true;
      }
      if(config_mail.auto_check_time && mode){
         switch(mode->Id()){
         case C_mode_accounts::ID:
         case C_mode_folders_list::ID:
         case C_mode_mailbox::ID:
            if((auto_update_counter += ms) >= int(config_mail.auto_check_time*60*1000) || config_mail.auto_check_time==9990){
               //PlayNewMailSound(); //Info("Check!!!"); //!!!
               auto_update_counter = 0;
               if(is_work_time){
                  LOG_RUN("Scheduled update");
                  PerformAutoUpdate();
               }else
               if(tick_was_working_time){
                              //got out of work hour
                  manage_timer = true;
               }
            }else{
            }
            break;
         }
      }
      /*
      if(!IsFocused() && mode && mode->Id()!=C_mode_accounts::ID){
#ifndef _DEBUG_
         if(GetTickTime()-last_focus_loss_time>1000*60)
#endif
         {
            LOG_RUN("Returning to accounts screen");
            SafeReturnToAccountsMode();
            RedrawScreen();
         }
      }
      */

      if(manage_timer)
         ManageTimer();
      tick_was_working_time = is_work_time;
   }else
      C_client::TimerTick(t, context, ms);
}

//----------------------------

void C_mail_client::ClientTick(C_mode &mod, dword time){

   C_client::ClientTick(mod, time);
}

//----------------------------

void C_mail_client::ProcessInput(S_user_input &ui){

   if(ui.key || (ui.mouse_buttons&MOUSE_BUTTON_1_DOWN)){
      if(simple_snd_plr || alert_manager.alerts_to_play.size()){
         simple_snd_plr = NULL;
         alert_manager.Clear();
         ManageTimer();
      }
   }

   switch(ui.key){
      /*
   case K_VOLUME_DOWN:
   case K_VOLUME_UP:
      if(config_mail.tweaks.use_volume_keys)
         ui.key = ui.key==K_VOLUME_DOWN ? K_CURSORDOWN : K_CURSORUP;
      break;
      */
#ifdef _DEBUG
   case 'x':
                              //simulate IDLE action
      if(ui.key_bits&GKEY_CTRL)
      for(dword i=0; i<NumAccounts(); i++){
         S_account &acc = accounts[i];
         C_mode_connection_imap *mod_idle = acc.background_processor.GetMode();
         if(mod_idle){
            C_socket::t_buffer line;
            static const char cmd[] = "* 7 EXISTS";
            line.Assign(cmd, cmd+StrLen(cmd)+1);
            Cstr_w err;
            ProcessLineImap(*mod_idle, line, err);
         }
      }
      break;
   case 'u':
      if(ui.key_bits&GKEY_CTRL){
         PerformAutoUpdate();
         return;
      }
      break;
#endif
   case ' ':
   case '0':
      if(ui.key_bits&GKEY_SHIFT){
         ToggleUseClientRect();
         return;
      }
      break;
   }
   C_client::ProcessInput(ui);
   auto_update_counter = 0;
   //ManageTimer();
}

//----------------------------

void C_mail_client::InitAfterScreenResize(){

   C_client::InitAfterScreenResize();
   LoadGraphics();
   spec_icons = NULL;
   folder_icons = NULL;
   home_screen_notify.InitAfterScreenResize();
}

//----------------------------

void C_mail_client::ProcessImapIdle(S_account &acc, E_SOCKET_EVENT ev){

   C_mode_connection_imap &mod = *acc.background_processor.GetMode();
   LoadMessages(mod.GetContainer());

   switch(ev){
   case SOCKET_CONNECTED:
      ConnectionUpdateState(mod.acc, S_account::UPDATE_WORKING);
   case SOCKET_DATA_SENT:
      //socket->Receive(mod.IsImapIdle() ? -1 : 0);
      //socket->SetTimeout(mod.IsImapIdle() ? -1 : 0);
      break;

   case SOCKET_SSL_HANDSHAKE:
      {
         C_socket::t_buffer line;
         const char *cmd = "*OK";
         line.Assign(cmd, cmd+StrLen(cmd));
         Cstr_w err;
         ProcessLineImap(mod, line, err);
         if(err.Length())
            ConnectionUpdateState(mod.acc, S_account::UPDATE_ERROR, err);
      }
      break;

   case SOCKET_DATA_RECEIVED:
      {
         //*
                              //determine major mode
         C_mode *m = mode, *mm = NULL;
         while(m && !mm){
            switch(m->Id()){
            case C_mode_mailbox::ID:
            case C_mode_accounts::ID:
            case C_mode_folders_list::ID:
               mm = m;
               break;
            default:
               m = m->GetParent();
            }
         }
         assert(mm);
         mod.saved_parent = mm;
         /**/
         Cstr_w err;
         ConnectionDataReceived(mod, acc.socket, err, false);
         /*
         C_socket::t_buffer line;
         while(acc.socket->GetLine(line)){
            if(!ProcessLineImap(mod, line, err))
               break;
         }
         */
         if(err.Length()){
            ConnectionUpdateState(mod.acc, S_account::UPDATE_ERROR, err);
         }
      }
      break;
   default:
      assert(0);
   }
}

//----------------------------

void C_mail_client::SocketEvent(E_SOCKET_EVENT ev, C_socket *socket, void *context){

   if(dword(context)&3){
#ifdef AUTO_CONNECTION_BACK_SWITCH
      if(dword(context)==0xffffffff){
         assert(socket==alt_test_socket);
         switch(ev){
         case SOCKET_CONNECTED:
            if(alt_test_connection->GetIapIndex()==0){
                              //connection successful, switch now
               alt_test_socket = NULL;
               //connection = alt_test_connection;
               connection = NULL;
               alt_test_connection = NULL;
                              //reconnect all idle accounts, close others
               for(int i=accounts.Size(); i--; ){
                  S_account &acc = accounts[i];
                  if(acc.use_imap_idle){
                     acc.CloseIdleConnection();
                     ConnectAccountInBackground(acc);
                  }else
                     acc.CloseConnection();
               }
               break;
            }
         case SOCKET_ERROR:
         case SOCKET_FINISHED:
            alt_test_connection = NULL;
            alt_test_socket = NULL;
            break;
         }
         return;
      }
#endif
                              //context means account index
      dword acc_i = dword(context)>>2;
      assert(acc_i<NumAccounts());
      S_account &acc = accounts[acc_i];

      if(acc.socket){
         assert(socket==acc.socket);
         if(acc.background_processor.state && acc.background_processor.GetMode()){
            switch(ev){
            case SOCKET_ERROR:
            case SOCKET_FINISHED:
               {
                  Cstr_w err_txt;
                  if(ev==SOCKET_FINISHED)
                     err_txt = L"Connection closed by server";
                  else
                     err_txt = GetErrorName(socket->GetSystemErrorText());
                  ConnectionUpdateState(acc, S_account::UPDATE_ERROR, err_txt);
                  acc.socket = NULL;
                  RedrawScreen();
                  UpdateScreen();
               }
               break;
            case SOCKET_DATA_RECEIVED:
            case SOCKET_DATA_SENT:
            case SOCKET_CONNECTED:
            case SOCKET_SSL_HANDSHAKE:
               ProcessImapIdle(acc, ev);
               break;
            default:
               assert(0);
            }
            UpdateScreen();
            return;
         }
      }

      if(mode && mode->Id()==C_mode_connection::ID){
         C_mode_connection &mod_con = (C_mode_connection&)*mode;
         //if(socket==mod_con.socket){
         //if(socket==acc.socket)
         {
            C_client::SocketEvent(ev, socket, &mod_con);
            //return;
         }//else
            //assert(0);
      }else{
         //assert(0);
         acc.CloseConnection();
      }
      UpdateScreen();
   }else
      C_client::SocketEvent(ev, socket, context);
}

//----------------------------

void C_mail_client::ReadContent(const char *&cp, S_complete_header &hdr) const{

   if(!hdr.content.Parse(cp))
      return;
                              //read params
                              // parameter = attribute "=" value
                              //    attribute = token ;matching of attributes is ALWAYS case-insensitive
                              //    value = token | quoted-string
                              // tspecials = "()<>@,;:\\\"/[]?="
   while(true){
      text_utils::SkipWS(cp);
      if(*cp!=';')
         break;
      ++cp;
      text_utils::SkipWS(cp);
      Cstr_c attr;
      if(!text_utils::ReadToken(cp, attr, " ()<>@,;:\\\"/[]?="))
         break;
      text_utils::SkipWS(cp);
      if(*cp!='=')
         break;
      ++cp;
      text_utils::SkipWS(cp);
      Cstr_c val;
      if(!text_utils::ReadQuotedString(cp, val) && !text_utils::ReadToken(cp, val, " ;"))
         break;
      attr.ToLower();
                              //interpret attr/value pair
      if(attr=="charset"){
         val.ToLower();
         encoding::CharsetToCoding(val, hdr.text_coding);
      }else
      if(attr=="name"){
                           //suggested filename (rfc1341, deprecated)
         DecodeEncodedText(val, hdr.suggested_filename);
      }else
      switch(hdr.content.type){
      case CONTENT_TEXT:
         if(attr=="format"){
            val.ToLower();
            if(val=="flowed")
               hdr.format_flowed = true;
         }else if(attr=="delsp"){
            val.ToLower();
            if(val=="yes")
               hdr.format_delsp = true;
         }
#ifdef _DEBUG_
         else if(attr=="reply-type" || attr=="profile" || attr=="method" || (attr[0]=='x' && attr[1]=='-')){
         }else{
            assert(0);
         }
#endif
         break;
      case CONTENT_MULTIPART:
         if(attr=="boundary")
            hdr.multipart_boundary = val;
#ifdef _DEBUG
         else
         if(attr=="type"){
                              //multipart/related type spec
            //Info("multipart/related type");
            //assert(0);
         }else
         if(attr=="report-type"){
            //Info("multipart/report type");
            //assert(0);
         }else
         if(attr=="protocol"){
         }else
         if(attr=="signed" || attr=="micalg"){
         }else{
            //assert(attr[0]=='x');
         }
#endif
         break;
      case CONTENT_IMAGE:
         break;
         /*
      default:
         assert(ToLower(attr[0])=='x');
         */
      }
   }
}

//----------------------------

void C_mail_client::DecodeEncodedText(const Cstr_c &src, Cstr_w &dst) const{

   int len = src.Length();
   const char *cp = src;
   C_vector<wchar> buf;
   buf.reserve(len*2);

   E_TEXT_CODING coding = COD_WESTERN;
   while(*cp){
      byte c = *cp++;
      if(c=='=' && *cp=='?'){
                              //encoded text
         ++cp;
         Cstr_c charset, encoding;

         static const char specials_1[] = " ()<>@,;:/\".[]?=\x7f";
         if(text_utils::ReadToken(cp, charset, specials_1)){
            if(*cp=='?'){
               ++cp;
               if(text_utils::ReadToken(cp, encoding, specials_1)){
                  if(*cp=='?'){
                     ++cp;
                              //find end of encoded text
                     int i;
                     for(i=0; cp[i]; i++){
                        if(cp[i]=='?' && cp[i+1]=='=')
                           break;
                     }
                     charset.ToLower();
                     encoding::CharsetToCoding(charset, coding);

                     C_vector<char> tmp;
                     tmp.reserve(i);

                     encoding.ToLower();
                     if(encoding=="b"){
                              //base64 encoding
                        C_vector<byte> txt;
                        Cstr_c b64;
                        b64.Allocate(cp, i);
                              //remove \t (header new-line continuation)
                        while(true){
                           int ii = b64.Find('\t');
                           if(ii==-1)
                              break;
                           Cstr_c s = b64.Left(ii);
                           s<<b64.RightFromPos(ii+1);
                           b64 = s;
                        }
                        DecodeBase64(b64, b64.Length(), txt);
                        tmp.insert(tmp.end(), (const char*)txt.begin(), (const char*)txt.end());
                        cp += i;
                     }else
                     if(encoding=="q"){
                        while(i--){
                           byte c1 = (byte)*cp++;
                           switch(c1){
                           case '=':
                              if(i>=2){
                                 i -= 2;
                                 dword dw;
                                 if(!text_utils::ScanHexByte(cp, dw))
                                    continue;
                                 c1 = byte(dw);
                              }
                              break;
                           case '&':
                              if(*cp=='#'){
                                 ++cp;
                                 --i;
                                 const char *cps = cp;
                                 int wc;
                                 if(text_utils::ScanDecimalNumber(cp, wc)){
                                    i -= cp-cps;
                                    if(*cp=='=' && cp[1]=='3' && ToLower(cp[2])=='b'){
                                       cp += 3;
                                       i -= 3;
                                    }
                                    if(wc<0 || wc>=256){
                                             //explicit wide-char; todo: encode back to original coding, or flush buffer and add this char to buf
                                       assert(0);
                                       continue;
                                    }
                                    c1 = byte(wc);
                                 }
                              }
                              break;
                           case '_':
                              c1 = ' ';
                              break;
                           }
                           tmp.push_back(c1);
                        }
                     }else{
                              //unrecognized encoding, use as-is
                        tmp.insert(tmp.end(), cp, cp+i);
                        cp += i;
                        coding = COD_WESTERN;
                     }
                     tmp.push_back(0);
                              //decode now
                     Cstr_w s;
                     ConvertMultiByteStringToUnicode(tmp.begin(), coding, s);
                              //remove invalid characters
                     for(dword j=0; j<s.Length(); j++){
                        if(word(s[j]) < ' ')
                           s.At(j) = '?';
                     }
                              //add to buf
                     buf.insert(buf.end(), s, s+s.Length());

                              //skip end '?='
                     if(*cp)
                        cp += 2;
                     text_utils::SkipWS(cp);
                     /* don't accept any space
                              //accept only space separator
                     if(cp[-1]==' ')
                        --cp;
                     */
                  }
               }
            }
         }
                              //add single space
         if(*cp==' ')
            buf.push_back(' ');
         text_utils::SkipWS(cp);
      }else
      if(c=='&' && *cp=='#'){
                              //numberic character reference
         ++cp;
         int wc;
         if(text_utils::ScanDecimalNumber(cp, wc) && *cp==';'){
            buf.push_back(wchar(wc));
            ++cp;
         }
      }else{
         buf.push_back(c);
      }
   }
                              //remove last redundant space
   if(buf.size() && buf.back()==' ')
      buf.pop_back();

   dst.Allocate(buf.begin(), buf.size());
}

//----------------------------

void C_mail_client::ReadContentDisposition(const char *&cp, S_complete_header &hdr) const{

                              //disposition = "Content-Disposition" ":" disposition-type *(";" disposition-parm)

                              //disposition-type = "inline" | "attachment" | extension-token ; values are not case-sensitive
                              // extension-token = ietf-token / x-token
   if(text_utils::CheckStringBegin(cp, "inline")){
      hdr.content_disposition = DISPOSITION_INLINE;
   }else
   if(text_utils::CheckStringBegin(cp, "attachment")){
      hdr.content_disposition = DISPOSITION_ATTACHMENT;
   }else{
      assert(0);
                              //ignore, and make it attachment
      Cstr_c str;
      text_utils::ReadWord(cp, str, text_utils::specials_rfc_822);
      hdr.content_disposition = DISPOSITION_ATTACHMENT;
   }

                              //disposition-parm = filename-parm | creation-date-parm | modification-date-parm |
                              //    read-date-parm | size-parm | parameter
                              // filename-parm = "filename" "=" value
                              // creation-date-parm = "creation-date" "=" quoted-date-time
                              // modification-date-parm = "modification-date" "=" quoted-date-time
                              // read-date-parm = "read-date" "=" quoted-date-time
                              // size-parm = "size" "=" 1*DIGIT
                              // quoted-date-time := quoted-string ; contents MUST be an RFC 822 `date-time', numeric timezones (+HHMM or -HHMM) MUST be used
   while(true){
      text_utils::SkipWS(cp);
      if(*cp!=';')
         break;
      ++cp;
      text_utils::SkipWS(cp);
      Cstr_c attr;
      if(!text_utils::ReadToken(cp, attr, " ()<>@,;:\\\"/[]?="))
         break;
      text_utils::SkipWS(cp);
      if(*cp!='=')
         break;
      ++cp;
      text_utils::SkipWS(cp);
      Cstr_c val;
      if(!text_utils::ReadQuotedString(cp, val) && !text_utils::ReadToken(cp, val, " ;"))
         break;
      attr.ToLower();
      if(attr=="filename")
         DecodeEncodedText(val, hdr.suggested_filename);
#ifdef _DEBUG
      else
      if(attr=="size" || attr=="creation-date" || attr=="modification-date"){
      }
#endif
      else{
         assert(0);
      }
   }
}

//----------------------------

bool ParseRecipients(const char *cp, C_vector<Cstr_c> &addresses, int *err_l, int *err_r){

   /*
                              //convert to single-char
   C_buffer<char> buf;
   buf.Resize(StrLen(str)+1);
   char *tmp = buf.Begin(), *cp = tmp;
   while(*str){
      wchar c = *str++;
                              //unicode chars are converted to something invalid
      if(c>=256)
         c = '*';
      *cp++ = char(c);
   }
   *cp = 0;
   */
   //const char *cp1 = tmp;
   const char *cp1 = cp;
   text_utils::SkipWS(cp1);
   while(*cp1){
      Cstr_c addr;
      if(!ReadAddressSpec(cp1, addr)){
                              //error, mark it
         if(err_l && err_r){
            *err_l = cp1 - cp;
            text_utils::ReadToken(cp1, addr, " ,");
            *err_r = cp1 - cp;
         }
         return false;
      }
      addresses.push_back(addr);
      text_utils::SkipWS(cp1);
      if(*cp1==','){
         ++cp1;
         text_utils::SkipWS(cp1);
      }
   }
   return true;
}

//----------------------------

static const char mail_keywords[] =
   "subject:\0"
   "from:\0"
   "date:\0"
   "received:\0"
   "to:\0"
   "cc:\0"
   "message-id:\0"
   "in-reply-to:\0"
   "mime-version:\0"
   "content-type:\0"
   "content-transfer-encoding:\0"
   "content-disposition:\0"
   "content-id:\0"
   "importance:\0"
   "x-priority:\0"
   "x-spam-score:\0"
   "reply-to:\0"
   "references:\0"
   ;

enum{
   KW_SUBJECT,
   KW_FROM,
   KW_DATE,
   KW_RECEIVED,
   KW_TO,
   KW_CC,
   KW_MSG_ID,
   KW_IN_REPLY_TO,
   KW_MIME_VER,
   KW_CONTENT_TYPE,
   KW_CONTENT_ENCODING,
   KW_CONTENT_DISPOSITION,
   KW_CONTENT_ID,
   KW_IMPORTANCE,
   KW_X_PRIORITY,
   KW_X_SPAM_SCORE,
   KW_REPLY_TO,
   KW_REFERENCES,
};

//----------------------------
// text = <any CHAR, including bare CR & bare LF, but NOT including CRLF>
void C_mail_client::ReadText(const char *&cp, Cstr_w &str) const{

   text_utils::SkipWS(cp);
   for(int i=0; ; i++){
      char c = cp[i];
      if(!c || c=='\n'){
         char *buf = new(true) char[i+1];
                              //copy there all characters except control ones
         char *bb = buf;
         for(int j=0; j<i; j++){
            byte c1 = cp[j];
            if(c1 >= ' ')
               *bb++ = c1;
            else
            if(c1=='\t')
               *bb++ = '\t';
            else
               assert(0);
         }
         *bb = 0;
         Cstr_c tmp = buf;
         DecodeEncodedText(tmp, str);
         delete[] buf;
         cp += i;
         break;
      }
   }
}

//----------------------------
// sub-domain = domain-ref | domain-literal
//    domain-ref = token
//    domain-literal = "[" *(dtext | quoted-pair) "]"
//       dtext = <any CHAR excluding "[", "]", "\" & CR, & including linear-white-space>
static bool ReadSubDomain(const char *&cp, Cstr_c &str){

   if(*cp=='['){
                              //domain-literal
      //assert(0);  //todo
      return false;
   }
   return text_utils::ReadToken(cp, str, text_utils::specials_rfc_822);
}

//----------------------------
// domain = sub-domain *("." sub-domain)
static bool ReadDomain(const char *&cp, Cstr_c &str){

   const char *save_cp = cp;
   if(!ReadSubDomain(cp, str))
      return false;
   while(*cp=='.'){
      ++cp;
      Cstr_c tmp;
      if(!ReadSubDomain(cp, tmp)){
         cp = save_cp;
         return false;
      }
      str<<'.';
      str<<tmp;
   }
   return true;
}

//----------------------------
// addr-spec = local-part "@" domain (global address).
bool ReadAddressSpec(const char *&cp, Cstr_c &str){

   const char *save_cp = cp;
   Cstr_c local_part;
   Cstr_c domain;
                              //local-part = word *("." word)
   if(!text_utils::ReadToken(cp, local_part, text_utils::specials_rfc_822))
      return false;
   while(*cp=='.'){
      ++cp;
      Cstr_c tmp;
      if(!text_utils::ReadToken(cp, tmp, text_utils::specials_rfc_822))
         goto fail;
      local_part<<'.';
      local_part<<tmp;
   }
   if(*cp!='@')
      goto fail;
   ++cp;
   if(!ReadDomain(cp, domain))
      goto fail;
                              //construct entire address
   str = local_part;
   str<<'@';
   str<<domain;
   str.Build();
   return true;
fail:
   cp = save_cp;
   return false;
}

//----------------------------
// encoding = "Content-Transfer-Encoding" ":" mechanism
static bool ReadContentEncoding(const char *&cp, E_CONTENT_ENCODING &encoding){

                              //mechanism = "7bit" | "8bit" | "binary" | "quoted-printable" | "base64" | ietf-token / x-token
   if(text_utils::CheckStringBegin(cp, "7bit")){
      encoding = ENCODING_7BIT;
   }else
   if(text_utils::CheckStringBegin(cp, "8bit") || text_utils::CheckStringBegin(cp, "utf-8")){
      encoding = ENCODING_8BIT;
   }else
   if(text_utils::CheckStringBegin(cp, "binary")){
      encoding = ENCODING_BINARY;
   }else
   if(text_utils::CheckStringBegin(cp, "quoted-printable")){
      encoding = ENCODING_QUOTED_PRINTABLE;
   }else
   if(text_utils::CheckStringBegin(cp, "base64")){
      encoding = ENCODING_BASE64;
   }else{
      //assert(0);
   }
   return true;
}

//----------------------------

static int FormatMailHeader(char *buf, const char *src, int size){

   char *dst = buf;

   const char *cp_end = src + size;
   while(src<cp_end){
      char c = *src++;
      if(c=='\n'){
                              //check if next line is tabbed (continued)
         char c1 = *src;
         if(c1==' ' || c1=='\t'){
            text_utils::SkipWS(src);
                              //replace continuation by single tab (not space, to distinquish that it is continuation)
            c = '\t';
         }
      }
      *dst++ = c;
   }
   *dst = 0;
   return dst - buf;
}

//----------------------------

static void ReplaceTabsBySpaces(Cstr_c &s){
   for(int i=s.Length(); i--; ){
      char &c = s.At(i);
      if(c=='\t')
         c = ' ';
   }
}

//----------------------------

bool C_mail_client::ParseMailHeader(const char *cp, int hdr_size, S_complete_header &hdr) const{

   Cstr_c headers;
   headers.Allocate(NULL, hdr_size+1);
   hdr_size = FormatMailHeader(&headers.At(0), cp, hdr_size);
   hdr.complete_headers = headers;
   cp = headers;

   Cstr_c in_reply_to;
   while(*cp){
      int ki = text_utils::FindKeyword(cp, mail_keywords);
      text_utils::SkipWS(cp);
      switch(ki){
      case KW_SUBJECT:
         {
            Cstr_w s;
            ReadText(cp, s);
            hdr.subject.ToUtf8(s);
            ReplaceTabsBySpaces(hdr.subject);
         }
         break;
      case KW_FROM:
         {
            Cstr_w s;
            ReadAddress(cp, s, hdr.sender.email);
            hdr.sender.display_name.ToUtf8(s);
         }
         break;

      case KW_DATE:
                              //get if we don't have it yet (from 'received' field)
         if(!hdr.date){
            S_date_time dt;
            text_utils::ReadDateTime_rfc822_rfc850(cp, dt);
            hdr.date = dt.GetSeconds();
         }
         break;

      case KW_RECEIVED:
         {
                              //find last ';' on line
            int last_i = -1;
            for(int i=0; cp[i] && cp[i]!='\n'; i++){
               if(cp[i]==';')
                  last_i = i+1;
            }
            if(last_i!=-1){
               cp += last_i;
               text_utils::SkipWS(cp);
               S_date_time dt;
                              //it may fail, we can go without this date
               bool miss_tz;
               if(text_utils::ReadDateTime_rfc822_rfc850(cp, dt, false, &miss_tz) && !miss_tz)
                  hdr.date = dt.GetSeconds();
               break;
            }
         }
         break;

      case KW_REPLY_TO:
         {
            Cstr_w name;
            ReadAddress(cp, name, hdr.reply_to_email);
         }
         break;
      case KW_TO:
      case KW_CC:
         {
            Cstr_c &emails = (ki==KW_TO) ? hdr.to_emails : hdr.cc_emails;
            Cstr_c &names = (ki==KW_TO) ? hdr.to_names : hdr.cc_names;
            while(true){
               text_utils::SkipWS(cp);
               Cstr_w name;
               Cstr_c email;
               if(!ReadAddress(cp, name, email))
                  break;
               if(emails.Length()){
                  emails<<", ";
                  names<<", ";
               }
               emails<<email;
               if(!name.Length())
                  name.Copy(email);
               names<<name.ToUtf8();

               text_utils::SkipWS(cp);
               if(*cp==',')
                  ++cp;
            }
         }
         break;
      case KW_MSG_ID:
      case KW_IN_REPLY_TO:
         {
            while(*cp && *cp!='\n' && *cp!='<') ++cp;
            if(*cp=='<'){
               ++cp;
               Cstr_c id;
               text_utils::ReadWord(cp, id, ">\n\t");
               if(hdr.flags&hdr.MSG_DRAFT){
                  if(ki==KW_IN_REPLY_TO)
                     hdr.message_id = id;
               }else{
                  if(ki==KW_MSG_ID)
                     hdr.message_id = id;
                  else{
                     in_reply_to.Clear();
                     in_reply_to<<'<' <<id <<'>';
                  }
               }
            }
         }
         break;
      case KW_REFERENCES:
         {
            while(*cp && *cp!='\n' && *cp!='<') ++cp;
            const char *eol = cp;
            for(; *eol && *eol!='\n'; ++eol);
            while(eol>cp && text_utils::IsSpace(eol[-1]))
               --eol;
            hdr.references.Allocate(cp, eol-cp);
            ReplaceTabsBySpaces(hdr.references);
         }
         break;
      case KW_MIME_VER:
         //if(cp[0]=='1' && cp[1]=='.' && cp[2]=='0')
            //is_mime = true;
         break;
      case KW_CONTENT_TYPE:
         ReadContent(cp, hdr);
         break;
      case KW_CONTENT_ENCODING:
         ReadContentEncoding(cp, hdr.content_encoding);
         break;
      case KW_CONTENT_DISPOSITION:
         ReadContentDisposition(cp, hdr);
         break;
      case KW_CONTENT_ID:
                              //msg-id      =  "<" addr-spec ">"
                              // sometimes also <token> or token
         text_utils::SkipWS(cp);
         if(*cp=='<'){
            ++cp;
            if(!ReadAddressSpec(cp, hdr.content_id) || *cp!='>')
               text_utils::ReadWord(cp, hdr.content_id, text_utils::specials_rfc_822);
         }else{
                              //unofficial content id
            text_utils::ReadWord(cp, hdr.content_id, text_utils::specials_rfc_822);
         }
         break;
      case KW_IMPORTANCE:
         {
            Cstr_c val;
            if(text_utils::ReadWord(cp, val, text_utils::specials_rfc_822)){
               val.ToLower();
               if(val=="high")
                  hdr.flags |= hdr.MSG_PRIORITY_HIGH;
               else
               if(val=="low")
                  hdr.flags |= hdr.MSG_PRIORITY_LOW;
            }
         }
         break;
      case KW_X_PRIORITY:
         {
            Cstr_c val;
            if(text_utils::ReadWord(cp, val, text_utils::specials_rfc_822)){
               int n;
               if(val>>n){
                  if(n==1)
                     hdr.flags |= hdr.MSG_PRIORITY_HIGH;
                  else
                  if(n>3)
                     hdr.flags |= hdr.MSG_PRIORITY_LOW;
               }
            }
         }
         break;

      case KW_X_SPAM_SCORE:
         {
            Cstr_c val;
            if(text_utils::ReadWord(cp, val, " \n\t")){
               const char *cp1 = val;
               if(text_utils::ScanDecimalNumber(cp1, hdr.x_spam_score)){
                  hdr.x_spam_score *= 100;
                  if(*cp1=='.'){
                     int c = *++cp1;
                     if(text_utils::IsDigit(char(c))){
                        c -= '0';
                        c *= 10;
                        if(hdr.x_spam_score<0)
                           hdr.x_spam_score -= c;
                        else
                           hdr.x_spam_score += c;
                     }
                  }
               }
            }
         }
         break;
      }
                              //go to next line
      while(*cp){
         if(*cp++=='\n')
            break;
      }
   }
                              //fix references, remove message-id if appended at end
   if(hdr.references.Length() && hdr.message_id.Length()){
      int last_lt = hdr.references.FindReverse('<');
      if(last_lt!=-1){
         Cstr_c msg_id;
         msg_id<<'<' <<hdr.message_id <<'>';
         Cstr_c last_ref = hdr.references.Mid(last_lt, msg_id.Length());
         if(last_ref==msg_id){
                              //cut it off
            while(last_lt && text_utils::IsSpace(hdr.references[last_lt-1])) --last_lt;
            hdr.references = hdr.references.Left(last_lt);
         }
      }
   }
   if(in_reply_to.Length()){
                              //append to references if it's not there yet
      bool append = true;
      int last_lt = hdr.references.FindReverse('<');
      if(last_lt!=-1){
         Cstr_c last_ref = hdr.references.Mid(last_lt, in_reply_to.Length());
         append = (last_ref!=in_reply_to);
      }
      if(append){
         if(hdr.references.Length())
            hdr.references<<' ';
         hdr.references<<in_reply_to;
      }
   }
   return true;
}

//----------------------------

const C_mail_client::S_rule *C_mail_client::DetectRule(const S_account &acc, const S_complete_header &hdr, const C_message_container *fld) const{

   for(dword i=0; i<rules.Size(); i++){
      const S_rule &rul = rules[i];
                              //ignore inactive rules
      if(!(rul.flags&rul.FLG_ACTIVE))
         continue;

      bool use_or = (rul.flags&rul.FLG_OP_OR);
      bool rule_ok = !use_or;
                              //check all conditions
      int num_c = rul.NumConds();
      for(int ci=0; ci<num_c; ci++){
         const S_rule::S_condition &cond = rul.conds[ci];
         switch(cond.cond){
         case S_rule::S_condition::SUBJECT_BEGINS: rule_ok = cond.CheckStringMatch(hdr.subject.FromUtf8(), true); break;
         case S_rule::S_condition::SUBJECT_CONTAINS: rule_ok = cond.CheckStringMatch(hdr.subject.FromUtf8(), false); break;
         case S_rule::S_condition::TO_CONTAINS:
            rule_ok = cond.CheckStringMatch(hdr.to_emails, false) || cond.CheckStringMatch(hdr.to_names, false);
            break;
         case S_rule::S_condition::SENDER_BEGINS:
            rule_ok = cond.CheckStringMatch(hdr.sender.display_name.FromUtf8(), true);
            if(!rule_ok)
               rule_ok = cond.CheckStringMatch(hdr.sender.email, true);
            break;
         case S_rule::S_condition::SENDER_CONTAINS:
            rule_ok = cond.CheckStringMatch(hdr.sender.display_name.FromUtf8(), false);
            if(!rule_ok)
               rule_ok = cond.CheckStringMatch(hdr.sender.email, false);
            break;
         case S_rule::S_condition::SIZE_LESS:
         case S_rule::S_condition::SIZE_MORE:
            rule_ok = (hdr.size < dword(cond.size*1024));
            if(cond.cond==cond.SIZE_MORE)
               rule_ok = !rule_ok;
            break;
         case S_rule::S_condition::AGE_LESS:
         case S_rule::S_condition::AGE_MORE:
            {
               S_date_time curr; curr.GetCurrent();
               int age_in_days = (curr.GetSeconds() - hdr.date) / (60*60*24);
               rule_ok = (age_in_days < cond.size);
               if(cond.cond==cond.AGE_MORE)
                  rule_ok = !rule_ok;
            }
            break;
         case S_rule::S_condition::SPAM_SCORE_LESS:
         case S_rule::S_condition::SPAM_SCORE_MORE:
            rule_ok = (hdr.x_spam_score < cond.size*100);
            if(cond.cond==cond.SPAM_SCORE_MORE)
               rule_ok = !rule_ok;
            break;
         case S_rule::S_condition::SENDER_IN_CONTACTS:
         case S_rule::S_condition::SENDER_NOT_IN_CONTACTS:
            {
               S_contact con;
               rule_ok = FindContactByEmail(hdr.sender.email, con);
               if(cond.cond==S_rule::S_condition::SENDER_NOT_IN_CONTACTS)
                  rule_ok = !rule_ok;
            }
            break;
         case S_rule::S_condition::SENDER_HEADER_CONTAINS:
            {
               Cstr_c s = cond.param.ToUtf8();
               int di = s.Find(':');
               if(di==-1)
                  break;
               int lp = di;
               while(lp && text_utils::IsSpace(s[lp-1]))
                  --lp;
               Cstr_c hdr_name = s.Left(lp);
               while(hdr_name.Length() && hdr_name[0]==' ')
                  hdr_name = hdr_name.RightFromPos(1);
               hdr_name.ToLower();
               hdr_name<<':';
               hdr_name<<'\0';
                              //find matching header field
               const char *cp = hdr.complete_headers;
               while(*cp){
                  int ki = text_utils::FindKeyword(cp, hdr_name);
                  text_utils::SkipWS(cp);
                  const char *eol;
                  for(eol=cp; *eol; ){
                     if(*eol++=='\n')
                        break;
                  }
                  if(ki==0){
                     const char *cp_what = s+di+1;
                     text_utils::SkipWS(cp_what);
                     Cstr_w what; what.Copy(cp_what);
                     while(what.Length() && what[what.Length()-1]==' ')
                        what = what.Left(what.Length()-1);
                     Cstr_c where; where.Allocate(cp, eol-cp);
                     if(cond.CheckStringMatch(what, where.FromUtf8(), false)){
                        rule_ok = true;
                        break;
                     }
                  }
                  cp = eol;
               }
            }
            break;
         default:
            rule_ok = false;
            assert(0);
         }
         if(use_or){
            if(rule_ok)
               break;
         }else{
            if(!rule_ok)
               break;
         }
      }
      if(rule_ok){
                              //check if action really valid
         bool valid = true;
         switch(rul.action){
         case S_rule::ACT_MOVE_TO_FOLDER:
            if(!fld)
               valid = false;
            else
            if(acc.GetFullFolderName(*fld)==rul.action_param)
               valid = false;
            break;
         }
         if(valid)
            return &rul;
      }
   }
   return NULL;
}

//----------------------------

#ifdef __SYMBIAN32__
                              //Symbian UID
#ifdef __SYMBIAN_3RD__
#pragma data_seg(".SYMBIAN")
SYMBIAN_UID(0xa000b86f, 0x1e000)
#else
#pragma data_seg(".E32_UID")
SYMBIAN_UID(0x20009599)
#endif

#pragma data_seg()
#ifdef __WINS__
extern const wchar app_path[] = L"Common";
#endif

#endif

//----------------------------
//----------------------------

void C_attach_browser::Init(){

   const int arrow_size = (app.fdb.line_spacing/2) | 1;
   int clip_sx = app.msg_icons[app.MESSAGE_ICON_ATTACH_CLIP]->SizeX();
   int icon_size = rc.sy-8;
   int arrow_y = (rc.y + 1 + icon_size+4) - arrow_size - 2;

   rc_arrow[0] = S_rect(rc.x + 2, arrow_y, arrow_size+1, arrow_size+2);
   rc_arrow[1] = rc_arrow[0];
   rc_arrow[1].x = rc.x+2+Max(arrow_size, clip_sx)+4 + (icon_size+4)*ATTACH_BROWSER_NUM_ICONS + 3;

   rc_icons = S_rect(rc.x + Max(arrow_size, clip_sx) + 6, rc.y + 2, (icon_size+4)*ATTACH_BROWSER_NUM_ICONS, icon_size+4);
}

//----------------------------

bool C_attach_browser::Tick(const S_user_input &ui, int num_atts, bool &redraw, bool &sel_changed, bool &popup_touch_menu){

   sel_changed = false;
   popup_touch_menu = false;
#ifdef USE_MOUSE
   if(ui.mouse_buttons&(MOUSE_BUTTON_1_DOWN|MOUSE_BUTTON_1_DRAG|MOUSE_BUTTON_1_UP)){
      if(ui.CheckMouseInRect(rc_icons)){
         if(ui.mouse_buttons&(MOUSE_BUTTON_1_DOWN|MOUSE_BUTTON_1_UP)){
            S_rect rci = rc_icons;
            rci.sx = rci.sy;
            rci.x -= scroll_offset%rc_icons.sy;
            for(int ai = scroll_offset/rc_icons.sy; rc.x<rc_icons.Right() && ai<num_atts; ++ai){
               if(ui.CheckMouseInRect(rci)){
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                     popup_touch_menu = true;
                     touch_down_selection = selection;
                     if(selection!=ai){
                        selection = ai;
                        sel_changed = true;
                        MakeSelectionVisible();
                        redraw = true;
                     }
                     beg_drag_offset = scroll_offset;
                  }
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
                     beg_drag_offset = -1;
                     int ts = touch_down_selection;
                     touch_down_selection = -1;
                     if(ts==ai)
                        return true;
                  }
                  break;
               }
               rci.x += rci.sy;
            }
         }
      }
      if(beg_drag_offset!=-1 && (ui.mouse_buttons&MOUSE_BUTTON_1_DRAG)){
         int so = scroll_offset - ui.mouse_rel.x;
         so = Max(0, Min((num_atts-ATTACH_BROWSER_NUM_ICONS)*rc_icons.sy, so));
         if(scroll_offset!=so){
            scroll_offset = so;
            if(Abs(beg_drag_offset-scroll_offset) >= app.fdb.cell_size_x*2)
               touch_down_selection = -1;
            redraw = true;
         }
      }
   }
#endif//USE_MOUSE
   switch(ui.key){
   case K_CURSORLEFT:
      if(selection){
         --selection;
         sel_changed = true;
         redraw = true;
         MakeSelectionVisible();
      }
      break;
   case K_CURSORRIGHT:
      if(selection < num_atts-1){
         ++selection;
         redraw = true;
         sel_changed = true;
         MakeSelectionVisible();
      }
      break;
   }
   return false;
}

//----------------------------

void C_attach_browser::MakeSelectionVisible(){
   scroll_offset = Min(scroll_offset, selection*rc_icons.sy);
   scroll_offset = Max(scroll_offset, (selection-ATTACH_BROWSER_NUM_ICONS+1)*rc_icons.sy);
}

//----------------------------

void C_attach_browser::Draw(const C_buffer<S_attachment> &attachments, C_fixed cursor_alpha) const{

   int num_atts = attachments.Size();
   if(!num_atts)
      return;

   dword col_text = app.GetColor(app.COL_TEXT);
                              //clear and prepare background
   {
      const dword c0 = app.GetColor(app.COL_SHADOW), c1 = app.GetColor(app.COL_HIGHLIGHT);
      app.DrawOutline(rc, c0, c1);
   }
   app.ClearWorkArea(rc);
   app.FillRect(rc, 0x80808080);

                              //draw attachment clip icon
   app.msg_icons[app.MESSAGE_ICON_ATTACH_CLIP]->DrawSpecial(rc.x + 3, rc.y+1, NULL, C_fixed::One(), col_text);

                              //draw icons background
   {
      const dword c0 = 0x80000000, c1 = 0xc0ffffff;
      app.DrawOutline(rc_icons, c0, c1);
      app.FillRect(rc_icons, 0x40000000);
   }
                              //draw scroll arrows (and buttons)
   for(int si=2; si--; ){
      if(!si){
         if(!scroll_offset)
            continue;
      }else{
         if(scroll_offset >= (num_atts-ATTACH_BROWSER_NUM_ICONS)*rc_icons.sy)
            continue;
      }
      const S_rect &rc1 = rc_arrow[si];
      app.DrawArrowHorizontal(rc1.x, rc1.y+1, rc1.sx-2, col_text, si);
   }
                              //draw visible icons
   S_rect rci = rc_icons;
   rci.sx = rci.sy;
   rci.x -= scroll_offset%rc_icons.sy;
   app.SetClipRect(rc_icons);
   for(int ai = scroll_offset/rc_icons.sy; rc.x<rc_icons.Right() && ai<num_atts; ++ai){
      const S_attachment &att = attachments[ai];
      Cstr_w ext = text_utils::GetExtension(att.suggested_filename);
      ext.ToLower();
      C_mail_client::E_FILE_TYPE ft = app.DetermineFileType(ext);
      if(ft>C_mail_client::FILE_TYPE_EMAIL_MESSAGE)
         ft = C_mail_client::FILE_TYPE_UNKNOWN;

      bool drawn = false;
      if(ft==C_client::FILE_TYPE_IMAGE && att.IsDownloaded()){
                              //draw image using its contents if possible
         if(!img_thumbnails.size() || img_thumbnails.size()!=attachments.Size())
            img_thumbnails.resize(attachments.Size());
         C_image *img = img_thumbnails[ai];
         S_rect rc1 = rci;
         rc1.Compact(3);
         if(!img){
            C_smart_ptr<C_image> img1 = ((C_client_file_mgr&)app).CreateImageThumbnail(att.filename.FromUtf8(), rc1.sx, rc1.sy, NULL);
            if(!img1){
               img1 = C_image::Create(app);
               img1->Release();
            }
            img = img1;
            img_thumbnails[ai] = img;
                              //try to load image now
            //img->Open(att.filename.FromUtf8(), rc1.sx, rc1.sy);
         }
         if(img->SizeY()){
            int x = rc1.x + (rc1.sx-(int)img->SizeX())/2;
            int y = rc1.y + (rc1.sy-(int)img->SizeY())/2;
            dword ol = 0x40000000;
            app.DrawOutline(S_rect(x, y, img->SizeX(), img->SizeY()), ol);
            img->Draw(x, y);
            drawn = true;
         }
      }
      if(!drawn){
                              //draw thumbnail
         C_fixed alpha(1);
         if(!att.IsDownloaded())
            alpha = C_fixed::Percent(50);
         int index = ft+3;
         int real_sz = app.icons_file->SizeY();
         S_rect rc_src(0, 0, 0, real_sz);
         rc_src.x = rc_src.sy*index*27/22;
         rc_src.sx = rc_src.sy*(index+1)*27/22;
         rc_src.sx -= rc_src.x;
         int draw_offs = (rci.sy-4-real_sz)/2;
         app.icons_file->DrawSpecial(rci.x+draw_offs, rci.y+2+draw_offs, &rc_src, alpha);
      }
                           //draw selection
      if(ai==dword(selection) && cursor_alpha!=C_fixed::Zero()){
         S_rect rc1 = rci;
         rc1.Compact(2);
         dword c0 = MulAlpha(0xffffff00, cursor_alpha.val), c1 = MulAlpha(0xffff0000, cursor_alpha.val);
         app.DrawOutline(rc1, c0, c0);
         rc1.Expand();
         app.DrawOutline(rc1, c1, c1);
      }
      rci.x += rci.sy;
   }
   app.ResetClipRect();

   const S_attachment &sel_att = attachments[selection];
   const dword sx = app.ScrnSX();
   if(!sel_att.IsDownloaded())
      col_text = MulAlpha(col_text, 0x8000);
                              //draw file name
   int tx = rc_icons.Right() + app.fdb.letter_size_x*2;
   const Cstr_w &att_name = sel_att.suggested_filename;
   app.DrawNiceFileName(att_name, tx, rc.y+1, app.UI_FONT_SMALL, col_text, sx - tx - 1);
   tx += app.fdb.letter_size_x*2;
   {
                              //draw attachment index
      Cstr_w s;
      s.Format(L"%/%") <<(selection+1) <<num_atts;
      int yy = rc.y+1 + Min(rc.sy/2, rc.sy-app.fdb.cell_size_y-1);
      app.DrawString(s, tx, yy, app.UI_FONT_BIG, FF_BOLD, col_text, -int(sx - tx));
   }
   {
                              //draw file size
      int file_size = sel_att.file_size;
      if(sel_att.IsDownloaded()){
         C_file ck;
         if(ck.Open(sel_att.filename.FromUtf8()))
            file_size = ck.GetFileSize();
      }
      if(file_size!=-1){
         Cstr_w s = text_utils::MakeFileSizeText(file_size, true, false);
         app.DrawString(s, tx + app.fdb.letter_size_x * 8, rc.y+1+rc.sy/2, app.UI_FONT_SMALL, 0, col_text, -int(sx - tx));
      }
   }
}

//----------------------------

static void CopyRectangle(const word *src, word *dst, int sx, int sy, int src_pitch, int dst_pitch){

   while(sy--){
      MemCpy(dst, src, sx*sizeof(word));
      src += src_pitch;
      dst += dst_pitch;
   }
}

//----------------------------

void C_mail_client::DrawUnreadMailNotify(C_notify_window *mail_notify){

                              //only when inactive
   if(IsFocused())
      return;
   if(!mail_notify)
      return;

#ifdef __SYMBIAN32__

   if(!config_mail.tweaks.show_new_mail_notify || !mail_notify->display_count)
      return;

   const int border = 3;
   const C_image *img = msg_icons[MESSAGE_ICON_NEW];
   S_rect rc(0, 0, 0, 0);
   rc.sx = img->SizeX() + border*2;
   rc.sy = img->SizeY() + border*2;
   rc.sy = Max(rc.sy, fdb.cell_size_y+border*2);

   Cstr_w s; s<<mail_notify->display_count;
   int w = GetTextWidth(s, UI_FONT_BIG, FF_BOLD);
   rc.sx += w + fdb.letter_size_x*3/2;
                              //don't shrink width if it was already bigger
   rc.sx = Max(rc.sx, mail_notify->GetWidth());
   rc.sx = (rc.sx+1) & -2;

   int scrn_sx = ScrnSX();
                              //save contents of backbuffer
   word *bbuf = (word*)GetBackBuffer();
   word *bb_save = new(true) word[rc.sx*rc.sy];
   CopyRectangle(bbuf, bb_save, rc.sx, rc.sy, scrn_sx, rc.sx);
                              //draw into backbuffer
   {
      S_rect rc1 = rc;
      rc1.Compact(1);
#ifdef USE_SYSTEM_SKIN
      if(!config.color_theme){
         DrawDialogBase(rc1, false, &rc1);
      }else
#endif
         FillRect(rc1, GetColor(COL_BACKGROUND));
      DrawOutline(rc1, 0xff000000);

      rc1.Compact(1);
      DrawOutline(rc1, 0xc0ffffff, 0x80000000);
   }
   int s_y = border + (rc.sy-border*2-img->SizeY())/2;
   img->Draw(border, s_y);
   DrawString(s, img->SizeX() + border*2 + fdb.letter_size_x/2, border, UI_FONT_BIG, FF_BOLD, GetColor(COL_TEXT_POPUP));
                              //redraw control
   int x = scrn_sx - Min(ScrnSX(), ScrnSY())/6 - rc.sx;
   int y = 7;
   mail_notify->DrawToBuffer(S_rect(x, y, rc.sx, rc.sy), bbuf, scrn_sx);
                              //restore backbuffer
   CopyRectangle(bb_save, bbuf, rc.sx, rc.sy, rc.sx, scrn_sx);
   delete[] bb_save;
#endif
#ifdef _WIN32_WCE
   mail_notify->ShowNotify();
#endif
}

//----------------------------

void C_mail_client::FlashNewMailLed(){

#if defined __SYMBIAN_3RD__ && defined S60
   if(!IsFocused() && !config_mail.tweaks.no_led_flash){
      if(!led_flash_notify){
         C_unknown *SymbianCreateLedFlash();
         led_flash_notify = SymbianCreateLedFlash();
         if(led_flash_notify)
            led_flash_notify->Release();
      }
   }
#endif
}

//----------------------------

bool C_mail_client::ExportSettings(const wchar *filename){

   const Cstr_w acc_fn = GetAccountsFilename(),
      sigs_fn = GetSignaturesFilename(),
      rul_fn = GetRulesFilename(),
      config_fn = GetConfigFilename();

   C_vector<const wchar*> fnames;
   fnames.push_back(acc_fn);
   C_file tmp;
   if(tmp.Open(sigs_fn))
      fnames.push_back(sigs_fn);
   if(tmp.Open(rul_fn))
      fnames.push_back(rul_fn);
   if(tmp.Open(config_fn))
      fnames.push_back(config_fn);
   tmp.Close();
   return CreateZipArchive(filename, NULL, fnames.begin(), fnames.size(), false);
}

//----------------------------

bool C_mail_client::ImportSettings(const wchar *filename){

   C_zip_package *zip = C_zip_package::Create(filename);
   if(zip){
      const wchar *fn;
      dword len;
      const Cstr_w config_fn = GetConfigFilename();
      const Cstr_w accounts_fn = GetAccountsFilename();
      const Cstr_w rules_fn = GetRulesFilename();
      const Cstr_w sigs_fn = GetSignaturesFilename();
      bool got_accounts = false;

      for(void *h=zip->GetFirstEntry(fn, len); h; h=zip->GetNextEntry(h, fn, len)){
         Cstr_w fn_dst;
         fn_dst<<mail_data_path <<DATA_PATH_PREFIX <<fn;
         if(fn_dst==accounts_fn ||
            fn_dst==rules_fn ||
            fn_dst==sigs_fn ||
            fn_dst==config_fn){
            C_file_zip fl;
            if(fl.Open(fn, zip)){
               C_file fd;
               if(fd.Open(fn_dst, C_file::FILE_WRITE|C_file::FILE_WRITE_CREATE_PATH)){
                  dword sz = fl.GetFileSize();
                  byte *buf = new byte[sz];
                  if(buf){
                     if(fl.Read(buf, sz))
                        fd.Write(buf, sz);
                     delete[] buf;
                  }
                  fd.Close();
               }
               if(fn_dst==accounts_fn)
                  got_accounts = true;
               if(fn_dst==config_fn){
                              //apply new config
                  LoadConfig();
               }
            }
         }
      }
                              //forget cached data
      rules.Clear();
      signatures.Clear();
      zip->Release();
      if(got_accounts){
         for(int i=NumAccounts(); i--; )
            accounts[i].DeleteAllFolders(mail_data_path);
         mode = NULL;
         accounts.Clear();
         FinishConstructWithPass(NULL, true);
      }
      return true;
   }
   return false;
}

//----------------------------

static bool ReadPhrase(const char *&cp, Cstr_c &str){

   bool ok = false;
   Cstr_c tmp;

   while(text_utils::ReadWord(cp, tmp, "<>\x7f")){
      str<<tmp;
      text_utils::SkipWS(cp);
      ok = true;
   }
   return ok;
}

//----------------------------
// route-addr = "<" [route] addr-spec ">"
//    route = 1#("@" domain) ":"
static bool ReadRouteAddress(const char *&cp, Cstr_c &str){

   if(*cp!='<')
      return false;
   const char *save_cp = cp;
   ++cp;
   if(*cp=='@'){
                              //route
      assert(0);
   }
   if(!ReadAddressSpec(cp, str) || *cp!='>'){
      cp = save_cp;
      return false;
   }
   ++cp;
   return true;
}

//----------------------------
// address =  mailbox (one addressee) | group (named list)
bool C_mail_client::ReadAddress(const char *&cp, Cstr_w &name, Cstr_c &email) const{
   
                              //we support 'mailbox' for only

                              //mailbox = addr-spec (simple address) | phrase route-addr (name & addr-spec)
   if(ReadAddressSpec(cp, email))
      return true;
                              //try 2nd form
   Cstr_c phr;
   if(ReadPhrase(cp, phr)){
      DecodeEncodedText(phr, name);
      text_utils::SkipWS(cp);
   }
   return ReadRouteAddress(cp, email);
}

//----------------------------

dword C_mail_client::GetColor(E_COLOR col) const{

   switch(col){
   case COL_AREA:
   case COL_MENU:
   case COL_BACKGROUND: return color_themes[config.color_theme].bgnd;
   case COL_TITLE: return color_themes[config.color_theme].title;
   case COL_SCROLLBAR: return color_themes[config.color_theme].scrollbar;
   case COL_SELECTION: return color_themes[config.color_theme].selection;

   //case COL_TEXT_SELECTED:
   //case COL_TEXT_SELECTED_BGND:
   //case COL_TEXT_INPUT:
   case COL_TEXT:
   case COL_TEXT_HIGHLIGHTED:
   case COL_EDITING_STATE:
   case COL_TEXT_SOFTKEY:
   case COL_TEXT_TITLE:
   case COL_TEXT_POPUP:
   case COL_TEXT_MENU:
   case COL_TEXT_MENU_SECONDARY:
      //Fatal("ct", config.color_theme);
      if(config.color_theme)
         return color_themes[config.color_theme].text_color;
      break;
   }
   return C_client::GetColor(col);
}

//----------------------------
#if defined _WINDOWS || defined WINDOWS_MOBILE
#include <String.h>
namespace win{
#include <Windows.h>
#undef GetDriveType
#undef DRIVE_REMOVABLE
}
#endif

int C_mail_client::GetPossibleDataLocations(C_vector<S_data_location> &paths){

   C_dir dir;
#if defined _WIN32_WCE
#ifdef _WIN32_WCE
   wchar prg_files[MAX_PATH];
#endif
   {
      S_data_location dl;
#ifdef _WIN32_WCE
      win::SHGetSpecialFolderPath(NULL, prg_files, CSIDL_PROGRAM_FILES, false);
      dl.path<<prg_files <<L"\\ProfiMail\\";
#else
      C_file::GetFullPath(L"", dl.path);
#endif
      dl.name = L"Device memory";
      paths.push_back(dl);
   }
   if(dir.ScanMemoryCards()){
      while(true){
         dword atts;
         const wchar *fn = dir.ScanGet(&atts);
         if(!fn)
            break;
         if((atts&C_file::ATT_DIRECTORY) && *fn){
            S_data_location dl;
            dl.path<<'\\' <<fn;
#ifdef _WIN32_WCE
            dl.path<<prg_files;
#endif
            dl.path<<L"\\ProfiMail\\";
            dl.name = fn;
            paths.push_back(dl);
         }
      }
   }
#else
   for(dword i=0; i<dir.GetNumDrives(); i++){
      char l = dir.GetDriveLetter(i);
      C_dir::E_DRIVE_TYPE t = dir.GetDriveType(l);
      switch(t){
      case C_dir::DRIVE_NORMAL:
      case C_dir::DRIVE_REMOVABLE:
      case C_dir::DRIVE_HARD_DISK:
         {
                              //add drive to list
            S_data_location dl;
            dl.path<<ToUpper(l) <<':';
            Cstr_w name;
            dir.GetDriveName(l, name);
            dl.name = dl.path;
            if(name.Length())
               dl.name.AppendFormat(L" (%)") <<name;
            paths.push_back(dl);
         }
         break;
      }
   }
#endif
                              //find which path is currently active
   for(int i=paths.size(); i--; ){
      if(mail_data_path==paths[i].path)
         return i;
   }
   return -1;
}

//----------------------------

void C_mail_client::ReplyWithQuestion(C_message_container &cnt, S_message &msg){

   class C_q: public C_multi_selection_callback{
      C_mail_client &app;
      C_message_container &cnt;
      S_message &msg;
      virtual void Select(int option){
         app.SetModeWriteMail(&cnt, &msg, (option==0), false, (option==1));
      }
   public:
      C_q(C_mail_client &_a, S_message &_msg, C_message_container &_c):
         app(_a),
            msg(_msg),
            cnt(_c)
         {}
   };
   const wchar *const opts[] = { GetText(TXT_REPLY), GetText(TXT_REPLY_ALL) };
   CreateMultiSelectionMode(*this, TXT_REPLY, NULL, opts, 2, new(true) C_q(*this, msg, cnt), true);      
}

//----------------------------

bool C_mail_client::SafeReturnToAccountsMode(){
                              //go back to accounts mode
   while(mode && mode->Id()!=C_mode_accounts::ID){
                              //check if we can close the mode
      switch(mode->Id()){
      case C_mode_connection::ID:
      case C_mode_connection_auth::ID:
      case C_mode_write_mail_base::ID:
      case MODE_ID_PASS_ENTER:
                              //prohibited to close
         RedrawScreen();
         return false;
      }
      //LOG_RUN("Close mode");
      CloseMode(*mode, false);
   }
   return (mode!=NULL);
}

//----------------------------
#ifdef USE_NOKIA_N97_WIDGET

//----------------------------

C_mail_client::C_hs_widget_profimail::~C_hs_widget_profimail(){
   //*
   if(IsInited()){
      //SetItem(C_hs_widget::ITEM_TEXT_2, "ProfiMail is closed");
      Cstr_w s; s<<app.GetText(TXT_CLICK_TO_START);
      SetItem(C_hs_widget::ITEM_TEXT_1, "");
      SetItem(C_hs_widget::ITEM_TEXT_2, s.ToUtf8());
      SetItem(C_hs_widget::ITEM_TEXT_3, "");
      Publish();
   }
   /**/
   Close();
}

//----------------------------

void C_mail_client::C_hs_widget_profimail::Close(){
   //Remove();
   imp = NULL;
   if(lib){
      lib->Close();
      delete lib;
      lib = NULL;
   }
}

//----------------------------

bool C_mail_client::C_hs_widget_profimail::Init(){
/*
   switch(system::GetDeviceId()){
   case 0x20014ddd:           //N97
   case 0x20014dde:           // ''
   case 0x20023766:           //N97 mini
   case 0x20024104:           //5228
   case 0x2002bf91:           //C6-00
   case 0x2002376b:           //5230
   case 0x20023763:           //5235
   case 0x20023764:           //5235
   case 0x20024105:           //5235
   case 0x200227dd:           //X6-00
   case 0x200227de:           //X6-00
   case 0x200227df:           //X6-00
   case 0x2001de9d:           //5530
   case 0x2001de9e:           //5530
      break;
   default:
      return false;
   }
   /**/

   lib = new(true) RLibrary;
   int err = lib->Load(
#ifdef _DEBUG
      _L("hswidget.dll")
#else
      _L("profimailhswidget_free.dll")
#endif
      );
   if(err==0){
      t_CreateHsWidget fp = (t_CreateHsWidget)lib->Lookup(2);
      if(fp){
         imp = fp("ProfiMail", "a000b86f", *this, C_hs_widget::TEMPL_THREEROWS);
         //C_hs_widget::TEMPL_WIDEIMAGE);
         if(imp){
            imp->Release();
            SetMifImage("\\resource\\apps\\profimail.mif");
            SetItem(C_hs_widget::ITEM_TEXT_1, "");
            SetItem(C_hs_widget::ITEM_TEXT_2, "");
            SetItem(C_hs_widget::ITEM_TEXT_3, "");
            Publish();
            return true;
         }
      }
      return true;
   }
   Close();
   return false;
}

//----------------------------

void C_mail_client::C_hs_widget_profimail::Event(E_EVENT ev){
   switch(ev){
   case EVENT_ACTIVATE:
      if(!suspended)
         app.MinMaxApplication(false);     //can't activate itself to foreground!
      activated = true;
      break;
   case EVENT_DEACTIVATE: activated = false; break;
   case EVENT_SUSPEND: suspended = true; break;
   case EVENT_RESUME:
      suspended = false;
      app.home_screen_notify.HsWidgetResume();
      break;
   }
}

//----------------------------

void C_mail_client::C_hs_widget_profimail::ItemSelected(E_ITEM item){

   if(item==ITEM_IMAGE){
      app.MinMaxApplication(false);
   }else{
      app.home_screen_notify.HsWidgetClickField(item-ITEM_TEXT_1);
   }
}

#endif//USE_NOKIA_N97_WIDGET
//----------------------------
