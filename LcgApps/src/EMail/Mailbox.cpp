#include "..\Main.h"
#include <Ui\MultiQuestion.h>
#include <UI\TextEntry.h>
#include "Main_Email.h"

//----------------------------

const int SUBJ_SCROLL_DELAY = 2000; //delay to scroll long subject

//----------------------------

bool C_mail_client::C_mode_mailbox::IsAnyMarked() const{
   for(int i=num_vis_msgs; i--; ){
      if(GetMessage(i).marked)
         return true;
   }
   return false;
}

//----------------------------

void C_message_container::ClearAllMessageMarks(){

   for(int i=messages.size(); i--; )
      messages[i].marked = false;
}

//----------------------------

class C_mode_mailbox_imp: public C_mail_client::C_mode_mailbox{
public:
   C_mode_mailbox_imp(C_mail_client &_app, C_mail_client::S_account &_acc, C_message_container *_fld):
      C_mode_mailbox(_app, _acc, _fld)
   {}
   virtual void ProcessInput(S_user_input &ui, bool &redraw);
};

//----------------------------
//----------------------------

C_mail_client::C_mode_mailbox &C_mail_client::SetModeMailbox(S_account &acc, C_message_container *folder, int top_line){

   C_mode_mailbox &mod = *new(true) C_mode_mailbox_imp(*this, acc, folder);
   CreateTimer(mod, 50);
   LoadMessages(mod.GetContainer());

   mod.GetContainer().ClearAllMessageMarks();

   GetVisibleMessagesCount(mod);

   if(!config_mail.sort_descending && 
      (config_mail.sort_mode==S_config_mail::SORT_BY_DATE || config_mail.sort_mode==S_config_mail::SORT_BY_RECEIVE_ORDER) && 
      mod.GetNumEntries())
      mod.selection = mod.GetNumEntries()-1;
   InitLayoutMailbox(mod);

   //mod.top_line = Max(0, Min(top_line, sb.total_space-sb.visible_space));

   ActivateMode(mod);
   return mod;
}

//----------------------------

void C_mail_client::C_mode_mailbox::InitWidthLength(){

   int max_x = GetMaxX();
   subj_draw_max_x = max_x;
   subj_draw_max_x -= app.fds.letter_size_x;
   subj_draw_shift = app.msg_icons[MESSAGE_ICON_NEW]->SizeX() + 6;
   subj_draw_max_x_today = subj_draw_max_x;
   subj_draw_max_x_last_year = subj_draw_max_x;
                           //reserve space for date
   S_date_time dt; dt.year = 2009; dt.month = dt.OCT; dt.day = 26;
   Cstr_w s;
   app.GetDateString(dt, s, true, false);
   subj_draw_max_x -= app.GetTextWidth(s, app.UI_FONT_SMALL);
   subj_draw_max_x_today -= app.GetTextWidth(L"12:34", app.UI_FONT_SMALL);
   app.GetDateString(dt, s, true, true);
   subj_draw_max_x_last_year -= app.GetTextWidth(s, app.UI_FONT_SMALL);
}

//----------------------------

void C_mail_client::InitLayoutMailbox(C_mode_mailbox &mod){

   const int border = 2;
   const int top = GetTitleBarHeight();
   mod.rc = S_rect(border, top, ScrnSX()-border*2, ScrnSY()-top-GetSoftButtonBarHeight()-border*2);

                           //compute # of visible lines, and resize rectangle to whole lines
   mod.entry_height = fdb.line_spacing + fds.line_spacing;
   if(config.flags&config_mail.CONF_SHOW_PREVIEW){
                              //compute size of preview
      if(IsWideScreen()){
         mod.sb.visible_space = mod.rc.sy;
         const int prv_sx = mod.rc.sx*config_mail.tweaks.preview_area_percent/100;
         mod.rc.sx -= prv_sx;
         mod.rc_preview = S_rect(mod.rc.Right()+4, mod.rc.y, prv_sx-4, mod.rc.sy);
      }else{
         const int prv_sy = mod.rc.sy*config_mail.tweaks.preview_area_percent/100;
         int bot = mod.rc.Bottom() + 2;
         mod.rc.sy -= prv_sy;
         mod.sb.visible_space = mod.rc.sy;
         mod.rc_preview = S_rect(border, mod.rc.Bottom()+4, mod.rc.sx, prv_sy);
         mod.rc_preview.sy = bot - mod.rc_preview.y;
      }
      const int sb_width = GetScrollbarWidth();
      mod.sb_preview.visible_space = mod.rc_preview.sy;
      mod.sb_preview.rc = S_rect(mod.rc_preview.Right()-sb_width-1, mod.rc_preview.y+3, sb_width, mod.rc_preview.sy-6);
      {
         mod.text_info.rc = mod.rc_preview;
         mod.text_info.rc.x += fdb.letter_size_x/2;
         mod.text_info.rc.sx -= sb_width + fdb.letter_size_x;
      }
   }else{
      mod.sb.visible_space = mod.rc.sy;
   }

                           //init scrollbar
   const int width = GetScrollbarWidth();
   C_scrollbar &sb = mod.sb;
   sb.rc = S_rect(mod.rc.Right()-width-1, mod.rc.y+1, width, mod.rc.sy-2);
   sb.total_space = mod.num_vis_msgs * mod.entry_height;
   sb.SetVisibleFlag();

                              //compute width of subject
   mod.InitWidthLength();

   mod.EnsureVisible();
   SetMailboxSelection(mod, mod.selection);
}

//----------------------------

void C_mail_client::GetVisibleMessagesCount(C_mode_mailbox &mod) const{

   if(mod.GetContainer().flags&C_message_container::FLG_NEED_SORT)
      SortMessages(mod.GetMessages(), mod.IsImap(), &mod.selection);

   dword num_find = mod.find_messages.size();
   C_vector<S_message> &messages = mod.GetMessages();
   if(mod.show_hidden)
      mod.num_vis_msgs = num_find ? num_find : messages.size();
   else{
      dword num_msg = num_find ? num_find : messages.size();
      for(mod.num_vis_msgs=0; mod.num_vis_msgs < num_msg; mod.num_vis_msgs++){
         const S_message &msg = mod.GetMessage(mod.num_vis_msgs);
         if(config_mail.tweaks.show_only_unread_msgs){
            bool unseen = (msg.IsRead() && !(msg.flags&S_message::MSG_DRAFT) && !(msg.flags&S_message::MSG_TO_SEND));
            if(unseen)
               break;
         }
         if(msg.flags&S_message::MSG_HIDDEN)
            break;
      }
   }
}

//----------------------------

void C_mail_client::SetMailboxSelection(C_mode_mailbox &mod, int sel){

   mod.selection = Abs(sel);
   if(sel>=0)
      mod.EnsureVisible();

   {
      S_date_time dt;
      dt.GetCurrent();

      dt.hour = dt.minute = dt.second = 0;
      dt.MakeSortValue();
      mod.today_begin = dt.sort_value;

      dt.month = dt.day = 0;
      dt.MakeSortValue();
      mod.this_year_begin = dt.sort_value;
   }

   mod.text_info.Clear();

   C_scrollbar &sb = mod.sb_preview;
   sb.visible = false;
   mod.subj_scroll_phase = mod.SUBSCRL_NO;
   mod.subj_scroll_len = 0;
   if(mod.num_vis_msgs){
      S_message &msg = mod.GetMessage(mod.selection);
      bool deleted = msg.IsDeleted();
      if(msg.HasBody() && (config.flags&config_mail.CONF_SHOW_PREVIEW) && !deleted){

         if(!OpenMessageBody(mod.GetContainer(), msg, mod.text_info, true)){
                              //invalid file? forget retrieval
            msg.body_filename.Clear();
            mod.GetContainer().MakeDirty();
         }
         if(msg.HasBody()){

            mod.text_info.ts.font_index = config.viewer_font_index;

                              //count lines
            CountTextLinesAndHeight(mod.text_info, config.viewer_font_index);

            //if(mod.text_info.HasDefaultBgndColor())
               //mod.text_info.bgnd_color = GetColor(COL_LIGHT_GREY) & 0xffffff;
               mod.text_info.ts.text_color = GetColor(COL_TEXT_POPUP);
            //mod.text_info.ts.bgnd_color = 0xffffffff;

            sb.pos = 0;
            sb.total_space = mod.text_info.total_height;
            sb.SetVisibleFlag();
         }
      }
                              //check if we'll scroll subject
      if(!deleted){
         bool old_year = (msg.date<mod.this_year_begin);
         int sw = GetTextWidth(msg.subject.FromUtf8(), UI_FONT_SMALL);
         int max_x = (msg.date>=mod.today_begin) ? mod.subj_draw_max_x_today : old_year ? mod.subj_draw_max_x_last_year : mod.subj_draw_max_x;
         int max_w = max_x - mod.rc.x - mod.subj_draw_shift;
         if(config_mail.sort_by_threads && !mod.find_messages.size())
            max_w -= mod.GetMsgDrawLevelOffset(msg.thread_level);
         //if(msg.date >= mod.today_begin){
         if(sw > max_w){
            mod.subj_scroll_len = (sw - max_w + 1)<<16;
            mod.subj_scroll_count = SUBJ_SCROLL_DELAY;
            mod.subj_scroll_phase = mod.SUBSCRL_WAIT_1;
         }
      }
   }
#ifdef USE_MOUSE
   Mailbox_SetButtonsState(mod);
#endif
}

//----------------------------

void C_mail_client::MsgDetailsHeaders(){

   C_mode_mailbox *mod;
   if(mode->Id()==C_mode_read_mail_base::ID)
      mod = &((C_mode_read_mail_base&)*mode).GetMailbox();
   else
    mod = &(C_mode_mailbox&)*mode;
   S_connection_params p; p.message_index = mod->GetRealMessageIndex(mod->selection);
   SetModeConnection(mod->acc, mod->folder, C_mode_connection::ACT_GET_MSG_HEADERS, &p);
}

//----------------------------

void C_mail_client::MailboxUpdateImapIdleFolder(C_mode_mailbox &mod, bool also_delete){

   S_account &acc = mod.acc;
   if(acc.IsImap()){
      if(acc.background_processor.state==acc.UPDATE_IDLING && acc.selected_folder==mod.folder){
                              //update flags and deletions now
         for(int i=mod.folder->messages.size(); i--; ){
            const S_message &msg = mod.folder->messages[i];
            if(msg.flags&(msg.MSG_DELETED_DIRTY|msg.MSG_IMAP_FLAGGED_DIRTY|msg.MSG_IMAP_READ_DIRTY|msg.MSG_IMAP_REPLIED_DIRTY|msg.MSG_IMAP_FORWARDED_DIRTY)){
               ImapIdleUpdateFlags(*acc.background_processor.GetMode(), also_delete);
               break;
            }
         }
      }
   }
}

//----------------------------

void C_mail_client::MailboxBack(C_mode_mailbox &mod){

   if(mod.find_messages.size()){
      MailboxResetSearch(mod);
      RedrawScreen();
      return;
   }

                              //clean empty temp imap floders
   bool deleted = false;
   if(mod.folder && mod.folder->IsTemp()){
      deleted = CleanEmptyTempImapFolders(mod.acc);
      if(deleted)
         FoldersList_InitView((C_mode_folders_list&)*mod.GetParent());
   }
   S_account &acc = mod.acc;
   MailboxUpdateImapIdleFolder(mod, true);
                              //clear recent flags
   for(int i=mod.GetMessages().size(); i--; ){
      S_message &msg = mod.GetMessages()[i];
      if(msg.IsRecent()){
         msg.flags &= ~msg.MSG_RECENT;
         mod.GetContainer().MakeDirty();
      }
   }

   if(!deleted && mod.GetContainer().msg_folder_id){
      mod.GetContainer().SaveAndUnloadMessages(mail_data_path);
   }
   CloseMode(mod);

   if(mode->Id()==C_mode_folders_list::ID){
      if(acc.IsImap()){
         if(config_mail.tweaks.imap_go_to_inbox)
            CloseMode(*mode);
      }else{
         if(acc.NumFolders()==1)
            CloseMode(*mode);
      }
   }
}

//----------------------------

void C_mail_client::GetMovableMessages(C_mode_mailbox &mod, C_vector<S_message*> &msgs) const{

   bool is_imap = mod.acc.IsImap();

   if(mod.IsAnyMarked()){
      for(dword i=0; i<mod.num_vis_msgs; i++){
         S_message &msg = mod.GetMessage(i);
         if(msg.marked)
            msgs.push_back(&msg);
      }
   }else
      msgs.push_back(&mod.GetMessage(mod.selection));

   for(int i=msgs.size(); i--; ){
      S_message &msg = *msgs[i];
      bool ok = true;
      if(is_imap)
         ok = (msg.flags&msg.MSG_SERVER_SYNC);
      else
         ok = msg.HasBody();
      ok = (ok && !(msg.flags&(msg.MSG_DELETED|msg.MSG_DRAFT|msg.MSG_TO_SEND|msg.MSG_PARTIAL_DOWNLOAD)));
      if(!ok)
         msgs.remove_index(i);
   }
}

//----------------------------

void GetMessageRecipients(const S_message &msg, const Cstr_c &ignore, C_vector<Cstr_c> &addresses){

   ParseRecipients(msg.to_emails, addresses);
   ParseRecipients(msg.cc_emails, addresses);
   for(int i=addresses.size(); i--; ){
      if(!text_utils::CompareStringsNoCase(addresses[i], ignore))
         addresses.remove_index(i);
   }
}

//----------------------------

void C_mail_client::C_mode_mailbox::MarkMessages( bool set_flags, dword flag, bool update_push_mail)
{

   if(IsAnyMarked()){
      for(int i=num_vis_msgs; i--; ){
         S_message &msg = GetMessage(i);
         if(msg.marked){
            if(flag==S_message::MSG_READ){
               msg.flags |= msg.MSG_IMAP_READ_DIRTY;
               if(app.config_mail.tweaks.show_only_unread_msgs)
                  GetContainer().flags |= C_message_container::FLG_NEED_SORT;
            }else if(flag==S_message::MSG_FLAGGED) msg.flags |= msg.MSG_IMAP_FLAGGED_DIRTY;
            if(set_flags)
               msg.flags |= flag;
            else
               msg.flags &= ~flag;
            msg.marked = false;
         }
      }
   }else
   if(num_vis_msgs){
      S_message &msg = GetMessage(selection);
      if(set_flags)
         msg.flags |= flag;
      else
         msg.flags &= ~flag;
      if(flag==S_message::MSG_READ){
         msg.flags |= msg.MSG_IMAP_READ_DIRTY;
         if(app.config_mail.tweaks.show_only_unread_msgs)
            GetContainer().flags |= C_message_container::FLG_NEED_SORT;
      }else if(flag==S_message::MSG_FLAGGED) msg.flags |= msg.MSG_IMAP_FLAGGED_DIRTY;
   }
   if(flag==S_message::MSG_HIDDEN){
      app.MailboxResetSearch(*this);
      app.SortMessages(GetMessages(), IsImap());
      app.Mailbox_RecalculateDisplayArea(*this);
   }
   GetContainer().MakeDirty();
   if(flag==S_message::MSG_READ){
      app.UpdateUnreadMessageNotify();
   }
                              //server flags are propagated to idle server immediately
   switch(flag){
   case S_message::MSG_READ:
   case S_message::MSG_FLAGGED:
      if(update_push_mail)
         app.MailboxUpdateImapIdleFolder(*this, false);
      break;
   }
}

//----------------------------
#ifdef USE_MOUSE

void C_mail_client::Mailbox_SetButtonsState(C_mode_mailbox &mod) const{

   mod.enabled_buttons = 2;
   if(!mod.IsOfflineFolder())
      mod.enabled_buttons |= 1;
   if(mod.num_vis_msgs){
      mod.enabled_buttons |= 4;
      if(!mod.IsAnyMarked()){
         const S_message &msg = mod.GetMessage(mod.selection);
         if(!msg.IsDeleted() && msg.HasBody() && !(msg.flags&(msg.MSG_DRAFT|msg.MSG_TO_SEND)))
            mod.enabled_buttons |= 8;
      }else
         mod.enabled_buttons |= 8;
   }
}

#endif
//----------------------------

static bool CheckPartialStringMatch(const wchar *s, dword s_len, const Cstr_w &match){

   int num_c = s_len - match.Length();
   if(num_c < 0 || !s_len)
      return false;
   const wchar *p = match;

   ++num_c;
   for( ; num_c--; ++s){
                              //compare the strings
      const wchar *s1 = p, *s2 = s;
      while(true){
         dword c1 = *s1++;
         if(!c1)
            return true;
         dword c2 = text_utils::LowerCase(*s2++);
         if(c1!=c2)
            break;
      }
   }
   return false;
}

//----------------------------

void C_mail_client::MailboxSearchMessagesMode(int option){

   C_mode_mailbox &mod = (C_mode_mailbox&)*mode;
   MailboxSearchMessages(mod, (C_mode_mailbox::E_SEARCH_MODE)option, NULL);
}

//----------------------------

void C_mail_client::SkipReFwd(const char *&cp){
   while(text_utils::CheckStringBegin(cp, "re:") || text_utils::CheckStringBegin(cp, "fw:") || text_utils::CheckStringBegin(cp, "fwd:"))
      text_utils::SkipWS(cp);
}

//----------------------------

void C_mail_client::MailboxSearchMessages(C_mode_mailbox &mod, C_mode_mailbox::E_SEARCH_MODE search_mode, const wchar *_what){

   if(search_mode==mod.SEARCH_LAST){
      const wchar *opts[] = { GetText(TXT_SORT_SUBJECT), GetText(TXT_MESSAGE), GetText(TXT_SORT_SENDER), GetText(TXT_RECIPIENT) };

      //C_client_multi_question::CreateMode(this, TXT_SEARCH, NULL, opts, 4, (C_client_multi_question::t_MultiQuestionSelect)&C_mail_client::MailboxSearchMessagesMode);
      CreateMultiSelectionMode(*this, TXT_SEARCH, NULL, opts, 4, &mod);
      return;
   }
   if(!_what || !*_what){
      Cstr_w init_text;
      if(mod.num_vis_msgs){
         const S_message &msg = mod.GetMessage(mod.selection);
         switch(search_mode){
         case C_mode_mailbox::SEARCH_SUBJECT:
            {
               const char *cp = msg.subject;
               SkipReFwd(cp);
               init_text = Cstr_c(cp).FromUtf8();
            }
            break;
         case C_mode_mailbox::SEARCH_SENDER: init_text.Copy(msg.sender.email); break;
         case C_mode_mailbox::SEARCH_RECIPIENT: init_text.Copy(msg.to_emails); break;
         }
      }
      class C_text_entry: public C_text_entry_callback{
         C_mail_client &app;
         C_mode_mailbox &mod;
         C_mode_mailbox::E_SEARCH_MODE mode;

         virtual void TextEntered(const Cstr_w &txt){
            app.MailboxSearchMessages(mod, mode, txt);
         }
      public:
         C_text_entry(C_mail_client &a, C_mode_mailbox &m, C_mode_mailbox::E_SEARCH_MODE _md): app(a), mod(m), mode(_md){}
      };
      CreateTextEntryMode(*this, TXT_SEARCH, new(true) C_text_entry(*this, mod, search_mode), true, 200, init_text);
      return;
   }

   C_vector<dword> find_results;
   Cstr_w what = _what;
   for(int i=what.Length(); i--; )
      what.At(i) = text_utils::LowerCase(what[i]);

   what.ToLower();

   assert(what.Length());

   for(dword i=0; i<mod.num_vis_msgs; i++){
      const S_message &msg = mod.GetMessage(i);
      bool match = false;
      switch(search_mode){
      case C_mode_mailbox::SEARCH_SENDER:
         {
            Cstr_w s = msg.sender.display_name.FromUtf8();
            match = CheckPartialStringMatch(s, s.Length(), what);
            if(!match){
               Cstr_w s1; s1.Copy(msg.sender.email);
               match = CheckPartialStringMatch(s1, s1.Length(), what);
            }
         }
         break;
      case C_mode_mailbox::SEARCH_RECIPIENT:
         {
            Cstr_w s = msg.to_names.FromUtf8();
            match = CheckPartialStringMatch(s, s.Length(), what);
            if(!match){
               Cstr_w s1; s1.Copy(msg.to_emails);
               match = CheckPartialStringMatch(s1, s1.Length(), what);
            }
         }
         break;
      case C_mode_mailbox::SEARCH_SUBJECT:
         {
            Cstr_w s = msg.subject.FromUtf8();
            match = CheckPartialStringMatch(s, s.Length(), what);
         }
         break;
      case C_mode_mailbox::SEARCH_BODY:
         if(msg.HasBody()){
            S_text_display_info tdi;
            if(OpenMessageBody(mod.GetContainer(), msg, tdi, true)){
               C_vector<wchar> body;
               ConvertFormattedTextToPlainText(tdi, body);
               match = CheckPartialStringMatch(body.begin(), body.size()-1, what);
            }
         }
         break;
      }
      if(match)
         find_results.push_back(i);
   }
   if(find_results.size()){
      mod.find_messages = find_results;
      mod.show_hidden = true;
      Mailbox_RecalculateDisplayArea(mod);
      RedrawScreen();
   }else{
      ShowErrorWindow(TXT_ERR_TEXT_NOT_FOUND, _what);
   }
}

//----------------------------

void C_mail_client::MailboxResetSearch(C_mode_mailbox &mod){

   if(mod.find_messages.size()){
      mod.selection = mod.GetRealMessageIndex(mod.selection);
      mod.find_messages.clear();
      Mailbox_RecalculateDisplayArea(mod);
   }
}

//----------------------------

void C_mail_client::C_mode_mailbox::CursorMove(bool down, bool shift, bool &redraw, bool wrap_list){

   if(num_vis_msgs){
      int ns = selection;
      if(!down){
         if(!ns){
            if(wrap_list)
               ns = num_vis_msgs;
            else
               ns = 1;
         }
         --ns;
      }else{
         if(++ns == num_vis_msgs){
            if(wrap_list)
               ns = 0;
            else
               --ns;
         }
      }
      if(shift){
                           //expand/clear selection
         GetMessage(selection).marked = shift_mark;
         GetMessage(ns).marked = shift_mark;
      }
      if(selection!=ns){
         app.SetMailboxSelection(*this, ns);
         redraw = true;
      }
   }
}

//----------------------------

bool C_mail_client::MailboxScrollEnd(C_mode_mailbox &mod, bool up){

   bool redraw = false;
   int ns = up ? 0 : mod.num_vis_msgs-1;
   if(mod.selection!=ns){
      SetMailboxSelection(mod, ns);
      redraw = true;
   }
   return redraw;
}

//----------------------------

void C_mail_client::MailboxMoveMessagesFolderSelected(C_message_container *fld){

   C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mode;
   MailboxResetSearch(mod_mbox);
   if(mod_mbox.acc.IsImap()){
      S_connection_params con_params;
      con_params.imap_folder_move_dest = fld;
      SetModeConnection(mod_mbox.acc, mod_mbox.folder, C_mode_connection::ACT_IMAP_MOVE_MESSAGES, &con_params);
   }else{
                              //move messages now
      LoadMessages(*fld);
      C_message_container &cnt = mod_mbox.GetContainer();

      C_vector<S_message*> msgs;
      GetMovableMessages(mod_mbox, msgs);
                        //move from top to bottom, so that multiple msgs are correctly deleted
      for(int i=msgs.size(); i--; ){
         S_message &msg = *msgs[i];
         if(msg.HasBody() && !(msg.flags&(msg.MSG_PARTIAL_DOWNLOAD|msg.MSG_DRAFT|msg.MSG_TO_SEND))){
            msg.MoveMessageFiles(mail_data_path, cnt, *fld);

            fld->messages.push_back(S_message());
            S_message &new_msg = fld->messages.back();
            new_msg = msg;
            new_msg.flags &= ~new_msg.MSG_SERVER_SYNC;

            if(msg.flags&msg.MSG_SERVER_SYNC){
               msg.attachments.Clear();
               msg.inline_attachments.Clear();
               msg.body_filename.Clear();
               //msg.flags |= msg.MSG_DELETED|msg.MSG_DELETED_DIRTY;
            }else{
               DeleteMessage(cnt, &msg - cnt.messages.begin(), false);
            }
            fld->MakeDirty();
            cnt.MakeDirty();
         }
      }
      SortMessages(fld->messages, fld->is_imap);
      fld->SaveAndUnloadMessages(mail_data_path);
      mod_mbox.GetContainer().ClearAllMessageMarks();
      Mailbox_RecalculateDisplayArea(mod_mbox);
      RedrawScreen();
   }
}

//----------------------------

void C_mail_client::MailboxProcessMenu(C_mode_mailbox &mod, int itm, dword menu_id){

   S_account &acc = mod.acc;
   int i;
   switch((menu_id<<16)|itm){
   case TXT_MESSAGE:
      {
         mod.menu = mod.CreateMenu();
         bool any_mark = mod.IsAnyMarked();
         const S_message *msg = !mod.num_vis_msgs ? NULL : &mod.GetMessage(mod.selection);
         bool msg_valid = (mod.num_vis_msgs && !any_mark && !msg->IsDeleted());
         bool is_offline_folder = mod.IsOfflineFolder();
         if(any_mark){
            if(!is_offline_folder)
               mod.menu->AddItem(TXT_DOWNLOAD_MSG);
         }else{
            if(msg && msg->flags&S_message::MSG_PARTIAL_DOWNLOAD){
                           //partial download, allow downloading entire
               mod.menu->AddItem(TXT_DOWNLOAD);
            }else{
               mod.menu->AddItem(TXT_OPEN, msg_valid ? 0 : C_menu::DISABLED, ok_key_name);
            }
         }
         mod.menu->AddSeparator();
         E_TEXT_ID txt_del = TXT_DELETE;
         if(mod.num_vis_msgs && !any_mark){
            const S_message &msg1 = mod.GetMessage(mod.selection);
            if(msg1.IsDeleted())
               txt_del = TXT_UNDELETE;
         }
         mod.menu->AddItem(txt_del, 0, delete_key_name, NULL, BUT_DELETE);
         if(!is_offline_folder)
            mod.menu->AddItem(TXT_DELETE_FROM_PHONE, mod.num_vis_msgs ? 0 : C_menu::DISABLED);
         {
            msg_valid = (msg_valid && msg->HasBody() && !(msg->flags&(msg->MSG_DRAFT|msg->MSG_TO_SEND)));
            mod.menu->AddItem(TXT_REPLY, msg_valid ? 0 : C_menu::DISABLED, "[6]", "[R]", BUT_REPLY);
            if(msg_valid){
               if(msg->HasMultipleRecipients(&acc))
                  mod.menu->AddItem(TXT_REPLY_ALL, 0, 0, "[L]");
            }
            mod.menu->AddItem(TXT_FORWARD, msg_valid ? 0 : C_menu::DISABLED, "[7]", "[F]");
            mod.menu->AddSeparator();
         }
         {
            bool msg_valid1 = (mod.num_vis_msgs && !any_mark && !msg->IsDeleted() && !(msg->flags&(msg->MSG_DRAFT|msg->MSG_TO_SEND)));
            mod.menu->AddItem(TXT_ADD_SENDER_TO_CONTACTS, (msg_valid1 ? 0 : C_menu::DISABLED) | C_menu::HAS_SUBMENU);
         }
         {
                           //move to other folder
            if(mod.acc.NumFolders()>1){
               C_vector<S_message*> msgs;
               GetMovableMessages(mod, msgs);
               mod.menu->AddItem(TXT_MOVE_TO_FOLDER, msgs.size() ? 0 : C_menu::DISABLED, "[1]", "[M]");
            }
         }
         mod.menu->AddItem(TXT_SHOW_DETAILS, (msg && !any_mark) ? 0 : C_menu::DISABLED, "[5]", "[E]");
         PrepareMenu(mod.menu);
      }
      break;

   case TXT_OPEN:
      OpenMessage(mod);
      break;

   case TXT_DELETE:
      {
         bool redraw;
         DeleteMarkedOrSelectedMessages(mod, true, redraw);
      }
      break;

   case TXT_UNDELETE:
      {
         bool redraw;
         DeleteMarkedOrSelectedMessages(mod, false, redraw);
      }
      break;

   case TXT_DELETE_FROM_PHONE:
      if(mod.num_vis_msgs){
         class C_question: public C_question_callback{
            C_mail_client &app;
            C_mode_mailbox &mod;
            virtual void QuestionConfirm(){
               app.DeleteMessagesFromPhone(mod);
            }
         public:
            C_question(C_mail_client &a, C_mode_mailbox &m): app(a), mod(m){}
         };
         CreateQuestion(*this, TXT_DELETE_FROM_PHONE, GetText(TXT_Q_DELETE_MESSAGES_FROM_PHONE), new(true) C_question(*this, mod), true);
      }
      break;

   case TXT_MOVE_TO_FOLDER:
      SetModeFolderSelector(mod.acc, (C_mode_folder_selector::t_FolderSelected)&C_mail_client::MailboxMoveMessagesFolderSelected, mod.folder);
      break;

   case TXT_SHOW_DETAILS:
      Mailbox_ShowDetails(mod);
      break;

   case TXT_UPDATE_MAILBOX:
      MailboxResetSearch(mod);
      SetModeConnection(acc, mod.folder, C_mode_connection::ACT_UPDATE_MAILBOX);
      break;

   case TXT_SEND_ALL:
      SetModeConnection(acc, mod.folder, C_mode_connection::ACT_SEND_MAILS);
      break;

   case TXT_DOWNLOAD_MSG:
      SetModeConnection(acc, mod.folder, C_mode_connection::ACT_GET_MARKED_BODIES);
      break;

   case TXT_DOWNLOAD:
      {
         S_connection_params p; p.message_index = mod.GetRealMessageIndex(mod.selection);
         SetModeConnection(acc, mod.folder, C_mode_connection::ACT_GET_BODY, &p);
      }
      break;

   case TXT_NEW_MSG:
   case TXT_REPLY:
   case TXT_FORWARD:
   case TXT_REPLY_ALL:
      {
         S_message *msg = NULL;
         if(mod.num_vis_msgs && (itm==TXT_REPLY || itm==TXT_REPLY_ALL || itm==TXT_FORWARD))
            msg = &mod.GetMessage(mod.selection);
         SetModeWriteMail(&mod.GetContainer(), msg, itm==TXT_REPLY, itm==TXT_FORWARD, itm==TXT_REPLY_ALL);
      }
      break;

   case TXT_ADD_SENDER_TO_CONTACTS:
      if(mod.num_vis_msgs){
         mod.menu = mod.CreateMenu(1);
         mod.menu->AddItem(TXT_NEW);
         mod.menu->AddItem(TXT_UPDATE_EXISTING);
         PrepareMenu(mod.menu);
      }
      break;

   case (1<<16)|TXT_NEW:
   case (1<<16)|TXT_UPDATE_EXISTING:
      {
         const S_message &msg = mod.GetMessage(mod.selection);
         S_contact c;
         c.AssignName(msg.sender.display_name.FromUtf8());
         c.email[0] = msg.sender.email;
         SetModeAddressBook_NewContact(c, (itm==TXT_UPDATE_EXISTING));
      }
      break;

   case TXT_MARK:
      mod.menu = mod.CreateMenu();
      mod.menu->AddItem(TXT_MARK_SELECTED, 0
#ifndef WINDOWS_MOBILE
         , GetShiftShortcut("[%+OK]")
#endif
         );
      mod.menu->AddItem(TXT_MARK_ALL, 0
#ifndef WINDOWS_MOBILE
         , GetShiftShortcut("[%+Right]")
#endif
         );
      mod.menu->AddItem(TXT_MARK_NONE, 0
#ifndef WINDOWS_MOBILE
         , GetShiftShortcut("[%+Left]")
#endif
         );
      if(config_mail.sort_by_threads && !mod.find_messages.size()){
                              //check if current message is part of conversation
         if(mod.GetMessage(mod.selection).thread_level ||
            (mod.selection+1<mod.GetNumEntries() && mod.GetMessage(mod.selection+1).thread_level)){
            mod.menu->AddItem(TXT_THREAD, 0, 0, "[T]");
         }
      }
      mod.menu->AddSeparator();
      if(mod.num_vis_msgs){
         mod.menu->AddItem((mod.GetMessage(mod.selection).flags&S_message::MSG_READ) ? TXT_MARK_UNREAD : TXT_MARK_READ, 0, "[9]", "[K]");
         mod.menu->AddItem((mod.GetMessage(mod.selection).flags&S_message::MSG_FLAGGED) ? TXT_MARK_UNFLAGGED : TXT_MARK_FLAGGED, 0, "[3]", "[G]");
         mod.menu->AddItem((mod.GetMessage(mod.selection).flags&S_message::MSG_HIDDEN) ? TXT_UNHIDE : TXT_HIDE, 0, "[8]", "[H]");
      }
      PrepareMenu(mod.menu);
      break;

   case TXT_THREAD:
      {
                              //mark selected msg and entire thread
         int mi = mod.selection;
         bool mark = !mod.GetMessage(mi).marked;
         while(mi && mod.GetMessage(mi).thread_level){
            --mi;
         }
         while(mi<mod.GetNumEntries()){
            mod.GetMessage(mi).marked = mark;
            ++mi;
            if(!mod.GetMessage(mi).thread_level)
               break;
         }

      }
      break;

   case TXT_MARK_SELECTED: 
      if(mod.num_vis_msgs){
         S_message &msg = mod.GetMessage(mod.selection);
         msg.marked = !msg.marked;
      }
      break;
   case TXT_MARK_ALL:
   case TXT_MARK_NONE:
      for(i=mod.num_vis_msgs; i--; )
         mod.GetMessage(i).marked = (itm==TXT_MARK_ALL);
      break;

   case TXT_MARK_READ:
   case TXT_MARK_UNREAD:
   case TXT_MARK_FLAGGED:
   case TXT_MARK_UNFLAGGED:
   case TXT_HIDE:
   case TXT_UNHIDE:
      mod.MarkMessages((itm==TXT_MARK_READ || itm==TXT_HIDE || itm==TXT_MARK_FLAGGED),
         (itm==TXT_HIDE || itm==TXT_UNHIDE) ? S_message::MSG_HIDDEN :
         (itm==TXT_MARK_READ || itm==TXT_MARK_UNREAD) ? S_message::MSG_READ :
         S_message::MSG_FLAGGED);
      InitLayoutMailbox(mod);
      break;

   case TXT_SORT_BY:
      {
         mod.menu = mod.CreateMenu();
         dword sf = config_mail.sort_mode;
         mod.menu->AddItem(TXT_SORT_DATE, sf==S_config_mail::SORT_BY_DATE ? C_menu::MARKED : 0);
         mod.menu->AddItem(TXT_SORT_SUBJECT, sf==S_config_mail::SORT_BY_SUBJECT ? C_menu::MARKED : 0);
         mod.menu->AddItem(TXT_SORT_SENDER, sf==S_config_mail::SORT_BY_SENDER ? C_menu::MARKED : 0);
         mod.menu->AddSeparator();
         mod.menu->AddItem(TXT_SORT_BY_THREAD, config_mail.sort_by_threads ? C_menu::MARKED : 0);
         mod.menu->AddSeparator();
         bool desc = config_mail.sort_descending;
         mod.menu->AddItem(TXT_SORT_ASCENDING, desc ? 0 : C_menu::MARKED);
         mod.menu->AddItem(TXT_SORT_DESCENDING, desc ? C_menu::MARKED : 0);
         PrepareMenu(mod.menu);
      }
      break;

   case TXT_SORT_ASCENDING:
      if(config_mail.sort_descending){
         MailboxResetSearch(mod);
         config_mail.sort_descending = false;
         SaveConfig();
         SortAllAccounts(&mod.GetContainer(), &mod.selection);
         mod.EnsureVisible();
         mod.GetContainer().MakeDirty();
      }
      break;

   case TXT_SORT_DESCENDING:
      if(!config_mail.sort_descending){
         MailboxResetSearch(mod);
         config_mail.sort_descending = true;
         SaveConfig();
         SortAllAccounts(&mod.GetContainer(), &mod.selection);
         mod.EnsureVisible();
         mod.GetContainer().MakeDirty();
      }
      break;

   case TXT_SORT_BY_THREAD:
      MailboxResetSearch(mod);
      config_mail.sort_by_threads = !config_mail.sort_by_threads;
      SaveConfig();
      SortAllAccounts(&mod.GetContainer(), &mod.selection);
      mod.EnsureVisible();
      mod.GetContainer().MakeDirty();
      break;

   case TXT_SORT_DATE:
   case TXT_SORT_SENDER:
   case TXT_SORT_SUBJECT:
      MailboxResetSearch(mod);
      config_mail.sort_mode = itm==TXT_SORT_DATE ? S_config_mail::SORT_BY_DATE : itm==TXT_SORT_SENDER ? S_config_mail::SORT_BY_SENDER : S_config_mail::SORT_BY_SUBJECT;
      SaveConfig();
      SortAllAccounts(&mod.GetContainer(), &mod.selection);
      mod.EnsureVisible();
      mod.GetContainer().MakeDirty();
      break;

   case TXT_SHOW_HIDDEN:
      mod.show_hidden = !mod.show_hidden;
      Mailbox_RecalculateDisplayArea(mod);
      break;

   case TXT_SHOW:
      mod.menu = mod.CreateMenu();
      mod.menu->AddItem(TXT_CFG_SHOW_PREVIEW, (config_mail.flags&config_mail.CONF_SHOW_PREVIEW) ? C_menu::MARKED : 0, "[2]", "[P]");
      mod.menu->AddItem(TXT_SHOW_ONLY_UNREAD, config_mail.tweaks.show_only_unread_msgs ? C_menu::MARKED : 0, NULL, "[J]");
      PrepareMenu(mod.menu);
      break;

   case TXT_SHOW_ONLY_UNREAD:
      config_mail.tweaks.show_only_unread_msgs = !config_mail.tweaks.show_only_unread_msgs;
      SortAllAccounts(&mod.GetContainer(), &mod.selection);
      Mailbox_RecalculateDisplayArea(mod);
      break;

   case TXT_SCROLL:
      mod.menu = mod.CreateMenu();
      mod.menu->AddItem(TXT_PAGE_UP, 0, "[*]", "[Q]");
      mod.menu->AddItem(TXT_PAGE_DOWN, 0, "[#]", "[A]");
      mod.menu->AddItem(TXT_TOP);
      mod.menu->AddItem(TXT_BOTTOM);
      PrepareMenu(mod.menu);
      break;

   case TXT_PAGE_UP:
   case TXT_PAGE_DOWN:
      {
         S_user_input ui;
         ui.Clear();
         ui.key = itm==TXT_PAGE_UP ? '*' : '#';
         bool redraw = false;
         mod.ProcessInputInList(ui, redraw);
      }
      break;

   case TXT_TOP:
   case TXT_BOTTOM:
      MailboxScrollEnd(mod, (itm==TXT_TOP));
      break;

   case TXT_SEARCH:
      MailboxSearchMessages(mod, mod.SEARCH_LAST, NULL);
      break;

   case TXT_CFG_SHOW_PREVIEW:
                              //toggle preview
      config.flags ^= config_mail.CONF_SHOW_PREVIEW;
      SaveConfig();
      InitLayoutMailbox(mod);
      SetMailboxSelection(mod, mod.selection);
      break;

   case TXT_FOLDER:
      {
         mod.menu = mod.CreateMenu();
         mod.menu->AddItem(TXT_NEW);
         PrepareMenu(mod.menu);
      }
      break;

   case TXT_NEW:
      {
         CloseMode(mod);
         if(mode->Id()!=C_mode_folders_list::ID)
            SetModeFoldersList(acc, false);
         FoldersList_StartCreate((C_mode_folders_list&)*mode, true);
      }
      break;

   case TXT_PURGE_DELETED:
      MailboxResetSearch(mod);
      SetModeConnection(acc, mod.folder, C_mode_connection::ACT_IMAP_PURGE);
      break;

   case TXT_BACK:
      MailboxBack(mod);
      break;
   case TXT_EXIT:
      Exit();
      break;
   }
}

//----------------------------

void C_mail_client::MailboxPreviewScrolled(C_mode_mailbox &mod, bool &redraw){

   if(config_mail.tweaks.scroll_preview_marks_read)
   if(mod.num_vis_msgs){
      S_message &msg = mod.GetMessage(mod.selection);
      if(!(msg.flags&S_message::MSG_READ) && msg.HasBody()){
         mod.MarkMessages(true, S_message::MSG_READ, false);
         redraw = true;
      }
   }
}

//----------------------------

void C_mode_mailbox_imp::ProcessInput(S_user_input &ui, bool &redraw){
//void C_mail_client::MailboxProcessInput(C_mode_mailbox &mod, S_user_input &ui, bool &redraw){

   if(GetNumEntries()){
      if((ui.mouse_buttons&MOUSE_BUTTON_1_DRAG) && mouse_mark!=-1){
         ui.mouse_buttons &= ~MOUSE_BUTTON_1_DRAG;
         int line = (ui.mouse.y - rc.y + sb.pos) / entry_height;
         if(line < (int)GetNumEntries()){
            line = Max(0, line);
            if(line!=selection){
               while(line!=selection){
                  GetMessage(selection).marked = mouse_mark;
                  if(selection<line)
                     ++selection;
                  else
                     --selection;
                  GetMessage(selection).marked = mouse_mark;
               }
               app.SetMailboxSelection(*this, selection);
               //EnsureVisible();
               DrawContents();
               DrawPreview(true);
            }
         }
      }
      int old_sel = selection;
      if(ProcessInputInList(ui, redraw)){
         if(ui.key_bits&GKEY_SHIFT){
            while(old_sel!=selection){
               GetMessage(old_sel).marked = shift_mark;
               if(old_sel<selection)
                  ++old_sel;
               else
                  --old_sel;
               GetMessage(old_sel).marked = shift_mark;
            }
            DrawContents();
         }
         app.SetMailboxSelection(*this, -selection);
         //DrawPreview(true);
         redraw = true;
      }
   }

   int scroll_pixels = 0;      //positive = down; negative = up
#ifdef USE_MOUSE
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){
         {
#ifdef USE_MOUSE
            app.Mailbox_SetButtonsState(*this);
#endif
            int but = app.TickBottomButtons(ui, redraw, enabled_buttons);
            if(but!=-1){
               switch(but){
               case 0:        //update
                  app.MailboxResetSearch(*this);
                  app.SetModeConnection(acc, folder, C_mail_client::C_mode_connection::ACT_UPDATE_MAILBOX);
                  break;
               case 2:
                  app.DeleteMarkedOrSelectedMessages(*this, true, redraw);
                  break;
               case 3:  //reply | mark as read
                  if(IsAnyMarked()){
                     ProcessMenu((GetMessage(selection).flags&S_message::MSG_READ) ? TXT_MARK_UNREAD : TXT_MARK_READ, 0);
                     break;
                  }
               case 1:  //new
                  S_message *msg = NULL;
                  if(num_vis_msgs && but==3){
                     msg = &GetMessage(selection);
                     if(msg->HasMultipleRecipients(&acc)){
                        app.ReplyWithQuestion(GetContainer(), *msg);
                        break;
                     }
                  }
                  app.SetModeWriteMail(&GetContainer(), msg, but==3, false, false);
                  break;
               }
               return;
            }
         }
         preview_kinetic_move.ProcessInput(ui, text_info.rc, 0, -1);
         C_scrollbar::E_PROCESS_MOUSE pm = app.ProcessScrollbarMouse(sb_preview, ui);
         switch(pm){
         case C_scrollbar::PM_PROCESSED: redraw = true; break;
         case C_scrollbar::PM_CHANGED:
            app.ScrollText(text_info, sb_preview.pos-text_info.top_pixel);
            app.MailboxPreviewScrolled(*this, redraw);
            redraw = true;
            break;
         default:
            if(ui.mouse_buttons&(MOUSE_BUTTON_1_DOWN|MOUSE_BUTTON_1_UP|MOUSE_BUTTON_1_DRAG)){
               if(preview_drag){
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
                     preview_drag = false;
                  }else
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_DRAG){
                     scroll_pixels = drag_mouse_y - ui.mouse.y;
                     drag_mouse_y = ui.mouse.y;
                     app.MailboxPreviewScrolled(*this, redraw);
                  }
               }else{
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                     if((app.config.flags&app.config_mail.CONF_SHOW_PREVIEW) && ui.CheckMouseInRect(rc_preview)){
                        if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                           preview_drag = true;
                           drag_mouse_y = ui.mouse.y;
                        }
                     }else if(ui.CheckMouseInRect(rc) && GetNumEntries()){
                        S_message &msg = GetMessage(selection);

                        int x = rc.x + app.msg_icons[app.MESSAGE_ICON_NEW]->SizeX() + 4 + app.fdb.letter_size_x*2;
                        if(ui.mouse.x<x){
                           int line = (ui.mouse.y - rc.y + sb.pos) / entry_height;
                           if(line < (int)GetNumEntries()){
                                       //toggle selection
                              msg.marked = !msg.marked;
                              mouse_mark = msg.marked;
                              //drag_mode = msg->marked ? DRAG_MARK : DRAG_UNMARK;
                              //SetMailboxSelection(mod, -selection);
                              //DrawMailboxMessage(mod, selection);
                              //DrawContents();
                              redraw = true;
                              touch_down_selection = -1;
                           }
                           ResetTouchInput();
                        }else{
                           menu = app.CreateTouchMenu();
                           bool is_offline_folder = IsOfflineFolder();
                           bool can_move = false;
                           if(acc.NumFolders()>1){
                              C_vector<S_message*> msgs;
                              app.GetMovableMessages(*this, msgs);
                              can_move = (msgs.size()>0);
                           }
                           if(!IsAnyMarked()){
                              bool has_body = msg.HasBody();
                              bool msg_valid = (has_body && !(msg.flags&(msg.MSG_DRAFT|msg.MSG_TO_SEND)));
                              if(has_body && !(msg.flags&S_message::MSG_PARTIAL_DOWNLOAD))
                                 menu->AddItem(TXT_REPLY, msg_valid ? 0 : C_menu::DISABLED, NULL, NULL, app.BUT_REPLY);
                              else
                                 menu->AddItem(TXT_DOWNLOAD, 0, NULL, NULL);
                              menu->AddItem(msg.IsDeleted() ? TXT_UNDELETE : TXT_DELETE, 0, NULL, NULL, app.BUT_DELETE);
                              menu->AddItem((msg.flags&S_message::MSG_HIDDEN) ? TXT_UNHIDE : TXT_HIDE);
                              if(can_move){
                                 menu->AddItem(TXT_MOVE_TO_FOLDER);
                              }else
                                 menu->AddSeparator();
                              menu->AddItem(TXT_SHOW_DETAILS);
                           }else{
                              if(!is_offline_folder)
                                 menu->AddItem(TXT_DOWNLOAD_MSG);
                              else
                                 menu->AddSeparator();
                              menu->AddItem(TXT_DELETE, 0, NULL, NULL, app.BUT_DELETE);
                              menu->AddItem((msg.flags&S_message::MSG_HIDDEN) ? TXT_UNHIDE : TXT_HIDE);
                              if(can_move){
                                 menu->AddItem(TXT_MOVE_TO_FOLDER);
                              }else
                                 menu->AddSeparator();
                              menu->AddItem((GetMessage(selection).flags&S_message::MSG_READ) ? TXT_MARK_UNREAD : TXT_MARK_READ);
                           }
                           app.PrepareTouchMenu(menu, ui);
                        }
                     }
                  }
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
                     mouse_mark = -1;
                  }
               }
            }
         }
      //}
   }
#endif

   if(num_vis_msgs){
      if(ui.key_bits&GKEY_SHIFT){
         if(shift_mark == -1)
            shift_mark = !GetMessage(selection).marked;
      }else
         shift_mark = -1;
   }

   app.MapScrollingKeys(ui);
   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      app.MailboxBack(*this);
      return;

   case 's':
      if(num_vis_msgs){
         app.MailboxSearchMessages(*this, SEARCH_LAST, NULL);
         redraw = true;
      }
      break;

#ifdef _DEBUG
   case 'a':
      if(num_vis_msgs){
         S_message &msg = GetMessage(selection);
         app.SaveMessageToFile(msg, L"D:\\1\\Test.eml");
      }
      break;

   case 'i':
      if(ui.key_bits&GKEY_CTRL){
         app.ConnectAccountInBackground(acc);
      }
      break;
#endif

   case 'u':
   case K_SEND:
      if(!IsOfflineFolder()){
                              //shortcut to connect
#ifdef UPDATE_IN_BACKGROUND
         ConnectAccountInBackground(acc, false, false);
#else
         if(!folder || !folder->IsTemp()){
            app.MailboxResetSearch(*this);
            app.SetModeConnection(acc, folder, C_mail_client::C_mode_connection::ACT_UPDATE_MAILBOX);
            return;
         }else
         if(app.CountMessagesForSending(acc, folder, C_mail_client::C_mode_connection::ACT_SEND_MAILS, NULL)){
            app.SetModeConnection(acc, folder, C_mail_client::C_mode_connection::ACT_SEND_MAILS);
            return;
         }
#endif
      }
      break;

   case 't':
      if(app.config_mail.sort_by_threads && !find_messages.size()){
                              //check if current message is part of conversation
         if(GetMessage(selection).thread_level || (selection+1<GetNumEntries() && GetMessage(selection+1).thread_level)){
            app.MailboxProcessMenu(*this, TXT_THREAD, 0);
            redraw = true;
         }
      }
      break;

   case 'n':
      app.SetModeWriteMail(NULL, NULL, false, false, false);
      return;

   case '1':
   case 'm':
      if(num_vis_msgs && acc.NumFolders()>1){
         C_vector<S_message*> msgs;
         app.GetMovableMessages(*this, msgs);
         if(msgs.size())
            app.SetModeFolderSelector(acc, (C_mail_client::C_mode_folder_selector::t_FolderSelected)&C_mail_client::MailboxMoveMessagesFolderSelected, folder);
         return;
      }
      break;

   case '2':
   case 'p':
                              //toggle preview
      app.config.flags ^= app.config_mail.CONF_SHOW_PREVIEW;
      app.SaveConfig();
      app.InitLayoutMailbox(*this);
      app.SetMailboxSelection(*this, selection);
      app.RedrawScreen();
      return;

   case '3':
   case 'g':
      if(num_vis_msgs){
         S_message &msg = GetMessage(selection);
         MarkMessages(!(msg.flags&S_message::MSG_FLAGGED), S_message::MSG_FLAGGED);
         redraw = true;
      }
      break;

   case 'k':
   case '9':
      if(num_vis_msgs){
         S_message &msg = GetMessage(selection);
         MarkMessages(!(msg.flags&S_message::MSG_READ), S_message::MSG_READ);
         redraw = true;
      }
      break;

   case 'h':
   case '8':
      if(num_vis_msgs){
         S_message &msg = GetMessage(selection);

         MarkMessages(!(msg.flags&S_message::MSG_HIDDEN), S_message::MSG_HIDDEN);
         app.InitLayoutMailbox(*this);
         redraw = true;
      }
      break;

      /*
   case 'j':
   case '9':
      MarkMessages(false, S_message::MSG_HIDDEN);
      redraw = true;
      break;
      */

   case 'e':
   case '5':                  //details
      if(num_vis_msgs){
         app.Mailbox_ShowDetails(*this);
         return;
      }
      break;

   case 'j':
      app.MailboxProcessMenu(*this, TXT_SHOW_ONLY_UNREAD, 0);
      redraw = true;
      break;

   case 'r':
   case 'f':
   case '6':                  //reply
   case '7':                  //forward
   case 'l':                  //reply all
      if(num_vis_msgs){
         S_message &msg = GetMessage(selection);
         if(!msg.IsDeleted() && msg.HasBody() && !(msg.flags&(msg.MSG_DRAFT|msg.MSG_TO_SEND))){
            bool r = (ui.key=='6' || ui.key=='r');
            bool f = (ui.key=='7' || ui.key=='f');
            bool a = (ui.key=='l');
            if(r && msg.HasMultipleRecipients(&acc)){
                              //reply all with question
               app.ReplyWithQuestion(GetContainer(), msg);
            }else
               app.SetModeWriteMail(&GetContainer(), &msg, r, f, a);
         }
      }
      break;

   case K_DEL:
#ifdef _WIN32_WCE
   case 'd':
#endif
      if(num_vis_msgs){
         app.DeleteMarkedOrSelectedMessages(*this, true, redraw);
         return;
      }
      break;

   case 'c':
   case '4':
      if(num_vis_msgs){
         const S_message &msg = GetMessage(selection);
                                 //if deleted, do nothing
         if(!msg.IsDeleted() && msg.HasBody()){
            GetContainer().ClearAllMessageMarks();
            app.SetModeReadMail(*this);
            ((C_mail_client::C_mode_read_mail_base&)*app.mode).StartCopyText();
            app.RedrawScreen();
            return;
         }
      }
      break;
   /*
   case 's':
   case '4':
      {
                              //send mails
         bool is_offline_folder = IsOfflineFolder();
         if(!is_offline_folder){
            SetModeConnection(acc, folder, C_mode_connection::ACT_SEND_MAILS);
            return;
         }
      }
      break;
      */

#ifdef _DEBUG

   case 'x':
      if(num_vis_msgs){
         GetContainer().DeleteMessageAttachments(GetMessage(selection), true);
         GetContainer().SaveMessages(app.GetMailDataPath(), true);
      }
      break;

   case 'H':
      if(num_vis_msgs){
         C_mail_client::S_connection_params p; p.message_index = GetRealMessageIndex(selection);
         app.SetModeConnection(acc, folder, C_mail_client::C_mode_connection::ACT_GET_MSG_HEADERS, &p);
      }
      break;
#endif//_DEBUG

#ifndef _DEBUG
   case 'Y':
#else
   case 'y':
#endif
      if(num_vis_msgs){
         if(IsAnyMarked()){
            C_vector<S_message> &messages = GetMessages();
            for(int i=messages.size(); i--; ){
               S_message &m = messages[i];
               if(m.marked){
                  GetContainer().DeleteMessageBody(app.GetMailDataPath(), m);
                  GetContainer().DeleteMessageAttachments(m, true);
               }
            }
            GetContainer().ClearAllMessageMarks();
         }else{
            S_message &m = GetMessage(selection);
            GetContainer().DeleteMessageBody(app.GetMailDataPath(), m);
            GetContainer().DeleteMessageAttachments(m, true);
         }
         GetContainer().SaveMessages(app.GetMailDataPath(), true);
         app.InitLayoutMailbox(*this);
         app.RedrawScreen();
      }
      return;

#ifndef _DEBUG
   case 'Z':
#else
   case 'z':
#endif
      if(num_vis_msgs){
         if(IsAnyMarked()){
            C_vector<S_message> &messages = GetMessages();
            for(int i=messages.size(); i--; ){
               S_message &m = messages[i];
               if(m.marked)
                  app.DeleteMessage(GetContainer(), i, true);
            }
         }else
            app.DeleteMessage(GetContainer(), selection, true);
         GetContainer().SaveMessages(app.GetMailDataPath(), true);
         app.Mailbox_RecalculateDisplayArea(*this);
         redraw = true;
      }
      break;

//#ifdef _DEBUG
   case 'S':
      {
         app.SortMessages(GetMessages(), IsImap());
#ifdef _DEBUG
         C_file fl;
         if(fl.Open(L"sort.txt", fl.FILE_WRITE)){
            for(int i=0; i<GetMessages().size(); i++){
               Cstr_c s; s<<GetMessages()[i].imap_uid <<'\n';
               fl.WriteString(s);
            }
         }
#endif
         redraw = true;
      }
      break;
//#endif

   case '0':
   case 'w':
      show_hidden = !show_hidden;
      app.Mailbox_RecalculateDisplayArea(*this);
      redraw = true;
      break;

   case K_ENTER:
      if(num_vis_msgs){
         if(ui.key_bits&GKEY_SHIFT){
            S_message &msg = GetMessage(selection);
            msg.marked = !msg.marked;
            redraw = true;
            break;
         }
         if(!IsAnyMarked()){
            app.OpenMessage(*this);
            return;
         }
      }
                              //flow... (when no shift, and some msgs are marked)
   case K_LEFT_SOFT:
   case K_MENU:
      {
         preview_kinetic_move.Reset();
         C_vector<S_message> &messages = GetMessages();
         bool is_offline_folder = IsOfflineFolder();

         auto_scroll_time = 0;
         menu = CreateMenu();
         menu->AddItem(TXT_MESSAGE, (!num_vis_msgs ? C_menu::DISABLED : 0) | C_menu::HAS_SUBMENU);
         if(!acc.IsImap())
            menu->AddItem(TXT_FOLDER, C_menu::HAS_SUBMENU);
         if(!is_offline_folder){
            bool send_key_used = false;
            if(!folder || !folder->IsTemp()){
               menu->AddItem(TXT_UPDATE_MAILBOX, 0, app.send_key_name, "[U]", app.BUT_UPDATE_MAILBOX);
               send_key_used = true;
            }
            if(app.CountMessagesForSending(acc, folder, C_mail_client::C_mode_connection::ACT_SEND_MAILS, NULL))
               menu->AddItem(TXT_SEND_ALL, 0, !send_key_used ? app.send_key_name : NULL, !send_key_used ? "[U]" : NULL);
            //if(!imap_folder)
               menu->AddItem(TXT_NEW_MSG, 0, NULL, "[N]", app.BUT_NEW);
            if(acc.IsImap() && !app.config_mail.imap_auto_expunge){
                              //check if we have deleted messages
               for(int i=messages.size(); i--; ){
                  if(messages[i].flags&S_message::MSG_DELETED){
                     menu->AddItem(TXT_PURGE_DELETED);
                     break;
                  }
               }
            }
         }
         menu->AddSeparator();
         if(messages.size() && (messages.back().flags&S_message::MSG_HIDDEN)){
            menu->AddItem(TXT_SHOW_HIDDEN, show_hidden ? C_menu::MARKED : 0, "[0]", "[W]");
            menu->AddSeparator();
         }
         menu->AddItem(TXT_MARK, (!num_vis_msgs ? C_menu::DISABLED : 0) | C_menu::HAS_SUBMENU);
         menu->AddItem(TXT_SORT_BY, C_menu::HAS_SUBMENU);
         menu->AddItem(TXT_SHOW, C_menu::HAS_SUBMENU);
         if(num_vis_msgs){
            menu->AddItem(TXT_SCROLL, C_menu::HAS_SUBMENU);
            menu->AddItem(TXT_SEARCH, //find_messages.size() ? C_menu::MARKED : 
               0, NULL, "[S]");
         }
         menu->AddSeparator();
         menu->AddItem(app.config_mail.tweaks.exit_in_menus ? TXT_EXIT : TXT_BACK);
         app.PrepareMenu(menu);
      }
      return;

      /*
   case K_CURSORUP:
   case K_CURSORDOWN:
      MailboxCursorMove(mod, (ui.key==K_CURSORDOWN), (ui.key_bits&GKEY_SHIFT), redraw);
      break;

   case '#':               //scroll page
   case '*':
   case 'q':
   case 'a':
   case K_PAGE_UP:
   case K_PAGE_DOWN:
      if(MailboxScrollPage(mod, (ui.key=='*' || ui.key=='q' || ui.key==K_PAGE_UP)))
         redraw = true;
      break;

   case K_HOME:
   case K_END:
      if(MailboxScrollEnd(mod, (ui.key==K_HOME)))
         redraw = true;
      break;
      */

   case K_CURSORLEFT:
   case K_CURSORRIGHT:
   case ' ':
      if(ui.key_bits&GKEY_SHIFT){
         for(int i=num_vis_msgs; i--; )
            GetMessage(i).marked = (ui.key==K_CURSORRIGHT);
#ifdef USE_MOUSE
         app.Mailbox_SetButtonsState(*this);
#endif
         redraw = true;
      }else
      if((app.config.flags&app.config_mail.CONF_SHOW_PREVIEW) && num_vis_msgs){
         preview_kinetic_move.Reset();
         auto_scroll_time = (rc_preview.sy - app.font_defs[app.config.viewer_font_index].line_spacing/2) << 8;
         if(ui.key==K_CURSORLEFT)
            auto_scroll_time = -auto_scroll_time;
         app.MailboxPreviewScrolled(*this, redraw);
         /*
      }else{
         ui.key = ui.key==K_CURSORLEFT ? K_ESC : K_ENTER;
         ui.mouse_buttons = 0;
         ProcessInput(ui, redraw);
         return;
         */
      }
      break;
   }

   if(scroll_pixels && text_info.Length() && app.ScrollText(text_info, scroll_pixels)){
      sb_preview.pos = text_info.top_pixel;
      redraw = true;
   }
}

//----------------------------

void C_mail_client::TickMailbox(C_mode_mailbox &mod, dword time, bool &redraw){

   if(&mod!=mode)
      return;

   int scroll_pixels = 0;
                              //auto-scrolling
   if(mod.auto_scroll_time){
      scroll_pixels = mod.auto_scroll_time >> 8;
      int add = time * 75;
      if(mod.auto_scroll_time > 0){
         if((mod.auto_scroll_time -= add) <= 0)
            mod.auto_scroll_time = 0;
      }else{
         if((mod.auto_scroll_time += add) >= 0)
            mod.auto_scroll_time = 0;
      }
      scroll_pixels -= mod.auto_scroll_time >> 8;
   }
   if(mod.preview_kinetic_move.IsAnimating()){
      S_point p;
      mod.preview_kinetic_move.Tick(time, &p);
      scroll_pixels += p.y;
   }
   if(scroll_pixels && mod.text_info.Length() && ScrollText(mod.text_info, scroll_pixels)){
      mod.sb_preview.pos = mod.text_info.top_pixel;
      mod.DrawPreview(true);
   }
                              //scroll subject
   if(mod.subj_scroll_phase){
      int add = time * 3277;
      bool redr = false;
      switch(mod.subj_scroll_phase){
      case C_mode_mailbox::SUBSCRL_WAIT_1:
         if((mod.subj_scroll_count -= time) <= 0){
            mod.subj_scroll_count = 0;
            mod.subj_scroll_phase = mod.SUBSCRL_GO_LEFT;
         }
         break;
      case C_mode_mailbox::SUBSCRL_GO_LEFT:
         if((mod.subj_scroll_count += add) >= mod.subj_scroll_len){
            mod.subj_scroll_count = SUBJ_SCROLL_DELAY;
            mod.subj_scroll_phase = mod.SUBSCRL_WAIT_2;
         }
         redr = true;
         break;
      case C_mode_mailbox::SUBSCRL_WAIT_2:
         if((mod.subj_scroll_count -= time) <= 0){
            mod.subj_scroll_count = mod.subj_scroll_len;
            mod.subj_scroll_phase = mod.SUBSCRL_GO_RIGHT;
         }
         break;
      case C_mode_mailbox::SUBSCRL_GO_RIGHT:
         if((mod.subj_scroll_count -= add*4) <- 0)
            mod.subj_scroll_phase = mod.SUBSCRL_NO;
         redr = true;
         break;
      }
      if(redr){
         if(!mod.menu){
            SetClipRect(mod.rc);
            DrawMailboxMessage(mod, mod.selection);
            ResetClipRect();
         }else
            redraw = true;
      }
   }
}

//----------------------------

int C_mail_client::C_mode_mailbox::GetThreadLevelOffset() const{
   return app.fdb.space_width*2;
}

//----------------------------

int C_mail_client::C_mode_mailbox::GetMsgDrawLevelOffset(byte thread_level) const{

   if(app.config_mail.sort_by_threads){
                              //only if sort by threads enabled
      return Min(GetThreadLevelOffset() * thread_level, app.ScrnSX()/3);
   }
   return 0;
}

//----------------------------

void C_mail_client::DrawMailboxMessage(const C_mode_mailbox &mod, int msg_i){

   const int max_x = mod.GetMaxX();
                              //compute item rect
   S_rect rc_item = mod.rc;
   rc_item.sx = max_x-mod.rc.x;
   rc_item.sy = mod.entry_height;

                              //pixel offset
   rc_item.y = mod.rc.y + msg_i*mod.entry_height - mod.sb.pos;
   if(rc_item.y>=mod.rc.Bottom() || rc_item.Bottom()<=mod.rc.y)
      return;

   int max_width = max_x-mod.rc.x - fdb.letter_size_x;
   int size_width = (fds.letter_size_x+1) * 5;

   dword col_text = GetColor(COL_TEXT);

   const C_image *img = msg_icons[MESSAGE_ICON_NEW];

   const S_message &msg = mod.GetMessage(msg_i);

   bool is_sent_folder = (mod.acc.IsImap() && !mod.folder->IsInbox() && mod.acc.GetFullFolderName(*mod.folder)==mod.acc.GetSentFolderName());
   bool is_sent_msg = is_sent_folder;
   if(!is_sent_msg)
      is_sent_msg = (msg.sender.email==mod.acc.primary_identity.email && msg.to_emails!=mod.acc.primary_identity.email);

   dword color = col_text;
   if(msg_i==mod.selection){
                        //draw selection
      DrawSelection(rc_item, true);
      color = GetColor(COL_TEXT_HIGHLIGHTED);
   }
   if(msg.flags&(0xf<<msg.MSG_COLOR_SHIFT)){
                              //colorize message
      dword col = rule_colors[((msg.flags>>msg.MSG_COLOR_SHIFT)&0xf) - 1];
      color = 0xff000000 | col;
      /*
      color = BlendColor(color, col, 0x8000);
      S_rect rc = rc_item;
      rc.Compact(); DrawOutline(rc, 0xff000000|col);
      rc.Compact(); DrawOutline(rc, 0x80000000|col);
      rc.Compact(); DrawOutline(rc, 0x40000000|col);
      */
   }
   int level_shift = 0;
   if(config_mail.sort_by_threads && !mod.find_messages.size())
      level_shift = mod.GetMsgDrawLevelOffset(msg.thread_level);
                        //draw separator
   if(rc_item.y!=mod.rc.y && (msg_i<mod.selection || msg_i>(mod.selection+1))){
      int l = rc_item.x + fdb.letter_size_x*3 + level_shift;
      int r = max_x - fdb.letter_size_x*3;
      DrawSeparator(l, r-l, rc_item.y);
   }

   if(msg.flags&(msg.MSG_DELETED|msg.MSG_HIDDEN))
      color = MulAlpha(color, 0x6000);

   if(config_mail.sort_by_threads && !mod.find_messages.size()){
                              //draw threading
      dword col_line = MulAlpha(col_text, 0x4000);
      int y = rc_item.y + rc_item.sy*1/4;
      const int tlo = mod.GetThreadLevelOffset();
      if(msg.thread_level){
                              //draw this message's connection to upper level
         int xr = mod.rc.x + level_shift + tlo*2/4;
         int xl = Max(rc_item.x+tlo/2, xr-tlo);
                                 //horizontal line
         FillRect(S_rect(xl+2, y, xr-xl-2, 1), col_line);
                              //vertical line
         FillRect(S_rect(xl, rc_item.y, 1, y-rc_item.y-1), col_line);
                              //draw small effect
         FillRect(S_rect(xl-1, y-1, 3, 3), col_line);
      }
                              //draw all other connection to levels below
      int num_m = mod.GetNumEntries();
      int lev1 = msg.thread_level+1;
      dword levels_drawn = 0;
      for(int i=msg_i+1; i<num_m; i++){
         const S_message &mn = mod.GetMessage(i);
         if(!mn.thread_level)
            break;
         lev1 = Min(lev1, int(mn.thread_level));
         if(mn.thread_level<=lev1){
            dword level_bit = 1<<mn.thread_level;
            if(!(levels_drawn&level_bit)){
               int x = mod.rc.x + mod.GetMsgDrawLevelOffset(mn.thread_level-1) + tlo*2/4;
               int top = mn.thread_level>=msg.thread_level ? y+2 : rc_item.y;
               FillRect(S_rect(x, top, 1, rc_item.Bottom()-top), col_line);
               levels_drawn |= level_bit;
            }
         }
      }
   }
   {
      int sx = rc_item.x + level_shift + 2, sy = rc_item.y + 2;
      C_fixed alpha = C_fixed::Percent((msg.flags&msg.MSG_HIDDEN) ? 50 : 100);
                        //draw icon (sprite)
      E_MESSAGE_ICON mi = MESSAGE_ICON_NEW_PARTIAL;
      if(msg.IsDeleted())
         mi = MESSAGE_ICON_DELETED;
      else
      if(msg.flags&msg.MSG_DRAFT)
         mi = msg.HasBody() ? MESSAGE_ICON_DRAFT : MESSAGE_ICON_DRAFT_PARTIAL;
      else
      if(msg.flags&msg.MSG_TO_SEND)
         mi = MESSAGE_ICON_TO_SEND;
      else
      if(msg.flags&msg.MSG_SENT)
         mi = MESSAGE_ICON_SENT;
      else
      if(msg.HasBody()){
         mi = (msg.flags&msg.MSG_READ) ? MESSAGE_ICON_OPENED : MESSAGE_ICON_NEW;
      }else
      if(msg.flags&msg.MSG_READ)
         mi = MESSAGE_ICON_OPENED_PARTIAL;
      const C_image *icon = msg_icons[mi];
      icon->DrawSpecial(sx, sy, NULL, alpha);

      if(!msg.IsDeleted()){
                        //draw modifications
         if(msg.attachments.Size() || (msg.flags&msg.MSG_HAS_ATTACHMENTS)){
            const C_image *img_a = msg_icons[MESSAGE_ICON_ATTACH_CLIP];
            img_a->DrawSpecial(sx+img->SizeX()-img_a->SizeX()-2, rc_item.y+mod.entry_height-img_a->SizeY()-2, NULL, alpha, col_text);
         }
         if(msg.flags&(msg.MSG_PRIORITY_HIGH|msg.MSG_PRIORITY_LOW)){
            const C_image *img1 = msg_icons[(msg.flags&msg.MSG_PRIORITY_HIGH) ? MESSAGE_ICON_PRIORITY_HIGH : MESSAGE_ICON_PRIORITY_LOW];
            img1->DrawSpecial(sx, sy+mod.entry_height-4-img1->SizeY(), NULL, alpha);
         }
         if(msg.flags&msg.MSG_REPLIED){
            const C_image *img_r = msg_icons[MESSAGE_ICON_REPLIED];
            img_r->DrawSpecial(sx+img->SizeX()-img_r->SizeX()+1, sy, NULL, alpha);
         }
         if(msg.flags&msg.MSG_FORWARDED){
            const C_image *img_f = msg_icons[MESSAGE_ICON_FORWARDED];
            img_f->DrawSpecial(sx+img->SizeX()-img_f->SizeX()+1, rc_item.y+mod.entry_height-img_f->SizeY()*3/2, NULL, alpha);
         }

         if(msg.flags&msg.MSG_FLAGGED){
            msg_icons[MESSAGE_ICON_FLAG]->DrawSpecial(sx, sy+2, NULL, alpha);
         }
         if(msg.IsRecent() && !msg.IsRead() && config_mail.tweaks.show_recent_flags){
            const C_image *img_r = msg_icons[MESSAGE_ICON_RECENT];
            img_r->DrawSpecial(sx+1, sy+1, NULL, alpha);
         }
         if(msg.flags&msg.MSG_PARTIAL_DOWNLOAD){
            const C_image *img_sc = msg_icons[MESSAGE_ICON_SCISSORS];
            int xx = sx + img->SizeX() - img_sc->SizeX();
            int yy = rc_item.y+mod.entry_height-img_sc->SizeY()-2;
            img_sc->DrawSpecial(xx, yy, NULL, alpha);
         }
         if(!(msg.flags&msg.MSG_SERVER_SYNC) && !(msg.flags&(S_message::MSG_SENT|S_message::MSG_TO_SEND))){
            int yy = sy+icon->SizeY()*6/8;
            yy = Min(yy, int(rc_item.Bottom()-msg_icons[MESSAGE_ICON_PIN]->SizeY()-1));
            msg_icons[MESSAGE_ICON_PIN]->DrawSpecial(sx+icon->SizeX()/5, yy, NULL, alpha*C_fixed::Percent(100));
         }
      }
      if(msg.marked)
         DrawCheckbox(sx+img->SizeX()-fdb.cell_size_x/2, sy+fdb.cell_size_y/8, fdb.cell_size_y, true, false);
   }
   {
      int left = rc_item.x + img->SizeX() + level_shift + 4 + fdb.letter_size_x*2;
      int yy = rc_item.y+2;
                        //draw e-mail info
      Cstr_w s;
      if(is_sent_msg || (msg.flags&(msg.MSG_SENT|msg.MSG_TO_SEND|msg.MSG_DRAFT))){
         if(msg.to_names.Length())
            s = msg.to_names.FromUtf8();
         else
            s.Copy(msg.to_emails);
         if(!is_sent_folder){
                              //also draw "To:"
            left = rc_item.x + level_shift + img->SizeX() + 6;
            left += DrawString(GetText(TXT_TO), left, yy, UI_FONT_BIG, 0, MulAlpha(color, 0x8000)) + fdb.cell_size_x/2;
         }
      }else
      if(msg.sender.display_name.Length())
         s = msg.sender.display_name.FromUtf8();
      else
         s.Copy(msg.sender.email);
      DrawString(s, left, yy, UI_FONT_BIG, msg.IsRead() ? 0 : FF_BOLD, color, -(max_width - left - size_width));
   }
   {
                        //draw size
      Cstr_w s = text_utils::MakeFileSizeText(msg.size, true, false);
      DrawString(s, rc_item.x + max_width + fdb.letter_size_x/2-1, rc_item.y+2+(fdb.line_spacing-fds.line_spacing), UI_FONT_SMALL, FF_RIGHT, color);
   }
   bool old_year = (msg.date<mod.this_year_begin);
   {
      S_rect cr = GetClipRect();
      int subj_max_x = (msg.date >= mod.today_begin) ? mod.subj_draw_max_x_today : old_year ? mod.subj_draw_max_x_last_year : mod.subj_draw_max_x;
      int xx = rc_item.x + level_shift + mod.subj_draw_shift, yy = rc_item.y+2+fdb.line_spacing;
      if(msg_i==mod.selection && mod.subj_scroll_phase){
                        //draw scrolling subject
         S_rect crc;
         crc.SetIntersection(S_rect(xx, yy, subj_max_x-xx, fdb.cell_size_y), cr);
         SetClipRect(crc);
         int offs;
         switch(mod.subj_scroll_phase){
         case C_mode_mailbox::SUBSCRL_GO_LEFT:
         case C_mode_mailbox::SUBSCRL_GO_RIGHT:
            offs = mod.subj_scroll_count; break;
         case C_mode_mailbox::SUBSCRL_WAIT_1: offs = 0; break;
         case C_mode_mailbox::SUBSCRL_WAIT_2: offs = mod.subj_scroll_len; break;
         default:
            offs = mod.subj_scroll_len;
         }
         offs >>= 16;
         xx -= offs;
      }
      DrawString(msg.subject.FromUtf8(), xx, yy, UI_FONT_SMALL, 0, color, -(subj_max_x-xx));
      SetClipRect(cr);
   }
   {
      Cstr_w s;
      dword flg = FF_RIGHT;
                        //write time for today's messages, and date for older 
      S_date_time dt;
      dt.SetFromSeconds(msg.date);
      if(msg.date >= mod.today_begin){
         s = text_utils::GetTimeString(dt, false);
         //col |= 0x800000;
      }else{
         GetDateString(dt, s, true, old_year);
         flg |= FF_ITALIC;
      }
      DrawString(s, rc_item.x + max_width + fdb.letter_size_x/2-2, rc_item.y+2+fdb.line_spacing, UI_FONT_SMALL, flg, MulAlpha(color, 0xc000));
   }
}

//----------------------------

void C_mail_client::C_mode_mailbox::DrawContents() const{

   app.ClearWorkArea(rc);
   if(num_vis_msgs){
                              //draw entries
      S_rect rc_item;
      int item_index = -1;
      while(BeginDrawNextItem(rc_item, item_index))
         app.DrawMailboxMessage(*this, item_index);
      EndDrawItems();
      app.DrawScrollbar(sb);
   }
}

//----------------------------

void C_mail_client::C_mode_mailbox::DrawPreview(bool draw_window) const{

   if(app.config.flags&app.config_mail.CONF_SHOW_PREVIEW){
      if(draw_window)
         app.DrawPreviewWindow(rc_preview);

                           //display body preview
      if(num_vis_msgs){
         const int border = 3;
         int y = rc_preview.y;
         int max_width = rc_preview.sx-border*2;

         const S_message &msg = GetMessage(selection);
         if(msg.IsDeleted()){
            //FillRect(mod.rc_preview, GetColor(COL_LIGHT_GREY));
            int xx = rc_preview.x + rc_preview.sx/2;
            app.DrawString(app.GetText(TXT_DELETED_MESSAGE), xx, y, app.UI_FONT_BIG, FF_CENTER, MulAlpha(app.GetColor(app.COL_TEXT_POPUP), 0x8000), -max_width);
         }else
         if(!msg.HasBody()){
            //FillRect(mod.rc_preview, GetColor(COL_LIGHT_GREY));
            int xx = rc_preview.x + rc_preview.sx/2;
            app.DrawString(app.GetText(TXT_BODY_NOT_RETRIEVED), xx, y, app.UI_FONT_BIG, FF_CENTER, MulAlpha(app.GetColor(app.COL_TEXT_POPUP), 0x8000), -max_width);
         }else{
            //const_cast<dword&>(mod.text_info.ts.text_color) = GetColor(COL_TEXT);
            app.DrawFormattedText(text_info, &rc_preview);
         }
         app.DrawScrollbar(sb_preview);
      //}else{
         //FillRect(mod.rc_preview, GetColor(COL_LIGHT_GREY));
      }
   }
}

//----------------------------

void C_mail_client::DrawMailbox(const C_mode_mailbox &mod){

   const S_account &acc = mod.acc;
   {
      if(mod.find_messages.size())
         DrawTitleBar(GetText(TXT_SEARCH_RESULTS), mod.rc.y-2);
      else{
         Cstr_w s;
         if(mod.folder && (mod.acc.IsImap() || !mod.folder->IsInbox())){
            s<<acc.name <<L" - ";
            s<<mod.folder->folder_name.FromUtf8();
         }else
            s<<GetText(TXT_MAILBOX) <<L" - " <<acc.name;
         DrawTitleBar(s, mod.rc.y-2);
      }
      if(mod.acc.background_processor.GetMode() && (mod.acc.selected_folder==mod.folder || !mod.IsImap())){
                              //draw connected mailbox
         DrawConnectIconType(1+fdb.cell_size_x/4, GetTitleBarHeight()/2, mod.acc.background_processor.state, true);
      }
   }
   {
      const S_rect &rc = (config.flags&config_mail.CONF_SHOW_PREVIEW) ? mod.rc_preview : mod.rc;
      ClearSoftButtonsArea(rc.Bottom() + 2);
   }
   DrawEtchedFrame(mod.rc);
   if(config.flags&config_mail.CONF_SHOW_PREVIEW){
      if(IsWideScreen())
         ClearToBackground(S_rect(mod.rc.Right()+2, mod.rc.y-2, ScrnSX()-mod.rc.Right()-2, mod.rc.sy+4));
      DrawPreviewWindow(mod.rc_preview);
   }
   mod.DrawContents();
   mod.DrawPreview(false);

   DrawSoftButtonsBar(mod, TXT_MENU, TXT_BACK);
#ifdef USE_MOUSE
   {
      bool any_mark = mod.IsAnyMarked();
      const char but_defs[] = { BUT_UPDATE_MAILBOX, BUT_NEW, BUT_DELETE, char(any_mark ? BUT_MARK_AS_READ : BUT_REPLY) };
      const dword tids[] = { TXT_UPDATE_MAILBOX, TXT_NEW, TXT_DELETE, any_mark ? TXT_MARK_READ : TXT_REPLY };
      DrawBottomButtons(mod, but_defs, tids, mod.enabled_buttons);
   }
#endif
   SetScreenDirty();
}

//----------------------------

void C_mail_client::SetModeUpdateMailboxes(bool auto_update){

   C_mode_update_mailboxes &mod = *new(true) C_mode_update_mailboxes(*this, mode, auto_update);
   CreateTimer(mod, 100);
   ActivateMode(mod, false);
   bool r = false;
   TickUpdateMailboxes(mod, 0, r);
}

//----------------------------

void C_mail_client::TickUpdateMailboxes(C_mode_update_mailboxes &mod, dword time, bool&){

   if((C_mode*)mode!=&mod)
      return;
                              //find next mailbox to update
   while(++mod.curr_mailbox < (int)NumAccounts()){
      S_account &acc = accounts[mod.curr_mailbox];
      if(acc.flags&acc.ACC_INCLUDE_IN_UPDATE_ALL){

         C_smart_ptr<C_mode_update_mailboxes> mod_this = &mod;
         mod.timer->Pause();

         CloseMode(mod, false);
         assert(mode->Id()==C_mode_accounts::ID);
         if(acc.IsImap()){
            int num_flds = 0;
            C_folders_iterator it(acc._folders, false);
            while(!it.IsEnd()){
               const C_message_container *fld = it.Next();
               if(!(fld->flags&fld->FLG_NOSELECT) && !fld->IsTemp())
                  ++num_flds;
            }
            /*
            int num_flds = acc.folders.Size();
            while(num_flds && (acc.folders[num_flds-1]->flags&C_message_container::FLG_HIDDEN))
               --num_flds;
               */
            if(!num_flds){
               mode = mod_this;
               continue;
            }
         }else{
            //SetModeMailbox(acc, fld);
            //C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mode;
            //mod_mbox.mod_upd_mboxes = mod_this;
         }
         C_mode_folders_list &mod_flds = SetModeFoldersList(acc, false);
         mod_flds.mod_upd_mboxes = mod_this;

         S_connection_params con_params;
         con_params.auto_update = mod.auto_update;
         con_params.alive_progress_pos = mod.alive_progress_pos;
         SetModeConnection(acc, *acc._folders.Begin(), C_mode_connection::ACT_UPDATE_ACCOUNTS, &con_params);
         return;
      }
   }
                              //close connection if it was automatic update
   if(mod.auto_update &&
      !(config.flags&config.CONF_KEEP_CONNECTION) &&
      !IsBackgroundConnected())
      CloseConnection();

   StartAlertSoundsPlayback();
                              //no more mailbox to update, switch back to previous mode
   CloseMode(mod);
}

//----------------------------

void C_mail_client::Mailbox_RecalculateDisplayArea(C_mode_mailbox &mod){

   GetVisibleMessagesCount(mod);

   mod.sb.total_space = mod.num_vis_msgs * mod.entry_height;
   mod.sb.SetVisibleFlag();
   mod.top_line = Max(0, Min(mod.top_line, int(mod.num_vis_msgs)-(mod.rc.sy/mod.entry_height)));
   mod.sb.pos = mod.top_line * mod.entry_height;
   SetMailboxSelection(mod, Max(0, Min(mod.selection, int(mod.num_vis_msgs)-1)));
   mod.EnsureVisible();
}

//----------------------------

void C_mail_client::OpenMessage(C_mode_mailbox &mod, bool force_preview){

   if(!mod.num_vis_msgs)
      return;
   S_message &msg = mod.GetMessage(mod.selection);
                              //if deleted, do nothing
   if(msg.IsDeleted() && !force_preview)
      return;
   mod.GetContainer().ClearAllMessageMarks();

   if(msg.IsDeleted()){
      SetModeReadMail(mod);
   }else
   if(!msg.HasBody()){
      S_connection_params p; p.message_index = mod.GetRealMessageIndex(mod.selection);
      SetModeConnection(mod.acc, mod.folder, C_mode_connection::ACT_GET_BODY, &p);
   }else
   if((msg.flags&(msg.MSG_DRAFT|msg.MSG_TO_SEND)) && !force_preview){
      mod.GetContainer().MakeDirty();
      SetModeWriteMail(&mod.GetContainer(), &msg, false, false, false);
      /*
#ifndef _DEBUG
                              //delete the message
      bool redraw;
      bool sync = (msg.flags&msg.MSG_SERVER_SYNC);
      DeleteMarkedOrSelectedMessages(mod, false, redraw, false);
      if(sync)
         mod.MarkMessages(true, S_message::MSG_HIDDEN);
      Mailbox_RecalculateDisplayArea(mod);
      if(mod.GetParent()->Id()==C_mode_folders_list::ID){
         if(CleanEmptyTempImapFolders(mod.acc)){
            FoldersList_InitView((C_mode_folders_list&)*mod.GetParent());
            mode->saved_parent = mod.GetParent();
         }
      }
#endif
      */
      RedrawScreen();
   }else{
      SetModeReadMail(mod);
   }
}

//----------------------------

void C_mail_client::DeleteMarkedOrSelectedMessagesQ(C_mode_mailbox &mod){

   bool redraw;
   DeleteMarkedOrSelectedMessages(mod, false, redraw);
}

//----------------------------

void C_mail_client::DeleteMarkedOrSelectedMessages(C_mode_mailbox &mod, bool prompt, bool &redraw, bool move_cursor_down){

   if(!mod.num_vis_msgs)
      return;

   class C_question: public C_question_callback{
      C_mail_client &app;
      C_mode_mailbox &mod;
      virtual void QuestionConfirm(){
         app.DeleteMarkedOrSelectedMessagesQ(mod);
      }
   public:
      C_question(C_mail_client &a, C_mode_mailbox &m): app(a), mod(m){}
   };
   if(prompt){
      if(mod.IsAnyMarked()){
                           //selection - always mark
         CreateQuestion(*this, TXT_Q_DELETE_MARKED_MESSAGES, GetText(TXT_Q_ARE_YOU_SURE), new(true) C_question(*this, mod), true);
      }else{
                           //single message - ask only if not server-synced
         const S_message &msg = mod.GetMessage(mod.selection);
         if(msg.flags&msg.MSG_SERVER_SYNC)
            DeleteMarkedOrSelectedMessages(mod, false, redraw);
         else{
            CreateQuestion(*this, TXT_Q_DELETE_MESSAGE, GetText(TXT_Q_ARE_YOU_SURE), new(true) C_question(*this, mod), true);
         }
      }
      return;
   }

   bool any_mark = false;
   bool any_removed = false;
   for(int i=mod.num_vis_msgs; i--; ){
      S_message &msg = mod.GetMessage(i);
      if(msg.marked){
         if(msg.flags&msg.MSG_SERVER_SYNC){
            if(msg.IsDeleted())
               msg.flags &= ~S_message::MSG_DELETED;
            else
               msg.flags |= S_message::MSG_DELETED;
            msg.flags |= S_message::MSG_DELETED_DIRTY;
            msg.marked = false;
         }else{
            DeleteMessage(mod.GetContainer(), i);
            any_removed = true;
         }
         any_mark = true;
      }
   }
   if(!any_mark){
      S_message &msg = mod.GetMessage(mod.selection);
      if(msg.flags&msg.MSG_SERVER_SYNC){
         msg.flags ^= msg.MSG_DELETED;
         msg.flags |= msg.MSG_DELETED_DIRTY;
         move_cursor_down = (move_cursor_down && msg.IsDeleted());
      }else{
         DeleteMessage(mod.GetContainer(), mod.selection);
         any_removed = true;
         move_cursor_down = false;
      }
   }
   mod.GetContainer().MakeDirty();
   if(any_removed)
      Mailbox_RecalculateDisplayArea(mod);
   SetMailboxSelection(mod, mod.selection);
   if(move_cursor_down && !any_mark)
      mod.CursorMove(true, false, redraw, false);
   redraw = true;
   //MailboxUpdateImapIdleFolder(mod); //can't, it makes expunge
}

//----------------------------

void C_mail_client::DeleteMessagesFromPhone(C_mode_mailbox &mod){

   if(!mod.num_vis_msgs)
      return;
                              //collect selection
   C_vector<int> msg_list;
   if(mod.IsAnyMarked()){
      for(dword i=0; i<mod.num_vis_msgs; i++){
         S_message &msg = mod.GetMessage(i);
         if(msg.marked){
            msg_list.push_back(i);
            msg.marked = false;
         }
      }
   }else{
      msg_list.push_back(mod.selection);
   }
   C_vector<S_message> &msgs = mod.GetMessages();
                              //process back to front
   for(int i=msg_list.size(); i--; ){
      int msg_i = msg_list[i];
      S_message &msg = mod.GetMessage(msg_i);
      if(msg.flags&S_message::MSG_SERVER_SYNC){
         mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
         msg.flags |= S_message::MSG_HIDDEN;
      }else{
                              //remove local message without body (it's useless)
         msgs.remove_index(msg_i);
      }
   }
   mod.GetContainer().MakeDirty();

   MailboxResetSearch(mod);
   SortMessages(mod.GetMessages(), mod.IsImap());
   Mailbox_RecalculateDisplayArea(mod);
   //mod.MarkMessages(true, S_message::MSG_HIDDEN);
   InitLayoutMailbox(mod);
}

//----------------------------
