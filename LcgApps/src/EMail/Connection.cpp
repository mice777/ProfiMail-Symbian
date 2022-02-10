#include "..\Main.h"
#include "Main_Email.h"
#include <UI\FatalError.h>
#include <Md5.h>
#include <TinyEncrypt.h>
#include <Base64.h>

extern const int VERSION_HI, VERSION_LO;

extern const wchar NETWORK_LOG_FILE[] =
#ifndef NO_FILE_SYSTEM_DRIVES
   L"C:"
#endif
   L"\\ProfiMailLog.txt";

//----------------------------

void C_mail_client::SetModeConnection(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params){

   CreateConnection();
   ManageTimer();

   ConnectionInit(acc, cnt, action, params);
}

//----------------------------

void C_mail_client::ConnectionFinishInit(C_mode_connection_base &mod){

#ifndef _DEBUG_
   if(mod.action==mod.ACT_IMAP_IDLE)
#endif
   {
      mod.saved_parent = NULL;
      mod.acc.background_processor.state = S_account::UPDATE_INIT;
      if(mod.Id()==C_mode_connection_auth::ID){
         mod.acc.background_processor.auth_check = &mod;
      }else{
         mod.acc.background_processor.auth_check = NULL;
         mod.acc.background_processor.mode = &mod;
      }
      ConnectionDrawAction(mod, GetText(TXT_PROGRESS_INITIALIZING));
      mod.Release();
      return;
   }

   mod.alive_progress.pos = mod.params.alive_progress_pos;
   CreateTimer(mod, 100);
   InitLayoutConnection(mod);

   ActivateMode(mod);

   Cstr_w s;
   s<<GetText(TXT_CONNECTING) <<' ' <<mod.acc.name <<L"...";
   ConnectionDrawTitle(mod, s);
   ConnectionDrawAction(mod, GetText(TXT_PROGRESS_INITIALIZING));
   UpdateScreen();
}

//----------------------------

void C_mail_client::AfterHeadersAdded(C_mode_connection_in &mod){

   C_message_container &cnt = mod.GetContainer();
   cnt.MakeDirty();
   switch(mod.GetParent()->Id()){
   case C_mode_mailbox::ID:
      {
         C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mod.GetParent();
         if(&cnt==mod_mbox.folder){
            SortMessages(cnt.messages, cnt.is_imap, &mod_mbox.selection);
            Mailbox_RecalculateDisplayArea(mod_mbox);
            mod_mbox.InitWidthLength();
         }else
            SortMessages(mod.GetMessages(), cnt.is_imap, NULL);
      }
      break;

   default:
      SortMessages(mod.GetMessages(), cnt.is_imap, NULL);
      break;
   }
   mod.headers_added = false;
}

//----------------------------

C_mail_client::S_bodystructure::S_part::S_part():
   content_encoding(ENCODING_7BIT),
   charset(COD_DEFAULT),
   size(0)
{}

//----------------------------

static const char platform[] =
#ifdef S60
   "S60"
#ifdef __SYMBIAN_3RD__
   "_3"
#endif
#elif defined WINDOWS_MOBILE
   "PPC"
#else
#error
#endif
;

//----------------------------

void C_mail_client::C_mode_connection_imap::AdoptOpenedConnection(){

   bool reuse_idle = false;
   switch(acc.background_processor.state){
   case S_account::UPDATE_IDLING:
      {
                           //adopt IDLE connection
         C_mode_connection_imap *mod_idle = acc.background_processor.GetMode();
         msg_seq_map = mod_idle->msg_seq_map;
         commands = mod_idle->commands;
         curr_tag_id = mod_idle->curr_tag_id;
         capability = mod_idle->capability;
         compress_out = mod_idle->compress_out;
         compress_in = mod_idle->compress_in;
         decompress_cache = mod_idle->decompress_cache;
         decompress_buf = mod_idle->decompress_buf;
         mod_idle->capability &= ~CAPS_IN_COMPRESSION;

         acc.background_processor.Close();
         reuse_idle = true;
      }
      break;
   }
   if(!reuse_idle)
      acc.CloseConnection();
}

//----------------------------

void encr_segment_0a(){}

void C_mail_client::ConnectionInit(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params){

   //LOG_RUN("ConnectionInit");
   if(cnt)
       LoadMessages(*cnt);

   switch(action){
   case C_mode_connection::ACT_SEND_MAILS:
   case C_mode_connection::ACT_UPDATE_MAILBOX:
   case C_mode_connection::ACT_UPDATE_ACCOUNTS:
   case C_mode_connection::ACT_UPDATE_IMAP_FOLDERS:
#ifdef _DEBUG
   case C_mode_connection::ACT_IMAP_IDLE:
#endif
                              //check if we're going to send messages
      {
         dword n = CountMessagesForSending(acc, cnt, action, NULL);
         if(n){
            SetModeConnectionSmtp(acc, cnt, action, params);
            return;
         }
                              //check if we're going to upload some messages
         if(CountMessagesForUpload(acc, NULL)){
            SetModeConnectionImapUpload(acc, cnt, action, params);
            return;
         }
         if(action==C_mode_connection::ACT_SEND_MAILS){
                              //nothing to do if only sending mails
            RedrawScreen();
            return;
         }
      }
      break;
   }
   C_mode_connection_in *mod_in;
   if(acc.IsImap()){
      C_mode_connection_imap *mod_imap = new(true) C_mode_connection_imap(*this, mode, acc, cnt, action, params);
      mod_in = mod_imap;
      mod_imap->AdoptOpenedConnection();
   }else{
      C_mode_connection_pop3 *mod_pop3 = new(true) C_mode_connection_pop3(*this, mode, acc, cnt, action, params);
      mod_in = mod_pop3;
      if(!text_utils::CompareStringsNoCase(acc.mail_server, "pop.gmail.com") || !text_utils::CompareStringsNoCase(acc.mail_server, "pop.googlemail.com"))
         mod_pop3->is_gmail = true;
   }
   C_mode_connection_in &mod = *mod_in;
   ConnectionFinishInit(mod);

   if(mod.acc.socket){
                              //reuse opened socket
      //mod.socket = mod.acc.socket;
      mod.using_cached_socket = true;
      if(mod.IsImap()){
         C_mode_connection_imap &mod_imap = (C_mode_connection_imap&)mod;
         mod_imap.capability = mod.acc.imap_capability;
         AfterImapLogin(mod_imap);
      }else{
                           //reuse of socket depends on operation - for getting new messages we must reopen connection
         switch(mod.action){
         case C_mode_connection::ACT_UPDATE_MAILBOX:
         case C_mode_connection::ACT_UPDATE_ACCOUNTS:
            mod.acc.socket = NULL;
            ConnectionInitSocket(mod);
            break;
         default:
            AfterPop3Login((C_mode_connection_pop3&)mod);
         }
      }
   }else
      ConnectionInitSocket(mod);
}

//----------------------------

void C_mail_client::Connection_AfterMailboxSelect(C_mode_connection_in &mod, int num_msg){

   switch(mod.action){
   case C_mode_connection::ACT_GET_BODY:
      {
         dword index = mod.params.message_index;
         S_message &msg = mod.GetMessages()[index];
         if(msg.HasBody()){
            mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
            mod.GetContainer().MakeDirty();
            mod.force_no_partial_download = true;
         }
         mod.progress.total = msg.size;
         mod.progress.pos = 0;
         ConnectionDrawProgress(mod);
         if(mod.IsImap())
            BeginRetrieveMessageImap((C_mode_connection_imap&)mod, index);
         else
            BeginRetrieveMessagePop3((C_mode_connection_pop3&)mod, index);
      }
      break;

   case C_mode_connection::ACT_GET_MSG_HEADERS:
      if(mod.IsImap())
         BeginRetrieveMsgHeadersImap((C_mode_connection_imap&)mod);
      else
         BeginRetrieveMessagePop3((C_mode_connection_pop3&)mod, mod.params.message_index);
      break;

   case C_mode_connection::ACT_IMAP_PURGE:
      {
                              //to be sure that msgs marked as deleted are really deleted on server, add them to deleted list now
         mod.msgs_to_delete.clear();
         C_vector<S_message> &messages = mod.GetMessages();
         for(int i=messages.size(); i--; ){
            const S_message &msg = messages[i];
            if(msg.IsDeleted()){
               mod.msgs_to_delete.push_back(msg.imap_uid);
            }
         }
         mod.need_expunge = true;

         ConnectionCleanupAndDisconnect(mod);
      }
      break;

   case C_mode_connection::ACT_GET_MARKED_BODIES:
      {
         C_vector<S_message> &messages = mod.GetMessages();
                              //cleanup marks, leave only real ones
         mod.num_get_bodies = 0;
         mod.get_bodies_index = 0;
         mod.progress.pos = 0;
         mod.progress.total = 0;
         C_vector<dword> msg_indexes;
         for(int i=0; i<messages.size(); i++){
            S_message &msg = messages[i];
            if(msg.marked){
               if(!msg.IsDeleted() && !msg.HasBody()){
                  msg_indexes.push_back(i);
                  ++mod.num_get_bodies;
               }else
                  msg.marked = false;
            }
         }
         if(!mod.num_get_bodies){
            ConnectionDisconnect(mod);
            break;
         }

         ConnectionDrawTitle(mod, GetText(TXT_PROGRESS_DOWNLOADING));

         if(!mod.IsImap())
            StartRetrievingMessageMarkedBodiesPOP3((C_mode_connection_pop3&)mod, &msg_indexes);
         else
            StartRetrievingMessageMarkedBodiesIMAP((C_mode_connection_imap&)mod, msg_indexes);
      }
      break;

   case C_mode_connection::ACT_IMAP_MOVE_MESSAGES:
      Connection_MoveImapMessages((C_mode_connection_imap&)mod);
      break;

   case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT:
   case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT_AND_OPEN:
   case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL:
      StartRetrievingMessageAttachment((C_mode_connection_imap&)mod);
      break;

   default:
      if(num_msg){
         mod.headers.clear();
         mod.headers_to_move.clear();
         mod.headers_to_download.clear();
         switch(mod.action){
         case C_mode_connection::ACT_UPDATE_MAILBOX:
         case C_mode_connection::ACT_UPDATE_ACCOUNTS:
         case C_mode_connection::ACT_UPDATE_IMAP_FOLDERS:
         case C_mode_connection::ACT_IMAP_IDLE:
            {
               if(!rules.Size())
                  LoadRules();
               if(mod.IsImap() && mod.acc.imap_last_x_days_limit){
                  C_mode_connection_imap &mod_imap = (C_mode_connection_imap&)mod;
                  if(!(mod_imap.capability&mod_imap.CAPS_NO_SEARCH)){
                     SearchLastMessagesIMAP(mod_imap);
                     break;
                  }
               }
               Cstr_w s; s<<GetText(TXT_PROGRESS_NUM_MESSAGES) <<L' ' <<num_msg;
               ConnectionDrawAction(mod, s);

               if(!mod.IsImap() || mod.action==C_mode_connection::ACT_UPDATE_MAILBOX)
                  ConnectionDrawTitle(mod, GetText(TXT_UPDATE_MAILBOX));

               if(mod.IsImap())
                  GetMessageListIMAP((C_mode_connection_imap&)mod, num_msg);
               else
                  GetMessageListPOP3((C_mode_connection_pop3&)mod, num_msg);
            }
            break;
         default:
            assert(0);
         }
      }else{
         switch(mod.action){
         case C_mode_connection::ACT_UPDATE_IMAP_FOLDERS:
         case C_mode_connection::ACT_UPDATE_ACCOUNTS:
            SyncMessages(mod);
            if(mod.IsImap())
               ConnectionImapFolderClose((C_mode_connection_imap&)mod);
            else
               ConnectionDisconnect(mod);
            break;

         case C_mode_connection::ACT_UPDATE_MAILBOX:
         case C_mode_connection::ACT_IMAP_IDLE:
            if(mod.IsImap()){
               C_mode_connection_imap &mod_imap = (C_mode_connection_imap&)mod;
               mod_imap.msg_seq_map.Clear();
               mod_imap.msg_seq_map.is_synced = true;
            }
                              //flow...
         default:
            SyncMessages(mod);
                                 //flow...
         case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT:
         case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT_AND_OPEN:
         case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL:
            ConnectionDisconnect(mod);
            break;
         }
      }
   }
}

//----------------------------

void C_mail_client::ConnectionInitSocket(C_mode_connection_in &mod){

   mod.using_cached_socket = false;
   dword acc_i = &mod.acc - accounts.Begin();

   const bool use_ssl = (mod.acc.secure_in==S_account_settings::SECURE_SSL),
      def_ssl = (mod.acc.secure_in==S_account_settings::SECURE_STARTTLS);

   C_socket *socket = CreateSocket(connection, (void*)((acc_i<<2) | 1), use_ssl, def_ssl, socket_log ? NETWORK_LOG_FILE : NULL);
   mod.acc.socket = socket;
   socket->Release();
   word port = mod.acc.port_in;
   if(!port){
      port = word(
         use_ssl ? (mod.IsImap() ? C_socket::PORT_IMAP4_SSL : C_socket::PORT_POP3_SSL) :
         (mod.IsImap() ? C_socket::PORT_IMAP4 : C_socket::PORT_POP3));
   }
   socket->Connect(mod.acc.mail_server, port);
}

//----------------------------

void C_mail_client::ConnectionDisconnect(C_mode_connection_in &mod){

   if(!mod.cancel_request){
      switch(mod.action){
      case C_mode_connection::ACT_UPDATE_ACCOUNTS:
      case C_mode_connection::ACT_UPDATE_MAILBOX:
      case C_mode_connection::ACT_UPDATE_IMAP_FOLDERS:
                              //check if we update multiple mailboxes
         C_mode *m;
         for(m = mode; m; m = m->GetParent()){
            if(m->Id()==C_mode_mailbox::ID){
               C_mode_mailbox &mod_m = (C_mode_mailbox&)*m;
               if(mod_m.mod_upd_mboxes)
                  break;
            }else
            if(m->Id()==C_mode_folders_list::ID){
               C_mode_folders_list &mod_m = (C_mode_folders_list&)*m;
               if(mod_m.mod_upd_mboxes)
                  break;
            }
         }
         if(!m){
            StartAlertSoundsPlayback();
         }
         break;
      }
      if(mod.acc.socket && mod.need_expunge){
         if(mod.IsImap()){
            if(config_mail.imap_auto_expunge || mod.action==mod.ACT_IMAP_PURGE){
               ConnectionExpungeIMAP((C_mode_connection_imap&)mod);
               return;
            }
         }else{
            ConnectionExpungePOP3((C_mode_connection_pop3&)mod);
            return;
         }
      }
   }

   if(mod.headers_added)
      AfterHeadersAdded(mod);

   if(!mod.container_invalid && mod.folder)
      mod.GetContainer().SaveMessages(mail_data_path);

   if(mod.acc.socket)
      mod.acc.socket->CancelTimeOut();

                              //keep ref of mode
   C_smart_ptr<C_mode> keep_ref = &mod;

                              //close only if this is active mode, as with IDLE this func may be called from any mode
   if(&mod==mode){
      if(mod.timer)
         mod.timer->Pause();
      CloseMode(mod, false);
   }

   UpdateUnreadMessageNotify();

   bool redraw = false;
   switch(mod.action){
   case C_mode_connection::ACT_GET_BODY:
      {
         C_vector<S_message> &messages = mod.GetMessages();
         dword index = mod.params.message_index;
         if(index < dword(messages.size())){
            S_message &msg = messages[index];
            if(msg.HasBody()){
               C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mode;
               OpenMessage(mod_mbox);
            }else
               redraw = true;
         }else
            redraw = true;
      }
      break;

   case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT_AND_OPEN:
      {
         const S_message &msg = mod.GetMessages()[mod.params.message_index];
         const S_attachment &att = msg.attachments[mod.params.attachment_index];
         if(att.IsDownloaded()){
            assert(mode->Id()==C_mode_read_mail_base::ID);
            if(!C_client_viewer::OpenFileForViewing(this, att.filename.FromUtf8(), att.suggested_filename, NULL, NULL, NULL, (C_mode_read_mail_base*)(C_mode*)mode)){
               OpenFileBySystem(att.filename.FromUtf8());
               redraw = true;
            }
            break;
         }
      }
                              //flow...
   case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT:
   case C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL:
      redraw = true;
      break;

   case C_mode_connection::ACT_UPDATE_ACCOUNTS:
      {
         mod.GetContainer().SaveAndUnloadMessages(mail_data_path);
         C_mode_folders_list &mod_flds = (C_mode_folders_list&)*mode;
         C_mode_update_mailboxes &mod_upd = *mod_flds.mod_upd_mboxes;
         mode = mod_flds.mod_upd_mboxes;
         if(mod.cancel_request)
            CloseMode(*mode);
         else{
            mod_upd.alive_progress_pos = mod.alive_progress.pos;
            mode->timer->Resume();
         }
      }
      break;

   case C_mode_connection::ACT_UPDATE_IMAP_FOLDERS:
      redraw = true;
      break;

   case C_mode_connection::ACT_IMAP_IDLE:
      ConnectionDrawAction(mod, NULL);
      break;

   default:
      if(mode->Id()==C_mode_mailbox::ID){
         C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mode;
         SetMailboxSelection(mod_mbox, mod_mbox.selection);
      }
      redraw = true;
   }
   //bool is_idle = false;
   if(mod.IsImap() && (
#ifndef UPDATE_IN_BACKGROUND
      (mod.acc.flags&S_account::ACC_USE_IMAP_IDLE) || 
#endif
      mod.acc.use_imap_idle) && mod.acc.socket){
      C_mode_connection_imap &mod_imap = (C_mode_connection_imap&)mod;
      BeginImapIdle(mod_imap);
   }else
   if(mod.acc.background_processor.state)
      ConnectionUpdateState(mod.acc, S_account::UPDATE_DISCONNECTED);

   keep_ref = NULL;
   if(redraw){
      RedrawScreen();
      UpdateScreen();
   }
   if(socket_log==SOCKET_LOG_YES)
      C_client_viewer::OpenFileForViewing(this, NETWORK_LOG_FILE, L"Network log:");
   //LOG_RUN("Connection closed");
}

//----------------------------

bool C_mail_client::ConnectionImapStartUploadingSent(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION act, const S_connection_params *params){

                              //find folder with Sent messages
   C_message_container *fld = FindFolder(acc, acc.GetSentFolderName());
   if(fld){
      SetModeConnection(acc, fld, act, params);
      return true;
   }
   return false;
}

//----------------------------

static int CompareHeadersImap(const void *h1, const void *h2, void *context){
   const S_message_header_base &hdr1 = *(S_message_header_base*)h1;
   const S_message_header_base &hdr2 = *(S_message_header_base*)h2;
   assert(hdr1.imap_uid!=hdr2.imap_uid);
   return hdr1.imap_uid<hdr2.imap_uid ? -1 : 1;
}

//----------------------------

static int CompareHeadersPop3(const void *h1, const void *h2, void *context){
   const S_message_header_base &hdr1 = *(S_message_header_base*)h1;
   const S_message_header_base &hdr2 = *(S_message_header_base*)h2;
   return StrCmp(hdr1.pop3_uid, hdr2.pop3_uid);
}

//----------------------------

void C_mail_client::C_mode_connection_in::SortHeaders(){
                              //sort headers by IMAP or POP3 ID
   QuickSort(headers.begin(), headers.size(), sizeof(S_message_header_base), IsImap() ? &CompareHeadersImap : &CompareHeadersPop3);
}

//----------------------------

int C_mail_client::C_mode_connection_in::FindHeader(const S_message_header &match) const{

   int lo = 0, hi = headers.size();
   if(hi){
      if(IsImap()){
         dword match_uid = match.imap_uid;
         do{
            int mid = (lo+hi)/2;
            const S_message_header_base &hmid = headers[mid];
            if(match_uid==hmid.imap_uid)
               return mid;
            if(match_uid>hmid.imap_uid)
               lo = mid+1;
            else
               hi = mid;
         }while(lo<hi);
      }else{
         const Cstr_c &match_uid = match.pop3_uid;
         do{
            int mid = (lo+hi)/2;
            const S_message_header_base &hmid = headers[mid];
            int cmp = StrCmp(match_uid, hmid.pop3_uid);
            if(!cmp)
               return mid;
            if(cmp>0)
               lo = mid+1;
            else
               hi = mid;
         }while(lo<hi);
      }
   }
   /*
   for(int j=headers.size(); j--; ){
      const S_message_header_base &hdr = headers[j];
      if(hdr.MatchUID(match, IsImap()))
         return j;
   }
   */
   return -1;
}

//----------------------------

void C_mail_client::SyncMessages(C_mode_connection_in &mod, bool use_delete_list){

   enum E_ACTION{
      NO_ACTION,
      RETRIEVE,
   };
   dword num_headers = mod.headers.size();
   C_buffer<byte> action;
   action.Resize(num_headers, RETRIEVE);

   bool need_update = false;
                              //delete local messages not present on server
   C_vector<S_message> &messages = mod.GetMessages();
   mod.msgs_to_delete.clear();
   mod.SortHeaders();
   {
      C_vector<S_message> new_msgs;
      new_msgs.reserve(messages.size());
                              //check all current messages, and add those still present
      for(int i=0; i<messages.size(); i++){
         S_message &msg = messages[i];
                              //keep non-synced msgs intact
         if(!(msg.flags&msg.MSG_SERVER_SYNC)){
            new_msgs.push_back(msg);
            continue;
         }
                              //search existing message among synced headers
         int j = mod.FindHeader(msg);
         if(j!=-1){
            const S_message_header_base &hdr = mod.headers[j];
            if(!mod.IsImap()){
                              //update POP3 server index
               if(msg.pop3_server_msg_index != hdr.pop3_server_msg_index){
                  msg.pop3_server_msg_index = hdr.pop3_server_msg_index;
                  need_update = true;
               }
            }else{
                              //update IMAP flags
               if(!msg.IsRead() && (hdr.flags&S_message::MSG_READ))
                  home_screen_notify.RemoveMailNotify(mod.acc, mod.GetContainer(), msg);

               if(!msg.IsDeleted() && (hdr.flags&hdr.MSG_DELETED) && !msg.IsHidden()){
                              //DELETED flag is just set, also hide the message
                  need_update = true;
                  msg.flags |= msg.MSG_HIDDEN;
                  mod.GetContainer().flags |= C_message_container::FLG_NEED_SORT;
               }
               if(msg.UpdateFlags(hdr.flags))
                  need_update = true;
            }
            if(msg.IsDeleted() && use_delete_list){
               if(msg.flags&msg.MSG_DELETED_DIRTY){
                              //message was instructed to be deleted by user, add it to delete list
                  mod.msgs_to_delete.push_back(mod.IsImap() ? hdr.imap_uid : hdr.pop3_server_msg_index);
               }else{
                              //message is deleted on server, need expunge to update changes
                  if(mod.IsImap())
                     mod.need_expunge = true;
               }
            }
            action[j] = NO_ACTION;
                              //message exists, keep it
            new_msgs.push_back(msg);
         }else{
                              //message will be dropped
            if(config_mail.tweaks.always_keep_messages && msg.HasBody() && !msg.IsDeleted()){
               msg.flags &= ~msg.MSG_SERVER_SYNC;
                              //message exists, keep it
               new_msgs.push_back(msg);
            }else{
               mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
               if(!msg.IsRead())
                  home_screen_notify.RemoveMailNotify(mod.acc, mod.GetContainer(), msg);
            }
            need_update = true;
         }
      }
      if(need_update){
                              //shrink current messages
         messages = new_msgs;
      }
   }
   {
                              //create new 2 lists for messages to retrieve, and messages to delete
      C_vector<S_message_header_base> retr_hdrs;
      retr_hdrs.reserve(num_headers);

      for(dword i=0; i<num_headers; i++){
         if(action[i]==RETRIEVE){
            const S_message_header_base &hdr = mod.headers[i];
            if(mod.IsImap()){
                           //do not download messages already deleted on server
               //if(!hdr.IsDeleted())
                  retr_hdrs.push_back(hdr);
            }else{
                           //sometimes some messages are not reported to be on server (through LIST), ignore them
               if(hdr.pop3_server_msg_index!=-1)
                  retr_hdrs.push_back(hdr);
            }
         }
      }
      mod.headers = retr_hdrs;
   }
   if(need_update){
      switch(mod.GetParent()->Id()){
      case C_mode_mailbox::ID:
         C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mod.GetParent();
         Mailbox_RecalculateDisplayArea(mod_mbox);
         break;
      }
      mod.GetContainer().MakeDirty();
      if(mod.IsImap()){
         C_mode_connection_imap &mod_imap = (C_mode_connection_imap&)mod;
         if(mod_imap.IsImapIdle())
            ImapIdleUpdateAfterOperation(mod_imap, false);
      }
   }
}

//----------------------------

bool C_mail_client::StartDeletingMessages(C_mode_connection_in &mod, bool allow_imap_thrash_move){

   if(!mod.msgs_to_delete.size())
      return false;
   if(mod.IsImap())
      StartDeletingMessagesImap((C_mode_connection_imap&)mod, allow_imap_thrash_move);
   else
      StartDeletingMessagesPop3((C_mode_connection_pop3&)mod);
   return true;
}

//----------------------------

void C_mail_client::RemoveDeletedMessages(C_mode_connection_in &mod){

   C_vector<S_message> &messages = mod.GetMessages();
   C_vector<S_message> left_msgs;
   left_msgs.reserve(messages.size());

   C_mode_mailbox *mod_mbox = mod.GetParent()->Id()==C_mode_mailbox::ID ? &(C_mode_mailbox&)*mod.GetParent() : NULL;
   bool removed = false;

   for(int i=0; i<messages.size(); i++){
      S_message &msg = messages[i];
                              //check if this message is among deleted
      if(msg.IsDeleted() && (msg.flags&msg.MSG_SERVER_SYNC) && !(msg.flags&msg.MSG_DELETED_DIRTY)){
         mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
         if(mod_mbox && mod_mbox->selection >= left_msgs.size() && mod_mbox->selection)
            --mod_mbox->selection;
         removed = true;
      }else{
                              //not deleted, keep it
         left_msgs.push_back(msg);
      }
   }
   if(removed){
      messages = left_msgs;
      mod.GetContainer().MakeDirty();
      if(mod_mbox)
         Mailbox_RecalculateDisplayArea(*mod_mbox);
   }
   mod.msgs_to_delete.clear();
}

//----------------------------

void C_mail_client::ConnectionCleanupAndDisconnect(C_mode_connection_in &mod, bool allow_imap_delete_thrash_move){

   if(StartDeletingMessages(mod, allow_imap_delete_thrash_move))
      return;
   if(mod.acc.IsImap()){
                              //try to update flags
      if(StartUpdatingServerFlags((C_mode_connection_imap&)mod))
         return;
   }else{
      if(mod.headers_to_move.size())
         Pop3CopyMessagesToFolders(mod);
   }

   switch(mod.action){
   case C_mode_connection::ACT_UPDATE_ACCOUNTS:
   case C_mode_connection::ACT_UPDATE_IMAP_FOLDERS:
      if(mod.IsImap()){
         ConnectionImapFolderClose((C_mode_connection_imap&)mod);
         break;
      }
   default:
      ConnectionDisconnect(mod);
   }
}

//----------------------------
#ifdef _DEBUG

static Cstr_w GetGoodMailName(const S_message_header &hdr){

   Cstr_w ret;
   ret.Copy(hdr.sender.email);
   ret.ToLower();
   return ret;
}

#endif

//----------------------------

bool C_mail_client::Connection_AfterUidList(C_mode_connection_in &mod, bool use_delete_list){

   switch(mod.action){
   case C_mode_connection::ACT_UPDATE_MAILBOX:
   case C_mode_connection::ACT_UPDATE_ACCOUNTS:
   case C_mode_connection::ACT_UPDATE_IMAP_FOLDERS:
   case C_mode_connection::ACT_IMAP_IDLE:
      {
                              //clean now and determine which msgs are new (to be retrieved)
         SyncMessages(mod, use_delete_list);
         if(!mod.headers.size()){
            ConnectionCleanupAndDisconnect(mod);
            return false;
         }
         mod.num_sync_hdrs = mod.num_hdrs_to_ask = mod.num_hdrs_to_get = mod.headers.size();
         mod.progress.pos = 0;
         mod.progress.total = mod.num_sync_hdrs;
         //LOG_RUN_N("New headers:", mod.num_sync_hdrs);
         ConnectionDrawProgress(mod);
         if(mod.IsImap())
            StartGettingNewHeadersIMAP((C_mode_connection_imap&)mod);
         else
            StartGettingNewHeadersPOP3((C_mode_connection_pop3&)mod);
      }
      break;

   default:
      assert(0);
   }
   return true;
}

//----------------------------

S_message &C_mail_client::Connection_AfterGotHeader(C_mode_connection_in &mod, S_message_header_base &hdr_sync, const char *hdr_string, dword hdr_size, int override_msg_size){

   S_message_header hdr;
   S_complete_header hdr_complete;
   (S_message_header_base&)hdr = hdr_sync;
   (S_message_header&)hdr_complete = hdr;
   ParseMailHeader(hdr_string, hdr_size, hdr_complete);
   hdr = hdr_complete;
   hdr.flags |= S_message::MSG_SERVER_SYNC;
   if(!(hdr.flags&S_message::MSG_READ))
      hdr.flags |= S_message::MSG_RECENT;

   bool get_entire_msg = (mod.acc.flags&mod.acc.ACC_UPDATE_GET_ENTIRE_MSG);
   dword partial_download_kb = 0;

   if(override_msg_size!=-1)
      hdr_complete.size = override_msg_size;

                              //auto-hide new header if it is already marked as deleted
   if(hdr.flags&hdr.MSG_DELETED){
      hdr.flags |= hdr.MSG_HIDDEN;
   }
                              //detect rule
   bool is_seen_hdr = true;
   is_seen_hdr = !(hdr_sync.flags&S_message::MSG_READ);
   bool is_heard_hdr = is_seen_hdr;
   const S_rule *rul = DetectRule(mod.acc, hdr_complete, mod.folder);
   if(rul){
      switch(rul->action){
      case S_rule::ACT_DELETE_IMMEDIATELY:
         mod.msgs_to_delete.push_back(mod.IsImap() ? hdr.imap_uid : hdr.pop3_server_msg_index);
         hdr.flags |= hdr.MSG_HIDDEN;
                              //flow...
      case S_rule::ACT_MARK_FOR_DELETE:
         hdr.flags |= hdr.MSG_DELETED | hdr.MSG_DELETED_DIRTY;
         is_heard_hdr = is_seen_hdr = false;
                              //flow...
      case S_rule::ACT_DOWNLOAD_HEADER:
         get_entire_msg = false;
         break;
      case S_rule::ACT_DOWNLOAD_BODY:
         get_entire_msg = true;
         break;
      case S_rule::ACT_PRIORITY_LOW:
         hdr.flags &= ~(hdr.MSG_PRIORITY_LOW | hdr.MSG_PRIORITY_HIGH);
         hdr.flags |= hdr.MSG_PRIORITY_LOW;
         break;
      case S_rule::ACT_PRIORITY_HIGH:
         hdr.flags &= ~(hdr.MSG_PRIORITY_LOW | hdr.MSG_PRIORITY_HIGH);
         hdr.flags |= hdr.MSG_PRIORITY_HIGH;
         break;

      case S_rule::ACT_SET_HIDDEN:
         hdr.flags |= hdr.MSG_HIDDEN;
         get_entire_msg = false;
         is_heard_hdr = is_seen_hdr = false;
         break;

      case S_rule::ACT_MOVE_TO_FOLDER:
         if(mod.IsImap()){
            //C_mode_connection_imap &con_imap = (C_mode_connection_imap&)mod;
            const Cstr_w &fld_name = rul->action_param;
            if(fld_name.Length() &&
               fld_name != mod.acc.GetFullFolderName(*mod.folder)
               ){
                              //change action to folder move (done after all headers downloaded)
               mod.headers_to_move.push_back(hdr_sync).move_folder_name = fld_name;
               is_heard_hdr = is_seen_hdr = false;
            }
            get_entire_msg = false;
         }else{
            const Cstr_w &fld_name = rul->action_param;
            mod.headers_to_move.push_back(hdr_sync).move_folder_name = fld_name;
            get_entire_msg = true;
         }
         break;

      case S_rule::ACT_DOWNLOAD_PARTIAL_BODY:
         get_entire_msg = true;
         partial_download_kb = rul->action_param_i;
         break;

      case S_rule::ACT_PLAY_SOUND:
         if(!C_phone_profile::IsSilentProfile())
            alert_manager.AddAlert(rul->action_param
            , rul->action_param_i
            );
         is_heard_hdr = false;
         break;

      case S_rule::ACT_SET_COLOR:
         if(rul->action_param_i<NUM_RULE_COLORS){
            hdr.flags |= (rul->action_param_i+1)<<hdr.MSG_COLOR_SHIFT;
         }else assert(0);
         break;

      default:
         assert(0);
      }
   }
   if(get_entire_msg && !(hdr.flags&hdr.MSG_DELETED)){
      if(mod.headers_to_download.capacity()==mod.headers_to_download.size())
         mod.headers_to_download.reserve(mod.headers_to_download.size()+100);
      mod.headers_to_download.push_back(C_mode_connection_in::S_message_header_download(hdr_sync)).partial_download_kb = partial_download_kb;
   }

                              //add new header now
   C_vector<S_message> &messages = mod.GetMessages();
                                 //make less memory copying by expanding the vector in big steps
   if(messages.capacity()==messages.size())
      messages.reserve(messages.size()+100);
   S_message &msg = messages.push_back(S_message());
   msg = hdr;
   mod.GetContainer().MakeDirty();
      
   mod.headers_added = true;

   if(is_seen_hdr){
                              //make new message alert
      if(mod.acc.IsImap() && (mod.acc.flags&S_account::ACC_IMAP_UPLOAD_SENT) &&
         mod.acc.GetFullFolderName(*mod.folder)==mod.acc.GetSentFolderName()
         ){
                              //don't do alert for messages in Sent folder
      }else{
         if(IsFocused())
            UpdateUnreadMessageNotify();
         FlashNewMailLed();
         home_screen_notify.AddNewMailNotify(mod.acc, *mod.folder, hdr, config_mail.tweaks.show_new_mail_notify);
      }
                              //add default sound
      if(is_heard_hdr){
         if(config_mail.alert_volume)
         {
            Cstr_w fn;
#ifdef USE_SYSTEM_PROFILES
            if(!C_phone_profile::GetEmailAlertTone(fn))
#endif
            if(!C_phone_profile::IsSilentProfile())
               fn = config_mail.alert_sound;
            if(fn.Length())
               alert_manager.AddAlert(fn
                  , config_mail.alert_volume
                  );
            alert_manager.vibrate = true;
         }
      }
   }
                              // increase progress indicator
   if(mod.action!=mod.ACT_IMAP_IDLE || mod.num_sync_hdrs){
      ++mod.progress.pos;
      dword tm = GetTickTime();
      if(mod.progress.pos==mod.progress.total || (tm-mod.last_progress_draw_time)>200){
         mod.last_progress_draw_time = tm;
         ConnectionDrawProgress(mod);
         Cstr_w s;
         s<<GetText(TXT_PROGRESS_NEW_HEADER) <<L' ' <<mod.progress.pos <<L'/' <<mod.num_sync_hdrs;
         ConnectionDrawAction(mod, s);
      }else
         mod.progress_drawn = true;
   }else
      ConnectionDrawAction(mod, GetText(TXT_PROGRESS_NEW_HEADER));
                              //move to next message
   --mod.num_hdrs_to_get;
                              //get next header, if there's some
   if(mod.num_hdrs_to_ask){
      if(!mod.IsImap())
         BeginGetHeaderPOP3((C_mode_connection_pop3&)mod);
      --mod.num_hdrs_to_ask;
   }
   return msg;
}

//----------------------------

void C_mail_client::Connection_AfterAllHeaders(C_mode_connection_in &mod){

   if(mod.headers_added)
      AfterHeadersAdded(mod);
   RedrawScreen();
   UpdateScreen();

   assert(!mod.headers.size());
   mod.headers.clear();
                              //check if there's more work to do with new headers
   if(mod.IsImap() && mod.headers_to_move.size()){
      StartMoveMessagesByRuleIMAP((C_mode_connection_imap&)mod);
      return;
   }
   if(mod.headers_to_download.size()){
      C_vector<S_message> &msgs = mod.GetMessages();

      C_vector<dword> retr_msg_indexes;
      retr_msg_indexes.reserve(mod.headers_to_download.size());

      mod.num_get_bodies = 0;
      mod.get_bodies_index = 0;
      mod.progress.pos = 0;
      mod.progress.total = 0;
      for(int i=0; i<mod.headers_to_download.size(); i++){
         const S_message_header_base &hdr = mod.headers_to_download[i];

         for(int j=msgs.size(); j--; ){
            S_message &msg = msgs[j];
            if(msg.MatchUID(hdr, mod.IsImap())){
               retr_msg_indexes.push_back(j);
               ++mod.num_get_bodies;
               mod.progress.total += msg.size;
               break;
            }
         }
      }
      if(mod.num_get_bodies){
         RedrawScreen();
         ConnectionDrawTitle(mod, GetText(TXT_PROGRESS_DOWNLOADING));
         //mod.action = mod.ACT_GET_MARKED_BODIES;
         ConnectionDrawProgress(mod);

         if(!mod.IsImap())
            StartRetrievingMessageMarkedBodiesPOP3((C_mode_connection_pop3&)mod, &retr_msg_indexes);
         else
            StartRetrievingMessageMarkedBodiesIMAP((C_mode_connection_imap&)mod, retr_msg_indexes);
         return;
      }
   }
   ConnectionCleanupAndDisconnect(mod);
}

//----------------------------

bool C_mail_client::ProcessReceivedLine(C_mode_connection_in &mod, const C_socket::t_buffer &line, Cstr_w &err){

   //LOG_RUN_N("ProcessReceivedLine", line.Size());

   if(mod.IsImap()){
      //LOG_RUN(line.Begin());
      //dword t = GetTickTime();
      if(!ProcessLineImap((C_mode_connection_imap&)mod, line, err))
         return false;
      /*
      t = GetTickTime()-t;
      if(t)
         LOG_RUN_N(" -e", t);
         */
   }else{
      if(!ProcessLinePop3((C_mode_connection_pop3&)mod, line, err))
         return false;
   }
   return true;
}

//----------------------------

void encr_segment_0b(){}

//----------------------------

void C_mail_client::AddRetrievedAttachmentData(const C_mode_connection_in &mod, S_download_body_data &body_data, const char *cp, dword sz, E_CONTENT_ENCODING content_encoding, Cstr_w &err) const{

                        //save non-text attachment
   if(!DecodeMessageAttachmentLine(cp, sz, content_encoding, body_data.att_saving.fl)){
      err = GetText(TXT_ERR_WRITE_DISK_FULL);
      mod.acc.socket = NULL;
   }
}

//----------------------------

void C_mail_client::FinishBodyRetrieval(C_mode_connection_in &mod, S_download_body_data &body_data, S_message &msg){

   msg.flags &= ~msg.MSG_PARTIAL_DOWNLOAD;
                              //do action depending on content type
   switch(body_data.retrieved_header.content.type){
   case CONTENT_MULTIPART:
      if(//body_data.partial_body && 
         !msg.HasBody()){
                              //allow saving partial text message (either limited part of body is downloaded, or multipart message is not correctly closed)
         S_download_body_data::S_multipart_info *pi = body_data.multi_part;
         if(!pi)
            break;
                              //allow saving multipart text type
         if(pi->hdr.content.type!=CONTENT_TEXT)
            break;

         if(!body_data.body_saving.IsStarted()){
            if(!BeginMessageBodyRetrieval(mod.GetContainer(), body_data, body_data.retrieved_header.content.subtype, NULL))
               break;
         }
         FinishMessageBodyRetrieval(mod.GetContainer(), body_data, pi->hdr, msg);
      }
      break;

   case CONTENT_TEXT:
      {
         if(!body_data.body_saving.IsStarted()){
            if(!BeginMessageBodyRetrieval(mod.GetContainer(), body_data, body_data.retrieved_header.content.subtype, NULL))
               break;
         }
         FinishMessageBodyRetrieval(mod.GetContainer(), body_data, body_data.retrieved_header, msg);
      }
      break;
   default:
      if(!msg.HasBody())
         SaveMessageBody(mod.GetContainer(), msg, "", 0);
      if(body_data.att_saving.filename.Length())
         FinishAttachmentRetrieval(mod, body_data, body_data.retrieved_header, msg);
   }
   body_data.multi_part = NULL;
   if(body_data.partial_body)
      msg.flags |= msg.MSG_PARTIAL_DOWNLOAD;
                              //exlicitly mark, also if we have no actual body contents
   //msg.flags |= msg.ST_BODY_RETRIEVED;
   if(!msg.HasBody()){
                              //make dummy body
      SaveMessageBody(mod.GetContainer(), msg, "", 0);
   }

   {
      mod.GetContainer().MakeDirty();
                              //reset selection, so that message preview is inited
      if(mod.GetParent() && mod.GetParent()->Id()==C_mode_mailbox::ID){
         C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mod.GetParent();
      //if(mod_mbox.selection==mod._msg_index)
         SetMailboxSelection(mod_mbox, mod_mbox.selection);
      }
   }
   if(mod.action == mod.ACT_GET_MARKED_BODIES){
      msg.marked = false;
      //RedrawScreen();
   }
}

//----------------------------

bool C_mail_client::BeginAttachmentRetrieval(const C_mode_connection_in &mod, S_download_body_data &body_data, const char *content_subtype, const wchar *suggested_filename) const{

                              //get extension of proposed filename
   Cstr_w ext;
   ext.Copy(content_subtype);
   Cstr_w s_ext = text_utils::GetExtension(suggested_filename);
   if(s_ext.Length()){
      s_ext.ToLower();
      ext = s_ext;
   }
   Cstr_w mail_path = mod.GetContainer().GetMailPath(mail_data_path);
   Cstr_w filename;
   file_utils::MakeUniqueFileName(mail_path, filename, ext);
   Cstr_w full_name;
   full_name<<mail_path <<filename;
   C_file::MakeSurePathExists(full_name);

   return body_data.att_saving.Start(full_name);
}

//----------------------------

void C_mail_client::FinishAttachmentRetrieval(C_mode_connection_in &mod, S_download_body_data &body_data, const S_complete_header &hdr, S_message &msg){

   bool is_inline = (hdr.content_disposition==DISPOSITION_INLINE && hdr.content.type==CONTENT_IMAGE && hdr.content_id.Length());
   C_buffer<S_attachment> &atts = is_inline ? msg.inline_attachments : msg.attachments;

   dword ai = atts.Size();
   atts.Resize(ai+1);
   S_attachment &att = atts[ai];
   att.filename = body_data.att_saving.filename.ToUtf8();
   att.suggested_filename = hdr.suggested_filename;
   att.content_id = hdr.content_id;
   mod.GetContainer().MakeDirty();

   body_data.att_saving.Finish();
}

//----------------------------

bool C_mail_client::AddRetrievedMessageLine(C_mode_connection_in &mod, S_download_body_data &body_data, S_message &msg, const C_buffer<char> &line, Cstr_w &err){

   mod.progress.pos += line.Size() + 1;
                              //draw progress & update screen (optimized)
   if(!mod.progress_drawn){
      mod.progress_drawn = true;
      ConnectionDrawProgress(mod);
      //ConnectionDrawFloatDataCounters(mod);
      //igraph->UpdateScreen();
   }

   const char *cp = line.Begin();
   dword line_len = StrLen(cp);
   if(mod.IsImap() || (!(cp[0]=='.' && cp[1]==0))){
      if(!body_data.got_header){
         if(*cp){
                              //save header line
            body_data.curr_hdr.insert(body_data.curr_hdr.end(), cp, cp+line_len);
            body_data.curr_hdr.push_back('\n');
            return true;
         }
                              //got entire header, parse it
         body_data.retrieved_header.Reset();
         ParseMailHeader(body_data.curr_hdr.begin(), body_data.curr_hdr.size(), body_data.retrieved_header);
         body_data.got_header = true;

         switch(body_data.retrieved_header.content.type){
         case CONTENT_MULTIPART:
            body_data.CreateMultiPartInfo();
            body_data.multi_part->boundary = body_data.retrieved_header.multipart_boundary;
            break;
         case CONTENT_TEXT:
                              //no setup nexessary
            break;
         default:
            if(body_data.retrieved_header.content_disposition==DISPOSITION_ATTACHMENT){
                              //start retrieving as attachment
               BeginAttachmentRetrieval(mod, body_data, body_data.retrieved_header.content.subtype, body_data.retrieved_header.suggested_filename);
            }
         }
         return true;
      }
                              //header retrieved, save body
      //dword sz = line.Size() - 1;
      //assert(line[sz]==0);
                              //take care of stuffed dots (ignore 1st such dot)
      if(!mod.IsImap() && *cp=='.'){
         ++cp;
         --line_len;
      }
                              //do action depending on content type
      switch(body_data.retrieved_header.content.type){
      case CONTENT_TEXT:
         {
            bool error = false;
            if(!body_data.body_saving.IsStarted()){
               error = !BeginMessageBodyRetrieval(mod.GetContainer(), body_data, body_data.retrieved_header.content.subtype, NULL);
            }
            if(!error){
               error = !DecodeMessageTextLine(cp, line_len, body_data.retrieved_header.content_encoding, body_data.body_saving.fl, &body_data);
            }
            if(error){
               mod.GetContainer().DeleteMessageBody(mail_data_path, msg);
               mod.GetContainer().MakeDirty();
               err = GetText(TXT_ERR_WRITE_DISK_FULL);
            }
         }
         break;

      case CONTENT_MULTIPART:
         {
            bool is_boundary = false;
                              //watch for boundary
            if(cp[0]=='-' && cp[1]=='-'){
               const char *cp1 = cp + 2;
                              //check if it's any (possibly nested) multipart's boundary
               S_download_body_data::S_multipart_info *pi = body_data.multi_part;
               if(mod.IsImap() && pi && !pi->upper_part && !pi->boundary.Length()){
                              //detect missing IMAP boundary
                  if(*cp1)
                     pi->boundary = cp+2;
               }
               while(pi){
                  is_boundary = (pi->boundary.Length() && text_utils::CheckStringBegin(cp1, pi->boundary, false));
                  if(is_boundary){
                     cp += 2 + pi->boundary.Length();
                     if(((S_download_body_data::S_multipart_info*)body_data.multi_part)!=pi){
                              //nested multipart not closed (may happen, but means corruption of part)
                        assert(0);
                        body_data.multi_part = pi;
                     }
                     break;
                  }
                  pi = pi->upper_part;
               }
            }

            S_download_body_data::S_multipart_info &pi = *body_data.multi_part;

            switch(pi.phase){
            case S_download_body_data::S_multipart_info::WAIT_PART_HEADER:
                              //waiting for boundary to appear
               if(is_boundary){
                              //begin retrieving part's header
                  pi.phase = pi.GET_PART_HEADER;
                  body_data.curr_hdr.clear();
               }
               break;
            case S_download_body_data::S_multipart_info::GET_PART_HEADER:
               {
                  if(is_boundary){
                              //possibly error, wait for next header
                     body_data.curr_hdr.clear();
                     break;
                  }
                  if(*cp){
                              //save header
                     body_data.curr_hdr.insert(body_data.curr_hdr.end(), cp, cp+line_len);
                     body_data.curr_hdr.push_back('\n');
                     break;
                  }
                              //got entire header, parse it
                  S_complete_header part_hdr;
                  ParseMailHeader(body_data.curr_hdr.begin(), body_data.curr_hdr.size(), part_hdr);
                  body_data.curr_hdr.clear();
                              //and switch to getting part's body

                  pi.phase = pi.IGNORE_PART;

                              //for alternative, previous body may be kept if newer mode is not supported
                  if(body_data.retrieved_header.content.subtype=="alternative"){

                     if(part_hdr.content.type==CONTENT_TEXT){
                        if(!msg.HasBody())
                           pi.phase = pi.GET_PART_BODY;
                        else{
                           if(part_hdr.content.subtype=="html"){
                                    //retrieve new body
                              //DeleteMessageFiles(msg);
                              if(!config_mail.tweaks.prefer_plain_text_body)
                                 pi.phase = pi.GET_PART_BODY;
                           }
                        }
                     }else
                     if(part_hdr.content.type==CONTENT_MULTIPART){
                        if(part_hdr.content.subtype=="mixed"){
                                       //in rare cases (from Apple mail), multipart/alternative has multipart/mixed as alternative part; accept it this way
                           pi.phase = pi.GET_PART_UNKNOWN;
                           body_data.retrieved_header = part_hdr;
                        }else
                        //if(part_hdr.content.subtype=="related")
                        {
                              //get all other parts
                           pi.phase = pi.GET_PART_UNKNOWN;
                        }
                     }else
                     if(pi.upper_part && pi.upper_part->hdr.content.type==CONTENT_MULTIPART && pi.upper_part->hdr.content.subtype=="related"){
                              //all other parts only if parent part is multipart/related
                        pi.phase = pi.GET_PART_ATTACHMENT;
                     }
                  }else{
                     switch(part_hdr.content_disposition){
                     case DISPOSITION_UNKNOWN:
                        if(part_hdr.content_id.Length())
                           body_data.retrieved_header.content_disposition = DISPOSITION_INLINE;
                     case DISPOSITION_INLINE:
                              //inline is allowed only text body
                        switch(part_hdr.content.type){
                        case CONTENT_TEXT:
                           pi.phase = pi.GET_PART_ATTACHMENT;
                           if(!msg.HasBody())
                              pi.phase = pi.GET_PART_BODY;
                           else{
                              if(part_hdr.content.subtype=="html"){
                                 if(!(msg.flags&msg.MSG_HTML)){
                                    if(!config_mail.tweaks.prefer_plain_text_body)
                                       pi.phase = pi.GET_PART_BODY;
                                 }
                              }
                           }
                           break;
                        case CONTENT_MULTIPART:
                           pi.phase = pi.GET_PART_UNKNOWN;
                           break;
                        default:
                              //get inline part as attachment
                           pi.phase = pi.GET_PART_ATTACHMENT;
                        }
                        break;
                     case DISPOSITION_ATTACHMENT:
                        pi.phase = pi.GET_PART_ATTACHMENT;
                        break;
                     default:
                        assert(0);
                     }
                  }

                  if(pi.phase != pi.IGNORE_PART){
                     pi.hdr = part_hdr;

                     if(pi.phase == pi.GET_PART_BODY){
                        if(!BeginMessageBodyRetrieval(mod.GetContainer(), body_data, pi.hdr.content.subtype, NULL))
                           pi.phase = pi.IGNORE_PART;
                     }else
                     if(pi.phase == pi.GET_PART_ATTACHMENT){
                              //attachment

                        if(!msg.HasBody())   //make dummy body
                           SaveMessageBody(mod.GetContainer(), msg, "", 0);

                        if(!BeginAttachmentRetrieval(mod, body_data, pi.hdr.content.subtype, pi.hdr.suggested_filename)){
                              //unknown file error, ignore the part
                           pi.phase = pi.IGNORE_PART;
                        }
                     }
                              //if it's nested multipart, create its nested info
                     if(pi.hdr.content.type == CONTENT_MULTIPART && pi.phase != pi.IGNORE_PART){
                        body_data.CreateMultiPartInfo();
                        body_data.multi_part->boundary = part_hdr.multipart_boundary;
                     }
                  }
               }
               break;
            case S_download_body_data::S_multipart_info::GET_PART_BODY:
            case S_download_body_data::S_multipart_info::GET_PART_ATTACHMENT:
               {
                              //save multipart message contents
                  if(pi.phase==pi.GET_PART_BODY){
                              //we accept only text-based main body
                     assert(pi.hdr.content.type==CONTENT_TEXT);
                     if(!is_boundary){
                        if(!DecodeMessageTextLine(cp, line_len, pi.hdr.content_encoding, body_data.body_saving.fl, &body_data)){
                           mod.GetContainer().DeleteMessageBody(mail_data_path, msg);
                           mod.GetContainer().MakeDirty();
                           err = GetText(TXT_ERR_WRITE_DISK_FULL);
                        }
                     }else{
                              //todo: remove last \n from body, it's not part of the body (by definition)

                        FinishMessageBodyRetrieval(mod.GetContainer(), body_data, pi.hdr, msg);
                     }
                  }else{
                     if(body_data.att_saving.filename.Length()){
                              //attachment, decode and save to file
                        if(!is_boundary){
                                 //save to file
                           AddRetrievedAttachmentData(mod, body_data, cp, line_len, pi.hdr.content_encoding, err);
                           if(err.Length()){
                                                //failed to save data, delete all message files
                              mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
                              mod.GetContainer().MakeDirty();
                              mod.acc.socket = NULL;
                           }
                        }else{
                                 //finalize - store attachment
                           FinishAttachmentRetrieval(mod, body_data, pi.hdr, msg);
                        }
                     }
                  }
               }
                              //flow...
            case S_download_body_data::S_multipart_info::IGNORE_PART:
            case S_download_body_data::S_multipart_info::GET_PART_UNKNOWN:
               if(is_boundary){
                  if(cp[0]=='-' && cp[1]=='-'){
                              //last boundary encountered, ignore rest of message
                     pi.phase = pi.FINISHED;
                     if(pi.upper_part){
                              //was nested multipart, go up one level
                        body_data.multi_part = pi.upper_part;
                     }
                  }else{
                              //switch to next header
                     pi.phase = pi.GET_PART_HEADER;
                  }
               }
               break;
            }
         }
         break;

      default:
         if(body_data.att_saving.filename.Length()){
            if(!DecodeMessageAttachmentLine(cp, line_len, body_data.retrieved_header.content_encoding, body_data.att_saving.fl)){
               mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
               mod.GetContainer().MakeDirty();
               err = GetText(TXT_ERR_WRITE_DISK_FULL);
               mod.acc.socket = NULL;
               return true;
            }
         }
      }
      return true;
   }
                              //end of retrieve
   return false;
}

//----------------------------
//----------------------------

bool C_mail_client::BeginMessageBodyRetrieval(const C_message_container &cnt, S_download_body_data &body_data, const char *ext, const wchar *suggested_filename) const{

                              //use "txt" instead of "plain"
   if(!StrCmp(ext, "plain"))
      ext = "txt";
   Cstr_w ext_w; ext_w.Copy(ext);
   if(!ext_w.Length())
      ext_w = L"msg";
   Cstr_w mail_path = cnt.GetMailPath(mail_data_path);
   Cstr_w filename = suggested_filename;
   
   //dword t = GetTickTime();
   file_utils::MakeUniqueFileName(mail_path, filename, ext_w);
   //LOG_RUN_N("MUFN", GetTickTime()-t);
   Cstr_w full_name; full_name<<mail_path <<filename;
   //C_file::MakeSurePathExists(full_name);

   body_data.flowed_text.Reset();
   if(body_data.retrieved_header.content.type==CONTENT_TEXT && body_data.retrieved_header.content.subtype=="plain" &&
      (body_data.retrieved_header.content_encoding==ENCODING_7BIT || body_data.retrieved_header.content_encoding==ENCODING_8BIT)){
      body_data.flowed_text.enable_decoding = true;
   }
   if(!body_data.body_saving.Start(full_name))
      return false;
   return true;
}

//----------------------------

bool C_mail_client::FinishMessageBodyRetrieval(C_message_container &cnt, S_download_body_data &body_data, const S_complete_header &hdr, S_message &msg) const{

   if(body_data.body_saving.fl.WriteFlush()!=C_file::WRITE_OK)
      return false;
   cnt.DeleteMessageBody(mail_data_path, msg);
   msg.body_filename = file_utils::GetFileNameNoPath(body_data.body_saving.filename).ToUtf8();
   msg.body_coding = hdr.text_coding;

   cnt.MakeDirty();
   body_data.body_saving.Finish();

   msg.flags &= ~msg.MSG_HTML;
   if(hdr.content.subtype=="html")
      msg.flags |= msg.MSG_HTML;
   return true;
}

//----------------------------

dword C_mail_client::CountMessagesForSending(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, C_message_container **outbox, int *_total_size) const{

   C_message_container *fld = NULL;
   switch(action){
   case C_mode_connection::ACT_SEND_MAILS:
      fld = cnt;
      break;
   default:
      fld = FindFolder(acc, S_account::default_outbox_folder_name);
   }
   if(!fld)
      return 0;
   fld->LoadMessages(mail_data_path);
   const C_vector<S_message> &messages = fld->messages;

   dword num = 0;
   dword total_size = 0;
   for(int i=messages.size(); i--; ){
      const S_message &msg = messages[i];
      if(!(msg.flags&msg.MSG_TO_SEND))
         continue;
                              //add this message
      ++num;
      total_size += fld->GetTotalMessageSize(msg, mail_data_path);
   }
   if(_total_size)
      *_total_size = total_size;
   if(outbox){
      *outbox = cnt;
      if(num)
         *outbox = fld;
   }
   return num;
}

//----------------------------

int C_mail_client::CountMessagesForUpload(S_account &acc, C_message_container **outbox, int *_total_size) const{

   if(!acc.IsImap())
      return 0;
   int num = 0;
   if(acc.flags&S_account::ACC_IMAP_UPLOAD_SENT){
                              //try sent folder
      C_message_container *cnt = FindFolder(acc, acc.GetSentFolderName());
      if(cnt){
         int total_size = 0;
         const C_vector<S_message> &messages = cnt->messages;
         for(int i=messages.size(); i--; ){
            const S_message &msg = messages[i];

            if((msg.flags&msg.MSG_SENT) && !(msg.flags&msg.MSG_SERVER_SYNC) && (msg.flags&msg.MSG_NEED_UPLOAD)){
                                    //add this message
               ++num;
                                    //determine message data size
               total_size += cnt->GetTotalMessageSize(msg, mail_data_path);
            }
         }
         if(_total_size)
            *_total_size = total_size;
         if(outbox)
            *outbox = cnt;
      }
   }
   if(!num){
                              //try drafts
      C_message_container *cnt = FindFolder(acc, acc.GetDraftFolderName());
      if(cnt){
         int total_size = 0;
         const C_vector<S_message> &messages = cnt->messages;
         for(int i=messages.size(); i--; ){
            const S_message &msg = messages[i];

            if((msg.flags&msg.MSG_DRAFT) && !(msg.flags&msg.MSG_SERVER_SYNC) && (msg.flags&msg.MSG_NEED_UPLOAD)){
                                    //add this message
               ++num;
                                    //determine message data size
               total_size += cnt->GetTotalMessageSize(msg, mail_data_path);
            }
         }
         if(_total_size)
            *_total_size = total_size;
         if(outbox)
            *outbox = cnt;
         num = -num;
      }
   }
   return num;
}

//----------------------------
// Function for nothing, revise.
static dword ConvertUnicodeToCodedChar(dword c, E_TEXT_CODING coding_hint, E_TEXT_CODING &coding){

   coding = COD_DEFAULT;
   if(c < 128)
      return c;

   switch(c&0xff00){
   case 0x000:
      coding = COD_WESTERN;
      return c;
   case 0x100:                //central european
      assert(0);
      coding = COD_CENTRAL_EUROPE;
      switch(c){
      case 0x104: return 0xa1;
      case 0x141: return 0xa3;
      case 0x13d: return 0xa5;
      case 0x15a: return 0xa6;
      case 0x160: return 0xa9;
      case 0x15e: return 0xaa;
      case 0x164: return 0xab;
      case 0x179: return 0xac;
      case 0x17d: return 0xae;
      case 0x17b: return 0xaf;

      case 0x105: return 0xb1;
      case 0x142: return 0xb3;
      case 0x13e: return 0xb5;
      case 0x15b: return 0xb6;
      case 0x161: return 0xb9;
      case 0x15f: return 0xba;
      case 0x165: return 0xbb;
      case 0x17a: return 0xbc;
      case 0x17e: return 0xbe;
      case 0x17c: return 0xbf;

      case 0x154: return 0xc0;
      case 0x102: return 0xc3;
      case 0x139: return 0xc5;
      case 0x106: return 0xc6;
      case 0x10c: return 0xc8;
      case 0x118: return 0xca;
      case 0x11a: return 0xcc;
      case 0x10e: return 0xcf;

      case 0x143: return 0xd1;
      case 0x147: return 0xd2;
      case 0x158: return 0xd8;
      case 0x16e: return 0xd9;
      case 0x170: return 0xdb;
      case 0x162: return 0xde;

      case 0x155: return 0xe0;
      case 0x103: return 0xe3;
      case 0x13a: return 0xe5;
      case 0x107: return 0xe6;
      case 0x10d: return 0xe8;
      case 0x119: return 0xea;
      case 0x11b: return 0xec;
      case 0x10f: return 0xef;

      case 0x144: return 0xf1;
      case 0x148: return 0xf2;
      case 0x159: return 0xf8;
      case 0x16f: return 0xf9;
      case 0x171: return 0xfb;
      case 0x163: return 0xfe;
      }
      break;
   case 0x400:                //cyrillic
      assert(0);
      coding = COD_CYRILLIC_WIN1251;
      if(c>=0x410 && c<0x450)
         return c+0xc0-0x410;
      switch(c){
      case 0x401: return 0xa8;
      case 0x451: return 0xb8;
      }
      break;
   }
                              //unknown character
   coding = COD_DEFAULT;
   return '?';
}

//----------------------------
// Encode given unicode string to 7-bit data, using best technique (plain or =?...?= coding).
static void EncodeText(const wchar *txt, int len, C_vector<char> &buf, bool use_quotes_for_spaces, bool force_quotes){

   int i;

   //E_TEXT_CODING cod = COD_DEFAULT;
                              //check if any char is out of 7-bit range
   bool has_special = false;
   bool use_quotes = force_quotes;
   for(i=0; i<len; i++){
      wchar c = txt[i];
      assert(c >= ' ');
      if(c >= 127){
         has_special = true;
         /*
                              //determine charset coding
         E_TEXT_CODING cod1;
         ConvertUnicodeToCodedChar(c, cod, cod1);
                              //here it is not accurate - we accept 1st non-standard coding
         if(cod==COD_DEFAULT || cod==COD_WESTERN)
            cod = cod1;
            */
      }else
      if(c==' ' && use_quotes_for_spaces)
         use_quotes = true;
      else
      if(c=='\"')
         has_special = true;
         /*
         if(!text_utils::IsAlNum(c)){
            has_special = true;
         }
         */
   }
   if(!has_special){
                              //use plain text
      if(use_quotes)
         buf.push_back('\"');
      for(i=len; i--; )
         buf.push_back(char(*txt++));
      if(use_quotes)
         buf.push_back('\"');
      return;
   }
   static const char charset[] = "utf-8";
   buf.push_back('=');
   buf.push_back('?');
                              //add charset
   buf.insert(buf.end(), charset, charset+sizeof(charset)-1);
   buf.push_back('?');
                              //add encoding
   buf.push_back('b');
   buf.push_back('?');

   Cstr_c utf8 = Cstr_w(txt).ToUtf8();
   EncodeBase64((const byte*)(const char*)utf8, utf8.Length(), buf);
   /*
                              //add text
   for(i=len; i--; ){
      wchar c = *txt++;

      byte utf8[4];
      text_utils::ConvertCharToUTF8(c, utf8);
      for(int ii=0; utf8[ii]; ii++){
         byte c1 = utf8[ii];

         if(c1==' ')
            buf.push_back('_');
         else
         //if(text_utils::IsAlNum(c1))
         if(c1>' ' && c1<127 && c1!='=' && c1!='?'){
                              //represent 'as is'
            buf.push_back(c1);
         }else{
                              //encode char
            buf.push_back('=');
            c1 = (byte)ConvertUnicodeToCodedChar(c1, cod, cod);
            assert(c1<256);
            word w = text_utils::MakeHexString(c1);
            buf.push_back(char(w&0xff));
            buf.push_back(char(w>>8));
         }
      }
   }
   */
   buf.push_back('?');
   buf.push_back('=');
}

//----------------------------

static void ADD_STRING(C_vector<char> &buf, const char *cp){
   buf.insert(buf.end(), cp, cp+StrLen(cp));
}
static void ADD_CSTR(C_vector<char> &buf, const Cstr_c &str){
   buf.insert(buf.end(), str, str+str.Length());
}

static const char
    mime_kw_content[] = "Content-Type: ",
    mime_kw_content_octet_stream[] = "application/octet-stream",
    mime_kw_content_msword[] = "application/msword",
    mime_kw_content_text[] = "text/plain",
    mime_kw_content_image[] = "image/",
    mime_kw_encoding[] = "Content-Transfer-Encoding: ",
    mime_kw_encoding_base64[] = "base64",
    mime_kw_boundary_prefix[] = "--",
    mime_kw_disposition[] = "Content-Disposition: attachment;\r\n filename=";

//----------------------------

void C_mail_client::PrepareMailHeadersForSending(const C_mode_connection &mod, C_connection_send &con_send, const S_message &msg, C_vector<char> &buf){

   buf.reserve(1024);

   static const char
      kw_subj[] = "Subject: ",
      kw_date[] = "Date: ",
      kw_from[] = "From: ",
      kw_to[] = "To: ",
      kw_cc[] = "Cc: ",
      kw_priority[] = "X-Priority: ",
      //kw_low[] = "Low",
      //kw_high[] = "High",
      kw_mime[] = "MIME-Version: 1.0\r\n",
      text_plain[] = "text/plain; charset=utf-8; format=flowed",
      multipart[] = "multipart/mixed;\r\n boundary=",
      quoted_p[] = "quoted-printable",
      mime_info[] = "This is a multi-part message in MIME format.\r\n\r\n",
      kw_x_mailer[] = "X-Mailer: LCG ProfiMail ",
      //kw_in_reply_to[] = "In-Reply-To: ",
      kw_message_id[] = "Message-ID: ",
      kw_reply_to[] = "Reply-to: ",
      kw_references[] = "References: ",
      eol[] = "\r\n";
                              //add subject
   ADD_STRING(buf, kw_subj);
   {
      Cstr_w s = msg.subject.FromUtf8();
      EncodeText(s, s.Length(), buf, false, false);
   }
   ADD_STRING(buf, eol);
                              //add date
   ADD_STRING(buf, kw_date);
   S_date_time_x dt;
   dt.SetFromSeconds(msg.date);
   Cstr_c date_str;
   {
      int timezone_delta = dt.GetTimeZoneMinuteShift();
      date_str.Format("%, #02% % #04% #02%:#02%:#02% %#02%#02%\r\n")
         <<dt.GetDayName()
         <<(dt.day+1)
         <<dt.GetMonthName()
         <<(int)dt.year
         <<(int)dt.hour
         <<(int)dt.minute
         <<(int)dt.second;

      date_str <<char(timezone_delta<0 ? '-' : '+');
      timezone_delta = Abs(timezone_delta);
      date_str <<(timezone_delta/60)
         <<(timezone_delta%60);
   }
   ADD_CSTR(buf, date_str);
                              //add sender, either from message (if filled), or from default account setting
   {
      ADD_STRING(buf, kw_from);
      Cstr_w sender;
      if(msg.sender.display_name.Length())
         sender = msg.sender.display_name.FromUtf8();
      else
         sender = mod.acc.primary_identity.display_name.FromUtf8();
      EncodeText(sender, sender.Length(), buf, true, true);
      buf.push_back(' ');
      buf.push_back('<');
      const Cstr_c &email = msg.sender.email.Length() ? msg.sender.email : mod.acc.primary_identity.email;
      ADD_CSTR(buf, email);
      buf.push_back('>');
      ADD_STRING(buf, eol);

      const Cstr_c &s_reply_to = msg.sender.email.Length() ? msg.sender.reply_to_email : mod.acc.primary_identity.reply_to_email;
      if(s_reply_to.Length()){
         ADD_STRING(buf, kw_reply_to);
         ADD_CSTR(buf, s_reply_to);
         ADD_STRING(buf, eol);
      }
   }
   /*
   if(mod.acc.reply_to_email.Length()){
      ADD_STRING(buf, reply_to);
      ADD_CSTR(buf, mod.acc.reply_to_email);
      ADD_STRING(buf, eol);
   }
   */

                              //add recipients (To: and Cc:)
   {
      for(int ii=0; ii<2; ii++){
         const Cstr_c &emails = !ii ? msg.to_emails : msg.cc_emails;
         C_vector<Cstr_c> addresses;
         if(ParseRecipients(emails, addresses) && addresses.size()){
            ADD_STRING(buf, !ii ? kw_to : kw_cc);
            for(int i=0; i<addresses.size(); i++){
               const Cstr_c &rcpt = addresses[i];
               if(i){
                  buf.push_back(',');
                  ADD_STRING(buf, eol);
                  buf.push_back(' ');
               }
                                       //look up in address book
               S_contact con;
               if(FindContactByEmail(rcpt, con)){
                                       //decorate name
                  //buf.push_back('\"');
                  Cstr_w cn = AddressBook_GetName(con);
                  EncodeText(cn, cn.Length(), buf, true, true);
                  //buf.push_back('\"');
                  buf.push_back(' ');
                  buf.push_back('<');
                  ADD_CSTR(buf, rcpt);
                  buf.push_back('>');
               }else{
                                       //not in address book, add as plain recipient
                  ADD_CSTR(buf, rcpt);
               }
            }
            ADD_STRING(buf, eol);
         }
      }
   }
                              //add priority
   if(msg.flags&(msg.MSG_PRIORITY_HIGH | msg.MSG_PRIORITY_LOW)){
      ADD_STRING(buf, kw_priority);
      buf.push_back((msg.flags&msg.MSG_PRIORITY_HIGH) ? '1' : '5');
      ADD_STRING(buf, eol);
   }
                              //add MIME
   ADD_STRING(buf, kw_mime);
   ADD_STRING(buf, kw_x_mailer);
   {
      Cstr_c s; s.Format("%.#02%") <<VERSION_HI <<VERSION_LO;
      ADD_STRING(buf, s);
   }
   ADD_STRING(buf, eol);

   {
                              //add Message-Id
      ADD_STRING(buf, kw_message_id);
      buf.push_back('<');
      ADD_CSTR(buf, msg.our_message_id);
      buf.push_back('>');
      ADD_STRING(buf, eol);
   }
   /*                         //don't post In-reply-to since we post References
   if(msg.message_id.Length()){
      ADD_STRING(buf, in_reply_to);
      buf.push_back('<');
      ADD_CSTR(buf, msg.message_id);
      buf.push_back('>');
      ADD_STRING(buf, eol);
   }
   */
   if(msg.references.Length() || msg.message_id.Length()){
      Cstr_c refs = msg.references;
      if(refs.Length() && refs[refs.Length()-1]!=' ')
         refs<<' ';
      refs<<'<' <<msg.message_id  <<'>';
      ADD_STRING(buf, kw_references);
      ADD_CSTR(buf, refs);
      ADD_STRING(buf, eol);
   }

                              //open message body now
   bool has_high = false;
   {
      S_text_display_info tmp;
      OpenMessageBody(mod.GetContainer(), msg, tmp, true);
      if(tmp.body_c.Length()){
                              //convert back from tdi to wide-char
         C_vector<wchar> buf1;
         buf1.reserve(tmp.body_c.Length());
         const char *cp = tmp.body_c;
         while(*cp){
            wchar c = byte(*cp++);
            if(S_text_style::IsControlCode(c)){
               if(c!=CC_WIDE_CHAR){
                  --cp;
                  S_text_style::SkipCode(cp);
                  continue;
               }
               c = S_text_style::ReadWideChar(cp);
            }else
               c = (wchar)encoding::ConvertCodedCharToUnicode(c, tmp.body_c.coding);
            buf1.push_back(c);
         }
         buf1.push_back(0);
                              //and now to utf-8
         has_high = con_send.send_body.ToUtf8(buf1.begin());
      }else
         has_high = con_send.send_body.ToUtf8(tmp.body_w);
   }
   {
                              //determine msg's size shown for progress
      con_send.msg_send_progress_size = 0;
      Cstr_w mail_path = mod.GetContainer().GetMailPath(mail_data_path);
      Cstr_w body_fname;
      body_fname<<mail_path <<msg.body_filename.FromUtf8();
      C_file ck;
      if(ck.Open(body_fname))
         con_send.msg_send_progress_size = ck.GetFileSize();
   }

   if(msg.attachments.Size()){
      ADD_STRING(buf, mime_kw_content);
      ADD_STRING(buf, multipart);
      con_send.send_multipart_boundary = "----=_ProfiMail.Boundary";
      buf.push_back('\"');
      ADD_CSTR(buf, con_send.send_multipart_boundary);
      buf.push_back('\"');
      ADD_STRING(buf, eol);
      ADD_STRING(buf, eol);
                              //begin 1st body part
      ADD_STRING(buf, mime_info);

      ADD_STRING(buf, mime_kw_boundary_prefix);
      ADD_CSTR(buf, con_send.send_multipart_boundary);
      ADD_STRING(buf, eol);
   }
   ADD_STRING(buf, mime_kw_content);

   {
                              //add header of body
      ADD_STRING(buf, text_plain);
      con_send.send_phase = con_send.SEND_TEXT_PLAIN;
      if(has_high){
         con_send.send_phase = con_send.SEND_TEXT_QUOTED_PRINTABLE;
                              //encode with quoted-printable
         ADD_STRING(buf, eol);
         ADD_STRING(buf, mime_kw_encoding);
         ADD_STRING(buf, quoted_p);
      }   
   }
   ADD_STRING(buf, eol);

   con_send.char_index = 0;
                              //empty line after headers
   ADD_STRING(buf, eol);
}

//----------------------------

bool C_mail_client::PrepareNextMessageData(C_mode_connection &mod, C_connection_send &con_send, const S_message &msg, C_vector<char> &buf, bool count_progress){

   static const char
      lfcr[] = "\r\n",
      jpeg[] = "jpeg";

   switch(con_send.send_phase){
   case C_connection_send::SEND_TEXT_PLAIN:
   case C_connection_send::SEND_TEXT_QUOTED_PRINTABLE:
      {
         const char *cp = con_send.send_body;
         if(cp[con_send.char_index]){
            cp += con_send.char_index;
                              //find next eol, and send one line
            int MAX_LINE_LEN;
            if(con_send.send_phase==C_connection_send::SEND_TEXT_QUOTED_PRINTABLE)
               MAX_LINE_LEN = 76;
            else{
               MAX_LINE_LEN = 78-2;  //flowed, max is 78, add 2 for stuffed and trailing spaces
               if(con_send.txt_flowed_curr_quote_count!=-1)
                  MAX_LINE_LEN = Max(1, MAX_LINE_LEN-con_send.txt_flowed_curr_quote_count);
            }
            int max_len = Min(MAX_LINE_LEN, (int)con_send.send_body.Length()-con_send.char_index);
            int send_len, last_space_i = -1;
            bool is_hard_break = false;
            {
               int fill_len = 0;
               for(send_len=0; send_len<max_len && fill_len<MAX_LINE_LEN; ++send_len, ++fill_len){
                  wchar c = cp[send_len];
                  if(c == '\n'){
                     is_hard_break = true;
                     break;
                  }
                  if(c==' ')
                     last_space_i = send_len;
                  if(con_send.send_phase==C_connection_send::SEND_TEXT_QUOTED_PRINTABLE){
                     if(!((c>=32 && c<=60) || (c>=62 && c<=126))){
                                 //count char as 3
                        fill_len += 2;
                     }
                  }
               }
            }
            buf.reserve(buf.size() + send_len*3+4);
            int consume_len = send_len;

            if(!is_hard_break){
                              //soft line break
               if(cp[send_len] && last_space_i!=-1){
                  send_len = last_space_i+1;
                  consume_len = send_len;
               }
            }else{
                              //hard line break
               ++consume_len;
            }
                              //pre-stuff dot by another dot
            if(!mod.acc.IsImap() && *cp=='.')
               buf.push_back('.');

            switch(con_send.send_phase){
            case C_connection_send::SEND_TEXT_PLAIN:
               {
                  if(send_len==3 && cp[0]=='-' && cp[1]=='-' && cp[2]==' '){
                              //special signature block
                  }else{
                              //remove any trailing spaces (conflicts with flowed format)
                     while(send_len && cp[send_len-1]==' ')
                        --send_len;
                  }
                  bool space_stuff = false;
                  if(con_send.txt_flowed_curr_quote_count>0){
                              //add quotes from previous line
                     for(int i=0; i<con_send.txt_flowed_curr_quote_count; i++)
                        buf.push_back('>');
                     space_stuff = true;
                  }
                  if(!is_hard_break){
                     if(con_send.txt_flowed_curr_quote_count == -1){
                                 //count quotes on this line, since this is beginning of flowed-split line
                        int qc;
                        for(qc=0; cp[qc]=='>'; ++qc);
                        con_send.txt_flowed_curr_quote_count = qc;
                     }
                  }else{
                              //mark hard-break line
                     con_send.txt_flowed_curr_quote_count = -1;
                  }
                  space_stuff = (*cp==' ') || space_stuff;

                  if(space_stuff)
                     buf.push_back(' ');
                              //add line contents
                  for(int i=0; i<send_len; i++)
                     buf.push_back(cp[i]);

                              //add space for soft line break
                  if(!is_hard_break)
                     buf.push_back(' ');
               }
               break;
            case C_connection_send::SEND_TEXT_QUOTED_PRINTABLE:
               for(int i=0; i<send_len; i++){
                  char c = cp[i];
                  if((c>=32 && c<=60) || (c>=62 && c<=126))
                     buf.push_back(c);
                  else{
                     buf.push_back('=');
                     word cc = text_utils::MakeHexString(byte(c));
                     buf.push_back(char(cc&0xff));
                     buf.push_back(char(cc>>8));
                  }
               }
               if(!is_hard_break && *cp){
                              //mark soft line break
                  buf.push_back('=');
               }
               break;
            }
                              //append <LFCR>
            ADD_STRING(buf, lfcr);

            cp += consume_len;
            if(is_hard_break && !*cp){
                              //for last eol, add one more <LFCR>, because last <LFCR>.<LFCR> doesn't belong to text
               ADD_STRING(buf, lfcr);
            }
                              //move pointer to next data
            con_send.char_index += consume_len;
            assert(con_send.char_index <= int(con_send.send_body.Length()));

            if(count_progress){
               int num_progress = Min(con_send.msg_send_progress_size, consume_len*(int)sizeof(wchar));
               if(num_progress){
                  mod.progress.pos += num_progress;
                  con_send.msg_send_progress_size -= num_progress;
               }
            }
            return false;
         }
         if(count_progress){
                              //fix progress for this msg
            mod.progress.pos += con_send.msg_send_progress_size;
         }
                              //if no attachments, then we're done
         if(!msg.attachments.Size())
            return true;
         con_send.send_phase = con_send.SEND_ATTACHMENT_PREPARE;
         con_send.attach_indx = 0;
         return PrepareNextMessageData(mod, con_send, msg, buf, count_progress);
      }
      break;

   case C_connection_send::SEND_ATTACHMENT_PREPARE:
      {
         buf.reserve(buf.size() + 1024);
         if(con_send.attach_indx==int(msg.attachments.Size())){
                              //last attachment was written, send final boundary
            ADD_STRING(buf, mime_kw_boundary_prefix);
            ADD_CSTR(buf, con_send.send_multipart_boundary);
            ADD_STRING(buf, mime_kw_boundary_prefix);
            ADD_STRING(buf, lfcr);
            con_send.send_phase = con_send.SEND_ATTACHMENT_DONE;
         }else{
            const S_attachment &att = msg.attachments[con_send.attach_indx];
                              //try to open attachment
            if(!con_send.fl_attachment_send.Open(att.filename.FromUtf8())){
                              //failed to open file, ignore attachment & send next
               ++con_send.attach_indx;
               return PrepareNextMessageData(mod, con_send, msg, buf, count_progress);
            }

                              //start sending attachment - prepare & send header
            ADD_STRING(buf, mime_kw_boundary_prefix);
            ADD_CSTR(buf, con_send.send_multipart_boundary);
            ADD_STRING(buf, lfcr);

                              //write content type
            ADD_STRING(buf, mime_kw_content);
            const wchar *ext = text_utils::GetExtension(att.suggested_filename);
            if(ext && !text_utils::CompareStringsNoCase(ext, L"jpg")){
               ADD_STRING(buf, mime_kw_content_image);
               ADD_STRING(buf, jpeg);
            }else
               ADD_STRING(buf, mime_kw_content_octet_stream);
            ADD_STRING(buf, lfcr);
                                 //write encoding
            ADD_STRING(buf, mime_kw_encoding);
            ADD_STRING(buf, mime_kw_encoding_base64);
            ADD_STRING(buf, lfcr);
                                 //write disposition
            ADD_STRING(buf, mime_kw_disposition);
            EncodeText(att.suggested_filename, att.suggested_filename.Length(), buf, false, true);
            ADD_STRING(buf, lfcr);

            ADD_STRING(buf, lfcr);
            con_send.send_phase = con_send.SEND_ATTACHMENT_SEND;
         }
         return false;
      }
      break;

   case C_connection_send::SEND_ATTACHMENT_SEND:
      {
         int sz = con_send.fl_attachment_send.GetFileSize() - con_send.fl_attachment_send.Tell();
         if(!sz){
                              //entire attachment sent, add empty line and move to next
            con_send.fl_attachment_send.Close();
            buf.insert(buf.end(), lfcr, lfcr+2);
            ++con_send.attach_indx;
            con_send.send_phase = con_send.SEND_ATTACHMENT_PREPARE;
            return false;
         }
         sz = Min(sz, 57);
         byte data[57];
         con_send.fl_attachment_send.Read(data, sz);

         buf.reserve(sz*4/3+2);
         EncodeBase64(data, sz, buf);
         buf.insert(buf.end(), lfcr, lfcr+2);

         if(count_progress){
            mod.progress.pos += sz;
         }
         return false;
      }
      break;

   case C_connection_send::SEND_ATTACHMENT_DONE:
      break;
   }
                              //nothing more to send
   return true;
}

//----------------------------

bool C_mail_client::SendNextMessageData(C_mode_connection &mod, C_connection_send &con_send, C_socket *socket, const S_message &msg, z_stream *compress_out){

   int prev_progress = mod.progress.pos;
   C_vector<char> buf;
   const int USE_SIZE = 8192;
   buf.reserve(USE_SIZE+2000);
   while(buf.size()<USE_SIZE){
      if(PrepareNextMessageData(mod, con_send, msg, buf, true))
         break;
   }
   if(!buf.size())
      return true;

   if(compress_out)
      SendCompressedData(socket, buf.begin(), buf.size(), *compress_out);
   else
      socket->SendData(buf.begin(), (dword)buf.size());
   if(mod.progress.pos!=prev_progress){
      ConnectionDrawProgress(mod);
   }
   return false;
}

//----------------------------

void C_mail_client::CancelMessageRetrieval(C_mode_connection &mod, dword msg_index){

   C_vector<S_message> &messages = mod.GetMessages();
   //assert(mod.state==mod.ST_POP3_RETR);
   S_message &msg = messages[msg_index];
   if(mod.GetContainer().DeleteMessageFiles(mail_data_path, msg))
      mod.GetContainer().MakeDirty();
}

//----------------------------

void C_mail_client::InitLayoutConnection(C_mode_connection_base &mod){

   mod.data_cnt_bgnd = NULL;

   int sx = ScrnSX();
   int sz_x = Min(sx - fdb.letter_size_x * 4, fdb.cell_size_x*20);
   const int DATA_SZ = fds.cell_size_x/2;
   const int title_sy = GetDialogTitleHeight();
   int sy = title_sy + fdb.line_spacing*mod.LINE_LAST;
                              
   mod.rc = S_rect((sx-sz_x)/2, (ScrnSY()-fdb.line_spacing*2-sy)/2, sz_x, sy);

   const int PROG_SY = fdb.cell_size_y;
   mod.progress.rc = S_rect(mod.rc.x+fdb.letter_size_x*2, mod.rc.y+title_sy+fdb.line_spacing*mod.LINE_PROGRESS+(fdb.line_spacing-PROG_SY-1)/2, mod.rc.sx-fdb.letter_size_x*4, PROG_SY);

   mod.rc_data_in = S_rect(mod.rc.x+2, mod.rc.y+title_sy+(fdb.line_spacing-DATA_SZ)/2, DATA_SZ, DATA_SZ);
   mod.rc_data_out = mod.rc_data_in; mod.rc_data_out.x += DATA_SZ*2;
   {
      mod.alive_progress.rc = S_rect(mod.rc_data_out.Right()+DATA_SZ*2, mod.rc_data_out.y, mod.rc.Right()-2, DATA_SZ);
      mod.alive_progress.rc.sx -= mod.alive_progress.rc.x+fdb.cell_size_x/4;
      C_image *img = C_image::Create(*this);
      mod.alive_progress.img = img;
      img->Release();
      img->Create(mod.alive_progress.rc.sx, mod.alive_progress.rc.sy);
      byte *data = (byte*)img->GetData();
      const dword pitch = img->GetPitch();
      const int center = mod.alive_progress.rc.sx/4;
      for(int x=0; x<mod.alive_progress.rc.sx; x++, data+=GetPixelFormat().bytes_per_pixel){
         S_rgb_x rgb;
         rgb.r = 0;
         rgb.g = 0;
         rgb.b = 0;
         if(x<center*2){
            rgb.g = byte((center-Abs(x-center))*255/center);
         }
         dword pix;
         switch(GetPixelFormat().bits_per_pixel){
         default:
#ifdef SUPPORT_12BIT_PIXEL
         case 12: pix = rgb.To12bit(); break;
#endif
         case 16: pix = rgb.To16bit(); break;
         case 32: pix = rgb.To32bit(); break;
         }
         for(int y=0; y<mod.alive_progress.rc.sy; y++){
            byte *d = data+pitch*y;
            switch(GetPixelFormat().bytes_per_pixel){
            case 2: *(word*)d = word(pix); break;
            case 4: *(dword*)d = pix; break;
            }
         }
      }
   }
}

//----------------------------

void C_mail_client::ConnectionError(C_mode_connection_in &mod, const wchar *err){

   if(mod.headers_added)
      AfterHeadersAdded(mod);

   if(!mod.params.auto_update){
      //mod.socket = NULL;
      mod.acc.socket = NULL;
      CloseMode(mod, false);
      if(mode->Id()==C_mode_folders_list::ID){
         C_mode_folders_list &mod_fl = (C_mode_folders_list&)*mode;
         if(mod_fl.mod_upd_mboxes)
            CloseMode(mod_fl, false);
      }
      if(socket_log==SOCKET_LOG_YES)
         C_client_viewer::OpenFileForViewing(this, NETWORK_LOG_FILE, L"Network log:");
      ShowErrorWindow(TXT_ERROR, err);
   }else{
      //mod.cancel_request = true;
      mod.need_expunge = false;
      ConnectionDisconnect(mod);
   }
}

//----------------------------

void C_mail_client::ConnectionDataReceived(C_mode_connection_in &mod, C_socket *socket, Cstr_w &err, bool check_mode){

   if(mod.acc.IsImap() && (((C_mode_connection_imap&)mod).capability&C_mode_connection_imap::CAPS_IN_COMPRESSION)){
                        //decompress stream
      C_mode_connection_imap &mod_imap = (C_mode_connection_imap&)mod;
      z_stream &zs = mod_imap.compress_in;
                        //read data
      C_socket::t_buffer buf;
      if(socket->GetData(buf, 1, 0)){
                        //decompress
         //LOG_RUN_N("B", buf.Size());
         //int n = 0;
         //dword t = GetTickTime();
         mod_imap.decompress_cache.insert(mod_imap.decompress_cache.end(), (const byte*)buf.Begin(), (const byte*)buf.Begin()+buf.Size());
         buf.Resize(1024);
         zs.next_in = mod_imap.decompress_cache.begin();
         zs.avail_in = mod_imap.decompress_cache.size();
         Cstr_c &dec_buf = mod_imap.decompress_buf;
         while(zs.avail_in){
            zs.next_out = (byte*)buf.Begin();
            zs.avail_out = buf.Size();
            int zerr = inflate(&zs, 0);
            if(zerr){
               if(zs.avail_in){
                  assert(0);
               }
               break;
            }
            dword len = buf.Size()-zs.avail_out;
            Cstr_c s;
            s.Allocate((const char*)buf.Begin(), len);
            dec_buf<<s;
         }
         mod_imap.decompress_cache.clear();

                        //now parse lines and process them
         while(true){
            int eol = dec_buf.Find('\n');
            if(eol==-1)
               break;
            int len = eol;
            if(len && dec_buf[len-1]=='\r')
               --len;
            C_socket::t_buffer line;
            line.Assign((const char*)dec_buf, (const char*)dec_buf+eol+1);
            line[len] = 0;
            dec_buf = dec_buf.RightFromPos(eol+1);
            //++n;
            if(!ProcessReceivedLine(mod, line, err))
               break;
            if(check_mode && &mod!=mode)
               return;
            if(err.Length())
               break;
         }
         //LOG_RUN_N("        e", GetTickTime()-t);
      }
   }else{
      C_socket::t_buffer line;
      while(socket->GetLine(line)){
         if(!ProcessReceivedLine(mod, line, err))
            break;
         if(check_mode && &mod!=mode)
            return;
         if(err.Length())
            break;
      }
   }
}

//----------------------------

void C_mail_client::ConnectionSocketEvent(C_mode_connection_in &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){

   ConnectionDrawSocketEvent(mod, ev);
   //ConnectionDrawFloatDataCounters(mod);

   mod.progress_drawn = false;
   switch(ev){
   case C_socket_notify::SOCKET_DATA_RECEIVED:
      {
         Cstr_w err;
         ConnectionDataReceived(mod, socket, err, true);
         if(err.Length())
            ConnectionError(mod, err);
      }
      break;

   case C_socket_notify::SOCKET_SSL_HANDSHAKE:
      {
         const char *cmd;
         if(mod.IsImap())
            cmd = "* OK";
         else
            cmd = "+OK";
         C_socket::t_buffer line;
         line.Assign(cmd, cmd+StrLen(cmd));
         Cstr_w err;
         ProcessReceivedLine(mod, line, err);
         if(err.Length())
            ConnectionError(mod, err);
      }
      break;

   case C_socket_notify::SOCKET_ERROR:
   case C_socket_notify::SOCKET_FINISHED:
      if(mod.using_cached_socket){
         mod.acc.socket = NULL;
         ConnectionInitSocket(mod);
      }else{
         LOG_RUN("Socket error");
         Cstr_w err;
         if(ev==C_socket_notify::SOCKET_FINISHED)
            err = L"Connection terminated by server";
         else
            err = GetErrorName(socket->GetSystemErrorText());
         mod.acc.socket = NULL;
         if(mod.IsImap())
            ConnectionErrorImap((C_mode_connection_imap&)mod, err);
         else
            ConnectionErrorPop3((C_mode_connection_pop3&)mod, err);
      }
      break;
   }
}

//----------------------------

void C_mail_client::TickConnection(C_mode_connection_base &mod, dword time, bool &redraw){

   mod.last_anim_draw_time += 100;
   TickAndDrawAliveProgress(mod, time);

                              //clear data indicators after some time
   if(mod.data_draw_last_time){
      dword d = GetTickTime() - mod.data_draw_last_time;
      if(d>500){
         ConnectionDrawSocketEvent(mod, C_socket_notify::SOCKET_CONNECTED);
         mod.data_draw_last_time = 0;
      }
   }
}

//----------------------------

void C_mail_client::ConnectionDrawSocketEvent(C_mode_connection_base &mod, C_socket_notify::E_SOCKET_EVENT ev){

   if(mod.action==mod.ACT_IMAP_IDLE)
   //if(mod.acc.background_processor.state)
      return;
   if(mod.last_drawn_socket_event!=ev){
      mod.last_drawn_socket_event = ev;
      if(ev==SOCKET_DATA_RECEIVED)
         FillRect(mod.rc_data_in, 0xff00ff00);
      else
         DrawDialogBase(mod.rc, false, &mod.rc_data_in);
      if(ev==SOCKET_DATA_SENT)
         FillRect(mod.rc_data_out, 0xffff0000);
      else
         DrawDialogBase(mod.rc, false, &mod.rc_data_out);
   }
   mod.data_draw_last_time = GetTickTime();
}

//----------------------------

void C_mail_client::ConnectionDrawText(const C_mode_connection_base &mod, const Cstr_w &t, dword line){

   S_rect rc_fill = mod.rc;
   rc_fill.y += GetDialogTitleHeight() + fdb.line_spacing*line;

   rc_fill.sy = fdb.line_spacing;
   DrawDialogBase(mod.rc, false, &rc_fill);

   SetClipRect(rc_fill);
   DrawString(t, rc_fill.x+fdb.letter_size_x, rc_fill.y, UI_FONT_BIG, 0, GetColor(COL_TEXT_POPUP), -(rc_fill.sx-fdb.letter_size_x));
   ResetClipRect();
}

//----------------------------

void C_mail_client::ConnectionDrawTitle(C_mode_connection_base &mod, const Cstr_w &t){

   if(mod.action==mod.ACT_IMAP_IDLE)
   //if(mod.acc.background_processor.state)
      return;

   mod.dlg_title = t;
   {
      S_rect rc1 = mod.rc;
      rc1.sy = GetDialogTitleHeight();
      DrawDialogBase(mod.rc, true, &rc1);
      //FillRect(rc1, 0x80ff0000);
   }
   DrawDialogTitle(mod.rc, t);
}

//----------------------------

void C_mail_client::ConnectionDrawAction(C_mode_connection_base &mod, const Cstr_w &s){

   //LOG_RUN("ConnectionDrawAction");
   mod.acc.background_processor.status_text = s;
   if(mod.action==mod.ACT_IMAP_IDLE)
   //if(mod.acc.background_processor.state)
   {
      ConnectionRedrawImapIdleFolder(mod.acc);
      return;
   }
   ConnectionDrawText(mod, s, mod.LINE_ACTION);
}

//----------------------------

void C_mail_client::ConnectionDrawFolderName(C_mode_connection_base &mod, const Cstr_w &fn){

   if(mod.action==mod.ACT_IMAP_IDLE)
   //if(mod.acc.background_processor.state)
      return;

   mod.dlg_folder = fn;
   Cstr_w s; s.Format(L"%: %") <<GetText(TXT_FOLDER) <<mod.dlg_folder;
   ConnectionDrawText(mod, s, mod.LINE_FOLDER_NAME);
}

//----------------------------

void C_mail_client::TickAndDrawAliveProgress(C_mode_connection_base &mod, dword time){

   mod.alive_progress.pos += time*50/1024;
   while(int(mod.alive_progress.pos)>=mod.alive_progress.rc.sx)
      mod.alive_progress.pos -= mod.alive_progress.rc.sx;
   DrawAliveProgress(mod);
}

//----------------------------
/*
void C_mail_client::ConnectionDrawFloatDataCounters(C_mode_connection_base &mod){

   if(mod.action==mod.ACT_IMAP_IDLE)
   //if(mod.acc.background_processor.state)
      return;
   dword tm = GetTickTime();
   if((tm-mod.last_data_count_draw_time) > 500){
      mod.last_data_count_draw_time = tm;
      DrawFloatDataCounters(mod.data_cnt_bgnd);
   }
}
*/
//----------------------------

void C_mail_client::ConnectionDrawProgress(const C_mode_connection &mod){

   if(mod.action==mod.ACT_IMAP_IDLE){
      if(mod.acc.background_processor.state){
         mod.acc.background_processor.progress_total = mod.progress.total;
         mod.acc.background_processor.progress_pos = mod.progress.pos;

         if(mode->Id()==C_mode_accounts::ID)
            ConnectionRedrawImapIdleFolder(mod.acc);
      }
      return;
   }

   {
      S_rect rc = mod.progress.rc;
      rc.Expand();
      DrawDialogBase(mod.rc, false, &rc);
   }
   DrawProgress(mod.progress);
}

//----------------------------

void C_mail_client::ConnectionClearProgress(const C_mode_connection &mod){

   if(mod.action==mod.ACT_IMAP_IDLE){
      if(mod.acc.background_processor.state){
         mod.acc.background_processor.progress_total = 0;
         if(mode->Id()==C_mode_accounts::ID)
            ConnectionRedrawImapIdleFolder(mod.acc);
      }
      return;
   }
   S_rect rc = mod.progress.rc;
   rc.Expand();
   DrawDialogBase(mod.rc, false, &rc);
}

//----------------------------

void C_mail_client::DrawAliveProgress(const C_mode_connection_base &mod){

   dword pos = mod.alive_progress.pos;
   const S_rect &rc = mod.alive_progress.rc;
   //pos = 91;
   SetClipRect(rc);
   mod.alive_progress.img->Draw(rc.x+pos, rc.y);
   if(pos)
      mod.alive_progress.img->Draw(rc.x-rc.sx+pos, rc.y);
      //FillRect(S_rect(mod.alive_progress.rc.x-mod.alive_progress.rc.sx+pos, mod.alive_progress.rc.y, mod.alive_progress.rc.sx, mod.alive_progress.rc.sy), 0xff00ff00);
   AddDirtyRect(rc);
   ResetClipRect();
}

//----------------------------

void C_mail_client::DrawConnection(const C_mode_connection_base &mod){

   mod.DrawParentMode(true);

   DrawDialogBase(mod.rc, true);

   DrawDialogTitle(mod.rc, mod.dlg_title);
   ConnectionDrawText(mod, mod.acc.background_processor.status_text, mod.LINE_ACTION);
   if(mod.dlg_folder.Length()){
      Cstr_w s; s.Format(L"%: %") <<GetText(TXT_FOLDER) <<mod.dlg_folder;
      ConnectionDrawText(mod, s, mod.LINE_FOLDER_NAME);
   }

   {
      const dword
         lt = 0x80000000,
         rb = 0xc0ffffff;
      DrawOutline(mod.rc_data_in, lt, rb);
      DrawOutline(mod.rc_data_out, lt, rb);
      DrawOutline(mod.alive_progress.rc, lt, rb);
   }

   DrawSoftButtonsBar(mod, TXT_NULL, mod.rsk);
   DrawAliveProgress(mod);
   mod.data_cnt_bgnd = NULL;
   //DrawFloatDataCounters(mod.data_cnt_bgnd);
}

//----------------------------

void C_mail_client::ConnectionUpdateState(S_account &acc, S_account::E_UPDATE_STATE state, const wchar *status_text){

   //C_mode_connection_imap *mod_idle = acc.background_processor.GetMode();   
   //if(mod_idle)
      //mod_idle->dlg_action.Clear();
   acc.background_processor.status_text = status_text;
   switch(state){
   case S_account::UPDATE_ERROR:
      acc.background_processor.error_time = GetTickTime();
      break;
   case S_account::UPDATE_IDLING:
      acc.background_processor.idle_begin_time = GetTickTime();
      break;
   case S_account::UPDATE_DISCONNECTED:
      acc.background_processor.mode = NULL;
      acc.socket = NULL;
      break;
   }
   if((state==S_account::UPDATE_IDLING || state==S_account::UPDATE_ERROR || state==S_account::UPDATE_FATAL_ERROR) &&
      (alert_manager.alerts_to_play.size() || alert_manager.vibrate)){
                              //start playing sounds if no other IMAP account is being updated
      int i;
      for(i=NumAccounts(); i--; ){
         S_account &a = accounts[i];
         if(&a==&acc)
            continue;
         S_account::E_UPDATE_STATE st = a.background_processor.state;
         if(st==S_account::UPDATE_INIT || st==S_account::UPDATE_WORKING)
            break;
      }
      if(i==-1){
         StartAlertSoundsPlayback();
      }
   }

   if(acc.background_processor.state!=state){
      acc.background_processor.state = state;

      ConnectionRedrawImapIdleFolder(acc);
   }
}

//----------------------------
