#include "..\Main.h"
#include "Main_Email.h"
#include <Md5.h>

//----------------------------

#ifdef _DEBUG

//#define DEBUG_NO_POP3_UIDL         //UIDL command not supported by server
//#define DEBUG_NO_POP3_TOP         //TOP command not supported by server

#endif

//----------------------------

static bool CheckIfPop3OK(const C_socket::t_buffer &line){

   int sz = line.Size();
   if(sz >= 3 && line[0]=='+' && line[1]=='O' && line[2]=='K')
      return true;
   return false;
}

//----------------------------

void C_mail_client::AfterPop3Login(C_mode_connection_pop3 &mod){

   mod.state = mod.ST_POP3_STAT;
   mod.SocketSendCString("STAT\r\n");
}

//----------------------------

void C_mail_client::ConnectionErrorPop3(C_mode_connection_pop3 &mod, const wchar *err){

   switch(mod.state){
   case C_mode_connection_pop3::ST_POP3_RETR:
      CancelMessageRetrieval(mod, mod.body_data.message_index);
      break;
   }
   ConnectionError(mod, err);
}

//----------------------------

void C_mail_client::StartGettingNewHeadersPOP3(C_mode_connection_pop3 &mod){//, bool force_entire_msg){

                              //allow multiple pending requestst if pipelining is enabled
   dword num = 1;
   if(mod.capability&mod.CAPS_PIPELINING)
      num = 16;
   while(num-- && mod.num_hdrs_to_ask){
      BeginGetHeaderPOP3(mod);
      --mod.num_hdrs_to_ask;
   }
}

//----------------------------

void C_mail_client::C_mode_connection_pop3::FixPop3Indexes(){

   C_vector<S_message> &messages = GetMessages();
   for(int mi=messages.size(); mi--; ){
      S_message &msg = messages[mi];
      int pop3_i = -1;
      for(int hi=headers.size(); hi--; ){
         const S_message_header_base &hdr = headers[hi];
         if(msg.MatchUID(hdr, false)){
            pop3_i = hdr.pop3_server_msg_index;
            break;
         }
      }
      if(msg.pop3_server_msg_index != pop3_i){
         msg.pop3_server_msg_index = pop3_i;
         GetContainer().MakeDirty();
      }
   }
}

//----------------------------

void C_mail_client::Pop3Login(C_mode_connection_pop3 &mod, Cstr_w &err){

   S_account &acc = mod.acc;
   Cstr_c s;
   if(acc.flags&acc.ACC_USE_APOP){
                        //find APOP time_stamp
      const char *cp = mod.apop_timestamp;
                        //find beginning of <
      while(*cp && *cp!='<') ++cp;
      Cstr_w name;
      if(ReadAddress(cp, name, s) && s.Length()){
                        //use APOP login
         Cstr_c ss;
         ss.Format("<%>%") <<s <<acc.password;
         C_md5 md5;
         md5.Update((byte*)(const char*)ss, ss.Length());
         char dbuf[33];
         md5.FinishToString(dbuf);

         s.Format("APOP % %\r\n") <<acc.username <<dbuf;
         mod.state = mod.ST_POP3_APOP;
      }else{
         err = GetText(TXT_ERR_CANT_USE_APOP);
         return;
      }
   }else{
                        //use USER / PASS login
      s.Format("USER %\r\n") <<acc.username;
      mod.state = mod.ST_POP3_USER;
   }
   mod.SocketSendString(s, 0, true);
   ConnectionDrawAction(mod, GetText(TXT_PROGRESS_LOGGING_IN));
}

//----------------------------

void C_mail_client::Pop3CopyMessagesToFolders(C_mode_connection_in &mod){

   C_vector<C_message_container*> dst_flds;
   for(int i=0; i<mod.headers_to_move.size(); i++){
      const C_mode_connection_in::S_message_header_imap_move &move = mod.headers_to_move[i];
                        //find the message
      C_vector<S_message> &msgs = mod.GetMessages();
      int mi;
      for(mi=msgs.size(); mi--; ){
         if(msgs[mi].pop3_server_msg_index==move.pop3_server_msg_index)
            break;
      }
      if(mi==-1){
         assert(0);
         continue;
      }
      S_message &msg = msgs[mi];
      if(!msg.HasBody() || (msg.flags&msg.MSG_PARTIAL_DOWNLOAD)){
         assert(0);
         continue;
      }

      bool created;
      C_message_container *fld = FindOrCreateImapFolder(mod.acc, move.move_folder_name, created);
      assert(fld);
      if(created){
         fld->flags &= ~fld->FLG_TEMP;
         SaveAccounts();
         C_mode *mp = FindMode(C_mode_folders_list::ID);
         if(mp)
            mp->InitLayout();
      }
                        //check if it's loaded
      int j;
      for(j=dst_flds.size(); j--; ){
         if(dst_flds[i]==fld)
            break;
      }
      if(j==-1){
         dst_flds.push_back(fld);
         LoadMessages(*fld);
      }
                        //move message now
      msg.MoveMessageFiles(mail_data_path, mod.GetContainer(), *fld);
      S_message &new_msg = fld->messages.push_back(S_message());
      new_msg = msg;
      new_msg.flags &= ~new_msg.MSG_SERVER_SYNC;
      msg.attachments.Clear();
      msg.inline_attachments.Clear();
      msg.body_filename.Clear();
      fld->MakeDirty();
      mod.GetContainer().MakeDirty();
   }
                        //sort and unload all dest folders
   for(int i=dst_flds.size(); i--; ){
      C_message_container *fld = dst_flds[i];
      SortMessages(fld->messages, fld->is_imap);
      fld->SaveAndUnloadMessages(mail_data_path);
   }
}

//----------------------------

bool C_mail_client::ProcessLinePop3(C_mode_connection_pop3 &mod, const C_socket::t_buffer &line, Cstr_w &err){

   S_account &acc = mod.acc;
   C_vector<S_message> &messages = mod.GetMessages();

   switch(mod.state){
   case C_mode_connection_pop3::ST_WAIT_CONNECT_OK:
      if(CheckIfPop3OK(line)){
         if(!(mod.capability&mod.CAPS_IN_TLS)){
            if(acc.flags&acc.ACC_USE_APOP)
               mod.apop_timestamp = line.Begin()+3;   //skip '+OK'

            if(acc.secure_in==S_account_settings::SECURE_STARTTLS){
                                 //switch to TLS
               mod.SocketSendString("STLS\r\n");
               mod.state = mod.ST_POP3_STARTTLS;
               break;
            }
         }
         Pop3Login(mod, err);
      }else
         err = GetText(TXT_ERR_CONNECT_FAIL);
      break;

   case C_mode_connection_pop3::ST_POP3_STARTTLS:
      if(CheckIfPop3OK(line)){
         mod.state = C_mode_connection_pop3::ST_WAIT_CONNECT_OK;
         mod.capability = (dword)mod.CAPS_IN_TLS;
         mod.acc.socket->BeginSSL();
      }else{
         err = L"POP3 server doesn't support STARTTLS command. Disable StartTLS option is account settings.";
      }
      break;

   case C_mode_connection_pop3::ST_POP3_USER:
      if(CheckIfPop3OK(line)){
         Cstr_c s;
         s.Format("PASS %\r\n") <<acc.password;
         mod.SocketSendString(s, 0, true);
         mod.state = mod.ST_POP3_PASS;
      }else{
         err.Copy(line.Begin());
      }
      break;

   case C_mode_connection_pop3::ST_POP3_APOP:
      if(!CheckIfPop3OK(line)){
                              //try USER/PASS method
         Cstr_c s;
         s.Format("USER %\r\n") <<acc.username;
         mod.SocketSendString(s, 0, true);
         mod.state = mod.ST_POP3_USER;
         break;
      }
                              //flow...
   case C_mode_connection_pop3::ST_POP3_PASS:
      if(CheckIfPop3OK(line)){
         mod.SocketSendCString("CAPA\r\n"); mod.state = mod.ST_POP3_CAPA_CHECK;
      }else{
         err.Copy(line.Begin());
      }
      break;

   case C_mode_connection_pop3::ST_POP3_CAPA_CHECK:
      if(CheckIfPop3OK(line)){
         mod.state = mod.ST_POP3_CAPA;
      }else{
                              //no CAPA, continue
         AfterPop3Login(mod);
      }
      break;

   case C_mode_connection_pop3::ST_POP3_CAPA:
      if(!(line[0]=='.' && line[1]==0)){
                              //parse capability
         const char *cp = line.Begin();
         Cstr_c s;
         if(text_utils::ReadToken(cp, s, " ")){
            s.ToLower();
            if(s=="pipelining") mod.capability |= mod.CAPS_PIPELINING;
            else if(s=="stls") mod.capability |= mod.CAPS_STARTTLS;
         }
      }else
         AfterPop3Login(mod);
      break;

   case C_mode_connection_pop3::ST_POP3_QUIT:
      if(CheckIfPop3OK(line)){
                           //server deletes marked messages when QUIT completes successfully
                           // this is time to also delete ours marked-to-delete messages
         switch(mod.action){
         case C_mode_connection_pop3::ACT_UPDATE_MAILBOX:
         case C_mode_connection_pop3::ACT_UPDATE_ACCOUNTS:
            RemoveDeletedMessages(mod);
            break;
         }
      }
      mod.acc.socket = NULL;
      ConnectionDisconnect(mod);
      return false;

   case C_mode_connection_pop3::ST_POP3_STAT:
      {
         const char *cp = line.Begin() + 3;
         int total_size;
         if(CheckIfPop3OK(line) && text_utils::ScanDecimalNumber(cp, (int&)mod.num_messages) && text_utils::ScanDecimalNumber(cp, total_size)){
            Connection_AfterMailboxSelect(mod, mod.num_messages);
         }else{
            mod.acc.socket = NULL;
            cp = line.Begin();
            if(text_utils::CheckStringBegin(cp, "-err")){
               err = L"Server error message:";
               text_utils::SkipWS(cp);
               err<<'\n' <<cp;
            }else
               err = L"STAT error";
            return false;
         }
      }
      break;

   case C_mode_connection_pop3::ST_POP3_LIST_BEGIN:
      if(CheckIfPop3OK(line)){
         mod.headers.resize(mod.num_messages);
         mod.state = mod.ST_POP3_LIST;
      }else{
         mod.acc.socket = NULL;
         err = L"LIST error";
         return false;
      }
      break;

   case C_mode_connection_pop3::ST_POP3_LIST:
      if(!(line[0]=='.' && line[1]==0)){
         const char *cp = line.Begin();
         dword msg_indx, size;
         if(text_utils::ScanDecimalNumber(cp, (int&)msg_indx) && *cp++==' ' && --msg_indx<dword(mod.headers.size()) && text_utils::ScanDecimalNumber(cp, (int&)size)){
            mod.headers[msg_indx].size = size;
         }else{
            mod.acc.socket = NULL;
            err = L"LIST error";
            return false;
         }
      }else{
                              //LIST finished
         mod.SocketSendCString(
#ifdef DEBUG_NO_POP3_UIDL
            "x"
#endif
            "UIDL\r\n");
         mod.state = mod.ST_POP3_UIDL_CHECK;
      }
      break;

   case C_mode_connection_pop3::ST_POP3_UIDL_CHECK:
      if(CheckIfPop3OK(line)){
                              //wait multi-line response for all messages
         mod.state = mod.ST_POP3_UIDL;
         mod.headers.resize(mod.num_messages);
      }else{
         err = L"UIDL command not supported by server";
         /*
                              //UIDL not supported, use different technique

                              //assign indexes to headers
         for(int i=mod.headers.size(); i--; )
            mod.headers[i].pop3_server_msg_index = i;
         if(!mod.headers.size()){
            ConnectionCleanupAndDisconnect(mod);
            return false;
         }
         StartGettingNewHeadersPOP3(mod, true);
         */
      }
      break;

   case C_mode_connection_pop3::ST_POP3_UIDL:
      if(!(line[0]=='.' && line[1]==0)){
         const char *cp = line.Begin();
         dword msg_indx;
         if(text_utils::ScanDecimalNumber(cp, (int&)msg_indx) && *cp++==' ' && --msg_indx<dword(mod.headers.size())){
            S_message_header_base &hdr = mod.headers[msg_indx];
            hdr.pop3_server_msg_index = msg_indx;
            hdr.pop3_uid = cp;
         }else{
            mod.acc.socket = NULL;
            err = L"UIDL error";
            return false;
         }
      }else{
         mod.uidls_fixed = true;
         switch(mod.action){
         case C_mode_connection::ACT_GET_BODY:
         case C_mode_connection_in::ACT_GET_MSG_HEADERS:
                              // fix pop3 indexes now in existing messages, and continue in work
            mod.FixPop3Indexes();
            if(!RetrieveMessagePop3(mod, mod.body_data.message_index))
               return false;
            break;

         default:
            if(mod.retr_msg_indexes.size()){
               mod.FixPop3Indexes();
               StartRetrievingMessageMarkedBodiesPOP3(mod, NULL);
            }else
            if(!Connection_AfterUidList(mod, true))
               return false;
         }
      }
      break;

   case C_mode_connection_pop3::ST_POP3_UIDL_SINGLE:
      assert(mod.action==mod.ACT_GET_BODY || mod.action==C_mode_connection_in::ACT_GET_MSG_HEADERS);
      if(CheckIfPop3OK(line)){
                              //check if this is the UIDL that we expect
         const char *cp = line.Begin() + 3;
         int n;
         if(text_utils::ScanDecimalNumber(cp, n)){
            const S_message &msg = messages[mod.params.message_index];

            if(n==(msg.pop3_server_msg_index+1) && *cp++==' '){
               if(msg.pop3_uid==cp){
                              //ok, message index correct
                              // retrieve message now
                  if(!RetrieveMessagePop3(mod, mod.body_data.message_index))
                     return false;
                  break;
               }
            }else{
               assert(0);
            }
         }
      }
                              //invalidate message's index
      mod.GetMessages()[mod.body_data.message_index].pop3_server_msg_index = -1;
      mod.GetContainer().MakeDirty();
                              //UID incorrect, need to retrieve all UIDs
      mod.SocketSendCString("UIDL\r\n");
      mod.state = mod.ST_POP3_UIDL_CHECK;
      break;

   case C_mode_connection_pop3::ST_POP3_GET_TOP_CHECK:
      if(CheckIfPop3OK(line)){
                              //get top of message
         mod.state = mod.ST_POP3_GET_HDR_TOP;
         mod.body_data.Reset();
      }else{
         //err = L"TOP command not supported by server";
                              //TOP error, continue on next message
         if(mod.num_hdrs_to_ask){
            BeginGetHeaderPOP3(mod);
            --mod.num_hdrs_to_ask;
         }
         --mod.num_hdrs_to_get;
         mod.headers.remove_index(mod.num_hdrs_to_get);
         if(!mod.num_hdrs_to_get)
            Connection_AfterAllHeaders(mod);
         else
            mod.state = mod.ST_POP3_GET_TOP_CHECK;
      }
      break;

   case C_mode_connection_pop3::ST_POP3_GET_HDR_TOP:
      if(!(line[0]=='.' && line[1]==0)){
         if(!mod.body_data.got_header){
            const char *cp = line.Begin();
            if(*cp){
               mod.body_data.curr_hdr.insert(mod.body_data.curr_hdr.end(), cp, cp+StrLen(cp));
               mod.body_data.curr_hdr.push_back('\n');
            }else{
                              //some servers (aol) sends complete message for TOP x 0, so here's workaround to ignore data after header
               mod.body_data.got_header = true;
            }
         }
      }else{
         S_message_header_base &hdr = mod.headers[mod.num_hdrs_to_get-1];
         Connection_AfterGotHeader(mod, hdr, mod.body_data.curr_hdr.begin(), mod.body_data.curr_hdr.size());
         assert(mod.num_hdrs_to_get==mod.headers.size()-1);
         mod.headers.remove_index(mod.num_hdrs_to_get);
         if(!mod.num_hdrs_to_get)
            Connection_AfterAllHeaders(mod);
         else
            mod.state = mod.ST_POP3_GET_TOP_CHECK;
      }
      break;

   case C_mode_connection_pop3::ST_POP3_DELE:
      if(CheckIfPop3OK(line)){
                              //clear dirty flag
         int index = mod.msgs_to_delete[mod.delete_message_index-1];
         for(int i=messages.size(); i--; ){
            S_message &msg = messages[i];
            if(msg.pop3_server_msg_index==index){
               assert(msg.flags&msg.MSG_DELETED_DIRTY);
               msg.flags &= ~msg.MSG_DELETED_DIRTY;
               mod.GetContainer().MakeDirty();
               break;
            }
         }
                              //delete next, or continue in work
         if(mod.delete_message_index < dword(mod.msgs_to_delete.size())){
            Pop3StartDeleteNextMessage(mod);
         }else{
                              //done, disconnect
            ConnectionDisconnect(mod);
            return false;
         }
      }else{
         mod.msgs_to_delete.clear();
         mod.acc.socket = NULL;
         err = L"DELE error";
         return false;
      }
      break;

   case C_mode_connection_pop3::ST_POP3_RETR_CHECK:
      if(CheckIfPop3OK(line)){
                              //continue with multi-line
         mod.state = mod.ST_POP3_RETR;
         mod.body_data.Reset();
         if(mod.action!=mod.ACT_GET_MSG_HEADERS){
            dword partial_kb = 0;
            if(mod.retr_msg_indexes.size()){
                                 //assign downloaded message index now
               mod.body_data.message_index = mod.retr_msg_indexes[mod.get_bodies_index];
               partial_kb = mod.headers_to_download.size() ? mod.headers_to_download[mod.get_bodies_index].partial_download_kb : 0;

               Cstr_w s;
               s<<GetText(TXT_PROGRESS_GETTING_BODY) <<L' ' <<++mod.get_bodies_index <<L'/' <<mod.num_get_bodies;
               ConnectionDrawAction(mod, s);
            }
                              //starting to download body now, prepare downloader
            S_message &msg = mod.GetMessages()[mod.body_data.message_index];
            if(!partial_kb)
               partial_kb = (mod.acc.max_kb_to_retrieve && !mod.force_no_partial_download && (mod.acc.max_kb_to_retrieve*1024 < (msg.size*8/10))) ? mod.acc.max_kb_to_retrieve : 0;
            mod.body_data.partial_body = (partial_kb!=0);
            mod.body_data.start_progress_pos = mod.progress.pos;

            //mod.body_data.retrieved_body.reserve(Min(65536, (int)msg.size));
            mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
         }
      }else{
                              //error retrieving message
         ConnectionDisconnect(mod);
         return false;
      }
      break;

   case C_mode_connection_pop3::ST_POP3_RETR:
      {
         S_message &msg = mod.GetMessages()[mod.body_data.message_index];
         bool ret;
         if(mod.action==mod.ACT_GET_MSG_HEADERS){
            const char *cp = line.Begin();
            if(cp[0]=='.' && cp[1]==0){
               mod.body_data.curr_hdr.push_back(0);

               mod.AddRef();
               C_mode_mailbox *mod_mbox;
               if(mod.GetParent()->Id()==C_mode_read_mail_base::ID)
                  mod_mbox = &((C_mode_read_mail_base&)*mod.GetParent()).GetMailbox();
               else
                  mod_mbox = &(C_mode_mailbox&)*mod.GetParent();
               ConnectionDisconnect(mod);
               Mailbox_ShowDetails(*mod_mbox, mod.body_data.curr_hdr.begin());

               mod.Release();
               ret = false;
            }else{
               mod.body_data.curr_hdr.insert(mod.body_data.curr_hdr.end(), cp, cp+StrLen(cp));
               mod.body_data.curr_hdr.push_back('\n');
               ret = true;
            }
         }else{
            ret = AddRetrievedMessageLine(mod, mod.body_data, msg, line, err);
            if(!ret){
               FinishBodyRetrieval(mod, mod.body_data, msg);
               if(mod.retr_msg_indexes.size()){
                                 //fix progress indicator
                  mod.progress.pos = mod.body_data.start_progress_pos + msg.size;

                  if(mod.get_bodies_index<mod.num_get_bodies){
                                 //waiting for more bodies to come
                     if(mod.get_body_ask_index<dword(mod.retr_msg_indexes.size())){
                                 //ask next body
                        RetrieveMessagePop3(mod, mod.retr_msg_indexes[mod.get_body_ask_index],
                           mod.headers_to_download.size() ? mod.headers_to_download[mod.get_bodies_index].partial_download_kb : 0);
                        ++mod.get_body_ask_index;
                     }else
                        mod.state = mod.ST_POP3_RETR_CHECK;
                     break;
                  }
               }
               ConnectionClearProgress(mod);
               ConnectionCleanupAndDisconnect(mod);
            }
         }
         return ret;
      }

   default:
      assert(0);
   }
   return true;
}

//----------------------------

void C_mail_client::GetMessageListPOP3(C_mode_connection_pop3 &mod, int num_msgs){

   mod.headers.resize(num_msgs);
                              //start with list, for getting message sizes
   mod.SocketSendCString("LIST\r\n");
   mod.state = mod.ST_POP3_LIST_BEGIN;
}

//----------------------------

void C_mail_client::BeginGetHeaderPOP3(C_mode_connection_pop3 &mod){

   const S_message_header_base &hdr = mod.headers[mod.num_hdrs_to_ask-1];
   int indx = hdr.pop3_server_msg_index;
   Cstr_c s;
#ifdef DEBUG_NO_POP3_TOP
   s<<"x";
#endif
   s<<"TOP " <<(indx+1) <<" 0\r\n";
   mod.SocketSendString(s);
   mod.state = mod.ST_POP3_GET_TOP_CHECK;
}

//----------------------------

void C_mail_client::StartRetrievingMessageMarkedBodiesPOP3(C_mode_connection_pop3 &mod, const C_vector<dword> *msg_indexes){

   if(msg_indexes)
      mod.retr_msg_indexes = *msg_indexes;
   mod.get_body_ask_index = 0;

   if(!mod.uidls_fixed){
      mod.SocketSendCString("UIDL\r\n");
      mod.state = mod.ST_POP3_UIDL_CHECK;
   }else{
                              //count total size (for progress) now
      mod.progress.total = 0;
      C_vector<S_message> &messages = mod.GetMessages();
      for(int i=mod.retr_msg_indexes.size(); i--; )
         mod.progress.total += messages[mod.retr_msg_indexes[i]].size;

                              //allow multiple pending requestst if pipelining is enabled
      dword num = 1;
      if(mod.capability&mod.CAPS_PIPELINING)
         num = 8;
      while(num-- && mod.get_body_ask_index<dword(mod.retr_msg_indexes.size())){
         RetrieveMessagePop3(mod, mod.retr_msg_indexes[mod.get_body_ask_index],
            mod.headers_to_download.size() ? mod.headers_to_download[mod.get_bodies_index].partial_download_kb : 0);
         ++mod.get_body_ask_index;
      }
   }
}

//----------------------------

void C_mail_client::BeginRetrieveMessagePop3(C_mode_connection_pop3 &mod, dword msg_index){

   const S_message &msg = mod.GetMessages()[msg_index];
   if(!mod.uidls_fixed){
      mod.body_data.message_index = msg_index;
                              //need to determine UIDLs first
      if(msg.pop3_server_msg_index!=-1 && (mod.action==mod.ACT_GET_BODY || mod.action==mod.ACT_GET_MSG_HEADERS)){
                                 //single UIDL
         Cstr_c s;
         s.Format("UIDL %\r\n") <<(msg.pop3_server_msg_index+1);
         mod.SocketSendString(s);
         mod.state = mod.ST_POP3_UIDL_SINGLE;
      }else{
         mod.SocketSendCString("UIDL\r\n");
         mod.state = mod.ST_POP3_UIDL_CHECK;
      }
   }else{
      RetrieveMessagePop3(mod, msg_index);
   }
}

//----------------------------

bool C_mail_client::RetrieveMessagePop3(C_mode_connection_pop3 &mod, dword msg_index, dword force_partial_kb){

   const S_message &msg = mod.GetMessages()[msg_index];
   if(mod.action==mod.ACT_GET_MSG_HEADERS){
      force_partial_kb = 0;
   }else{
      if(!force_partial_kb){
         if(mod.acc.max_kb_to_retrieve && !mod.force_no_partial_download && (mod.acc.max_kb_to_retrieve*1024 < (msg.size*8/10)))
            force_partial_kb = mod.acc.max_kb_to_retrieve;
      }
   }
   int indx = msg.pop3_server_msg_index + 1;
   if(!indx){
                              //no such message on server, disconnect
      ConnectionError(mod, L"Message doesn't exist on server");
      return false;
   }
   if(mod.action==mod.ACT_GET_BODY){
      ConnectionDrawAction(mod, GetText(TXT_PROGRESS_GETTING_BODY));
   }
   Cstr_c s;
   if(force_partial_kb){
      int lines = force_partial_kb*1024 / 70;
      s.Format("TOP % %\r\n") <<indx <<lines;
   }else if(mod.action==mod.ACT_GET_MSG_HEADERS){
      s.Format("TOP % 0\r\n") <<indx;
   }else
   if(mod.is_gmail){
      s.Format("TOP % 100000\r\n") <<indx;
   }else
      s.Format("RETR %\r\n") <<indx;
   mod.SocketSendString(s);
   mod.state = mod.ST_POP3_RETR_CHECK;
   return true;
}

//----------------------------

void C_mail_client::StartDeletingMessagesPop3(C_mode_connection_pop3 &mod){

   if(!mod.uidls_fixed){
      assert(0);
      mod.msgs_to_delete.clear();
      return;
   }
   mod.delete_message_index = 0;
   Pop3StartDeleteNextMessage(mod);
}

//----------------------------

void C_mail_client::Pop3StartDeleteNextMessage(C_mode_connection_pop3 &mod){

   assert(mod.delete_message_index < dword(mod.msgs_to_delete.size()));

   int msg_index = mod.msgs_to_delete[mod.delete_message_index++];

   Cstr_c cmd;
   cmd.Format("DELE %\r\n") <<(msg_index+1);
   mod.SocketSendString(cmd);
   mod.state = mod.ST_POP3_DELE;
   mod.need_expunge = true;

   Cstr_w msg;
   msg<<GetText(TXT_PROGRESS_DELETING) <<L' ' <<mod.delete_message_index <<L'/' <<mod.msgs_to_delete.size();
   ConnectionDrawAction(mod, msg);
}

//----------------------------

void C_mail_client::ConnectionExpungePOP3(C_mode_connection_pop3 &mod){

   mod.SocketSendCString("QUIT\r\n");
   mod.state = mod.ST_POP3_QUIT;
                              //can't keep socket
   //mod.acc.socket = NULL;
}

//----------------------------

void C_mail_client::ConnectionProcessInputPop3(C_mode_connection_pop3 &mod, S_user_input &ui, bool &redraw){

#ifdef USE_MOUSE
   ProcessMouseInSoftButtons(ui, redraw);
#endif
   if(ui.key==K_RIGHT_SOFT || ui.key==K_BACK || ui.key==K_ESC){
      switch(mod.state){
      case C_mode_connection_pop3::ST_POP3_QUIT:
         return;
      case C_mode_connection_pop3::ST_POP3_RETR:
         CancelMessageRetrieval(mod, mod.body_data.message_index);
         break;
      }
      mod.cancel_request = true;
      mod.rsk = TXT_NULL;
      S_account &acc = mod.acc;
      ConnectionDisconnect(mod);
      acc.socket = NULL;
   }
}

//----------------------------
