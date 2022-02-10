#include "..\Main.h"
#include "Main_Email.h"

#include <D:\Develope\LibSrc\Android\C++\JNIpp.h>

//----------------------------

static const JNINativeMethod ProfiMail_methods[] = {
   { "GetSignaturesCursor", "()Lcom/lonelycatgames/ProfiMail/SignaturesCursor;", (void*)&C_mail_client::_JniGetSignaturesCursor },
   { "GetAccountsCursor", "()Lcom/lonelycatgames/ProfiMail/AccountsCursor;", (void*)&C_mail_client::_JniGetAccountsCursor },
   { "ResetDataCounters", "()V", (void*)&C_mail_client::_JniResetDataCounters },
   { "NotificationSoundEntered", "(Ljava/lang/String;)V", (void*)&C_mail_client::_JniNotificationSoundEntered },
   { "RulesSoundEntered", "(Ljava/lang/String;)V", (void*)&C_mail_client::_JniRulesSoundEntered },
   { 0 },
};

void C_mail_client::JniRegisterNatives(){
   jni::RegisterNatives("com/lonelycatgames/ProfiMail/ProfiMailApplication", ProfiMail_methods);
}

//----------------------------

#define JB_CURRENT_CLASS C_jni_ProfiMailApplication

JB_DEFINE_WRAPPER_CLASS(
   "com/lonelycatgames/ProfiMail/ProfiMailApplication",
   NoFields
   ,
   Methods
   (StartDataCounters, "StartDataCounters", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V")
   (EditSignatures, "EditSignatures", "()V")
   (pickAttachment, "pickAttachment", "()V")
   (SelectNotificationSound, "SelectNotificationSound", "(Ljava/lang/String;Z)V")
)

void C_jni_ProfiMailApplication::StartDataCounters(const Cstr_w &cs_s, const Cstr_w &cs_r, const Cstr_w &t_s, const Cstr_w &t_r){
   return JB_CALL_THIS(VoidMethod, StartDataCounters, jni::PString::New(cs_s), jni::PString::New(cs_r), jni::PString::New(t_s), jni::PString::New(t_r));
}

void C_jni_ProfiMailApplication::EditSignatures(){ return JB_CALL_THIS(VoidMethod, EditSignatures); }
void C_jni_ProfiMailApplication::PickAttachment(){ return JB_CALL_THIS(VoidMethod, pickAttachment); }
void C_jni_ProfiMailApplication::SelectNotificationSound(const Cstr_w &curr, bool rules){
   JB_CALL_THIS(VoidMethod, SelectNotificationSound, curr.Length() ? jni::PString::New(curr) : NULL, jboolean(rules));
}

#undef JB_CURRENT_CLASS
//----------------------------
#define JB_CURRENT_CLASS C_jni_Account

JB_DEFINE_WRAPPER_CLASS("com/lonelycatgames/ProfiMail/Account",
   Fields
      (name, "name", "Ljava/lang/String;")
      (default_signature, "default_signature", "Ljava/lang/String;")
   ,
   Methods
      (Ctor, "<init>", "()V")
)

//----------------------------
C_jni_Account::C_jni_Account(): jni::Object(JB_NEW(Ctor)){}
//----------------------------
Cstr_w C_jni_Account::GetName() const{
   jni::PString s = jni::PString::Wrap(JB_GET_THIS(ObjectField, name));
   return s->Get();
}
//----------------------------
Cstr_w C_jni_Account::GetDefaultSignature() const{
   jni::PString s = jni::PString::Wrap(JB_GET_THIS(ObjectField, default_signature));
   if(!s)
      return NULL;
   return s->Get();
}
//----------------------------
void C_jni_Account::SetName(const Cstr_w &n){ JB_SET_THIS(ObjectField, name, jni::PString::New(n)); }
void C_jni_Account::SetDefaultSignature(const Cstr_w &b){ JB_SET_THIS(ObjectField, default_signature, jni::PString::New(b)); }

#undef JB_CURRENT_CLASS
//----------------------------
#define JB_CURRENT_CLASS C_jni_ArrayList_base

JB_DEFINE_WRAPPER_CLASS("com/lonelycatgames/ProfiMail/AccountsList",
   NoFields,
   Methods
      (Size, "size", "()I")
      (Add, "add", "(Ljava/lang/Object;)Z")
      (Get, "get", "(I)Ljava/lang/Object;")
      (Ctor, "<init>", "()V")
)

C_jni_ArrayList_base::C_jni_ArrayList_base(): jni::Object(JB_NEW(Ctor)){}
void C_jni_ArrayList_base::Add(jni::PObject o){ JB_CALL_THIS(VoidMethod, Add, o); }
jni::PObject C_jni_ArrayList_base::Get(int pos) const{ return new jni::Object(JB_CALL_THIS(ObjectMethod, Get, pos)); }
int C_jni_ArrayList_base::Size() const{ return JB_CALL_THIS(IntMethod, Size); }

#undef JB_CURRENT_CLASS
//----------------------------

class C_jni_SignaturesCursor: public C_jni_Cursor{
   JB_LIVE_CLASS(C_jni_SignaturesCursor);

   C_mail_client &app;
   bool need_save;

   void OnChange(){
      C_jni_Cursor::OnChange();
      need_save = true;
   }
   S_signature &Curr() const{ return app.signatures[GetPosition()]; }
public:
   C_jni_SignaturesCursor(C_mail_client &_a);
   virtual int GetCount() const{
      return app.signatures.Size();
   }
//----------------------------
   virtual Cstr_w GetString(int column_index) const{
      switch(column_index){
      case C_mail_client::SIG_COL_NAME: return Curr().name;
      case C_mail_client::SIG_COL_BODY: return Curr().body;
      }
      return NULL;
   }
//----------------------------
   virtual void SetString(int column_index, const Cstr_w &s){
      switch(column_index){
      case C_mail_client::SIG_COL_NAME: Curr().name = s; break;
      case C_mail_client::SIG_COL_BODY: Curr().body = s; break;
      default:
         return;
      }
      OnChange();
   }
//----------------------------
   virtual bool RemoveRow(){
      int pos = GetPosition();
      int num = GetCount();
      if(pos<0 || pos>=num)
         return false;
      for(int i=pos+1; i<num; i++)
         app.signatures[i-1] = app.signatures[i];
      app.signatures.Resize(--num);
      OnChange();
      return true;
   }
//----------------------------
   virtual bool InsertRow(){
      int pos = GetPosition();
      int num = GetCount();
      if(num && pos!=num)
         return false;
      app.signatures.Resize(++num);
      MoveToPosition(app.signatures.Size()-1);
      OnChange();
      return true;
   }
//----------------------------
   virtual void CommitChanges(){
      if(need_save){
         app.SaveSignatures();
         need_save = false;
      }
   }
};
typedef jni::ObjectPointer<C_jni_SignaturesCursor> PC_jni_SignaturesCursor;

//----------------------------
#define JB_CURRENT_CLASS C_jni_SignaturesCursor

JB_DEFINE_LIVE_CLASS("com/lonelycatgames/ProfiMail/SignaturesCursor",
   NoFields,
   Methods( Ctr, "<init>", "()V" ),
   NoCallbacks
)

//----------------------------

C_jni_SignaturesCursor::C_jni_SignaturesCursor(C_mail_client &_a):
   C_jni_Cursor(JB_NEW(Ctr), GetInstanceFieldID()),
   app(_a),
   need_save(false)
{
}

#undef JB_CURRENT_CLASS
//----------------------------

C_jni_Cursor *C_mail_client::CreateSignaturesCursor(){
   if(!signatures.Size())
      LoadSignatures();
   return new C_jni_SignaturesCursor(*this);
}

//----------------------------

class C_jni_AccountsCursor: public C_jni_Cursor{
   JB_LIVE_CLASS(C_jni_AccountsCursor);

   C_mail_client &app;
   bool need_save;

   void OnChange(){
      C_jni_Cursor::OnChange();
      need_save = true;
   }
   C_mail_client::S_account &Curr() const{ return app.accounts[GetPosition()]; }
public:
   C_jni_AccountsCursor(C_mail_client &_a);
   virtual int GetCount() const{
      return app.accounts.Size();
   }
//----------------------------
   virtual Cstr_w GetString(int column_index) const{
      switch(column_index){
      case C_mail_client::ACC_COL_NAME: return Curr().name;
      case C_mail_client::ACC_COL_DEFAULT_SIGNATURE: return Curr().signature_name.FromUtf8();
      }
      return NULL;
   }
//----------------------------
   virtual void SetString(int column_index, const Cstr_w &s){
      switch(column_index){
      case C_mail_client::ACC_COL_NAME: Curr().name = s; break;
      case C_mail_client::ACC_COL_DEFAULT_SIGNATURE: Curr().signature_name = s.ToUtf8(); break;
      default:
         return;
      }
      OnChange();
   }
//----------------------------
   virtual bool RemoveRow(){
      /*
      int pos = GetPosition();
      int num = GetCount();
      if(pos<0 || pos>=num)
         return false;
      for(int i=pos+1; i<num; i++)
         app.signatures[i-1] = app.signatures[i];
      app.signatures.Resize(--num);
      OnChange();
      */
      return true;
   }
//----------------------------
   virtual bool InsertRow(){
      /*
      int pos = GetPosition();
      int num = GetCount();
      if(num && pos!=num)
         return false;
      app.signatures.Resize(++num);
      MoveToPosition(app.signatures.Size()-1);
      OnChange();
      */
      return true;
   }
//----------------------------
   virtual void CommitChanges(){
      if(need_save){
         app.SaveAccounts();
         need_save = false;
      }
   }
};
typedef jni::ObjectPointer<C_jni_AccountsCursor> PC_jni_AccountsCursor;

//----------------------------
#define JB_CURRENT_CLASS C_jni_AccountsCursor

JB_DEFINE_LIVE_CLASS("com/lonelycatgames/ProfiMail/AccountsCursor",
   NoFields,
   Methods( Ctr, "<init>", "()V" ),
   NoCallbacks
)

//----------------------------

C_jni_AccountsCursor::C_jni_AccountsCursor(C_mail_client &_a):
   C_jni_Cursor(JB_NEW(Ctr), GetInstanceFieldID()),
   app(_a),
   need_save(false)
{
}

#undef JB_CURRENT_CLASS
//----------------------------

C_jni_Cursor *C_mail_client::CreateAccountsCursor(){
   return new C_jni_AccountsCursor(*this);
}

//----------------------------

void C_mail_client::JniNotificationSoundEntered(jstring js){
   if(js){

      jni::PString s = jni::PString::Wrap(js);
      config_mail.alert_sound = s->Get();
   }else
      config_mail.alert_sound.Clear();
   SaveConfig();
   PlayNewMailSound();
   RedrawScreen();
}

//----------------------------
#include <stdarg.h>
void C_mail_client::_JniRunFunction(JNIEnv*, jobject, int _fnc, ...){

   va_list arguments;
   va_start (arguments, _fnc); 
   typedef void (*t_fnc)(...);
   t_fnc fnc = (t_fnc)_fnc;
   (*fnc)(arguments);
}

//----------------------------
/*
const wchar *C_mail_client::GetText(dword id) const{

   if(!id)
      return NULL;
   if(id>=0x8000)
   switch(id){
   case SPECIAL_TEXT_OK: id = TXT_OK; break;
   case SPECIAL_TEXT_SELECT: id = TXT_SELECT; break;
   case SPECIAL_TEXT_CANCEL: id = TXT_CANCEL; break;
   case SPECIAL_TEXT_SPELL: id = TXT_EDIT; break;
   case SPECIAL_TEXT_PREVIOUS: id = TXT_PREVIOUS; break;
   case SPECIAL_TEXT_CUT: id = _TXT_CUT; break;
   case SPECIAL_TEXT_COPY: id = _TXT_COPY; break;
   case SPECIAL_TEXT_PASTE: id = _TXT_PASTE; break;
   case SPECIAL_TEXT_YES: id = TXT_YES; break;
   case SPECIAL_TEXT_NO: id = TXT_NO; break;
   case SPECIAL_TEXT_BACK: id = TXT_BACK; break;
   case SPECIAL_TEXT_MENU: id = TXT_MENU; break;
   default: assert(0);
   }
   if(id>=TXT_LAST)
      return NULL;
   Cstr_w &cache = texts_cache[id];
   if(cache.Length())
      return cache;
   JNIEnv &env = jni::GetEnv();
   jstring js = (jstring)env.CallObjectMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "GetText", "(I)Ljava/lang/String;"), id);
   if(!env.ExceptionCheck()){
      cache = jni::GetString(js, true);
   }else{
      env.ExceptionClear();
      cache.Format(L"<%>") <<id;
   }
   return cache;
}
*/
//----------------------------

extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailApplication_JniOnConfigurationChanged(JNIEnv*, jobject thiz, C_mail_client *app){

                              //reset texts cache
   for(int i=TXT_LAST; i--; )
      app->texts_cache[i].Clear();
}

//----------------------------

void C_mail_client::MakeTouchFeedback(E_TOUCH_FEEDBACK_MODE _mode){
   C_client::MakeTouchFeedback(_mode);
}

//----------------------------

extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailActivity_JniComposeEmail(JNIEnv*, jobject thiz, C_mail_client *app, jobjectArray _to, jobjectArray cc, jobjectArray bcc, jstring j_subj, jstring j_body, jobjectArray att){

   JNIEnv &env = jni::GetEnv();
   C_mail_client::C_compose_mail_data cdata;
   if(j_subj)
      cdata.subj = jni::GetString(j_subj, true);
   if(j_body)
      cdata.body = jni::GetString(j_body, true);
   for(int i=0; i<3; i++){
      jobjectArray arr = !i ? _to : i==1 ? cc : bcc;
      if(arr){
         dword num = env.GetArrayLength(arr);
         Cstr_c &to = cdata.rcpt[i];
         for(dword n=0; n<num; n++){
            Cstr_w s = jni::GetString((jstring)env.GetObjectArrayElement(arr, n), true);
            Cstr_c sc = s.ToUtf8();
            const char *cp = sc;
            text_utils::CheckStringBegin(cp, "mailto:");
            Cstr_w name;
            Cstr_c email;
            if(app->ReadAddress(cp, name, email)){
               if(to.Length())
                  to<<", ";
               to<<email;
               //LOG_RUN_N(email, i);
            }
         }
         //env.DeleteLocalRef(arr);
      }
   }
   if(att){
      dword num = env.GetArrayLength(att);
      cdata.atts.reserve(num);
      for(dword n=0; n<num; n++){
         Cstr_w fn = jni::GetString((jstring)env.GetObjectArrayElement(att, n), true);
         for(int i=fn.Length(); i--; ){
            wchar &c = fn.At(i);
            if(c=='/')
               c = '\\';
         }
         //LOG_RUN(fn.ToUtf8());
         cdata.atts.push_back(fn);
      }
      //env.DeleteLocalRef(att);
   }
   app->ComposeEmail(cdata);
   LOG_RUN("ComposeEmail done");
}

//----------------------------

extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailActivity_JniOpenEmlFile(JNIEnv*, jobject thiz, C_mail_client *app, jstring _path){
   Cstr_w path = jni::GetString(_path, true);
   for(int i=path.Length(); i--; ){
      wchar &c = path.At(i);
      if(c=='/')
         c = '\\';
   }
   app->SetModeReadMail(path);
}

//----------------------------

extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailService_JniStartedAsService(JNIEnv*, jobject thiz, C_mail_client *app){
   //LOG_RUN("auto svc");
   app->home_screen_notify.Activate();
   app->home_screen_notify.ManageSystemNotification();
   app->home_screen_notify.UpdateAppWidget();
   if(!app->config_mail.tweaks.run_as_service)
      system::SetAsSystemApp(false);
}

//----------------------------

void C_mail_client::C_home_screen_notify::ManageSystemNotification(){
   //LOG_RUN("manage");
   if(!active || !app.config_mail.tweaks.show_new_mail_notify || !app.mode)
      return;
   const wchar *title = L"";
   Cstr_w name, text;
   E_ICON icon = ICON_NOTIFY_NO;

   if(app.config_mail.tweaks.bg_status_icon){
      for(int i=0; i<app.NumAccounts(); i++){
         const S_account &acc = app.accounts[i];
         S_account::E_UPDATE_STATE st = acc.background_processor.state;
         if(st==S_account::UPDATE_ERROR || st==S_account::UPDATE_FATAL_ERROR){
                              //prioritize to display error (first one)
            icon = ICON_NOTIFY_ERROR;
            name = app.GetText(TXT_ERROR);
            text = acc.background_processor.status_text;
            break;
         }
         if(st>S_account::UPDATE_INIT){
                              //display connected accounts otherwise
            if(!icon){
               icon = ICON_NOTIFY_IDLE;
               name = app.GetText(TXT_PUSH_CONNECTED);
               if(!new_hdrs.size())
                  text<<app.GetText(TXT_ACCOUNTS) <<':';
            }
            if(!new_hdrs.size())
               text<<' ' <<acc.name;
         }
      }
      if(!icon && app.config_mail.work_times_set || app.config_mail.auto_check_time){
                              //check if we're in mode where check occurs
         switch(app.mode->Id()){
         case C_mode_accounts::ID:
         case C_mode_folders_list::ID:
         case C_mode_mailbox::ID:
                              //display scheduled time
            icon = ICON_NOTIFY_WAIT;
            name = app.GetText(TXT_SCHEDULED_MAIL_CHECK);
            if(app.config_mail.IsWorkingTime()){
                                    //compute time of next scheduled time
               int t = int(app.config_mail.auto_check_time) - ((app.auto_update_counter)/60000);
               text.Format(app.GetText(TXT_NEXT_MAIL_CHECK_IN_N_MINUTES)) <<t;
            }else{
                                    //compute time of next work time
               text = app.GetText(TXT_NEXT_CHECK_ON);
               text<<' ';
               dword t = app.GetNextEnabledWorkTime();
               S_date_time dt_work, dt_curr;
               dt_work.SetFromSeconds(t);
               dt_curr.GetCurrent();
               if(dt_work.year!=dt_curr.year || dt_work.month!=dt_curr.month || dt_work.day!=dt_curr.day){
                                    //show also day
                  Cstr_w s;
                  app.GetDateString(dt_work, s, true);
                  text<<s <<' ';
               }
               text<<text_utils::GetTimeString(dt_work, false);
            }
         }
      }
   }

                              //finally combine with new message info
   if(new_hdrs.size()){
      //int sum = 0;
      bool add_text = (!text.Length());
      for(int i=0; i<app.NumAccounts(); i++){
         const S_account &acc = app.accounts[i];
         dword stats[C_message_container::STAT_LAST];
         acc.GetMessagesStatistics(stats);
         int recent = stats[C_message_container::STAT_RECENT];
         if(recent && add_text)
            text.AppendFormat(L"% (%) ") <<acc.name <<recent;
         //sum += recent;
      }
      name.Format(L"% (%)") <<app.GetText(TXT_NEW_MSG) <<new_hdrs.size();
      title = app.GetText(TXT_NEW_MSG);
      switch(icon){
      default: icon = ICON_NOTIFY_NEW; break;
      case ICON_NOTIFY_ERROR: icon = ICON_NOTIFY_NEW_ERROR; break;
      case ICON_NOTIFY_IDLE: icon = ICON_NOTIFY_NEW_IDLE; break;
      case ICON_NOTIFY_WAIT: icon = ICON_NOTIFY_NEW_WAIT; break;
      }
   }
   if(icon){
      if(!mail_notify){
         mail_notify = C_notify_window::Create(app);
         mail_notify->Release();
      }
      mail_notify->ShowNotify(icon, title, name, text, L"ProfiMailActivity", (new_hdrs.size()!=0));
   }else
      mail_notify = NULL;
}

//----------------------------

extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailApplication_JniNetworkChanged(JNIEnv*, jobject thiz, C_mail_client *app){

   //LOG_RUN_N("JniNetworkChanged", (int)app);
                              //notified about network state change
                              // try to reconnect accounts which are in error state
   if(app->connection && app->IsImapIdleConnected()){
      //bool is_work_time = app->config_mail.IsWorkingTime();
      bool manage_timer = false;
      for(int i=app->NumAccounts(); i--; ){
         C_mail_client::S_account &acc = app->accounts[i];
         if(acc.use_imap_idle){
            if(acc.background_processor.state==C_mail_client::S_account::UPDATE_ERROR){
               acc.background_processor.error_time = 0;
               manage_timer = true;
               //if(is_work_time){
                  //acc.CloseConnection();
                  //app->ConnectAccountInBackground(acc);
               //}
            }
         }
      }
      if(manage_timer){
         LOG_RUN("Conn switched, schedule to reconnect");
         app->ManageTimer();
      }
   }
   //LOG_RUN("JniNetworkChanged OK");
}

//----------------------------

void C_mail_client::C_home_screen_notify::SetAppWidgetOnExit(){

   JNIEnv &env = jni::GetEnv();
   Cstr_w s;
   s<<app.GetText(TXT_CLICK_TO_START) <<L" ProfiMail  >>";
   jstring j_msg = jni::NewString(s);
   env.CallVoidMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "UpdateAppWidget",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZZ)V"),
      NULL, j_msg, NULL, NULL, NULL, 0, 0, false, false);
   env.DeleteLocalRef(j_msg);
}

//----------------------------

void C_mail_client::C_home_screen_notify::UpdateAppWidget(){
   //LOG_RUN("Upd AW");

   JNIEnv &env = jni::GetEnv();
   bool aw_active = env.CallBooleanMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "IsAppWidgetActive", "()Z"));
   //LOG_RUN_N("AW active", aw_active);
   if(!aw_active){
      return;
   }

   jstring j_subj = NULL, j_send = NULL, j_body = NULL, j_acc = NULL, j_date = NULL;
   bool deleted = false;
   bool has_body = false;

   if(!new_hdrs.size()){
      j_subj = jni::NewString(app.GetText(TXT_NO_RECENT_MESSAGES));
      /*
      int num_unr = app.CountUnreadVisibleMessages();
      if(num_unr){
         Cstr_w s;
         s.Format(L"%: %") <<app.GetText(TXT_UNREAD) <<num_unr;
         j_body = jni::NewString(s);
      }
      */
   }else{
      const S_msg &hdr = new_hdrs[display_hdr_index];
                           //make sender
      Cstr_w sender = hdr.hdr.sender.display_name.FromUtf8();
      if(hdr.hdr.sender.email.Length()){
         if(sender.Length())
            sender<<' ';
         sender.AppendFormat(L"<%>") <<hdr.hdr.sender.email.FromUtf8();
      }
      j_send = jni::NewString(sender);
                           //make subject
      j_subj = jni::NewString(hdr.hdr.subject.FromUtf8());

      const S_message *msg = FindMessage(hdr);
      if(msg){
         deleted = msg->IsDeleted();
         Cstr_w body;
         if(!deleted){
            if(msg->HasBody()){
               S_text_display_info tdi;
               if(app.OpenMessageBody(*hdr.cnt, *msg, tdi, true)){
                  C_vector<wchar> buf;
                  app.ConvertFormattedTextToPlainText(tdi, buf);
                  body = buf.begin();
                  has_body = true;
               }else
                  body = L"Failed to open message";
            }else{
               body = L"Click to download body";
            }
         }else{
            body = app.GetText(TXT_DELETED_MESSAGE);
         }
         j_body = jni::NewString(body);
         j_acc = jni::NewString(hdr.acc->name);
         {
                              //date/time
            S_date_time dt;
            dt.GetCurrent();
            dt.hour = dt.minute = dt.second = 0;
            dt.MakeSortValue();
            dword today_begin = dt.sort_value;

            dt.month = dt.day = 0;
            dt.MakeSortValue();
            dword this_year_begin = dt.sort_value;

            Cstr_w tmp;
            dt.SetFromSeconds(msg->date);
            if(msg->date<=this_year_begin)
               app.GetDateString(dt, tmp);
            else
            if(msg->date<=today_begin)
               app.GetDateString(dt, tmp, true);
            else
               tmp = text_utils::GetTimeString(dt, false);
            j_date = jni::NewString(tmp);
         }
      }
   }

   env.CallVoidMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "UpdateAppWidget",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZZ)V"),
      j_send, j_subj, j_body, j_acc, j_date, new_hdrs.size()-1-display_hdr_index, new_hdrs.size(), deleted, has_body);

   if(j_subj) env.DeleteLocalRef(j_subj);
   if(j_send) env.DeleteLocalRef(j_send);
   if(j_body) env.DeleteLocalRef(j_body);
   if(j_date) env.DeleteLocalRef(j_date);
   if(j_acc) env.DeleteLocalRef(j_acc);

}

//----------------------------

void C_mail_client::C_home_screen_notify::AppWidgetButtonPressed(int index){

   enum{                      //indexes match those in ProfiMailWidget.java
      AW_BODY, AW_REPLY, AW_HIDE, AW_DELETE, AW_PREV, AW_NEXT,
   };
   JNIEnv &env = jni::GetEnv();
   bool update = false, update_not = false;
   switch(index){
   case AW_NEXT:
      if(new_hdrs.size() && display_hdr_index){
         --display_hdr_index;
         update = true;
      }
      break;
   case AW_PREV:
      if(new_hdrs.size() && display_hdr_index<new_hdrs.size()-1){
         ++display_hdr_index;
         update = true;
      }
      break;
   case AW_HIDE:
                              //just remove current msg from recent
      if(new_hdrs.size() && display_hdr_index<new_hdrs.size()){
         S_msg &hdr = new_hdrs[display_hdr_index];
         S_message *msg = FindMessage(hdr);
         if(msg){
            if(msg->IsRecent()){
               msg->flags &= ~S_message::MSG_RECENT;
               hdr.cnt->MakeDirty();
            }
            if(msg->flags&S_message::MSG_DELETED_DIRTY){
               S_account &acc = *hdr.acc;
               if(acc.IsImap()){
                  if(acc.background_processor.state==S_account::UPDATE_IDLING && (C_message_container*)acc.selected_folder==hdr.cnt){
                     app.ImapIdleUpdateFlags(*acc.background_processor.GetMode(), true);
                  }
               }
            }
         }
         new_hdrs.remove_index(display_hdr_index);
         if(display_hdr_index)
            --display_hdr_index;
         update = true;
         update_not = true;
      }
      break;

   case AW_DELETE:
      if(new_hdrs.size()){
         S_msg &hdr = new_hdrs[display_hdr_index];
         S_message *msg = FindMessage(hdr);
         if(msg && msg->IsServerSynced()){
            if(!msg->IsDeleted()){
                              //mark to delete
               msg->flags |= S_message::MSG_DELETED;
            }else{
               msg->flags &= ~S_message::MSG_DELETED;
            }
            msg->flags |= S_message::MSG_DELETED_DIRTY;
            hdr.cnt->MakeDirty();
            update = true;
         }
      }
      break;

   case AW_BODY:
   case AW_REPLY:
      if(new_hdrs.size()){
         if(app.SafeReturnToAccountsMode()){
            const S_msg &hdr = new_hdrs[display_hdr_index];
            app.SetModeFoldersList(*hdr.acc);
            C_mail_client::C_mode_mailbox &mod_mbox = app.SetModeMailbox(*hdr.acc, hdr.cnt);
                              //find message index
            C_vector<S_message> &msgs = mod_mbox.GetMessages();
            for(int i=mod_mbox.num_vis_msgs; i--; ){
               S_message &m = msgs[i];
               if(m==hdr.hdr){
                  app.SetMailboxSelection(mod_mbox, i);
                  if(!m.IsDeleted()){
                     if(index==AW_BODY)
                        app.OpenMessage(mod_mbox);
                     else if(index==AW_REPLY){
                        app.SetModeWriteMail(hdr.cnt, &m, true, false, false);
                     }
                  }
                  break;
               }
            }
                              //activate app
            env.CallVoidMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "StartProfiMailActivity", "()V"));
         }
      }
      break;
   }
   if(update)
      UpdateAppWidget();
   if(update_not)
      ManageSystemNotification();
}

//----------------------------

extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailApplication_JniAppWidgetButtonPressed(JNIEnv*, jobject thiz, C_mail_client *app, int index){
   app->home_screen_notify.AppWidgetButtonPressed(index);
}

//----------------------------

extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailActivity_JniOnActivityDestroy(JNIEnv*, jobject thiz, C_mail_client *app){
   //LOG_RUN("JniOnActivityDestroy");
                              //return to accounts mode, for case that some Java activities tied to our modes would be destroyed
   app->SafeReturnToAccountsMode();
}

//----------------------------
/*
extern"C"
__attribute__ ((visibility("default")))
void Java_com_lonelycatgames_ProfiMail_ProfiMailApplication_JniRunFunction(JNIEnv*, jobject thiz, C_mail_client *app, int ifnc){
   void(C_mail_client::*fnc)() = NULL;
   *(dword*)&fnc = ifnc;
   (app->*fnc)();
}
*/
//----------------------------
#ifdef USE_ANDROID_RINGTONES
Cstr_w C_mail_client::S_config_mail::GetDefaultAlertSound(){
   JNIEnv &env = jni::GetEnv();
   jstring s = (jstring)env.CallObjectMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "GetDefaultAlertSound", "()Ljava/lang/String;"));
   return jni::GetString(s, true);
}

//----------------------------

Cstr_w C_mail_client::GetAlertSoundFriendlyName(const Cstr_w &uri){
   if(!uri.Length())
      return NULL;
   JNIEnv &env = jni::GetEnv();
   jni::PString ps = jni::PString::New(uri);
   jstring s = (jstring)env.CallObjectMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "GetAlertSoundName", "(Ljava/lang/String;)Ljava/lang/String;"), ps.GetJObject());
   return jni::GetString(s, true);
}

//----------------------------

void C_mail_client::SelectAndroidNotification(const Cstr_w &curr, bool _rules){
   PC_jni_ProfiMailApplication japp = PC_jni_ProfiMailApplication::Wrap(android_globals.java_app);
   japp->SelectNotificationSound(curr, _rules);
}

#endif
//----------------------------
bool hasActivity(){
   JNIEnv &env = jni::GetEnv();
   return env.CallBooleanMethod(android_globals.java_app, env.GetMethodID(android_globals.java_app_class, "hasActivity", "()Z"));
}
//----------------------------

E_PREFERENCE_TYPE C_mail_client::C_configuration_editing_email::GetPreferenceType(const S_config_item &ec) const{

   switch(ec.ctype){
   case CFG_ITEM_WORK_HOURS_SUM:
   case CFG_ITEM_TWEAKS:
   case CFG_ITEM_TWEAKS_RESET:
      return TYPE_CLICKABLE;
   case CFG_ITEM_IMAP_CONNECT_MODE:
   case CFG_ITEM_DATE_FORMAT:
   case CFG_ITEM_DATA_LOCATION:
   case CFG_ITEM_WORK_HOUR:
      return TYPE_LIST;
   case CFG_ITEM_TIME_OUT:
      return TYPE_RANGE;
   case CFG_ITEM_ALERT_SOUND:
      return TYPE_RINGTONE;
   }
   return super::GetPreferenceType(ec);
}

//----------------------------

Cstr_w C_mail_client::C_configuration_editing_email::GetCursorString(const S_config_item &ec, E_PREFERENCE_CURSOR_COLUMN column, byte *cfg_base) const{

   switch(ec.ctype){
   case CFG_ITEM_WORK_HOURS_SUM:
      if(column==COLUMN_DATA){
         C_mode_config_mail m((C_mail_client&)app, NULL, 0, NULL);
         Cstr_w s;
         bool x;
         m.GetConfigOptionText(ec, s, x, x);
         return s;
      }
      break;
   case CFG_ITEM_IMAP_CONNECT_MODE:
      if(column>=COLUMN_LIST_ITEMS_BASE){
         Cstr_w s;
         switch(column-COLUMN_LIST_ITEMS_BASE){
         case S_config_mail::IDLE_CONNECT_ASK: s = app.GetText(TXT_ASK); break;
         case S_config_mail::IDLE_CONNECT_AUTOMATIC: s = app.GetText(TXT_AUTOMATIC); break;
         case S_config_mail::IDLE_CONNECT_MANUAL: s = app.GetText(TXT_MANUAL); break;
         }
         return s;
      }
      break;
   case CFG_ITEM_DATE_FORMAT:
      if(column>=COLUMN_LIST_ITEMS_BASE){
         return S_config_mail::GetDateFormatString((S_config_mail::E_DATE_FORMAT)(column-COLUMN_LIST_ITEMS_BASE));
      }
      break;
   case CFG_ITEM_WORK_HOUR:
      if(column>=COLUMN_LIST_ITEMS_BASE){
         int index = column-COLUMN_LIST_ITEMS_BASE;
         S_date_time dt;
         dt.SetFromSeconds(60*index*15);
         return text_utils::GetTimeString(dt, false);
      }
      break;
   case CFG_ITEM_DATA_LOCATION:
      if(column>=COLUMN_LIST_ITEMS_BASE){
         return data_locations[column-COLUMN_LIST_ITEMS_BASE].name;
      }
      break;
   case CFG_ITEM_TIME_OUT:
      if(column==COLUMN_RANGE_UNITS)
         return app.GetText(TXT_CFG_SEC);
      break;
   case CFG_ITEM_ALERT_SOUND:
      if(column==COLUMN_DATA)
         return *(Cstr_w*)(cfg_base+ec.elem_offset);
      break;
   }
   return super::GetCursorString(ec, column, cfg_base);
}

//----------------------------

int C_mail_client::C_configuration_editing_email::GetCursorInt(const S_config_item &ec, E_PREFERENCE_CURSOR_COLUMN column, byte *cfg_base) const{

   switch(ec.ctype){
   case CFG_ITEM_IMAP_CONNECT_MODE:
      switch(column){
      case COLUMN_LIST_NUM_ITEMS:
         return 3;
      case COLUMN_DATA:
         return (*(C_mail_client::S_config_mail*)cfg_base).imap_idle_startup_connect;
      }
      break;
   case CFG_ITEM_DATE_FORMAT:
      switch(column){
      case COLUMN_LIST_NUM_ITEMS: return C_mail_client::S_config_mail::DATE_LAST;
      case COLUMN_DATA: return (*(C_mail_client::S_config_mail*)cfg_base).date_format;
      }
      break;
   case CFG_ITEM_WORK_HOUR:
      switch(column){
      case COLUMN_LIST_NUM_ITEMS: return 24*60/15;
      case COLUMN_DATA: return *(word*)(cfg_base+ec.elem_offset)/15;
      }
      break;
   case CFG_ITEM_DATA_LOCATION:
      switch(column){
      case COLUMN_LIST_NUM_ITEMS: return data_locations.size();
      case COLUMN_DATA: return curr_loc_index;
      }
      break;
   case CFG_ITEM_TIME_OUT:
      switch(column){
      case COLUMN_RANGE_MIN: return 10;
      case COLUMN_RANGE_MAX: return 90;
      case COLUMN_RANGE_STEP: return 10;
      case COLUMN_DATA: return *(int*)(cfg_base+ec.elem_offset);
      }
      break;
   case CFG_ITEM_ALERT_SOUND:
      switch(column){
      case COLUMN_RINGTONE_SHOW_DEFAULT: return true;
      case COLUMN_RINGTONE_SHOW_SILENT: return true;
      case COLUMN_RINGTONE_TYPE:
         // RingtoneManager.TYPE_NOTIFICATION = 2
         // TYPE_ALARM = 4
         // TYPE_RINGTONE = 1
         return 2;
      }
      break;
   }
   return super::GetCursorInt(ec, column, cfg_base);
}

//----------------------------

void C_mail_client::C_configuration_editing_email::SetCursorString(const S_config_item &ec, const Cstr_w &s, byte *cfg_base){

   switch(ec.ctype){
   case CFG_ITEM_ALERT_SOUND:
      LOG_RUN(s.ToUtf8());
      *(Cstr_w*)(cfg_base+ec.elem_offset) = s;
      break;
   }
   super::SetCursorString(ec, s, cfg_base);
}

//----------------------------

void C_mail_client::C_configuration_editing_email::SetCursorInt(const S_config_item &ec, int v, byte *cfg_base){
   switch(ec.ctype){
   case CFG_ITEM_IMAP_CONNECT_MODE:
      (*(C_mail_client::S_config_mail*)cfg_base).imap_idle_startup_connect = (C_mail_client::S_config_mail::E_IDLE_CONNECT_MODE)v;
      break;
   case CFG_ITEM_DATE_FORMAT:
      (*(C_mail_client::S_config_mail*)cfg_base).date_format = (C_mail_client::S_config_mail::E_DATE_FORMAT)v;
      break;
   case CFG_ITEM_WORK_HOUR:
      *(word*)(cfg_base+ec.elem_offset) = word(v*15);
      break;
   case CFG_ITEM_DATA_LOCATION:
      curr_loc_index = v;
      break;
   case CFG_ITEM_TIME_OUT:
      *(int*)(cfg_base+ec.elem_offset) = v;
      break;
   }
   super::SetCursorInt(ec, v, cfg_base);
}

//----------------------------

Cstr_w C_mail_client::C_configuration_editing_work_times::GetCursorString(const S_config_item &ec, E_PREFERENCE_CURSOR_COLUMN column, byte *cfg_base) const{
                              //no help for work days
   if(ec.ctype==CFG_ITEM_CHECKBOX && ec.param!=0 && column==COLUMN_SUMMARY)
      return NULL;
   return super::GetCursorString(ec, column, cfg_base);
}

//----------------------------

int C_mail_client::C_configuration_editing_work_times::GetCursorInt(const S_config_item &ec, E_PREFERENCE_CURSOR_COLUMN column, byte *cfg_base) const{

   if(ec.elem_offset!=OffsetOf(S_config_mail, work_times_set) && column==COLUMN_DEPENDENCY){
      return 0;
   }
   super::GetCursorInt(ec, column, cfg_base);
}

//----------------------------
