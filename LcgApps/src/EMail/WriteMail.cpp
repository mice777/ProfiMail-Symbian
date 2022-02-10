#include "..\Main.h"
#include "Main_Email.h"
#include <Ui\MultiQuestion.h>
#include <RndGen.h>

//----------------------------

const int MAX_WRITE_TEXT_LEN = 250000;

//----------------------------
//----------------------------

class C_mode_write_mail: public C_mail_client::C_mode_write_mail_base, public C_multi_selection_callback, public C_file_browser_callback, public C_ctrl_text_entry::C_ctrl_text_entry_notify{
                           //C_multi_selection_callback:
   virtual void Select(int option){
                              //question for canceling writing
      if(!GetAccount().IsImap() && option>=1)
         ++option;

      switch(option){
      case 0:                 //save draft locally
         SaveAndClose(false, true, false);
         break;
      case 1:                 //upload draft
         SaveAndClose(false, true, true);
         break;
      case 2:                 //delete draft
         set_flags = 0;
         UpdateOriginalMessage();
         C_mode::Close();
         break;
      case 3:                 //cancel writing
         if(orig_cnt)
            orig_cnt->ClearAllMessageMarks();
         C_mode::Close();
         break;
      }
   }

//----------------------------
                           //C_file_browser_callback:
   virtual void FileSelected(const Cstr_w &file){
      C_vector<Cstr_w> tmp;
      tmp.push_back(file);
      FilesSelected(tmp);
   }
   virtual void FilesSelected(const C_vector<Cstr_w> &files){
      for(int i=0; i<files.size(); i++)
         AddAttachment(files[i]);
   }
//----------------------------

   S_rect rc;
   Cstr_w subject;
                           //email addresses, separated by ' '
   Cstr_c rcpt_to, rcpt_cc, rcpt_bcc;

//----------------------------
                              //C_ctrl_text_entry_notify:
   virtual void TextEntryCursorMovedUpOnTop(C_ctrl_text_entry *ctrl){
      SetEditField(FIELD_SUBJECT);
      Draw();
   }
   virtual void TextEntryCursorMovedDownOnBottom(C_ctrl_text_entry *ctrl){
      if(attachments.Size()){
         SetEditField(FIELD_ATTACHMENTS);
         Draw();
      }
   }

//----------------------------
   C_ctrl_text_entry *ctrl_text_body;

//----------------------------

   virtual void DrawControl(const C_ui_control *ctrl) const{
      C_mode::DrawControl(ctrl);
      if(ctrl==ctrl_text_body){
         const dword c0 = app.GetColor(app.COL_SHADOW), c1 = app.GetColor(app.COL_HIGHLIGHT);
         app.DrawOutline(rc, c0, c1);
      }
   }

//----------------------------

   class C_ctrl_header: public C_ui_control{
   public:
      C_ctrl_header(C_mode *m):
         C_ui_control(m)
      {}
      virtual void Draw() const{
         ((C_mode_write_mail&)mod).DrawHeader();
      }
   };
   C_ctrl_header *ctrl_header;
//----------------------------
   C_ctrl_text_entry_line *ctrl_te_line;
//----------------------------
   Cstr_c in_reply_to_msg_id, msg_references;

   C_vector<S_identity> identities;
   S_identity current_identity;

   enum E_FIELD{
      FIELD_TO,
      FIELD_CC,
      FIELD_BCC,
      FIELD_SUBJECT,
      FIELD_BODY,
      FIELD_ATTACHMENTS,
   } edit_field;

   enum{
      PRIORITY_LOW,
      PRIORITY_NORMAL,
      PRIORITY_HIGH,
   } priority;

                           //attachment browser
   C_attach_browser attach_browser;
   C_buffer<S_attachment> attachments;

   C_smart_ptr<C_message_container> orig_cnt;
   dword set_flags;        //MSG_REPLIED or MSG_FORWARDED
   bool is_new_msg;
   bool edited;            //to detect changes in case of Cancel
   bool send_immediately;
   bool editing_draft;

   struct S_auto_complete{
      Cstr_c email;
      Cstr_w name;
      bool match_email;
   };

//----------------------------
   class C_list: public C_list_mode_base{
      C_application_ui &app;
      virtual C_application_ui &AppForListMode() const{ return app; }
   public:
      C_vector<S_auto_complete> emails;

      C_list(C_application_ui &_app):
         app(_app)
      {}

      virtual int GetNumEntries() const{ return emails.size(); }
   } list_auto_complete;
//----------------------------

   void AddAutoComplete(const Cstr_w &first_name, const Cstr_w &last_name, const Cstr_c &email, bool match_email);

//----------------------------

   bool IsEmptyMessage() const{
      return (!subject.Length() && !rcpt_to.Length() && !rcpt_cc.Length() && !rcpt_bcc.Length() &&
         !attachments.Size() && !ctrl_text_body->GetTextLength());
   }

//----------------------------

   C_mail_client::S_account &GetAccount(){
      if(saved_parent->Id()==C_mail_client::C_mode_folders_list::ID){
         return ((C_mail_client::C_mode_folders_list&)*saved_parent).acc;
      }else
         return ((C_mail_client::C_mode_mailbox&)*saved_parent).acc;
   }
//----------------------------
   virtual void ResetTouchInput(){
      attach_browser.ResetTouchInput();
   }

   void ComputeEditHeight(){
      int bot = ctrl_header->GetRect().y + 1 + app.fdb.line_spacing*5/2;
      rc.sy = app.ScrnSY() - bot - app.GetSoftButtonBarHeight()-1;
      dword num_att = attachments.Size();
      if(num_att)
         rc.sy -= attach_browser.rc.sy;

      attach_browser.rc.y = rc.Bottom()+1;
      attach_browser.Init();

      ctrl_text_body->SetRect(rc);
   }
//----------------------------
   void InitAutoCompleteRect();
   void SetEditField(E_FIELD fld);
   virtual void AddAttachment(const Cstr_w &filename, bool set_edited_flag = true);
   void RemoveSelectedAttachment();
//----------------------------
   void SetAttachmentBrowser(){
      C_client_file_mgr::C_mode_file_browser &mod = C_client_file_mgr::SetModeFileBrowser(&app, C_client_file_mgr::C_mode_file_browser::MODE_EXPLORER, true, NULL,
         TXT_CHOOSE_FILE, NULL, C_client_file_mgr::GETDIR_FILES|C_client_file_mgr::GETDIR_DIRECTORIES, NULL, this);
      mod.flags = mod.FLG_ACCEPT_FILE | mod.FLG_ALLOW_DETAILS | mod.FLG_ALLOW_MARK_FILES | mod.FLG_ALLOW_RENAME | mod.FLG_ALLOW_SHOW_SUBMENU | mod.FLG_SAVE_LAST_PATH | mod.FLG_SELECT_OPTION;
   }

   virtual void InitMenu();

//----------------------------
   virtual void AddRecipient(const Cstr_c &e_mail);
   void CreateSignaturesMenu();
//----------------------------
   void DeactivateEditors(){
      //SetFocus(NULL);
      //if(text_editor)
         //text_editor->Activate(false);
   }
//----------------------------
   void SelectAutoComplete();
   virtual bool SaveMessage(bool draft, bool upload_draft);
   void SaveAndClose(bool ask, bool save_draft = false, bool upload_draft = false);
   void SendMessage();

   void SkipToAdjacentField(bool next){
      switch(edit_field){
      case FIELD_TO: SetEditField(!next ? (attachments.Size() ? FIELD_ATTACHMENTS : FIELD_BODY) : FIELD_CC); break;
      case FIELD_CC: SetEditField(!next ? FIELD_TO : FIELD_BCC); break;
      case FIELD_BCC: SetEditField(!next ? FIELD_CC : FIELD_SUBJECT); break;
      case FIELD_SUBJECT: SetEditField(!next ? FIELD_BCC : FIELD_BODY); break;
      case FIELD_BODY: SetEditField(!next ? FIELD_SUBJECT : attachments.Size() ? FIELD_ATTACHMENTS : FIELD_TO); break;
      case FIELD_ATTACHMENTS: SetEditField(!next ? FIELD_BODY : FIELD_TO); break;
      }
   }

//----------------------------
// Update flags on original message (reply/forward), or delete it if it was edited draft. Original message is the marked one in orig_cnt.
   void UpdateOriginalMessage();

//----------------------------
   void SetupCurrentIdentity(const char *to){
      const C_mail_client::S_account &acc = GetAccount();
      identities.clear();
      identities.push_back(acc.primary_identity);
      identities.insert(identities.end(), acc.identities.Begin(), acc.identities.End());
      current_identity = acc.primary_identity;
      if(to){
         C_vector<Cstr_c> rcpts;
         if(ParseRecipients(to, rcpts) && rcpts.size()){
            for(int i=0; i<identities.size(); i++){
               const Cstr_c &em = identities[i].email;
               int j;
               for(j=rcpts.size(); j--; ){
                  if(em==rcpts[j]){
                     current_identity = identities[i];
                     break;
                  }
               }
               if(j>=0)
                  break;
            }
         }
      }
   }
//----------------------------
   virtual void OnSoftBarButtonPressed(dword index){
      switch(index){
      case 0: SendMessage(); break;
      //case 2: WriteMail_CreateSignaturesMenu(mod); break;
      case 3: SetAttachmentBrowser(); break;
      }
   }
//----------------------------
   virtual void Close(bool redraw){
      if(list_auto_complete.emails.size())
         list_auto_complete.emails.clear();
      else{
         SaveAndClose(true);
      }
   }
//----------------------------
public:
   C_mode_write_mail(C_mail_client &_app, bool is_new):
      C_mode_write_mail_base(_app),
      attach_browser(_app),
      is_new_msg(is_new),
      list_auto_complete(_app),
      edit_field(FIELD_BODY),
      priority(PRIORITY_NORMAL),
      edited(false),
      editing_draft(false),
      set_flags(0),
      ctrl_text_body(NULL),
      ctrl_header(NULL),
      ctrl_te_line(NULL),
      send_immediately(false)
   {
      mode_id = ID;
      send_immediately = (app.config.flags&C_mail_client::S_config_mail::CONF_SEND_MSG_IMMEDIATELY);

      SetTitle(app.GetText(TXT_COMPOSE_MESSAGE));

      C_ctrl_softkey_bar &skb = *GetSoftkeyBar();
      skb.InitButton(0, app.BUT_SEND, TXT_SEND);
      skb.InitButton(3, app.BUT_ADD_ATTACHMENT, TXT_ADD_ATTACHMENT);

      ctrl_text_body = new(true) C_ctrl_text_entry(this, MAX_WRITE_TEXT_LEN, TXTED_ALLOW_PREDICTIVE, app.config.viewer_font_index, this);
      ctrl_text_body->SetCase(C_text_editor::CASE_CAPITAL);
      AddControl(ctrl_text_body);

      ctrl_header = new(true) C_ctrl_header(this);
      AddControl(ctrl_header);

      //SetupCurrentIdentity();
   }
   void Init(C_message_container *cnt, S_message *msg, bool reply, bool forward, bool reply_all, const char *rcpt);
   void Init(const char *to, const char *cc, const char *bcc, const wchar *subject, const wchar *body);

   virtual void InitLayout();
   virtual void ProcessInput(S_user_input &ui, bool &redraw);
   virtual void ProcessMenu(int itm, dword menu_id);
   virtual void TextEditNotify(bool cursor_moved, bool text_changed, bool &redraw);
   virtual void Draw() const;
   void DrawHeader() const;
};

//----------------------------

void C_mail_client::SetModeWriteMail(C_message_container *cnt, S_message *msg, bool reply, bool forward, bool reply_all, const char *rcpt){

   C_mode_write_mail &mod = *new(true) C_mode_write_mail(*this, (!msg));
   mod.Init(cnt, msg, reply, forward, reply_all, rcpt);
}

//----------------------------

void C_mode_write_mail::Init(C_message_container *cnt, S_message *msg, bool reply, bool forward, bool reply_all, const char *rcpt){

   const C_mail_client::S_account &acc = GetAccount();
   is_new_msg = !msg;
                              //init message
   C_vector<wchar> body;
   if(msg){

      S_text_display_info td;
      if(cnt)
         app.OpenMessageBody(*cnt, *msg, td, true);
      body.reserve(td.body_w.Length()*5/4 + 100);
      /*
                              //adopt identity from message
      if(msg->sender.email.Length())
         current_identity.email = msg->sender.email;
      if(msg->sender.display_name.Length())
         current_identity.display_name = msg->sender.display_name;
         */

      int i;

      if((reply || forward || reply_all)){
         cnt->ClearAllMessageMarks();
         msg->marked = true;
         orig_cnt = cnt;
         set_flags = (reply||reply_all) ? S_message::MSG_REPLIED : S_message::MSG_FORWARDED;

         if(app.config_mail.tweaks.quote_when_reply || forward){
            if(!app.config_mail.tweaks.reply_below_quote){
                                 //add some empty lines first, so that we've space to type the message
               for(i=0; i<2; i++)
                  body.push_back('\n');
            }
            {
               const wchar *wp = app.GetText(TXT_WRT_ORIGINAL_MSG);
               body.insert(body.end(), wp, wp+StrLen(wp));
               body.push_back('\n');
            }
            if(msg->sender.display_name.Length() || msg->sender.email.Length()){
               const wchar *wp = app.GetText(TXT_FROM);
               if(!forward){
                  body.push_back('>'); body.push_back(' ');
               }
               body.insert(body.end(), wp, wp+StrLen(wp));
               body.push_back(' ');
               if(msg->sender.display_name.Length()){
                  Cstr_w s = msg->sender.display_name.FromUtf8();
                  wp = s;
                  body.insert(body.end(), wp, wp+StrLen(wp));
               }
               const char *cp = msg->sender.email;
               if(*cp){
                  body.push_back(' ');
                  body.push_back('<');
                  while(*cp)
                     body.push_back(*cp++);
                  body.push_back('>');
               }
               body.push_back('\n');
            }
            {
               if((reply || reply_all || forward) && (msg->to_emails.Length() || msg->to_names.Length())){
                  const wchar *wp = app.GetText(TXT_TO);
                  if(!forward){
                     body.push_back('>'); body.push_back(' ');
                  }
                  body.insert(body.end(), wp, wp+StrLen(wp));
                  body.push_back(' ');
                  Cstr_w tmp;
                  tmp.Copy(msg->to_emails);
                  wp = tmp;
                  body.insert(body.end(), wp, wp+StrLen(wp));
                  body.push_back('\n');
               }
               if(msg->cc_emails.Length()){
                  const wchar *wp = L"Cc: ";
                  body.push_back('>'); body.push_back(' ');
                  body.insert(body.end(), wp, wp+StrLen(wp));
                  Cstr_w tmp;
                  tmp.Copy(msg->cc_emails);
                  wp = tmp;
                  body.insert(body.end(), wp, wp+StrLen(wp));
                  body.push_back('\n');
               }
            }
            {
               const wchar *wp = app.GetText(TXT_SENT);
               if(!forward){
                  body.push_back('>'); body.push_back(' ');
               }
               body.insert(body.end(), wp, wp+StrLen(wp));
               body.push_back(' ');
                                    //date
               Cstr_w str;
               S_date_time dt; dt.SetFromSeconds(msg->date);
               app.GetDateString(dt, str);
               wp = str; body.insert(body.end(), wp, wp+StrLen(wp));
                                    //time
               str.Format(L",  %:#02%") <<(int)dt.hour <<(int)dt.minute;
               wp = str; body.insert(body.end(), wp, wp+StrLen(wp));
               body.push_back('\n');
            }
            if(!forward){
               body.push_back('>'); body.push_back(' ');
            }
            body.push_back('\n');
         }
      }else
      if(msg->IsDraft() || (msg->flags&S_message::MSG_TO_SEND)){
         cnt->ClearAllMessageMarks();
         msg->marked = true;
         orig_cnt = cnt;
         editing_draft = true;
      }
      /*
      if(msg->flags&(msg->MSG_DRAFT|msg->MSG_TO_SEND)){
                              //always tread as edited, because original message is already marked deleted
         edited = true;
      }
      */

      const void *vp = td.is_wide ? (const char*)(const wchar*)td.body_w : (const char*)td.body_c;
      bool add_prefix = true;
      int quote_count = 1;
                              //add original message, quoted
      if(app.config_mail.tweaks.quote_when_reply || forward || (msg && (msg->flags&(S_message::MSG_DRAFT|S_message::MSG_TO_SEND)))){
         int quote_beg_offs = body.size();
         while(true){
            dword c = text_utils::GetChar(vp, td.is_wide);
            if(!c)
               break;
            if(S_text_style::IsControlCode(c)){
               const char *&cp = (const char*&)vp;
               if(c==CC_BLOCK_QUOTE_COUNT){
                  S_text_style ts;
                  --cp;
                  ts.ReadCode(cp, NULL);
                  quote_count = ts.block_quote_count+1;
                  continue;
               }else
               if(c!=CC_WIDE_CHAR){
                  --cp;
                  S_text_style::SkipCode(cp);
                  continue;
               }
               c = S_text_style::ReadWideChar(cp);
            }else
               c = encoding::ConvertCodedCharToUnicode(c, td.body_c.coding);
            if((reply || reply_all) && add_prefix){
               for(int j=0; j<quote_count; j++)
                  body.push_back('>');
               if(c!='>')
                  body.push_back(' ');
            }
            body.push_back(wchar(c));
            add_prefix = (c=='\n');
         }
         if(reply || reply_all){
                              //try to remove signature from original message
            for(int j=quote_beg_offs; j<body.size()-2; j++){
               if(body[j]=='-' && body[j+1]=='-' && body[j+2]==' ' && body[j+3]=='\n'){
                              //from now on, ignore signature
                  body.erase(body.begin()+j, body.end());
                  if(body.size()>2 && body[body.size()-1]==' ' && body[body.size()-2]=='>' && body[body.size()-3]=='\n')
                     body.erase(body.end()-3, body.end());
                  break;
               }
            }
         }
      }
                              //prepare subject
      if(reply || reply_all || forward){
         C_vector<wchar> tmp;
         Cstr_w old_subject = msg->subject.FromUtf8();
         const wchar *wp = old_subject;
         while(*wp==' ') ++wp;
                              //detect and ignore previous "Re:", "Aw:", "Fw:", etc
         if((
            (ToLower(wp[0])=='r' && ToLower(wp[1])=='e') ||
            (ToLower(wp[0])=='a' && ToLower(wp[1])=='w') ||
            (ToLower(wp[0])=='f' && ToLower(wp[1])=='w')
            ) && wp[2]==':')
            wp += 3;
         while(*wp==' ') ++wp;

         const wchar *pp;
         if(reply || reply_all)
            pp = L"Re:";
         else
            pp = L"Fw:";
         tmp.insert(tmp.end(), pp, pp+StrLen(pp));
         tmp.push_back(' ');
         while(*wp)
            tmp.push_back(*wp++);
         tmp.push_back(0);
         subject = tmp.begin();

         if(reply || reply_all){
            const bool is_sent_folder = (acc.IsImap() && cnt && !cnt->IsInbox() && acc.GetFullFolderName(*cnt)==acc.GetSentFolderName());
                              //add sender among recipients
            const Cstr_c &reply_addr = is_sent_folder ? msg->to_emails : msg->reply_to_email.Length() ? msg->reply_to_email : msg->sender.email;
            rcpt_to.Copy(reply_addr);
            if(reply_all){
                              //add all 
               C_vector<Cstr_c> addresses;
               GetMessageRecipients(*msg, acc.primary_identity.email, addresses);
               for(int j=0; j<addresses.size(); j++){
                  if(rcpt_cc.Length())
                     rcpt_cc<<L", ";
                  Cstr_w s; s.Copy(addresses[j]);
                  rcpt_cc<<s;
               }
            }
         }
      }else{
         subject = msg->subject.FromUtf8();
         rcpt_to = msg->to_emails;
         rcpt_cc = msg->cc_emails;
         rcpt_bcc = msg->bcc_emails;
      }
      in_reply_to_msg_id = msg->message_id;
      msg_references = msg->references;
      /*
      if(msg->message_id.Length()){
         if(reply || forward || reply_all)
            had_msg_id = true;
         else if(msg->flags&S_message::MSG_SEND_MESSAGE_ID)
            had_msg_id = true;
         else if((msg->flags&S_message::MSG_DRAFT) && (msg->flags&S_message::MSG_SERVER_SYNC))
            had_msg_id = true;
      }
      */
      if(msg->flags&msg->MSG_PRIORITY_LOW)
         priority = PRIORITY_LOW;
      else
      if(msg->flags&msg->MSG_PRIORITY_HIGH)
         priority = PRIORITY_HIGH;

      if(!(reply || reply_all)){
                              //adopt forwarded attachments
         {
            C_vector<S_attachment> tmp;
                              //collect both normal and inline attachments
            tmp.insert(tmp.end(), msg->attachments.Begin(), msg->attachments.End());
            tmp.insert(tmp.end(), msg->inline_attachments.Begin(), msg->inline_attachments.End());
            attachments.Assign(tmp.begin(), tmp.end());
         }
         //attachments = msg->attachments;
                              //remove non-existing attachments
         int num_a = attachments.Size();
         for(int j=attachments.Size(); j--; ){
            C_file ck;
            const S_attachment &att = attachments[j];
            if(!att.IsDownloaded() || !ck.Open(att.filename.FromUtf8())){
               --num_a;
               attachments[j] = attachments[num_a];
            }
         }
         attachments.Resize(num_a);
      }
      assert(!rcpt);
   }else{
      if(rcpt)
         rcpt_to = rcpt;
   }
   dword cursor_pos = 0;
   if(app.config_mail.tweaks.reply_below_quote){
      if(body.size())
         body.push_back('\n');
      cursor_pos = body.size();
   }

   if(acc.signature_name.Length() && (reply || reply_all || forward || !msg)){
                              //insert default signature
      Cstr_w sig_name = acc.signature_name.FromUtf8();
      if(!app.signatures.Size())
         app.LoadSignatures();
      for(int i=app.signatures.Size(); i--; ){
         const S_signature &sig = app.signatures[i];
         if(sig.name==sig_name){
                              //add this
            static const wchar eols[] = L"\n\n";
            const wchar *wp_sig = sig.body;
            if(!app.config_mail.tweaks.reply_below_quote){
               body.insert(body.begin(), wp_sig, wp_sig+sig.body.Length());
               body.insert(body.begin(), eols, eols+StrLen(eols));
            }else{
               cursor_pos = body.size();
               body.insert(body.end(), eols, eols+StrLen(eols));
               body.insert(body.end(), wp_sig, wp_sig+sig.body.Length());
            }
            break;
         }
      }
   }
   body.push_back(0);
   ctrl_text_body->SetText(body.begin());
   ctrl_text_body->SetCursorPos(cursor_pos);

   SetupCurrentIdentity(msg ? (const char*)msg->to_emails : NULL);
   InitLayout();

   if(!rcpt_to.Length())
      SetEditField(FIELD_TO);
   else
      SetEditField(FIELD_BODY);
   app.ActivateMode(*this);
}

//----------------------------

C_mail_client::C_mode_write_mail_base &C_mail_client::SetModeWriteMail(const char *to, const char *cc, const char *bcc, const wchar *subject, const wchar *body){

   C_mode_write_mail &mod = *new(true) C_mode_write_mail(*this, true);

   mod.Init(to, cc, bcc, subject, body);
   return mod;
}

//----------------------------

void C_mode_write_mail::Init(const char *to, const char *cc, const char *bcc, const wchar *_subject, const wchar *body){

   ctrl_text_body->SetText(body);
   ctrl_text_body->SetCursorPos(ctrl_text_body->GetTextLength());
   rcpt_to = to;
   rcpt_cc = cc;
   rcpt_bcc = bcc;
   subject = _subject;
   edited = !IsEmptyMessage();

   InitLayout();
   if(!rcpt_to.Length())
      SetEditField(FIELD_TO);
   else
      SetEditField(FIELD_BODY);
   SetupCurrentIdentity(to);
   app.ActivateMode(*this);
}

//----------------------------

void C_mode_write_mail::InitLayout(){

   C_mode::InitLayout();
   const int border = 1;

   int hdr_lines = 2;
   if(edit_field < FIELD_BODY)
      hdr_lines = 4;          //show cc/bcc
   const S_rect rc_header(0, app.GetTitleBarHeight(), app.ScrnSX(), app.fdb.line_spacing*hdr_lines + app.fdb.line_spacing/2);
   ctrl_header->SetRect(rc_header);
   int bot = rc_header.y + 1 + app.fdb.line_spacing*5/2;
   rc = S_rect(border, bot, app.ScrnSX()-border*2, 0);
   int att_sy = app.icons_file->SizeY() + 8;
   //att_sy = Max(att_sy, fds.cell_size_y+app.fdb.cell_size_y+2);
   att_sy = Max(att_sy, (int)app.C_client::GetSoftButtonBarHeight());

   attach_browser.rc = S_rect(1, 0, app.ScrnSX()-2, att_sy);

   attach_browser.ResetAfterScreenResize();

   ComputeEditHeight();

   if(list_auto_complete.emails.size())
      InitAutoCompleteRect();
}

//----------------------------

void C_mode_write_mail::SetEditField(E_FIELD fld){

   edit_field = fld;
   if(ctrl_te_line){
      RemoveControl(ctrl_te_line);
      ctrl_te_line = NULL;
   }
   list_auto_complete.emails.clear();

   int hdr_lines = 2;
   if(fld < FIELD_BODY)
      hdr_lines = 4;          //show cc/bcc
   {
      S_rect rc_header = ctrl_header->GetRect();
      rc_header.sy = app.fdb.line_spacing*hdr_lines + app.fdb.line_spacing/2;
      ctrl_header->SetRect(rc_header);
   }

   if(fld==FIELD_BODY){
      SetFocus(ctrl_text_body);
      return;
   }
   SetFocus(NULL);

   if(fld==FIELD_ATTACHMENTS)
      return;
                              //init text editor
   dword ted_flgs = TXTED_ACTION_NEXT;
   switch(fld){
   case FIELD_SUBJECT: ted_flgs |= TXTED_ALLOW_PREDICTIVE; break;
   default: ted_flgs |= TXTED_EMAIL_ADDRESS;
   }
   ctrl_te_line = new(true) C_ctrl_text_entry_line(this, 500, ted_flgs);
   AddControl(ctrl_te_line);
   SetFocus(ctrl_te_line);

   int xx, yy = ctrl_header->GetRect().y + app.fdb.line_spacing/3;
   Cstr_w txt;
   switch(fld){
   case FIELD_TO:
      ctrl_te_line->SetCase(C_text_editor::CASE_LOWER);
      txt.Copy(rcpt_to);
      xx = app.GetTextWidth(app.GetText(TXT_TO), app.UI_FONT_BIG);
      break;
   case FIELD_CC:
      ctrl_te_line->SetCase(C_text_editor::CASE_LOWER);
      txt.Copy(rcpt_cc);
      xx = app.GetTextWidth(L"Cc:", app.UI_FONT_BIG);
      yy += app.fdb.line_spacing * 1;
      break;
   case FIELD_BCC:
      ctrl_te_line->SetCase(C_text_editor::CASE_LOWER);
      txt.Copy(rcpt_bcc);
      xx = app.GetTextWidth(L"Bcc:", app.UI_FONT_BIG);
      yy += app.fdb.line_spacing * 2;
      break;
   case FIELD_SUBJECT:
      ctrl_te_line->SetCase(C_text_editor::CASE_CAPITAL);
      txt = subject;
      xx = app.GetTextWidth(app.GetText(TXT_SUBJECT), app.UI_FONT_BIG);
      yy += app.fdb.line_spacing * 3;
      break;
   default:
      xx = yy = 0;
   }
   ctrl_te_line->SetText(txt);
   xx += app.fdb.letter_size_x*3/2;

   ctrl_te_line->SetRect(S_rect(xx, yy, app.ScrnSX() - app.fdb.cell_size_x/2 - xx, app.fdb.line_spacing));
   ctrl_te_line->SetCursorPos(ctrl_te_line->GetCursorPos());
}

//----------------------------

void C_mode_write_mail::AddRecipient(const Cstr_c &e_mail){

   C_vector<char> l;

   Cstr_c *str;
   switch(edit_field){
   case FIELD_TO: str = &rcpt_to; break;
   case FIELD_CC: str = &rcpt_cc; break;
   case FIELD_BCC: str = &rcpt_bcc; break;
   default:
      assert(0);
      return;
   }
   edited = true;

   l.reserve(str->Length()+100);
   l.insert(l.begin(), *str, *str+str->Length());
                              //remove last spaces and commas
   while(l.size() && (l.back()==' ' || l.back()==','))
      l.pop_back();
   if(l.size()){
      //l.push_back(',');
      l.push_back(' ');
   }
   int ss = l.size();
   l.insert(l.end(), e_mail, e_mail+e_mail.Length());
   l.push_back(0);
   *str = l.begin();

   SetEditField(edit_field);
   ctrl_te_line->SetCursorPos(Min(str->Length(), ctrl_te_line->GetMaxLength()), ss);
}

//----------------------------

void C_mode_write_mail::UpdateOriginalMessage(){

   if(orig_cnt){
      C_vector<S_message> &messages = orig_cnt->messages;
      for(int i=messages.size(); i--; ){
         S_message &msg = messages[i];
         if(!msg.marked)
            continue;

         if((set_flags&S_message::MSG_REPLIED) && !(msg.flags&S_message::MSG_REPLIED))
            msg.flags |= S_message::MSG_IMAP_REPLIED_DIRTY;
         if((set_flags&S_message::MSG_FORWARDED) && !(msg.flags&S_message::MSG_FORWARDED))
            msg.flags |= S_message::MSG_IMAP_FORWARDED_DIRTY;
         //msg.flags &= ~(S_message::MSG_REPLIED | S_message::MSG_FORWARDED);
         msg.flags |= set_flags;
         if(msg.IsDraft() || (msg.flags&msg.MSG_TO_SEND)){
                              //delete previous draft now
            if(GetParent()->Id()==C_mail_client::C_mode_mailbox::ID){
               C_mail_client::C_mode_mailbox &mod_mbox = (C_mail_client::C_mode_mailbox&)*GetParent();
               bool sync = (msg.flags&msg.MSG_SERVER_SYNC);
               bool redraw;
               app.DeleteMarkedOrSelectedMessages(mod_mbox, false, redraw, false);
               if(sync)
                  mod_mbox.MarkMessages(true, S_message::MSG_HIDDEN);
               app.Mailbox_RecalculateDisplayArea(mod_mbox);
               if(mod_mbox.GetParent()->Id()==C_mail_client::C_mode_folders_list::ID){
                  if(app.CleanEmptyTempImapFolders(mod_mbox.acc)){
                     app.FoldersList_InitView((C_mail_client::C_mode_folders_list&)*mod_mbox.GetParent());
                     app.mode->saved_parent = mod_mbox.GetParent();
                  }
               }

            }
         }
         msg.marked = false;
         orig_cnt->MakeDirty();
         break;
      }
   }
}

//----------------------------

bool C_mode_write_mail::SaveMessage(bool draft, bool upload_draft){

                              //if msg is totally empty, do not save it
   if(IsEmptyMessage())
      return false;

                              //set flags on original message (which is being marked now)
   UpdateOriginalMessage();

   C_message_container *cnt;
   C_mail_client::S_account &acc = GetAccount();
   {
      bool created;
      C_mail_client::C_mode_folders_list *mod_flds;
      if(GetParent()->Id()==C_mail_client::C_mode_mailbox::ID){
         C_mail_client::C_mode_mailbox &mod_mbox = (C_mail_client::C_mode_mailbox&)*GetParent();
         mod_flds = &(C_mail_client::C_mode_folders_list&)*mod_mbox.GetParent();
      }else
         mod_flds = &(C_mail_client::C_mode_folders_list&)*GetParent();
      Cstr_w fld_name;
      if(draft){
         fld_name = acc.GetDraftFolderName();
      }else
         fld_name = C_mail_client::S_account::default_outbox_folder_name;
      cnt = app.FindOrCreateImapFolder(acc, fld_name, created);
      if(created){
         app.FoldersList_InitView(*mod_flds);
         app.SaveAccounts();
      }
   }
   C_vector<S_message> &messages = cnt->messages;
   cnt->MakeDirty();
   messages.push_back(S_message());
   S_message &msg = messages.back();
   int sz = ctrl_text_body->GetTextLength();
   {
      Cstr_c tmp;
      tmp.ToUtf8(ctrl_text_body->GetText());
      if(!app.SaveMessageBody(*cnt, msg, tmp, tmp.Length(), "draft")){
         messages.pop_back();
         app.SaveAccounts();
         return false;
      }
   }
   //if(draft && mod_mbox)
      //SetMailboxSelection(*mod_mbox, messages.size()-1);
   msg.subject = subject.ToUtf8();
   msg.to_emails.Copy(rcpt_to);
   msg.cc_emails = rcpt_cc;
   msg.bcc_emails = rcpt_bcc;
   msg.message_id = in_reply_to_msg_id;
   msg.references = msg_references;
   msg.sender = current_identity;
   {
                              //convert email addresses to user-friendly names
      C_vector<Cstr_c> rcpts;
      if(ParseRecipients(rcpt_to, rcpts) && rcpts.size()){
         for(int i=0; i<rcpts.size(); i++){
            if(i)
               msg.to_names<<", ";
            const Cstr_c &rcpt = rcpts[i];
            S_contact con;
            if(app.FindContactByEmail(rcpt, con)){
               msg.to_names<<(app.AddressBook_GetName(con).ToUtf8());
            }else
               msg.to_names<<rcpt;
         }
         msg.to_names.Build();
      }
   }
   msg.size = sz*sizeof(wchar);
   S_date_time dt_curr; dt_curr.GetCurrent();
   msg.date = dt_curr.GetSeconds();
   msg.flags |= draft ? msg.MSG_DRAFT : msg.MSG_TO_SEND;
   if(draft && upload_draft)
      msg.flags |= msg.MSG_NEED_UPLOAD;
   switch(priority){
   case PRIORITY_LOW: msg.flags |= msg.MSG_PRIORITY_LOW; break;
   case PRIORITY_HIGH: msg.flags |= msg.MSG_PRIORITY_HIGH; break;
   }
   msg.attachments = attachments;
   for(int i=msg.attachments.Size(); i--; ){
      C_file ck;
      if(ck.Open(msg.attachments[i].filename.FromUtf8()))
         msg.size += ck.GetFileSize();
   }
   if(!msg.our_message_id.Length()){
                              //generate Message-Id if there's not one
      Cstr_c s;
      C_rnd_gen rnd;
      for(int i=0; i<10; i++)
         s<<int(rnd.Get(10));
      s<<'.' <<dt_curr.GetSeconds();
      s<<'@';
      s<<acc.primary_identity.email.RightFromPos(acc.primary_identity.email.Find('@')+1);
      msg.our_message_id = s;
   }

   app.SortMessages(messages, acc.IsImap());//, mod_mbox ? &mod_mbox->selection : NULL);
   cnt->SaveMessages(app.GetMailDataPath(), true);
   if(GetParent()->Id()==C_mail_client::C_mode_mailbox::ID){
      C_mail_client::C_mode_mailbox &mod_mbox = (C_mail_client::C_mode_mailbox&)*GetParent();
      app.GetVisibleMessagesCount(mod_mbox);
      app.Mailbox_RecalculateDisplayArea(mod_mbox);
   }
   return true;
}

//----------------------------

void C_mode_write_mail::SaveAndClose(bool ask, bool save_draft, bool upload_draft){

   C_mail_client::S_account &acc = GetAccount();
   if(ask && !IsEmptyMessage() && edited){
      bool allow_upload = acc.IsImap();
      const wchar *opts[] = { app.GetText(TXT_SAVE_DRAFT), app.GetText(TXT_UPLOAD_DRAFT), app.GetText(TXT_Q_DELETE_MESSAGE), app.GetText(TXT_CANCEL_WRITE) };
      int num_opts = 3;
      if(!allow_upload){
         opts[1] = opts[2];
         opts[2] = opts[3];
         --num_opts;
      }
      if(editing_draft)
         ++num_opts;
      CreateMultiSelectionMode(app, TXT_CANCEL_WRITE, NULL, opts, num_opts, this);
      return;
   }
   bool saved = (save_draft && SaveMessage(true, upload_draft));
   if(orig_cnt)
      orig_cnt->ClearAllMessageMarks();
   C_mode::Close();

   if(saved && acc.IsImap() && upload_draft){
                              //initiate draft upload
      C_message_container *fld = app.FindFolder(acc, acc.GetDraftFolderName());
      if(fld)
         app.SetModeConnection(acc, fld, C_mail_client::C_mode_connection::ACT_SEND_MAILS);
   }
}

//----------------------------

void C_mode_write_mail::SendMessage(){

   bool has_recipients = false;
                              //check recipients
   {
      C_vector<Cstr_c> addresses;
      for(int i=0; i<3; i++){
         Cstr_c &s = !i ? rcpt_to : i==1 ? rcpt_cc : rcpt_bcc;
         int l, r;
         if(!ParseRecipients(s, addresses, &l, &r)){
            SetEditField(!i ? FIELD_TO : i==1 ? FIELD_CC : FIELD_BCC);
            ctrl_te_line->SetCursorPos(r, l);
            Cstr_w err;
            err.Copy(s.Mid(l, r-l));
            app.ShowErrorWindow(TXT_ERR_INVALID_RECIPIENT, err);
            return;
         }
      }
      if(addresses.size())
         has_recipients = true;
   }
                              //some recipients must be specified
   if(!has_recipients){
      SetEditField(FIELD_TO);
      app.ShowErrorWindow(TXT_ERROR, TXT_ERR_NO_RECIPIENTS);
      return;
   }
                              //ok, save & go to mailbox
   SaveMessage(false, false);
   bool this_send_immediately = send_immediately;
   C_mail_client::S_account &acc = GetAccount();
   C_mode::Close(false);

   if(this_send_immediately){
      C_message_container *fld = app.FindFolder(acc, C_mail_client::S_account::default_outbox_folder_name);
      if(fld)
         app.SetModeConnection(acc, fld, C_mail_client::C_mode_connection::ACT_SEND_MAILS);
   }else
      app.RedrawScreen();
}

//----------------------------

void C_mode_write_mail::AddAttachment(const Cstr_w &filename, bool set_edited_flag){

   int num = attachments.Size();
   attach_browser.selection = num;
   attachments.Resize(num+1);
   attach_browser.MakeSelectionVisible();
   S_attachment &att = attachments[num];
   att.filename = filename.ToUtf8();
   int i = filename.FindReverse('\\');
   att.suggested_filename = filename+i+1;
   if(!num)
      ComputeEditHeight();
   if(set_edited_flag)
      edited = true;
   SetEditField(FIELD_ATTACHMENTS);
   app.RedrawScreen();
}

//----------------------------

void C_mode_write_mail::RemoveSelectedAttachment(){

   int num = attachments.Size();
   for(int i=attach_browser.selection+1; i<num; i++)
      attachments[i-1] = attachments[i];
   attachments.Resize(--num);
   if(num){
      attach_browser.selection = Min(attach_browser.selection, num-1);
      attach_browser.MakeSelectionVisible();
   }else{
                              //close attachment browser
      SetEditField(FIELD_BODY);
      ComputeEditHeight();
   }
   edited = true;
   app.RedrawScreen();
}

//----------------------------

void C_mode_write_mail::CreateSignaturesMenu(){

   if(!app.signatures.Size())
      app.LoadSignatures();
   menu = app.CreateMenu(*this);
   dword i;
   for(i=0; i<app.signatures.Size(); i++)
      menu->AddItem(app.signatures[i].name);
   if(i)
      menu->AddSeparator();
   menu->AddItem(TXT_EDIT_SIGNATURES);
   app.PrepareMenu(menu);
}

//----------------------------

void C_mode_write_mail::InitAutoCompleteRect(){

   assert(list_auto_complete.emails.size());
   C_list_mode_base &l = list_auto_complete;
   l.entry_height = app.fdb.line_spacing;
   if(app.HasMouse())
      l.entry_height = l.entry_height*3/2;

   l.sb = C_scrollbar();
   l.sb.total_space = list_auto_complete.emails.size();

   l.rc = S_rect(rc.x+app.fdb.cell_size_x, ctrl_header->GetRect().y + app.fdb.line_spacing*3/2 + edit_field*app.fdb.line_spacing, rc.sx-app.fdb.cell_size_x*2, rc.sy);
   int num_lines = l.rc.sy / l.entry_height;
   num_lines = Min(num_lines, l.sb.total_space);
   l.rc.sy = num_lines*l.entry_height;

   const int sb_width = app.GetScrollbarWidth();
   l.sb.rc = S_rect(l.rc.Right()-sb_width-1, l.rc.y+3, sb_width, l.rc.sy-6);

   l.sb.visible_space = num_lines;
   l.sb.SetVisibleFlag();
   l.selection = -1;
   l.EnsureVisible();
}

//----------------------------

void C_mode_write_mail::SelectAutoComplete(){

   Cstr_w s = ctrl_te_line->GetText();
   Cstr_c *fld;
   switch(edit_field){ default:
   case FIELD_TO: fld = &rcpt_to; break;
   case FIELD_CC: fld = &rcpt_cc; break;
   case FIELD_BCC: fld = &rcpt_bcc; break;
   }

   int ci = fld->FindReverse(' ');
   if(ci!=-1){
      s = s.Left(ci+1);
      //s<<' ';
   }else
      s.Clear();
   Cstr_w email; email.Copy(list_auto_complete.emails[list_auto_complete.selection].email);
   s<<email;
   ctrl_te_line->SetText(s);
   dword len = ctrl_te_line->GetTextLength();
   ctrl_te_line->SetCursorPos(len);//-(email.Length()));
   fld->Copy(s);
   edited = true;
}

//----------------------------

void C_mode_write_mail::AddAutoComplete(const Cstr_w &first_name, const Cstr_w &last_name, const Cstr_c &email, bool match_email){

   Cstr_w name = first_name;
   if(last_name.Length()){
      if(name.Length())
         name<<' ';
      name<<last_name;
   }
   //if(list_auto_complete.emails.size() >= 12)
      //return;

   for(int i=list_auto_complete.emails.size(); i--; ){
      const S_auto_complete &ac = list_auto_complete.emails[i];
      if(ac.name==name && ac.email==email)
         return;
   }
   S_auto_complete ac;
   ac.email = email;
   ac.name = name;
   ac.match_email = match_email;
   list_auto_complete.emails.push_back(ac);
}

//----------------------------

void C_mode_write_mail::TextEditNotify(bool cursor_moved, bool text_changed, bool &redraw){

   C_mode::TextEditNotify(cursor_moved, text_changed, redraw);
   if(text_changed)
      edited = true;
   if(text_changed && ctrl_te_line){
      redraw = true;
      const wchar *txt = ctrl_te_line->GetText();
      switch(edit_field){
      case FIELD_TO:
      case FIELD_CC:
      case FIELD_BCC:
         {
            Cstr_c tmp; tmp.Copy(txt);
            switch(edit_field){
            case FIELD_TO: rcpt_to = tmp; break;
            case FIELD_CC: rcpt_cc = tmp; break;
            case FIELD_BCC: rcpt_bcc = tmp; break;
            }
            
            list_auto_complete.emails.clear();
                              //make auto-complete info
                           //search in email addresses
                              //search partial text in address book, fill rest
            const char *cp = tmp;
            int ci = tmp.FindReverse(' ');
            if(ci!=-1){
               ++ci;
               while(cp[ci]==' ')
                  ++ci;
               cp += ci;
               txt += ci;
            }
            Cstr_c s = cp;
            if(s.Length()){
               s.ToLower();
               Cstr_w sw = txt;
               for(int i=sw.Length(); i--; )
                  sw.At(i) = text_utils::LowerCase(sw[i]);

               C_vector<S_contact_match> matches;
               app.CollectMatchingContacts(sw, s, matches);
               for(int i=0; i<matches.size(); i++){
                  const S_contact_match &con = matches[i];
                  dword mask = con.email_match_mask;
                  if(mask){
                     for(int mi=0; mi<3; mi++){
                        if(mask&(1<<mi))
                           AddAutoComplete(con.first_name, con.last_name, con.email[mi], true);
                     }
                  }else{
                              //add all emails
                     for(int j=0; j<3; j++){
                        if(con.email[j].Length())
                           AddAutoComplete(con.first_name, con.last_name, con.email[j], false);
                     }
                  }
               }
               if(list_auto_complete.emails.size())
                  InitAutoCompleteRect();
            }
         }
         break;
      case FIELD_SUBJECT: subject = txt; break;
      }
   }
}

//----------------------------

void C_mode_write_mail::ProcessMenu(int itm, dword menu_id){

   switch((menu_id<<24)|itm){
   case TXT_SEND:
      SendMessage();
      break;
      
      /*
   case TXT_SAVE_DRAFT:
      WriteMail_SaveAndClose(mod, false, true);
      break;
      */

   case TXT_EDIT:
      menu = app.CreateEditCCPSubmenu((ctrl_te_line ? ctrl_te_line->GetTextEditor() : ctrl_text_body->GetTextEditor()), menu);
      if(!ctrl_te_line){
         menu->AddSeparator();
         menu->AddItem(TXT_MARK, C_menu::HAS_SUBMENU);
      }
      app.PrepareMenu(menu);
      break;

   case C_mail_client::SPECIAL_TEXT_CUT:
   case C_mail_client::SPECIAL_TEXT_COPY:
   case C_mail_client::SPECIAL_TEXT_PASTE:
      app.ProcessCCPMenuOption(itm, ctrl_te_line ? ctrl_te_line->GetTextEditor() : ctrl_text_body->GetTextEditor());
      break;

   case TXT_MARK:
      menu = app.CreateMenu(*this);
      menu->AddItem(TXT_MARK_ALL);
      app.PrepareMenu(menu);
      break;

   case TXT_MARK_ALL:
      ctrl_text_body->SetCursorPos(ctrl_text_body->GetTextLength(), 0);
      break;

   case TXT_CANCEL_WRITE:
      SaveAndClose(true);
      return;

   case TXT_ADD_RECIPIENT:
      if(edit_field>=FIELD_SUBJECT)
         SetEditField(FIELD_TO);
      app.SetModeAddressBook(C_mail_client::AB_MODE_SELECT);
      break;

   case TXT_PRIORITY:
      menu = CreateMenu();
      menu->AddItem(TXT_HIGH, priority==PRIORITY_HIGH ? C_menu::MARKED : 0);
      menu->AddItem(TXT_NORMAL, priority==PRIORITY_NORMAL ? C_menu::MARKED : 0);
      menu->AddItem(TXT_LOW, priority==PRIORITY_LOW ? C_menu::MARKED : 0);
      app.PrepareMenu(menu);
      break;
   case TXT_LOW: priority = PRIORITY_LOW; break;
   case TXT_NORMAL: priority = PRIORITY_NORMAL; break;
   case TXT_HIGH: priority = PRIORITY_HIGH; break;

   case TXT_USE_IDENTITY:
      menu = CreateMenu(2);
      for(int i=0; i<identities.size(); i++){
         const S_identity &idn = identities[i];
         Cstr_w s = idn.display_name.FromUtf8();
         if(idn.email.Length())
            s.AppendFormat(L" <%>") <<idn.email;
         menu->AddItem(s, idn==current_identity ? C_menu::MARKED : 0);
      }
      app.PrepareMenu(menu);
      break;

   case TXT_SIGNATURES:
      CreateSignaturesMenu();
      break;

   case TXT_EDIT_SIGNATURES:
      app.SetModeEditSignatures();
      return;

   default:
      if(itm >= 0x10000){
         itm -= 0x10000;
         if(menu_id==2){
                              //identity
            assert(itm<identities.size());
            current_identity = identities[itm];
         }else if(menu_id==1){
                              //account to use
            assert(itm<(int)app.NumAccounts());
            C_mail_client::S_account &acc = app.accounts[itm];
            if(&acc!=&GetAccount()){
                              //sending account changed
               AddRef();
               if(saved_parent->Id()!=C_mail_client::C_mode_folders_list::ID){
                              //close mailbox mode
                  saved_parent = saved_parent->saved_parent;
               }
               C_mail_client::C_mode_accounts &mod_acc = (C_mail_client::C_mode_accounts&)*saved_parent->saved_parent;
               mod_acc.selection = itm;
                              //close folders list and replace by new
               app.mode = &mod_acc;

               C_mail_client::C_mode_folders_list &mod_fl = app.SetModeFoldersList(acc, false);
               saved_parent = &mod_fl;
               app.ActivateMode(*this);
               SetupCurrentIdentity(NULL);
            }
         }else{
                           //signature
            assert(itm < int(app.signatures.Size()));
                              //make sure we're in body field
            if(edit_field!=FIELD_BODY)
               SetEditField(FIELD_BODY);

            const Cstr_w &s = app.signatures[itm].body;
            ctrl_text_body->ReplaceSelection(s);
                              //put recent signature to front
            if(itm){
               while(itm--)
                  Swap(app.signatures[itm], app.signatures[itm+1]);
               app.SaveSignatures();
            }
            //app.RedrawScreen();
         }
      }
      break;

   case TXT_BROWSE_ATTACHMENTS:
      SetEditField(FIELD_ATTACHMENTS);
      break;

   case TXT_ADD_ATTACHMENT:
      SetAttachmentBrowser();
      break;

   case TXT_REMOVE_ATTACHMENT:
      RemoveSelectedAttachment();
      break;

   case TXT_CFG_SEND_IMMEDIATELY:
      send_immediately = !send_immediately;
      break;

   case TXT_USE_ACCOUNT:
      {
         menu = CreateMenu(1);
         for(dword i=0; i<app.NumAccounts(); i++){
            const C_mail_client::S_account &acc = app.accounts[i];
            menu->AddItem(acc.name, (&acc==&GetAccount()) ? C_menu::MARKED : 0);
         }
         app.PrepareMenu(menu);
      }
      break;
   }
}

//----------------------------

void C_mode_write_mail::InitMenu(){

   menu->AddItem(TXT_SEND, 0, C_application_ui::send_key_name, NULL, C_mail_client::BUT_SEND);
   //menu->AddItem(TXT_SAVE_DRAFT);
   app.AddEditSubmenu(menu);
   menu->AddItem(TXT_ADD_ATTACHMENT, 0, NULL, NULL, C_mail_client::BUT_ADD_ATTACHMENT);
   if(attachments.Size()){
      if(edit_field==FIELD_ATTACHMENTS)
         menu->AddItem(TXT_REMOVE_ATTACHMENT);
      else
      if(!app.HasMouse())
         menu->AddItem(TXT_BROWSE_ATTACHMENTS);
   }
   menu->AddItem(TXT_ADD_RECIPIENT);
   menu->AddSeparator();
   menu->AddItem(TXT_CFG_SEND_IMMEDIATELY, send_immediately ? C_menu::MARKED : 0);
   if(//!is_new_msg &&
      app.NumAccounts()>1)
      menu->AddItem(TXT_USE_ACCOUNT, C_menu::HAS_SUBMENU);
   if(identities.size()>1)
      menu->AddItem(TXT_USE_IDENTITY, C_menu::HAS_SUBMENU);
   menu->AddItem(TXT_PRIORITY, C_menu::HAS_SUBMENU);
   menu->AddItem(TXT_SIGNATURES, C_menu::HAS_SUBMENU, NULL, NULL, C_mail_client::BUT_ADD_SIGNATURE);
   menu->AddSeparator();
   menu->AddItem(TXT_CANCEL_WRITE);
}

//----------------------------

void C_mode_write_mail::ProcessInput(S_user_input &ui, bool &redraw){

   bool was_edit_body = (edit_field==FIELD_BODY);  //controls fix
   //C_mode::ProcessInput(ui, redraw);
//#ifdef USE_MOUSE
   if(list_auto_complete.emails.size()){
      C_list_mode_base &l = list_auto_complete;
      C_scrollbar::E_PROCESS_MOUSE pm = app.ProcessScrollbarMouse(l.sb, ui);
      switch(pm){
      case C_scrollbar::PM_PROCESSED:
      case C_scrollbar::PM_CHANGED:
         redraw = true;
         break;
      default:
         if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
            if(ui.CheckMouseInRect(l.rc)){
               int line = (ui.mouse.y-l.rc.y) / l.entry_height;
               if(line>=0 && line<list_auto_complete.emails.size()){
                  l.selection = line+l.top_line;
                  SelectAutoComplete();
                  ui.key = K_ENTER;
                  redraw = true;
               }else assert(0);
            }else{
               list_auto_complete.emails.clear();
               redraw = true;
            }
         }
      }
   }else{
      C_mode::ProcessInput(ui, redraw);
      if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
         const S_rect &rc_header = ctrl_header->GetRect();
         if(ui.CheckMouseInRect(rc_header)){
                           //activate header fields
            int i = (ui.mouse.y - rc_header.y - app.fdb.line_spacing/3) / app.fdb.line_spacing;
            if(edit_field>=FIELD_BODY && i)
               i += 2;
            if(i>=0 && i<=3){
               E_FIELD fld = (E_FIELD)i;
               if(edit_field!=fld){
                  SetEditField(fld);
                  redraw = true;
               }else
               if(!ctrl_te_line || !ui.CheckMouseInRect(ctrl_te_line->GetRect()))
                  ui.key = K_ENTER;
            }
         }else
         if(ui.CheckMouseInRect(rc)){
            if(edit_field!=FIELD_BODY){
               SetEditField(FIELD_BODY);
               redraw = true;
            }
         }else
         if(attachments.Size() && ui.CheckMouseInRect(attach_browser.rc)){
            if(edit_field!=FIELD_ATTACHMENTS){
               SetEditField(FIELD_ATTACHMENTS);
               ui.mouse_buttons = 0;
               redraw = true;
            }//else
            //if(attach_browser.Tick(ui, int num_atts, bool &redraw);
         }
      }
   }
//#endif

   switch(ui.key){
   case K_SEND:
      SendMessage();
      return;
   case K_TAB:
      SkipToAdjacentField(!(ui.key_bits&GKEY_SHIFT));
      redraw = true;
      break;
   }
   switch(edit_field){
   case FIELD_SUBJECT:
   case FIELD_TO:
   case FIELD_CC:
   case FIELD_BCC:
      if(IsActive()){
         if(list_auto_complete.emails.size()){
            int &sel = list_auto_complete.selection;
            switch(ui.key){
            case K_CURSORUP:
            case K_CURSORDOWN:
               {
                  if(ui.key==K_CURSORUP){
                     if(sel<=0)
                        sel = list_auto_complete.emails.size()-1;
                     else
                        --sel;
                  }else{
                     if(++sel>=list_auto_complete.emails.size())
                        sel = 0;
                  }
                  list_auto_complete.EnsureVisible();

                  SelectAutoComplete();
                  redraw = true;
               }
               break;
            case K_ENTER:
               if(sel!=-1){
                              //add space at end
                  Cstr_w s = ctrl_te_line->GetText();
                  s<<L" ";
                  ctrl_te_line->SetText(s);

                  dword l = ctrl_te_line->GetTextLength();
                  ctrl_te_line->SetCursorPos(l);
               }
               list_auto_complete.emails.clear();
               redraw = true;
               break;
            case K_RIGHT_SOFT:
               list_auto_complete.emails.clear();
               redraw = true;
               break;
            }
         }else
         switch(ui.key){
         case K_ENTER:
            switch(edit_field){
            case FIELD_TO:
            case FIELD_CC:
            case FIELD_BCC:
                           //add recipient
               app.SetModeAddressBook(C_mail_client::AB_MODE_SELECT);
               return;
            }
            break;

         case K_CURSORUP:
            switch(edit_field){
            case FIELD_CC: SetEditField(FIELD_TO); redraw = true; break;
            case FIELD_BCC: SetEditField(FIELD_CC); redraw = true; break;
            case FIELD_SUBJECT:
               if(was_edit_body)
                  break;
               SetEditField(FIELD_BCC); redraw = true;
               break;
            }
            break;

         case K_CURSORDOWN:
            switch(edit_field){
            case FIELD_SUBJECT:
                              //back do body editor
               SetEditField(FIELD_BODY);
               redraw = true;
               break;
            case FIELD_TO: SetEditField(FIELD_CC); redraw = true; break;
            case FIELD_CC: SetEditField(FIELD_BCC); redraw = true; break;
            case FIELD_BCC: SetEditField(FIELD_SUBJECT); redraw = true; break;
            }
            break;
         }
      }
      break;

   case FIELD_ATTACHMENTS:
      switch(ui.key){
      case K_CURSORUP:
         SetEditField(FIELD_BODY);
         redraw = true;
         break;
      default:
         {
            bool sel_changed, popup_touch_menu;
            if(!attach_browser.Tick(ui, attachments.Size(), redraw, sel_changed, popup_touch_menu)){
#ifdef USE_MOUSE
               if(popup_touch_menu){
                  menu = app.CreateTouchMenu();
                  for(int i=0; i<4; i++)
                     menu->AddSeparator();
                  menu->AddItem(TXT_REMOVE_ATTACHMENT);
                  app.PrepareTouchMenu(menu, ui);
               }
#endif
               break;
            }
         }
                              //flow...
      case K_ENTER:
         {
                              //open selected attachment
            const S_attachment &att = attachments[attach_browser.selection];
            if(C_client_viewer::OpenFileForViewing(&app, att.filename.FromUtf8(), att.suggested_filename))
               return;
            if(app.OpenFileBySystem(att.filename.FromUtf8()))
               return;
         }
         break;
      case K_DEL:
         RemoveSelectedAttachment();
         break;
      }
      break;
   }
}

//----------------------------

void C_mode_write_mail::DrawHeader() const{

   dword col_text = app.GetColor(app.COL_TEXT_POPUP);
   int xx;
   int x = app.fdb.letter_size_x/2;
   const S_rect &rc_header = ctrl_header->GetRect();
   int y = rc_header.y;
   //FillRect(rc_header, col_bgnd);
   app.DrawDialogBase(rc_header, false, &rc_header);
   y += app.fdb.line_spacing/3;

   xx = app.DrawString(app.GetText(TXT_TO), x, y, app.UI_FONT_BIG, 0, col_text);
   xx += x + app.fdb.letter_size_x;
   //const C_text_editor &te = *text_editor;
   if(edit_field == FIELD_TO){
      //app.DrawEditedText(te);
   }else{
      app.DrawStringSingle(rcpt_to, xx, y, app.UI_FONT_BIG, 0, col_text, -int(rc_header.sx - 2 - xx));
   }
   y += app.fdb.line_spacing;
   if(edit_field < FIELD_BODY){
                           //draw cc/bcc fields
      xx = app.DrawString(L"Cc:", x, y, app.UI_FONT_BIG, 0, col_text);
      xx += x + app.fdb.letter_size_x;
      if(edit_field == FIELD_CC){
         //app.DrawEditedText(te);
      }else{
         app.DrawStringSingle(rcpt_cc, xx, y, app.UI_FONT_BIG, 0, col_text, -int(rc_header.sx - 2 - xx));
      }
      y += app.fdb.line_spacing;

      xx = app.DrawString(L"Bcc:", x, y, app.UI_FONT_BIG, 0, col_text);
      xx += x + app.fdb.letter_size_x;
      if(edit_field == FIELD_BCC){
         //app.DrawEditedText(te);
      }else{
         app.DrawStringSingle(rcpt_bcc, xx, y, app.UI_FONT_BIG, 0, col_text, -int(rc_header.sx - 2 - xx));
      }
      y += app.fdb.line_spacing;

      app.DrawHorizontalLine(0, rc_header.Bottom(), app.ScrnSX(), 0x80000000);

      app.DrawShadow(rc_header, false);
   }

   xx = app.DrawString(app.GetText(TXT_SUBJECT), x, y, app.UI_FONT_BIG, 0, col_text);
   xx += x + app.fdb.letter_size_x;
   if(edit_field == FIELD_SUBJECT){
      //app.DrawEditedText(te);
   }else{
      app.DrawString(subject, xx, y, app.UI_FONT_BIG, 0, col_text, -int(rc_header.sx - 2 - xx));
   }
}

//----------------------------

void C_mode_write_mail::Draw() const{

   C_mode::Draw();
   if(priority!=PRIORITY_NORMAL){
                              //draw priority
      const C_image *img = app.msg_icons[priority==PRIORITY_HIGH ? C_mail_client::MESSAGE_ICON_PRIORITY_HIGH : C_mail_client::MESSAGE_ICON_PRIORITY_LOW];
      img->Draw(1, 1);
   }
   /*
   if(edit_field < FIELD_BODY)
      app.DrawSimpleShadow(rc, true);
   else
      app.DrawShadow(rc, true);
      */

   attach_browser.Draw(attachments, edit_field==FIELD_ATTACHMENTS ? 1 : 0);
   if(list_auto_complete.emails.size()){
      const C_list_mode_base &l = list_auto_complete;
      const S_rect &lrc = l.rc;
      app.FillRect(lrc, 0xffffffff);
      app.DrawOutline(lrc, 0xff000000);
      app.DrawShadow(lrc, true);
      dword col_text = 0xff000000;
                              //draw entries
      for(int i=0; i<l.sb.visible_space; i++){
         int ei = i + l.top_line;
         dword col = col_text;
         int y = l.rc.y + i * l.entry_height;
         if(ei==l.selection){
            col ^= 0xffffff;
            app.FillRect(S_rect(l.rc.x, y, l.rc.sx-l.sb.rc.sx-2, l.entry_height), 0xff0000ff);
         }
         const S_auto_complete &ac = list_auto_complete.emails[ei];
         int x = l.rc.x+app.fdb.cell_size_x/2;
         y += (l.entry_height-app.fdb.line_spacing)/2;
         int max_w = l.rc.sx-l.sb.rc.sx-app.fdb.cell_size_x;
         dword col_half = MulAlpha(col, 0x8000);
         int w = app.DrawStringSingle(ac.email, x, y, app.UI_FONT_BIG, 0, ac.match_email ? col : col_half, -max_w);
         if(ac.name.Length()){
            x += w;
            max_w -= w;
            Cstr_w s; s.Format(L" (%)") <<ac.name;
            app.DrawString(s, x, y, app.UI_FONT_BIG, 0, !ac.match_email ? col : col_half, -max_w);
         }
      }
      app.DrawScrollbar(l.sb);
   }
}

//----------------------------
