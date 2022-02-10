#include "..\Main.h"
#include "Main_Email.h"
#include <TimeDate.h>

//----------------------------
#ifdef _DEBUG
//#define DEBUG_NO_IDLE_CAPS
//#define DEBUG_NO_SEARCH
#endif

#define ALWAYS_IDLE_CAPS      //always consider that server supports IDLE

//----------------------------
void encr_segment_01a(){}

//----------------------------

static int CompareDword(const void*v0, const void*v1, void*){
   dword d0 = *(dword*)v0, d1 = *(dword*)v1;
   return (d0<d1) ? -1 : 1;
}

//----------------------------
// Make smart IMAP uid sequence string, consuming minimal size.
static Cstr_c MakeImapSequenceString(const C_vector<dword> &_uids){

   const bool sort = true;
   int count = _uids.size();
   C_vector<dword> uids;
   uids.insert(uids.end(), _uids.begin(), _uids.begin()+count);
   if(sort)
      QuickSort(uids.begin(), uids.size(), 4, &CompareDword);
   Cstr_c s;
   uids.push_back(0xffffffff);

   dword seg_s = 0, seg_e = 0;
   for(int i=0; i<uids.size(); i++){
      dword uid = uids[i];
      if(!i)
         seg_s = seg_e = uid;
      else{
         if(seg_e+1==uid)
            ++seg_e;
         else{
            if(s.Length())
               s<<',';
            s<<seg_s;
            if(seg_s!=seg_e)
               s<<':' <<seg_e;
            seg_s = seg_e = uid;
         }
      }
   }
   return s;
}

//----------------------------

void C_mail_client::SendCompressedData(C_socket *socket, const void *data, dword len, z_stream &zs, int override_timeout){

   C_buffer<byte> buf;
   buf.Resize(len*2);
   zs.next_in = (byte*)data;
   zs.avail_in = len;
   zs.next_out = buf.Begin();
   zs.avail_out = buf.Size();
   int err = deflate(&zs, Z_SYNC_FLUSH);
   assert(!err);
   dword num_out = buf.Size()-zs.avail_out;
   socket->SendData(buf.Begin(), num_out, override_timeout);
}

//----------------------------

void C_mail_client::SendImapCommand(C_mode_connection_imap &mod, const char *str, C_mode_connection_imap::C_command *cmd, int override_timeout){

   ++mod.curr_tag_id;
   mod.commands.push_back(cmd);
   cmd->tag_id = mod.curr_tag_id;
   cmd->Release();
   mod.state = cmd->state; //!!

   Cstr_c s;
   s.Format("% %\r\n") <<mod.curr_tag_id <<str;
   if(mod.capability&mod.CAPS_IN_COMPRESSION){
      SendCompressedData(mod.acc.socket, (const char*)s, s.Length(), mod.compress_out);
   }else
      mod.SocketSendCString(s, override_timeout, (cmd->state==mod.ST_IMAP_LOGIN));
}

//----------------------------

void C_mail_client::ConnectionImapSelectFolder(C_mode_connection_imap &mod){

   if(mod.action==mod.ACT_UPDATE_ACCOUNTS || mod.action==mod.ACT_UPDATE_IMAP_FOLDERS){
      ConnectionDrawAction(mod, GetText(TXT_UPDATE_MAILBOX));
      //ConnectionDrawFolderName(mod, mod.acc.GetFullFolderName(*mod.folder));
      ConnectionDrawFolderName(mod, mod.folder->folder_name.FromUtf8());
   }
   Cstr_c cmd;
   cmd<<"SELECT " <<mod.acc.GetImapEncodedName(*mod.folder);
   SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command_select);
}

//----------------------------

void C_mail_client::AfterImapLogin(C_mode_connection_imap &mod){

   if(mod.commands.size()==1 && mod.commands.front()->state==mod.ST_IMAP_IDLE){
                              //terminate IDLE first
      ImapIdleSendDone(mod);
      return;
   }

   //mod.acc.socket = mod.socket;
   mod.acc.imap_capability = mod.capability;
   switch(mod.action){
   case C_mode_connection_in::ACT_REFRESH_IMAP_FOLDERS:
      {
         ConnectionDrawAction(mod, GetText(TXT_RETRIEVING_FOLDERS));
         Cstr_c cmd;
         Cstr_w tmp;
         tmp<<mod.acc.imap_root_path <<"*";
         cmd<<"LIST \"\" ";
         cmd<<EncodeImapFolderName(tmp);
         SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command_list);
      }
      break;

   case C_mode_connection_in::ACT_DELETE_IMAP_FOLDER:
      {
         Cstr_w s;
         s<<GetText(TXT_PROGRESS_DELETING)
            <<L": \'"
            <<mod.folder->folder_name.FromUtf8()
            <<'\'';
         ConnectionDrawAction(mod, s);
         Cstr_c cmd; cmd<<"DELETE " <<mod.acc.GetImapEncodedName(*mod.folder);
         SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_DELETE_FOLDER));
      }
      break;

   case C_mode_connection_in::ACT_RENAME_IMAP_FOLDER:
      {
         ConnectionDrawAction(mod, GetText(TXT_RENAMING_IMAP_FOLDER));
                                 //if names equals, just do nothing
         Cstr_c cmd;
         if(mod.acc.GetFullFolderName(*mod.folder) == mod.params.text){
            cmd = "NOOP";
         }else{
            cmd<<"RENAME " <<mod.acc.GetImapEncodedName(*mod.folder) <<' ' <<EncodeImapFolderName(mod.params.text);
         }
         SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_RENAME_FOLDER));
      }
      break;

   case C_mode_connection_in::ACT_CREATE_IMAP_FOLDER:
      {
         ConnectionDrawAction(mod, GetText(TXT_CREATING_IMAP_FOLDER));
         Cstr_c cmd; cmd<<"CREATE " <<EncodeImapFolderName(mod.params.text);
         SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command_create);
      }
      break;

   case C_mode_connection_in::ACT_UPLOAD_SENT:
   case C_mode_connection_in::ACT_UPLOAD_DRAFTS:
      StartUploadingMessageToImap((C_mode_connection_imap_upload&)mod);
      break;

   case C_mode_connection_in::ACT_DOWNLOAD_IMAP_ATTACHMENT:
   case C_mode_connection_in::ACT_DOWNLOAD_IMAP_ATTACHMENT_AND_OPEN:
   case C_mode_connection_in::ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL:
   case C_mode_connection_in::ACT_IMAP_MOVE_MESSAGES:
   case C_mode_connection_in::ACT_GET_BODY:
   case C_mode_connection_in::ACT_GET_MSG_HEADERS:
   case C_mode_connection_in::ACT_GET_MARKED_BODIES:
   case C_mode_connection_in::ACT_IMAP_PURGE:
      if(mod.acc.selected_folder==mod.folder){
                              //folder already selected, go shorter way
         Connection_AfterMailboxSelect(mod, 0);
         break;
      }
                              //flow...
   case C_mode_connection_in::ACT_UPDATE_MAILBOX:
   case C_mode_connection_in::ACT_UPDATE_ACCOUNTS:
   case C_mode_connection_in::ACT_UPDATE_IMAP_FOLDERS:
   case C_mode_connection_in::ACT_IMAP_IDLE:
      ConnectionImapSelectFolder(mod);
      break;

   default:
      assert(0);
   }
}

//----------------------------

void C_mail_client::ImapProcessCapability(C_mode_connection_imap &mod, const char *cp){

   mod.capability &= mod.CAPS_IN_TLS;
   Cstr_c attr, val;
   while(text_utils::GetNextAtrrValuePair(cp, attr, val, true)){
      if(attr=="auth"){
         if(val=="login") mod.capability |= mod.CAPS_AUTH_LOGIN;
         else if(val=="cram-md5") mod.capability |= mod.CAPS_AUTH_MD5;
         else if(val=="plain") mod.capability |= mod.CAPS_AUTH_PLAIN;
#ifdef _DEBUG
         else
         if(val=="anonymous");
         else
         if(val=="scram-md5");
         else
         if(val=="ntlm" || val=="otp");
         else{
            //assert(0);
         }
#endif
#ifndef DEBUG_NO_IDLE_CAPS
      }else if(attr=="idle"){ mod.capability |= mod.CAPS_IDLE;
#endif
      }else if(attr=="uidplus") mod.capability |= mod.CAPS_UIDPLUS;
      else if(attr=="compress"){
         if(val=="deflate")
            mod.capability |= mod.CAPS_COMPRESS;
      }else if(attr=="starttls") mod.capability |= mod.CAPS_STARTTLS;
      else if(attr=="id") mod.capability |= mod.CAPS_ID;
      /*
#ifdef _DEBUG
      else if(attr=="imap4rev1"){
      }else if(attr=="imap4"){
      }else if(attr=="namespace"){
      }else if(attr=="mailbox-referrals"){
      }else if(attr=="binary"){
      }else if(attr=="unselect"){
      }else if(attr=="scan"){
      }else if(attr=="sort"){
      }else if(attr=="thread"){
      }else if(attr=="multiappend"){
      }else if(attr=="sasl-ir"){
      }else if(attr=="login-referrals"){
      }else if(attr=="starttls"){
      }else if(attr=="literal+"){
      }else
         assert(0);
#endif
      */
#ifdef ALWAYS_IDLE_CAPS
      mod.capability |= mod.CAPS_IDLE;
#endif
   }
}

//----------------------------

void C_mail_client::GetMessageListIMAP(C_mode_connection_imap &mod, int num_msgs){

   mod.headers.reserve(num_msgs);
   SendImapCommand(mod, "FETCH 1:* (UID flags)", new(true) C_mode_connection_imap::C_command_get_uids);
   mod.msg_seq_map.Clear();
   mod.msg_seq_map.map.reserve(num_msgs);
}

//----------------------------

static Cstr_c MakeSearchCommand(dword day_limit){

   S_date_time_x dt;
   dt.GetCurrent();
   dt.SetFromSeconds(dt.GetSeconds() - day_limit*24*60*60);
   Cstr_c s;
   s.Format("SEARCH SINCE %-%-%") <<(dt.day+1) <<dt.GetMonthName() <<dt.year;
#ifdef DEBUG_NO_SEARCH
   s = "COPY 1 XXX";   
#endif
   return s;
}

//----------------------------

void C_mail_client::SearchLastMessagesIMAP(C_mode_connection_imap &mod){

   Cstr_c s = MakeSearchCommand(mod.acc.imap_last_x_days_limit);
   SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command_search_since_date(false));
   mod.msg_seq_map.Clear();
}

//----------------------------

struct S_uid_seq{
   dword uid;
   bool is_new;
   S_uid_seq(){}
   S_uid_seq(dword u, bool n): uid(u), is_new(n){}
   static int Compare(const void *_u0, const void *_u1, void*){
      S_uid_seq *u0 = (S_uid_seq*)_u0, *u1 = (S_uid_seq*)_u1;
      return (u0->uid < u1->uid) ? -1 : 1;
   }
};

//----------------------------

static Cstr_c MakeNewHeadersCommand(const Cstr_c &range, bool use_uid = true, bool fetch_flags = false){

   Cstr_c s;
   if(use_uid)
      s<<"UID ";
   s<<"FETCH ";
   s<<range <<" (";
   s<<
      "UID "
      "bodystructure "
      "body.peek[header] "
      "rfc822.size";
   if(fetch_flags)
      s<<" FLAGS";
   s<<')';
   return s;
}

//----------------------------

void C_mail_client::StartGettingNewHeadersIMAP(C_mode_connection_imap &mod, bool force_optimized_uid_range){

                              //reverse headers for faster operation
   int num_hdrs = mod.headers.size();
   for(int i=num_hdrs/2; i--; ){
      Swap(mod.headers[i], mod.headers[num_hdrs-1-i]);
   }
   const C_vector<S_message> &msgs = mod.GetMessages();

   C_mode_connection_imap::C_command_get_hdr *cmd = new(true) C_mode_connection_imap::C_command_get_hdr;

   Cstr_c range;
   if(!mod.acc.imap_last_x_days_limit || force_optimized_uid_range){
                              //build optimized uid range
      C_vector<S_uid_seq> uids;
      uids.reserve(num_hdrs + msgs.size() + 1);
      for(int i=0; i<num_hdrs; i++)
         uids.push_back(S_uid_seq(mod.headers[i].imap_uid, true));
      for(int i=0; i<msgs.size(); i++)
         uids.push_back(S_uid_seq(msgs[i].imap_uid, false));
      QuickSort(uids.begin(), uids.size(), sizeof(S_uid_seq), &S_uid_seq::Compare);
      uids.push_back(S_uid_seq(0, false));

      dword seg_start = 0, seg_end = 0;
      bool seg_valid = false;
      for(int i=0; i<uids.size(); i++){
         const S_uid_seq &uid = uids[i];
         if(uid.is_new){
            if(!seg_valid){
               seg_valid = true;
               seg_start = uid.uid;
            }
            seg_end = uid.uid;
         }else
         if(seg_valid){
            if(range.Length())
               range<<',';
            range<<seg_start;
            if(seg_start!=seg_end)
               range<<':' <<seg_end;
            seg_valid = false;
         }
      }
      cmd->optimized_uid_range = true;
   }else{
      C_vector<dword> uids;
      for(int i=0; i<num_hdrs; i++)
         uids.push_back(mod.headers[i].imap_uid);
      range = MakeImapSequenceString(uids);
#if defined _DEBUG && 0
      range<<'!';
#endif
   }

   Cstr_c s = MakeNewHeadersCommand(range, true, false);
   cmd->sync_with_headers = true;
   SendImapCommand(mod, s, cmd);

   mod.num_hdrs_to_ask = 0;
}

//----------------------------

bool C_mail_client::C_mode_connection_imap::C_command_list_base::ParseFlags(const char *&cp){

   text_utils::SkipWS(cp);
   flags = 0;
   if(*cp!='(')
      return false;
   ++cp;
               //read flags
   while(*cp){
      text_utils::SkipWS(cp);
      if(*cp==')'){
         ++cp;
         break;
      }
      Cstr_c flg;
      if(!text_utils::ReadToken(cp, flg, " \t\n()"))
         break;
      flg.ToLower();
      if(flg=="\\noselect") flags |= FLAG_NOSELECT;
      else if(flg=="\\noinferiors") flags |= FLAG_NOINFERIORS;
   }
   return true;
}

//----------------------------

bool C_mail_client::ImapProcessList(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_list *cmd_lst, const char *cp){

   if(!cmd_lst->ParseFlags(cp))
      return false;
   text_utils::SkipWS(cp);
               //read delimiter
   Cstr_c delimiter;
   if(!(text_utils::CheckStringBegin(cp, "nil") || text_utils::ReadWord(cp, delimiter, " ()")))
      return false;
   if(!cmd_lst->delimiter && delimiter.Length()==1)
      cmd_lst->delimiter = delimiter[0];
   text_utils::SkipWS(cp);
   if(*cp=='{' && (++cp, text_utils::ScanDecimalNumber(cp, mod.multiline.string_size)) && *cp=='}' && !cp[1]){
      mod.multiline.cmd = cmd_lst;
   }else{
      cmd_lst->AddFolder(cp, true);
   }
   return true;
}

//----------------------------

bool C_mail_client::ImapProcessMultiLine(C_mode_connection_imap &mod, const C_socket::t_buffer &line, Cstr_w &err){

   C_mode_connection_imap::C_command *cmd = mod.multiline.cmd;

   const char *cp = line.Begin();
   if(mod.multiline.string_size==-1){
                     //last line of multi-line
      mod.multiline.string_size = 0;
      mod.multiline.cmd = NULL;
      switch(cmd->state){
      case C_mode_connection_imap::ST_IMAP_LIST:
         break;
      default:
         ParseImapFetch(mod, cmd, cp);
      }
   }else{
      int line_sz = line.Size();
      mod.multiline.string_size -= line_sz;

      switch(cmd->state){
      case C_mode_connection_imap::ST_IMAP_GET_HDR:
         {
            C_mode_connection_imap::C_command_get_hdr *cmd_get_hdr = (C_mode_connection_imap::C_command_get_hdr*)cmd;
            S_bodystructure &bs = cmd_get_hdr->bodystructure;
            if(!bs.src_open_braces_count){
                              //we're getting multi-line header
               cmd_get_hdr->hdr_data.insert(cmd_get_hdr->hdr_data.end(), cp, cp+StrLen(cp));
               cmd_get_hdr->hdr_data.push_back('\n');
               break;
            }
            Cstr_c str; str.Allocate(cp, line_sz+mod.multiline.string_size);
            const char *cp1 = str;
            bs.src_open_braces_count = S_bodystructure::GetTextInBraces(cp1, bs.src_data, bs.src_open_braces_count);
         }
         break;

      case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
         {
            C_mode_connection_imap::C_command_get_attachment_body *cmd_get = (C_mode_connection_imap::C_command_get_attachment_body*)cmd;
            const C_socket::t_buffer *l = &line;
            C_socket::t_buffer tmp;
            if(mod.multiline.string_size < 0){
               int eol = line_sz+mod.multiline.string_size;
               tmp.Assign(l->Begin(), l->Begin()+eol+1);
               tmp[eol] = 0;
               l = &tmp;
            }
            mod.progress.pos += l->Size() + 1;
            if(!mod.progress_drawn){
               mod.progress_drawn = true;
               ConnectionDrawProgress(mod);
               //ConnectionDrawFloatDataCounters(mod);
            }
            const char *cp1 = l->Begin();
            AddRetrievedAttachmentData(mod, *cmd_get, cp1, StrLen(cp1), cmd_get->retrieved_header.content_encoding, err);
         }
         break;

      case C_mode_connection_imap::ST_IMAP_GET_MSG_HEADERS:
         {
            C_mode_connection_imap::C_command_get_msg_headers *cmd_hdrs = (C_mode_connection_imap::C_command_get_msg_headers*)cmd;
            cmd_hdrs->full_hdr.insert(cmd_hdrs->full_hdr.end(), cp, cp+StrLen(cp));
            cmd_hdrs->full_hdr.push_back('\n');
         }
         break;

      case C_mode_connection_imap::ST_IMAP_GET_BODY:
         {
            C_mode_connection_imap::C_command_get_body *cmd_body = (C_mode_connection_imap::C_command_get_body*)cmd;

            const C_socket::t_buffer *l = &line;
            C_socket::t_buffer tmp;
            if(mod.multiline.string_size < 0){
               int eol = line_sz+mod.multiline.string_size;
               tmp.Assign(l->Begin(), l->Begin()+eol+1);
               tmp[eol] = 0;
               l = &tmp;
            }
            if(cmd_body->phase==cmd_body->PHASE_GET_HDRS){
               const char *cp1 = l->Begin();
               cmd_body->curr_hdr.insert(cmd_body->curr_hdr.end(), cp1, cp1+StrLen(cp1));
               cmd_body->curr_hdr.push_back('\n');
            }else
               AddRetrievedMessageLine(mod, *cmd_body, cmd_body->temp_msg, *l, err);
         }
         break;

      case C_mode_connection_imap::ST_IMAP_LIST:
         {
            C_mode_connection_imap::C_command_list *cmd_lst = (C_mode_connection_imap::C_command_list*)cmd;
            assert(mod.multiline.string_size==-2);
            cmd_lst->AddFolder(cp, false);
         }
         break;

      default:
         assert(0);
      }
      if(mod.multiline.string_size <= 0){
                     //got all data
         if(mod.multiline.string_size < 0){
            cp += line_sz + mod.multiline.string_size;
            mod.multiline.string_size = 0;
            cmd->AddRef();
            mod.multiline.cmd = NULL;
            switch(cmd->state){
            case C_mode_connection_imap::ST_IMAP_LIST:
               break;
            default:
               ParseImapFetch(mod, cmd, cp);
            }
            cmd->Release();
         }else
            mod.multiline.string_size = -1;
      }
   }
   return true;
}

//----------------------------

static bool ReadFetchAttribute(const char *&cp, Cstr_c &attr){

   int open_brace_count = 0;
   while(*cp){
      char c = *cp++;
      switch(c){
      case '[':
      case '(':
         ++open_brace_count;
         break;
      case ']':
      case ')':
         assert(open_brace_count);
         --open_brace_count;
         break;
      default:
         if(!open_brace_count && c==' ')
            return true;
      }
      attr<<c;
   }
   //if(!ReadToken(cp, attr, " \t\n()"))
   return true;
}

//----------------------------

bool C_mail_client::ParseImapFetch(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd, const char *cp){

   C_mode_connection_imap::E_STATE state = cmd->state;
   while(*cp){
      Cstr_c attr, val;
      bool val_read_ok;

      S_bodystructure *bs = cmd->GetBodyStruct();
      if(bs && bs->src_open_braces_count){
                              //getting contents of body (multiline) struct
         attr = "body";
         val_read_ok = false;
      }else{
         text_utils::SkipWS(cp);
         if(*cp==')')
            break;
         if(!ReadFetchAttribute(cp, attr))
            break;
         attr.ToLower();
         text_utils::SkipWS(cp);
         val_read_ok = text_utils::ReadWord(cp, val, " ()\t\n");
      }

      if(attr=="uid"){
                              //expected only while we get UID list
         assert(val_read_ok);
         dword uid;
         if(!(val>>uid))
            assert(0);

         switch(cmd->state){
         case C_mode_connection_imap::ST_IMAP_GET_UIDS:
            {
               mod.headers.back().imap_uid = uid;
               C_mode_connection_imap::C_command_get_uids *cmd_uids = (C_mode_connection_imap::C_command_get_uids*)cmd;
               mod.msg_seq_map.Assign(cmd_uids->seq_num, uid);
            }
            break;

         case C_mode_connection_imap::ST_IMAP_GET_HDR:
            {
               C_mode_connection_imap::C_command_get_hdr *cmd_get_hdr = (C_mode_connection_imap::C_command_get_hdr*)cmd;
               mod.msg_seq_map.Assign(cmd_get_hdr->seq_num, uid);
               cmd_get_hdr->got_uid = true;
            }
                              //flow...
         case C_mode_connection_imap::ST_IMAP_GET_BODY:
            {
                              //save uid for message identification when fetch completes
               C_mode_connection_imap::C_command_uid *cmd_get_uid = (C_mode_connection_imap::C_command_uid*)cmd;
               cmd_get_uid->uid = uid;
            }
            break;

         case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
            {
               C_mode_connection_imap::C_command_get_attachment_body *cmd_body = (C_mode_connection_imap::C_command_get_attachment_body*)cmd;
               cmd_body->uid = uid;
            }
            break;
         }
      }else
      if(attr=="rfc822.size"){
         assert(val_read_ok);
         if(cmd->state==mod.ST_IMAP_GET_HDR){
            C_mode_connection_imap::C_command_get_hdr *cmd_get_hdr = (C_mode_connection_imap::C_command_get_hdr*)cmd;
            text_utils::ScanInt(val, (int&)cmd_get_hdr->hdr_size);
         }
      }else
      if(attr=="body[header]" || attr=="body[]" || attr=="body[]<0>"){
         assert(val_read_ok);
         const char *cp1 = val;
         if(*cp1=='{' && (++cp1, text_utils::ScanDecimalNumber(cp1, mod.multiline.string_size)) && *cp1=='}' && !cp1[1]){
                              //switch to mode receiving next lines
            mod.multiline.cmd = cmd;
            if(!mod.multiline.string_size)
               --mod.multiline.string_size;
            break;
         }else
         if(!*cp1 || text_utils::CheckStringBegin(cp1, "nil")){
                              //empty header
         }else{
            assert(0);
            return false;
         }
      }else
      if(attr=="flags"){
                              //expected only while we get UID list
         if(!val_read_ok){
            if(*cp=='(')
               S_bodystructure::GetTextInBraces(cp, val);
            else
               break;
         }
         dword flags = 0;
                        //parse flags
         const char *cp1 = val;
         while(*cp1){
            text_utils::SkipWS(cp1);
            Cstr_c flg;
            if(!text_utils::ReadToken(cp1, flg, " \t\n()"))
               break;
            flg.ToLower();
            if(flg=="\\seen") flags |= S_message_header_base::MSG_READ;
            else if(flg=="\\draft") flags |= S_message_header_base::MSG_DRAFT;
            else if(flg=="\\answered") flags |= S_message_header_base::MSG_REPLIED;
            else if(flg=="$forwarded") flags |= S_message_header_base::MSG_FORWARDED;
            else if(flg=="\\flagged") flags |= S_message_header_base::MSG_FLAGGED;
            //else if(flg=="\\recent") flags |= S_message_header_base::MSG_RECENT;
            else if(flg=="\\deleted") flags |= S_message_header_base::MSG_DELETED;
            //else if(flg=="PROFIMAIL_SENT") flags |= S_message_header_base::MSG_SENT;
         }
         switch(state){
         case C_mode_connection_imap::ST_IMAP_GET_UIDS:
            mod.headers.back().flags |= flags;
            break;

         case C_mode_connection_imap::ST_IMAP_GET_HDR:
            {
               assert(mod.IsImapIdle());
               C_mode_connection_imap::C_command_get_hdr *cmd_get_hdr = (C_mode_connection_imap::C_command_get_hdr*)cmd;
               cmd_get_hdr->flags = flags;
            }
            break;

         case C_mode_connection_imap::ST_IMAP_FETCH_SYNC:
            {
               C_mode_connection_imap::C_command_fetch_sync *cmd_sync = (C_mode_connection_imap::C_command_fetch_sync*)cmd;
               cmd_sync->need_update = true;

               if(!cmd_sync->curr_msg->IsRead() && (flags&S_message::MSG_READ)){
                  home_screen_notify.RemoveMailNotify(mod.acc, *mod.folder, *cmd_sync->curr_msg);
                  mod.GetContainer().flags |= C_message_container::FLG_NEED_SORT;
               }
               if(cmd_sync->curr_msg->UpdateFlags(flags))
                  mod.GetContainer().MakeDirty();
               if((flags&S_message::MSG_DELETED)){
                  mod.GetContainer().flags |= C_message_container::FLG_NEED_SORT;
                  if(!cmd_sync->curr_msg->IsHidden()){
                     cmd_sync->curr_msg->flags |= S_message::MSG_HIDDEN;
                     C_mode_mailbox *mod_mbox = (C_mode_mailbox*)FindMode(C_mode_mailbox::ID);
                     if(mod_mbox){
                        MailboxResetSearch(*mod_mbox);
                        SortMessages(mod_mbox->GetMessages(), mod_mbox->IsImap());
                        Mailbox_RecalculateDisplayArea(*mod_mbox);
                     }
                  }
               }
            }
            break;

         case C_mode_connection_imap::ST_IMAP_GET_BODY:
            {
                              //can be sent only for untagged fetch, since we didn't request this
               C_mode_connection_imap::C_command_get_body *cmd_body = (C_mode_connection_imap::C_command_get_body*)cmd;
               dword &flg = cmd_body->temp_msg.flags;
               flg &= S_message::MSG_HTML;
               flg |= flags;
               cmd_body->got_flags = true;
            }
            break;

         case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
            {
               C_mode_connection_imap::C_command_get_attachment_body *cmd_att = (C_mode_connection_imap::C_command_get_attachment_body*)cmd;
               cmd_att->untagged_flags = flags;
               cmd_att->got_flags = true;
            }
            break;

         case C_mode_connection_imap::ST_IMAP_GET_MSG_HEADERS:
            {
               C_mode_connection_imap::C_command_get_msg_headers *cmd_hdrs = (C_mode_connection_imap::C_command_get_msg_headers*)cmd;
               cmd_hdrs->untagged_flags = flags;
               cmd_hdrs->got_flags = true;
            }
            break;

         default:
            assert(0);
         }
      }else
      if(attr=="body" || attr=="bodystructure"){
         if(bs){
            C_vector<char> &src_data = bs->src_data;
            if(!val_read_ok){
                                 //read into buffer
               bs->src_open_braces_count = S_bodystructure::GetTextInBraces(cp, src_data, bs->src_open_braces_count);
            }else
               assert(0);
            if(!bs->src_open_braces_count){
                                 //entire params received, parse now
               bs->Parse(*this);
            }else{
                                 //it's multi-line response
               int sz = src_data.size();
               const char *cp1 = src_data.end();
               if(sz && cp1[-1]=='}'){
                  while(sz--){
                     if(*--cp1=='{')
                        break;
                  }
                  if(*cp1=='{'){
                     if(*cp1=='{' && (++cp1, text_utils::ScanDecimalNumber(cp1, mod.multiline.string_size))){
                        mod.multiline.cmd = cmd;
                        if(!mod.multiline.string_size)
                           --mod.multiline.string_size;
                     }else
                        assert(0);
                  }
                  src_data.resize(sz);
               }else{
                  assert(0);
               }
            }
         }
      }else
      if(attr.Length()>6 && attr.Left(5)=="body["){
         assert(val_read_ok);
         const char *cp1 = attr+5;
         /*
         int body_num;
         if(!text_utils::ScanDecimalNumber(cp1, body_num)){
            }else
            if(!text_utils::CheckStringBegin(cp1, "text"))
               return false;
            body_num = 1;
         }
         */
         switch(state){
         case C_mode_connection_imap::ST_IMAP_GET_BODY:
            {
               C_mode_connection_imap::C_command_get_body *cmd_body = (C_mode_connection_imap::C_command_get_body*)cmd;
               if(text_utils::CheckStringBegin(cp1, "1")){
                  (text_utils::CheckStringBegin(cp1, ".1") || text_utils::CheckStringBegin(cp1, ".2"));
                  if(text_utils::CheckStringBegin(cp1, ".mime")){
                     assert(cmd_body->phase==cmd_body->PHASE_GET_HDRS);
                  }else
                  if(*cp1==']'){
                     assert(cmd_body->phase==cmd_body->PHASE_GET_BODY_TEXT);
                  }else
                     assert(0);
               }else assert(0);
            }
            /*
         case C_mode_connection_imap::ST_IMAP_GET_TEXT_BODY_1:
                                 //text specified directly in response
                  C_buffer<char> line;
                  line.Assign(val, val+val.Length()+1);
                  Cstr_w err;
                  S_message &msg = mod.GetMessages()[cmd_body->message_index];
                  bool ret = AddRetrievedMessageLine(mod, *cmd_body, msg, line, err);
                  if(!ret){
                     FinishBodyRetrievalIMAP(mod, msg, cmd_body);
                     AfterBodyRetrieval(mod);
                  }
                  return ret;
            break;
            */
         case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
            {
               //C_mode_connection_imap::C_command_get_attachment_body *cmd_body = (C_mode_connection_imap::C_command_get_attachment_body*)cmd;
               //assert(body_num-2==cmd_body->attachment_index);
               cp1 = val;
               if(*cp1=='{' && (++cp1, text_utils::ScanDecimalNumber(cp1, mod.multiline.string_size)) && *cp1=='}' && !cp1[1]){
                  mod.multiline.cmd = cmd;
                  if(!mod.multiline.string_size)
                     --mod.multiline.string_size;
               }else
               if(text_utils::CheckStringBegin(cp1, "nil")){
               }else{
                  assert(0);
               }
            }
            break;
         default:
            assert(0);
         }
      }else{
         assert(0);
#ifdef _DEBUG
         //Fatal(attr);
#endif
      }
   }
   if(!mod.multiline.string_size)
      ImapFetchDone(mod, cmd);
   return true;
}

//----------------------------
/*
static int VectorElementIndex(const C_vector<dword> &vec, dword id){

   int i;
   for(i=vec.size(); i--; ){
      if(vec[i]==id)
         break;
   }
   return i;
}
*/
//----------------------------

void C_mail_client::ImapFetchDone(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd){

                           //entire fetch received
   switch(cmd->state){

   case C_mode_connection_imap::ST_IMAP_GET_HDR:
      {
                           //add new header now
         C_mode_connection_imap::C_command_get_hdr *cmd_get_hdr = (C_mode_connection_imap::C_command_get_hdr*)cmd;
         if(mod.IsImapIdle() && !cmd_get_hdr->sync_with_headers){
            if(cmd_get_hdr->got_uid){
                           //verify that we don't get duplicated header
               const C_vector<S_message> &messages = mod.GetMessages();
               int i;
               for(i=messages.size(); i--; ){
                  if(messages[i].imap_uid==cmd_get_hdr->uid)
                     break;
               }
               if(i==-1){
                  S_message_header_base hdr;
                  hdr.imap_uid = cmd_get_hdr->uid;
                  hdr.flags = cmd_get_hdr->flags;
                  ImapAddNewHeader(mod, hdr, cmd_get_hdr);
               }//else
                  //assert(0);
            }
         }else{
            int hi;
            for(hi=mod.headers.size(); hi--; ){
               S_message_header_base &hdr = mod.headers[hi];
               if(hdr.imap_uid==cmd_get_hdr->uid){
                  ImapAddNewHeader(mod, hdr, cmd_get_hdr);
                  //assert(hi==mod.headers.size()-1);
                  mod.headers.remove_index(hi);
                  break;
               }
            }
            if(!mod.acc.imap_last_x_days_limit){
                              //if days limit is set, we may have delivered unwanted header due to optimized UID range
               assert(hi!=-1);
            }
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_GET_BODY:
      {
                              //message body retrieved, store it
         C_mode_connection_imap::C_command_get_body *cmd_body = (C_mode_connection_imap::C_command_get_body*)cmd;
         if(cmd_body->phase==cmd_body->PHASE_GET_HDRS){
                              //just store needed header data
            S_complete_header hdr;
            ParseMailHeader(cmd_body->curr_hdr.begin(), cmd_body->curr_hdr.size(), hdr);
            C_mode_connection_imap::C_command_get_body::S_mime_header &mime = cmd_body->mime_headers.back();
            mime.content = hdr.content;
            mime.content_encoding = hdr.content_encoding;
            mime.coding = hdr.text_coding;
            mime.multipart_boundary = hdr.multipart_boundary;
            mime.format_flowed = hdr.format_flowed;
            mime.format_delsp = hdr.format_delsp;
         }else{
            if(cmd_body->uid){
               S_message *msg = mod.FindImapMessage(cmd_body->uid);
               if(msg){
                              //assign message now
                  S_message &tmp = cmd_body->temp_msg;

                              //check if message is among expected ones
                  //int uid_i = VectorElementIndex(cmd_body->curr_uid_range, cmd_body->uid);
                  //if(uid_i!=-1){
                  //assert(cmd_body->body_saving.IsStarted());
                  mod.GetContainer().DeleteMessageBody(mail_data_path, *msg);
                  msg->body_filename = tmp.body_filename;
                  msg->body_coding = tmp.body_coding;
                  
                  if(mod.acc.flags&S_account::ACC_IMAP_DOWNLOAD_ATTACHMENTS){
                              //we have attachments downloaded in 'tmp', so remove those in msg
                     mod.GetContainer().DeleteMessageAttachments(*msg);
                  }
                  if(tmp.attachments.Size()){
                     int di = msg->attachments.Size();
                     msg->attachments.Resize(di+tmp.attachments.Size());
                     for(dword i=0; i<tmp.attachments.Size(); i++)
                        msg->attachments[di++] = tmp.attachments[i];
                  }
                  msg->inline_attachments = tmp.inline_attachments;
                  const dword FLG_MASK = msg->MSG_HTML;// | msg.MSG_PRIORITY_LOW
                  msg->flags &= ~FLG_MASK;
                  msg->flags |= tmp.flags&FLG_MASK;

                              //fix progress indicator
                  mod.progress.pos = cmd_body->start_progress_pos;
                  mod.progress.pos += (cmd_body->phase==cmd_body->PHASE_GET_BODY_TEXT) ? msg->imap_text_part_size : msg->size;
                  //ConnectionDrawProgress(mod);

                  tmp = S_message();

                  msg->marked = false;

                  if(mod.action==mod.ACT_GET_BODY && !(msg->flags&msg->MSG_READ)){
                              //update 'read' flag now
                     msg->flags |= msg->MSG_READ | msg->MSG_IMAP_READ_DIRTY;
                     if(config_mail.tweaks.show_only_unread_msgs)
                        mod.GetContainer().flags |= C_message_container::FLG_NEED_SORT;
                  }
                  mod.GetContainer().MakeDirty();
                  FinishBodyRetrieval(mod, *cmd_body, *msg);
                  //RedrawScreen();
                  ConnectionDrawProgress(mod);
               }else
                  assert(0);
            }else
            if(cmd_body->got_flags){
                              //unexpected fetch, just update existing message
               dword uid = mod.msg_seq_map.FindUid(cmd_body->seq_num);
               if(uid!=0xffffffff){
                  S_message *msg = mod.FindImapMessage(uid);
                  if(msg){
                     if(msg->UpdateFlags(cmd_body->temp_msg.flags))
                        mod.GetContainer().MakeDirty();
                  }else
                     assert(0);
               }else
                  assert(0);
            }else
               assert(0);
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
      {
         C_mode_connection_imap::C_command_get_attachment_body *cmd_body = (C_mode_connection_imap::C_command_get_attachment_body*)cmd;
         if(cmd_body->uid){
            S_message *msg = mod.FindImapMessage(cmd_body->uid);
            if(msg && cmd_body->attachment_index<msg->attachments.Size()){
                              //store attachment
               S_attachment &att = msg->attachments[cmd_body->attachment_index];
               att.filename = cmd_body->att_saving.filename.ToUtf8();
               mod.GetContainer().MakeDirty();

               cmd_body->att_saving.Finish();
            }else
               assert(0);
         }else
         if(cmd_body->got_flags){
                              //unexpected fetch, just update existing message
            dword uid = mod.msg_seq_map.FindUid(cmd_body->seq_num);
            if(uid!=0xffffffff){
               S_message *msg = mod.FindImapMessage(uid);
               if(msg){
                  if(msg->UpdateFlags(cmd_body->untagged_flags))
                     mod.GetContainer().MakeDirty();
               }else
                  assert(0);
            }else
               assert(0);
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_FETCH_SYNC:
      {
         C_mode_connection_imap::C_command_fetch_sync *cmd_sync = (C_mode_connection_imap::C_command_fetch_sync*)cmd;
         if(cmd_sync->need_update){
            ImapIdleUpdateAfterOperation(mod, false);
            mod.GetContainer().ResetStats();
            cmd_sync->need_update = false;
            UpdateUnreadMessageNotify();
         }
         cmd_sync->curr_msg = NULL;
      }
      break;
   }
}

//----------------------------

void C_mail_client::ImapAddNewHeader(C_mode_connection_imap &mod, S_message_header_base &hdr, C_mode_connection_imap::C_command_get_hdr *cmd_get_hdr){

   const S_bodystructure &bs = cmd_get_hdr->bodystructure;

   int num_a = bs.GetNumAttachments();
                              //got this header
   hdr.size = cmd_get_hdr->hdr_size;
   int override_size = -1;
   if(!(mod.acc.flags&S_account::ACC_IMAP_DOWNLOAD_ATTACHMENTS) && !(hdr.flags&hdr.MSG_DRAFT)){
                              //attachments won't be downloaded; use message's size without attachments
      override_size = bs.GetSizeOfTextPart();
   }
   dword text_subpart = 0;
   if(bs.has_text_part && num_a){
                              //assign where is text part
      const S_bodystructure::S_part &p1 = bs.root_part.nested_parts.front();
      if(p1.content_type.type==CONTENT_TEXT)
         text_subpart = 1;
      else if(p1.content_type.type==CONTENT_MULTIPART && p1.content_type.subtype=="alternative"){
         dword n = p1.nested_parts.size();
         if(n){
            const S_bodystructure::S_part &p11 = p1.nested_parts[0];
            if(p11.content_type.type==CONTENT_TEXT){
               text_subpart = 2;
               override_size = p11.size;
            }
                              //here we make selection to prefer html over plain text
            if(n>1 && !config_mail.tweaks.prefer_plain_text_body){
               const S_bodystructure::S_part &p12 = p1.nested_parts[1];
               if(p12.content_type.type==CONTENT_TEXT && p12.content_type.subtype=="html"){
                  text_subpart = 3;
                  override_size = p12.size;
               }
            }
         }
      }else text_subpart = 1;
   }else{
                              //only search for text part in body if there're no attachments and no text part
      if(!bs.has_text_part && !num_a)
         text_subpart = 1;
   }

   S_message &msg = Connection_AfterGotHeader(mod, hdr, cmd_get_hdr->hdr_data.begin(), cmd_get_hdr->hdr_data.size(), override_size);
   msg.imap_text_part_size = override_size;
   msg.flags |= text_subpart<<hdr.MSG_TEXT_PART_TYPE_SHIFT;
                              //now init attachments from bodystructure
   if(num_a){
      msg.flags |= hdr.MSG_HAS_ATTACHMENTS;
      msg.attachments.Clear();
      msg.attachments.Resize(num_a);
      const S_bodystructure::S_part *part = bs.GetFirstAttachment();
      for(int i=0; i<num_a; i++, ++part){
         S_attachment &att = msg.attachments[i];
         att.suggested_filename = part->name;
         if(part->content_type.type==CONTENT_MESSAGE && part->content_type.subtype=="rfc822"){
                              //for message/rfc822 attachments, force EML extension, so that we can see them as messages and open them
            Cstr_w ext = text_utils::GetExtension(att.suggested_filename);
            ext.ToLower();
            if(ext!=L"eml"){
               att.suggested_filename<<L".eml";
            }
         }
         att.file_size = part->size;
         att.content_encoding = part->content_encoding;
         att.part_index = word(1+i+int(bs.has_text_part));
      }
   }
   if(!bs.has_text_part && !text_subpart){
                              //message has no text part, make empty text now
      SaveMessageBody(mod.GetContainer(), msg, "", 0);
                              //don't d/l body
      for(int i=mod.headers_to_download.size(); i--; ){
         if(mod.headers_to_download[i].imap_uid==hdr.imap_uid){
            mod.headers_to_download.remove_index(i);
            break;
         }
      }
   }
}

//----------------------------

bool C_mail_client::ImapProcessOkUntagged(C_mode_connection_imap &mod, const char *cp, C_mode_connection_imap::E_STATE state, Cstr_w &err){

   S_account &acc = mod.acc;
   //C_vector<S_message> &messages = mod.GetMessages();

   switch(state){
   case C_mode_connection_imap::ST_NULL:
      if(mod.state==C_mode_connection_imap::ST_WAIT_CONNECT_OK){
         //if(!mod.IsImapIdle())
            //acc.background_processor.Close();
         acc.selected_folder = NULL;

         /*
                              //check if server sends capability
                              //! doesn't work, some servers don't send complete caps here
         text_utils::SkipWS(cp);
         if(text_utils::CheckStringBegin(cp, "[capability")){
            text_utils::SkipWS(cp);
                              //shorttut, process capability here
            Cstr_c s = cp;
            int ei = s.Find(']');
            if(ei!=-1){
               s = s.Left(ei);
               ImapProcessCapability(mod, s);
               return ImapAfterGotCapability(mod, err);
            }
         }
         */
         SendImapCommand(mod, "CAPABILITY", new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_CAPABILITY));
      }else
         assert(0);
      break;

   case C_mode_connection_imap::ST_IMAP_SELECT:
      text_utils::SkipWS(cp);
      if(text_utils::CheckStringBegin(cp, "[uidvalidity")){
         text_utils::SkipWS(cp);
         dword uid_validity;
         if(text_utils::ScanInt(cp, (int&)uid_validity)){
            C_message_container &cnt = mod.GetContainer();
            if(cnt.imap_uid_validity!=uid_validity){
                              //UID validity changed, reset folder content
               cnt.imap_uid_validity = uid_validity;
               cnt.MakeDirty();
               assert(!mod.headers.size());
               SyncMessages(mod, false);
            }
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_IDLE:
   case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE_DONE:
      break;

   //default:
      //assert(0);
   }
   return true;
}

//----------------------------
// Parse IMAP sequence range.
static bool ParseSequenceRange(const char *cp, C_vector<dword> &uids){

   while(true){
      dword v;
      if(!text_utils::ScanDecimalNumber(cp, (int&)v))
         return false;
      if(*cp==':'){
         ++cp;
         dword max;
         if(!text_utils::ScanDecimalNumber(cp, (int&)max))
            return false;
         if(v>max)
            Swap(v, max);
         for(; v<=max; ++v)
            uids.push_back(v);
      }else{
         uids.push_back(v);
      }
      if(!*cp)
         break;
      if(*cp!=',')
         return false;
      ++cp;
   }
   return true;
}

//----------------------------

void C_mail_client::C_mode_connection_in::AddDeletedToDeleteList(){

   msgs_to_delete.clear();
   C_vector<S_message> &messages = GetMessages();
   for(int i=messages.size(); i--; ){
      const S_message &msg = messages[i];
      if(msg.IsDeleted() && (msg.flags&msg.MSG_DELETED_DIRTY)){
         msgs_to_delete.push_back(msg.imap_uid);
         need_expunge = true;
      }
   }
}

//----------------------------

bool C_mail_client::ImapMoveMessagesToFolder(C_mode_connection_imap &mod, const C_vector<dword> &uids, const char *uid_map, C_message_container &fld_dst, bool clear_delete_flag){

   text_utils::SkipWS(uid_map);
   dword uid_validity;
   if(!text_utils::ScanDecimalNumber(uid_map, (int&)uid_validity))
      return false;
                              //check if validity matches dest mailbox
   if(fld_dst.imap_uid_validity && fld_dst.imap_uid_validity!=uid_validity)
      return false;

   C_vector<dword> src_uids, dst_uids;
   {
                              //parse 2 uid ranges
      Cstr_c src, dst;
      text_utils::SkipWS(uid_map);
      if(!text_utils::ReadToken(uid_map, src, " "))
         return false;
      text_utils::SkipWS(uid_map);
      if(!text_utils::ReadToken(uid_map, dst, " ]"))
         return false;
      if(!ParseSequenceRange(src, src_uids) || !ParseSequenceRange(dst, dst_uids) || src_uids.size()!=dst_uids.size())
         return false;
   }
   bool was_loaded = fld_dst.loaded;
   fld_dst.LoadMessages(mail_data_path);
   fld_dst.imap_uid_validity = uid_validity;
   fld_dst.MakeDirty();
   fld_dst.ResetStats();

   C_vector<S_message> &messages = mod.GetMessages();

   for(int ui=uids.size(); ui--; ){
      dword uid = uids[ui];
      for(int mi=messages.size(); mi--; ){
         S_message &msg = messages[mi];
         if(msg.imap_uid!=uid)
            continue;
                              //move files to dest folder if possible
         for(int j=src_uids.size(); j--; ){
            if(src_uids[j]!=uid)
               continue;
                        //move message files to target folder
            S_message &msg_dst = fld_dst.messages.push_back(msg);
            msg_dst.MoveMessageFiles(mail_data_path, *mod.folder, fld_dst);
            msg_dst.imap_uid = dst_uids[j];
            if(clear_delete_flag)
               msg_dst.flags &= ~(S_message::MSG_DELETED|S_message::MSG_DELETED_DIRTY);

            msg.body_filename.Clear();
            for(int i=msg.attachments.Size(); i--; )
               msg.attachments[i].filename.Clear();
            for(int i=msg.inline_attachments.Size(); i--; )
               msg.inline_attachments[i].filename.Clear();
            break;
         }
         break;
      }
   }
   SortMessages(fld_dst.messages, true);
   if(!was_loaded)
      fld_dst.SaveAndUnloadMessages(mail_data_path);
   return true;
}

//----------------------------

void C_mail_client::ImapMoveMessagesToFolderAndDelete(C_mode_connection_imap &mod, const C_vector<dword> &uids, const char *uid_map, C_message_container &fld_dst){

   if(uid_map)
      ImapMoveMessagesToFolder(mod, uids, uid_map, fld_dst);
                              //mark all moved messages as deleted
   C_vector<S_message> &messages = mod.GetMessages();
   for(int ui=uids.size(); ui--; ){
      dword uid = uids[ui];
      for(int mi=messages.size(); mi--; ){
         S_message &msg = messages[mi];
         if(msg.imap_uid!=uid)
            continue;
         mod.msgs_to_delete.push_back(uid);
         msg.flags |= msg.MSG_DELETED | msg.MSG_DELETED_DIRTY;
         break;
      }
   }
   mod.folder->MakeDirty();
}

//----------------------------

void C_mail_client::ImapLogin(C_mode_connection_imap &mod){

   const S_account &acc = mod.acc;
   bool name_has_specials = false, pwd_has_specials = false, pwd_has_quotes = false;

   Cstr_c user = acc.username;
   for(int i=user.Length(); i--; ){
      char c = user[i];
      if(!text_utils::IsAlNum(c)){
         name_has_specials = true;
         if(c=='\\'){
                        //replace by double '\'
            Cstr_c tmp;
            tmp<<user.Left(i) <<'\\' <<user.RightFromPos(i);
            user = tmp;
         }
      }
   }
   Cstr_c password = acc.password;
   for(int i=password.Length(); i--; ){
      char c = password[i];
      if(!text_utils::IsAlNum(c)){
         pwd_has_specials = true;
         if(c=='\'' || c=='\"')
            pwd_has_quotes = true;
         else if(c=='\\'){
                        //replace by double '\'
            Cstr_c tmp;
            tmp<<password.Left(i) <<'\\' <<password.RightFromPos(i);
            password = tmp;
         }
      }
   }

   Cstr_c s;
   s = "LOGIN ";
   if(name_has_specials){
      s<<'\"' <<user <<'\"';
   }else
       s<<user;
   
   s<<' ';
   if(!pwd_has_specials)
      s<<password;
   else
   if(pwd_has_quotes){
            //send password as additional data
      s.AppendFormat("{%}")<<acc.password.Length();
   }else{
            //send password quoted
      s.AppendFormat("\"%\"") <<password;
   }
   SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_LOGIN));
   ConnectionDrawAction(mod, GetText(TXT_PROGRESS_LOGGING_IN));
}

//----------------------------

bool C_mail_client::ImapAfterGotCapability(C_mode_connection_imap &mod, Cstr_w &err){

   if(mod.acc.secure_in==S_account_settings::SECURE_STARTTLS && !(mod.capability&mod.CAPS_IN_TLS)){
               //SSL connection required, check if available
      if(mod.capability&mod.CAPS_STARTTLS){
         SendImapCommand(mod, "STARTTLS", new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_STARTTLS));
      }else{
         err = L"IMAP server doesn't support STARTTLS command. Disable StartTLS option is account settings.";
      }
      return true;
   }
#ifndef ALWAYS_IDLE_CAPS
   if(mod.IsImapIdle() && !(mod.capability&mod.CAPS_IDLE)){
                        //idle not supported, close
      mod.AddRef();
      mod.acc.CloseConnection();
      ConnectionUpdateState(mod.acc, S_account::UPDATE_FATAL_ERROR, L"This server doesn't support IMAP IDLE connection.");
      mod.Release();
      return false;
   }
#endif
   if(mod.capability&mod.CAPS_ID){
                        //send ID
      Cstr_c s;
      //2 ID ("guid" "62cf09bf2aab0e34d43319c42fb8f54341e3f40f")
      s.Format("ID (\"vendor\" \"Lonely Cat Games\" \"os\" "
#ifdef __SYMBIAN32__
         "\"Symbian\""
#elif defined WINDOWS_MOBILE
         "\"Windows Mobile\""
#else
         "\"Windows\""
#endif
         " \"name\" \"ProfiMail\" \"version\" \"%.#02%\" \"date\" \"" __DATE__ "\""
         " \"guid\" \"1\"" //just hack for yahoo imap, remove when not needed
         ")"
         ) <<VERSION_HI <<VERSION_LO;
      SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_ID));
   }else
      ImapLogin(mod);
   return true;
}

//----------------------------

C_mail_client::C_mode_connection_imap::~C_mode_connection_imap(){

   if(capability&CAPS_IN_COMPRESSION){
      deflateEnd(&compress_out);
      inflateEnd(&compress_in);
   }
}

//----------------------------
static const char *const subpart_names[3] = { "1", "1.1", "1.2" };

bool C_mail_client::ImapProcessOkTagged(C_mode_connection_imap &mod, const char *cp, C_mode_connection_imap::C_command *cmd, Cstr_w &err){

   S_account &acc = mod.acc;

   switch(cmd->state){
   case C_mode_connection_imap::ST_IMAP_CAPABILITY:
      return ImapAfterGotCapability(mod, err);

   case C_mode_connection_imap::ST_IMAP_STARTTLS:
      mod.state = C_mode_connection_imap::ST_WAIT_CONNECT_OK;
                              //clear capabilities and leave only in-tls flag
      mod.capability = (dword)mod.CAPS_IN_TLS;
      mod.acc.socket->BeginSSL();
      break;

   case C_mode_connection_imap::ST_IMAP_ID:
      ImapLogin(mod);
      break;

   case C_mode_connection_imap::ST_IMAP_LOGIN:
      //LOG_RUN("Logged in");
      if(!socket_log && 
         (mod.capability&mod.CAPS_COMPRESS)){
                              //enable compression
         SendImapCommand(mod, "compress deflate", new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_COMPRESS));
         break;
      }
      AfterImapLogin(mod);
      break;

   case C_mode_connection_imap::ST_IMAP_COMPRESS:
      {
                              //compression activated
         mod.capability |= mod.CAPS_IN_COMPRESSION;
         MemSet(&mod.compress_out, 0, sizeof(mod.compress_out));
         MemSet(&mod.compress_in, 0, sizeof(mod.compress_in));
         int zerr = deflateInit2(&mod.compress_out, 5, Z_DEFLATED, -MAX_WBITS, 9, Z_DEFAULT_STRATEGY);
         assert(!zerr);
         zerr = inflateInit2(&mod.compress_in, -MAX_WBITS);
         assert(!zerr);
         AfterImapLogin(mod);
      }
      break;

   case C_mode_connection_imap::ST_IMAP_SELECT:
      {
         acc.selected_folder = mod.folder;
         if(mod.action==mod.ACT_IMAP_IDLE){
            ConnectionRedrawImapIdleFolder(acc);
         }
         C_mode_connection_imap::C_command_select *cmd_sel = (C_mode_connection_imap::C_command_select*)cmd;
         Connection_AfterMailboxSelect(mod, cmd_sel->num_msgs);
      }
      break;

   case C_mode_connection_imap::ST_IMAP_GET_UIDS:
      mod.msg_seq_map.is_synced = true;
      if(!Connection_AfterUidList(mod, !mod.IsImapIdle()))
         return false;
      break;

   case C_mode_connection_imap::ST_IMAP_SEARCH_SINCE:
      {
         C_mode_connection_imap::C_command_search_since_date *cmd_s = (C_mode_connection_imap::C_command_search_since_date*)cmd;
         if(cmd_s->idle_refresh){
                              //search for new headers from IDLE
            if(cmd_s->seqs.size()){
                              //ask for new headers
               Cstr_c range = MakeImapSequenceString(cmd_s->seqs);
               Cstr_c s = MakeNewHeadersCommand(range, false, true);
               SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command_get_hdr);
               mod.num_hdrs_to_ask = 0;
            }else{
                              //no matching headers found, so go back to IDLE
               BeginImapIdle(mod);
            }
         }else{
            if(cmd_s->seqs.size()){
               mod.headers.reserve(cmd_s->seqs.size());
               {
                  Cstr_w s; s<<GetText(TXT_PROGRESS_NUM_MESSAGES) <<L' ' <<cmd_s->seqs.size();
                  ConnectionDrawAction(mod, s);
               }
               if(mod.action==C_mode_connection::ACT_UPDATE_MAILBOX)
                  ConnectionDrawTitle(mod, GetText(TXT_UPDATE_MAILBOX));

               Cstr_c s; s.Format("FETCH % (UID flags)") <<MakeImapSequenceString(cmd_s->seqs);
               SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command_get_uids);
            }else
               Connection_AfterMailboxSelect(mod, 0);
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_DELETE_MESSAGES:
      {
                              //clear dirty flags
         C_vector<S_message> &messages = mod.GetMessages();
         for(int i=mod.msgs_to_delete.size(); i--; ){
            dword uid = mod.msgs_to_delete[i];
            for(int j=messages.size(); j--; ){
               S_message &msg = messages[j];
               if(msg.imap_uid==uid){
                  if(msg.flags&msg.MSG_DELETED_DIRTY){
                     msg.flags &= ~msg.MSG_DELETED_DIRTY;
                     if(!msg.IsHidden()){
                              //hide deleted message
                        msg.flags |= msg.MSG_HIDDEN;
                        mod.GetContainer().flags |= C_message_container::FLG_NEED_SORT;
                     }
                     mod.GetContainer().MakeDirty();
                  }
                  break;
               }
            }
         }
         mod.msgs_to_delete.clear();
         if(mod.GetParent()->Id()==C_mode_mailbox::ID){
            C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mod.GetParent();
            int sel = mod_mbox.selection;
            Mailbox_RecalculateDisplayArea(mod_mbox);
            mod_mbox.selection = Min(sel, mod_mbox.GetNumEntries()-1);
            mod_mbox.EnsureVisible();
         }
         ConnectionCleanupAndDisconnect(mod);
         //}
      }
      break;

   case C_mode_connection_imap::ST_IMAP_GET_HDR:
      {
         if(mod.IsImapIdle()){
            if(mod.headers_added){
               AfterHeadersAdded(mod);
               ImapIdleUpdateAfterOperation(mod, true);
            }
            ImapIdleAfterGotHeaders(mod);
            UpdateUnreadMessageNotify();
            break;
         }
         UpdateUnreadMessageNotify();
                              //all new headers retrieved, continue
         Connection_AfterAllHeaders(mod);
      }
      break;

   case C_mode_connection_imap::ST_IMAP_GET_BODY:
      {
         C_mode_connection_imap::C_command_get_body *cmd_body = (C_mode_connection_imap::C_command_get_body*)cmd;
         if(!cmd_body->got_fetch){
            err = L"Message doesn't exist on server";
            break;
         }
         switch(cmd_body->phase){
         case C_mode_connection_imap::C_command_get_body::PHASE_GET_HDRS:
            {
                              //now actually download text bodies
               Cstr_c s;
               s.Format("UID FETCH % (UID body.peek[%])") <<MakeImapSequenceString(cmd_body->curr_uid_range) <<subpart_names[cmd_body->curr_text_subpart];
               cmd_body->phase = cmd_body->PHASE_GET_BODY_TEXT;
               cmd_body->AddRef();
               SendImapCommand(mod, s, cmd_body);
            }
            break;
         case C_mode_connection_imap::C_command_get_body::PHASE_GET_BODY_TEXT:
            cmd_body->curr_uid_range.clear();
            for(int i=0; i<3; i++)
            if(cmd_body->uids_text[i].size()){
                              //start getting next bunch of message texts
               cmd_body->AddRef();
               StartRetrievingBodiesTextIMAP(mod, cmd_body);
               return true;
            }
                              //flow... start getting full bodies
         case C_mode_connection_imap::C_command_get_body::PHASE_GET_BODY_FULL:
                              //continue in work
            cmd_body->curr_uid_range.clear();
            if(cmd_body->uids_full.size()){
               cmd_body->AddRef();
               StartRetrievingBodiesFullIMAP(mod, cmd_body);
            }else{
               ConnectionClearProgress(mod);
               ConnectionCleanupAndDisconnect(mod);
               return false;
            }
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
      {
         C_mode_connection_imap::C_command_get_attachment_body *cmd_body = (C_mode_connection_imap::C_command_get_attachment_body*)cmd;
         if(cmd_body->got_fetch){
            if(mod.action==mod.ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL){
                     //find next attachment to download
               if(RetrieveNextMessageAttachment(mod))
                  break;
            }
         }else
            err = L"Attachment not found on server";
         ConnectionDisconnect(mod);
      }
      return false;

   case C_mode_connection_imap::ST_IMAP_GET_MSG_HEADERS:
      {
         C_mode_connection_imap::C_command_get_msg_headers *cmd_body = (C_mode_connection_imap::C_command_get_msg_headers*)cmd;
         if(cmd_body->got_fetch){
            cmd_body->AddRef();
            C_mode_mailbox *mod_mbox;
            if(mod.GetParent()->Id()==C_mode_read_mail_base::ID)
               mod_mbox = &((C_mode_read_mail_base&)*mod.GetParent()).GetMailbox();
            else
               mod_mbox = &(C_mode_mailbox&)*mod.GetParent();
            ConnectionDisconnect(mod);
            cmd_body->full_hdr.push_back(0);
            Mailbox_ShowDetails(*mod_mbox, cmd_body->full_hdr.begin());
            cmd_body->Release();
         }else
            err = L"Message not found on server";
      }
      return false;

   case C_mode_connection_imap::ST_IMAP_LIST:
      {
         C_mode_connection_imap::C_command_list *cmd_lst = (C_mode_connection_imap::C_command_list*)cmd;
         if(cmd_lst->phase!=cmd_lst->INBOX && mod.acc.imap_root_path.Length()){
            Cstr_w tmp = mod.acc.imap_root_path;
            tmp.ToLower();
            if(tmp!=L"inbox"){
               cmd_lst->phase = cmd_lst->INBOX;

               cmd_lst->AddRef();
               SendImapCommand(mod, "LIST \"\" \"inbox*\"", cmd_lst);
               break;
            }
         }
         CleanImapFolders(mod, *cmd_lst);
         mod.container_invalid = true;
         ConnectionDisconnect(mod);
      }
      return false;

   case C_mode_connection_imap::ST_IMAP_DELETE_FOLDER:
      {
                  //successfully deleted IMAP folder
                  // delete local folder, save messages and refresh IMAP folders mode
         C_mode_folders_list &mod_flds = (C_mode_folders_list&)*mod.GetParent();
         assert(mod_flds.Id()==C_mode_folders_list::ID);
         FoldersList_FinishDelete(mod_flds);

         mod.container_invalid = true;
         ConnectionDisconnect(mod);
         return false;
      }
      break;

   case C_mode_connection_imap::ST_IMAP_RENAME_FOLDER:
      {
         C_mode_folders_list &mod_flds = (C_mode_folders_list&)*mod.GetParent();
         assert(mod_flds.Id()==C_mode_folders_list::ID);
         FoldersList_FinishRename(mod_flds);
         ConnectionDisconnect(mod);
         return false;
      }
      break;

   case C_mode_connection_imap::ST_IMAP_CREATE_FOLDER:
      {
         C_mode_connection_imap::C_command_create *cmd_create = (C_mode_connection_imap::C_command_create*)cmd;
         if(cmd_create->in_list){
            C_mode_folders_list &mod_flds = (C_mode_folders_list&)*mod.GetParent();
            assert(mod_flds.Id()==C_mode_folders_list::ID);
            C_message_container &fld = *mod_flds.GetSelectedFolder();
            if(cmd_create->flags&C_mode_connection_imap::C_command_list_base::FLAG_NOINFERIORS)
               fld.flags |= fld.FLG_NOINFERIORS;
            if(cmd_create->flags&C_mode_connection_imap::C_command_list_base::FLAG_NOSELECT)
               fld.flags |= fld.FLG_NOSELECT;

            ImapFolders_FinishCreate(mod_flds);
            ConnectionDisconnect(mod);
            return false;
         }else{
                              //list for getting folder params
            cmd_create->in_list = true;
            Cstr_c cmd1;
            cmd1.Format("LIST \"\" \"%\"") <<EncodeImapFolderName(mod.params.text);
            cmd_create->AddRef();
            SendImapCommand(mod, cmd1, cmd_create);
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_CLOSE:
      RemoveDeletedMessages(mod);
      if(!ConnectionImapFolderClose(mod))
         return false;
      break;

   case C_mode_connection_imap::ST_IMAP_EXPUNGE:
      mod.need_expunge = false;
      RemoveDeletedMessages(mod);
      if(mod.IsImapIdle()){
         ImapIdleAfterGotHeaders(mod);
      }else
         ConnectionDisconnect(mod);
      break;

   case C_mode_connection_imap::ST_IMAP_MOVE_MESSAGES_TO_THRASH:
      {
         C_mode_connection_imap::C_command_move_to_folder *cmd_move = (C_mode_connection_imap::C_command_move_to_folder*)cmd;
         switch(cmd_move->phase){
         case C_mode_connection_imap::C_command_move_to_folder::PHASE_CREATE:
            {
                                 //trash folder created
               bool created;
               C_message_container *fld = FindOrCreateImapFolder(mod.acc, mod.acc.GetTrashFolderName(), created);
               if(created){
                  fld->flags &= ~C_message_container::FLG_TEMP;
                  SaveAccounts();
               }else assert(0);
               C_mode_mailbox *mod_mbox = mod.GetParent()->Id()==C_mode_mailbox::ID ? &(C_mode_mailbox&)*mod.GetParent() : NULL;
               if(mod_mbox)
                  mod_mbox->folder = mod.folder;
               {
                  C_mode_folders_list *mod_flds;
                  if(mod_mbox)
                     mod_flds = &(C_mode_folders_list&)*mod_mbox->GetParent();
                  else
                     mod_flds = &(C_mode_folders_list&)*mod.GetParent();
                  FoldersList_InitView(*mod_flds);
               }
                                 //resend copy command now
               cmd_move->phase = cmd_move->PHASE_MOVE;
               cmd_move->AddRef();
               SendImapCommand(mod, cmd_move->imap_cmd, cmd_move);
            }
            break;
         case C_mode_connection_imap::C_command_move_to_folder::PHASE_MOVE:
            {
               if(mod.capability&mod.CAPS_UIDPLUS){
                                 //use UID+ (copyuid)
                  text_utils::SkipWS(cp);
                  if(text_utils::CheckStringBegin(cp, "[copyuid")){
                     C_message_container *fld = FindFolder(mod.acc, mod.acc.GetTrashFolderName());
                     if(fld){
                        ImapMoveMessagesToFolder(mod, cmd_move->uids, cp, *fld, true);
                     }
                  }
               }
               StartDeletingMessages(mod, false);
            }
            break;
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_MOVE_MESSAGES_COPY:
      {
         C_mode_connection_imap::C_command_move_to_folder *cmd_move = (C_mode_connection_imap::C_command_move_to_folder*)cmd;
                              //messages copied
         text_utils::SkipWS(cp);
         if(!(mod.capability&mod.CAPS_UIDPLUS) || !text_utils::CheckStringBegin(cp, "[copyuid"))
            cp = NULL;
         ImapMoveMessagesToFolderAndDelete(mod, cmd_move->uids, cp, *mod.params.imap_folder_move_dest);

         C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mod.GetParent();
         mod_mbox.GetContainer().ClearAllMessageMarks();
         ConnectionCleanupAndDisconnect(mod, false);
      }
      break;

   case C_mode_connection_imap::ST_IMAP_MOVE_TO_FOLDER_BY_RULE:
      {
         C_mode_connection_imap::C_command_move_to_folder *cmd_move = (C_mode_connection_imap::C_command_move_to_folder*)cmd;
         switch(cmd_move->phase){
         case C_mode_connection_imap::C_command_move_to_folder::PHASE_MOVE:
            {
               text_utils::SkipWS(cp);
               if(!(mod.capability&mod.CAPS_UIDPLUS) || !text_utils::CheckStringBegin(cp, "[copyuid"))
                  cp = NULL;
               C_message_container *fld_dst = FindFolder(mod.acc, cmd_move->folder_name);
               if(!fld_dst)
                  cp = NULL;
               ImapMoveMessagesToFolderAndDelete(mod, cmd_move->uids, cp, *fld_dst);
               mod.need_expunge = true;
               if(!mod.commands.size()){
                                    //all move commands finished, continue in work
                  if(mod.IsImapIdle())
                     ImapIdleAfterGotHeaders(mod);
                  else
                     Connection_AfterAllHeaders(mod);
               }
            }
            break;

         case C_mode_connection_imap::C_command_move_to_folder::PHASE_CREATE:
                              //folder created, repeat the command
            cmd_move->phase = cmd_move->PHASE_MOVE;
            cmd_move->AddRef();
            SendImapCommand(mod, cmd_move->imap_cmd, cmd_move);
            break;
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE:
   case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE_SEND:
      assert(0);
      ConnectionDisconnect(mod);
      return false;

   case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE_CREATE_FOLDER:
                              //folder successfully created
      if(mod.GetContainer().IsTemp()){
                              // clear temp flag, and upload again
         mod.GetContainer().flags &= ~C_message_container::FLG_TEMP;
         SaveAccounts();
      }
      StartUploadingMessageToImap((C_mode_connection_imap_upload&)mod);
      break;

   case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE_DONE:
      {
         C_mode_connection_imap_upload &mod_up = (C_mode_connection_imap_upload&)mod;

         C_message_container &fld = mod.GetContainer();
                              //message successfully uploaded to server
         bool msg_copied = false;
         if(mod.capability&mod.CAPS_UIDPLUS){
                                 //use UID+
            text_utils::SkipWS(cp);
            if(text_utils::CheckStringBegin(cp, "[appenduid")){
               text_utils::SkipWS(cp);
               dword uid_validity;
               if(text_utils::ScanDecimalNumber(cp, (int&)uid_validity)){
                                          //check if validity matches dest mailbox
                  if(!fld.imap_uid_validity || fld.imap_uid_validity==uid_validity){
                     fld.imap_uid_validity = uid_validity;
                     fld.flags &= ~fld.FLG_TEMP;

                     text_utils::SkipWS(cp);
                     dword uid;
                     if(text_utils::ScanDecimalNumber(cp, (int&)uid)){
                        S_message &msg = mod.GetMessages()[mod_up.message_index];
                        msg.flags |= msg.MSG_SERVER_SYNC;
                        msg.flags &= ~(msg.MSG_NEED_UPLOAD | msg.MSG_SENT);
                        msg.flags |= (mod.action==mod.ACT_UPLOAD_SENT ? 0 : msg.MSG_DRAFT);
                        msg.imap_uid = uid;
                                    //fix sender
                        if(!msg.sender.display_name.Length())
                           msg.sender.display_name = acc.primary_identity.display_name;
                        if(!msg.sender.email.Length())
                           msg.sender.email = acc.primary_identity.email;
                        //msg.to_emails = msg.sender_email;
                        //msg.to_names = msg.sender_name;

                        msg_copied = true;
                     }
                  }
               }
            }
         }
         if(!msg_copied){
                              // delete local copy
            DeleteMessage(mod.GetContainer(), mod_up.message_index, true);
         }
         mod.GetContainer().MakeDirty();

         if(mod.GetParent()->Id()==C_mode_mailbox::ID){
            C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mod.GetParent();
            Mailbox_RecalculateDisplayArea(mod_mbox);
         }
               //upload next
         StartUploadingMessageToImap((C_mode_connection_imap_upload&)mod);
      }
      break;

   case C_mode_connection_imap::ST_IMAP_IDLE:
      {
         switch(mod.action){
         case C_mode_connection::ACT_IMAP_IDLE:
            if(acc.selected_folder!=mod.folder){
                              //select proper folder
               AfterImapLogin(mod);
            }else{
                              //out of IDLE mode, get all new headers
               mod.headers_to_download.clear();
               mod.headers_to_move.clear();
               mod.msgs_to_delete.clear();
               mod.num_sync_hdrs = 0;
               dword max = mod.msg_seq_map.GetMaxSeqNum();
               Cstr_c range; range.Format("%:*") <<(max+1);

               if(mod.acc.imap_last_x_days_limit && !(mod.capability&mod.CAPS_NO_SEARCH)){
                                 //limited by days, so use SEARCH to get seq numbers
                  Cstr_c s = MakeSearchCommand(mod.acc.imap_last_x_days_limit);
                  s <<' ' <<range;
                  SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command_search_since_date(true));
               }else{
                              //directly ask for headers
                  Cstr_c s = MakeNewHeadersCommand(range, false, true);
                  SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command_get_hdr);
                  mod.num_hdrs_to_ask = 0;
               }
            }
            break;

         case C_mode_connection::ACT_UPLOAD_SENT:
         case C_mode_connection::ACT_UPLOAD_DRAFTS:
            StartUploadingMessageToImap((C_mode_connection_imap_upload&)mod);
            break;

         case C_mode_connection_in::ACT_IMAP_IDLE_UPDATE_FLAGS:
                              //just disconnect, it will update flags
            mod.action = mod.ACT_IMAP_IDLE;
            if(mod.folder==mod.acc.selected_folder && mod.msg_seq_map.is_synced){
                           //nothing to do for idling folder, just cleanup work
               ConnectionCleanupAndDisconnect(mod);
            }else{
               assert(0);
               //AfterImapLogin(mod);
            }
            break;

         case C_mode_connection_in::ACT_IMAP_IDLE_UPDATE_FLAGS_AND_DELETE:
            mod.action = mod.ACT_IMAP_IDLE;
            if(mod.folder==mod.acc.selected_folder && mod.msg_seq_map.is_synced){
               mod.AddDeletedToDeleteList();
               ConnectionCleanupAndDisconnect(mod);
            }else{
               assert(0);
               AfterImapLogin(mod);
            }
            break;

         case C_mode_connection_in::ACT_UPDATE_MAILBOX:
         case C_mode_connection_in::ACT_UPDATE_ACCOUNTS:
         case C_mode_connection_in::ACT_UPDATE_IMAP_FOLDERS:
                              //add freshly deleted dirty messages to delete list
            mod.AddDeletedToDeleteList();
                              //flow...
         default:
            AfterImapLogin(mod);
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_SET_FLAGS:
   case C_mode_connection_imap::ST_IMAP_MOVE_MESSAGES_SET_FLAGS:
      {
                              //wait until all these commands are finished
         if(mod.commands.size())
            break;
                              //continue in work
         if(cmd->state==mod.ST_IMAP_MOVE_MESSAGES_SET_FLAGS)
            Connection_MoveImapMessages(mod);
         else
            ConnectionCleanupAndDisconnect(mod);
      }
      break;

   default:
      assert(0);
   }
   return true;
}

//----------------------------

bool C_mail_client::ImapProcessNoTagged(C_mode_connection_imap &mod, const char *cp, C_mode_connection_imap::C_command *cmd, Cstr_w &err){

                              //report errors on tagged negative responses
   switch(cmd->state){
   case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE_CREATE_FOLDER:
   case C_mode_connection_imap::ST_IMAP_DELETE_MESSAGES:
      return ImapProcessOkTagged(mod, cp, cmd, err);

   case C_mode_connection_imap::ST_IMAP_ID:
      ImapLogin(mod);
      break;

   case C_mode_connection_imap::ST_IMAP_STARTTLS:
      break;

   case C_mode_connection_imap::ST_IMAP_COMPRESS:
                              //compression not enabled, continue normally
      AfterImapLogin(mod);
      break;

   case C_mode_connection_imap::ST_IMAP_MOVE_MESSAGES_TO_THRASH:
      {
         text_utils::SkipWS(cp);
         if(text_utils::CheckStringBegin(cp, "[trycreate]")){
            C_mode_connection_imap::C_command_move_to_folder *cmd_move = (C_mode_connection_imap::C_command_move_to_folder*)cmd;

                              //failed to upload message, try to create the mailbox
            ConnectionDrawAction(mod, GetText(TXT_CREATING_IMAP_FOLDER));
            cmd_move->AddRef();
            cmd_move->phase = cmd_move->PHASE_CREATE;
            Cstr_c s;
            s<<"CREATE " <<cmd_move->enc_folder_name;
            SendImapCommand(mod, s, cmd_move);
         }else{
                              //some other failure, go on without moving messages
            //StartDeletingMessages(mod, false);
            err = L"Can't move messages to Trash. Check Trash folder name in account configuration, and make sure that such folder exists on server.";
         }
      }
      break;

   case C_mode_connection_imap::ST_IMAP_MOVE_TO_FOLDER_BY_RULE:
      {
                              //try to create the folder
         C_mode_connection_imap::C_command_move_to_folder *cmd_move = (C_mode_connection_imap::C_command_move_to_folder*)cmd;
         if(cmd_move->phase == cmd_move->PHASE_MOVE){
            cmd_move->phase = cmd_move->PHASE_CREATE;
            Cstr_c s;
            s<<"CREATE " <<cmd_move->enc_folder_name;
            cmd_move->AddRef();
            SendImapCommand(mod, s, cmd_move);
         }else
            err.Format(L"Can't move to IMAP folder \"%\".\n") <<cmd_move->folder_name;
      }
      break;

   case C_mode_connection_imap::ST_IMAP_SEARCH_SINCE:
      {
         mod.capability |= mod.CAPS_NO_SEARCH;
         ConnectionImapSelectFolder(mod);
         //err = L"This server doesn't support searching messages by date.";
      }
      break;

   case C_mode_connection_imap::ST_IMAP_GET_HDR:
                              //behave as OK, reality shows that server may return NO for single message, while other were retrieved successfully
      return ImapProcessOkTagged(mod, cp, cmd, err);

   case C_mode_connection_imap::ST_IMAP_CREATE_FOLDER:
      {
         C_mode_connection_imap::C_command_create *cmd_create = (C_mode_connection_imap::C_command_create*)cmd;
         if(cmd_create->in_list)
            return ImapProcessOkTagged(mod, cp, cmd, err);
      }
                              //flow...
   default:
      text_utils::SkipWS(cp);
      err.Copy(cp);
      mod.acc.socket = NULL;
      return false;
   }
   return true;
}

//----------------------------

void C_mail_client::ImapProcessExists(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd0, dword num_msgs){

   switch(cmd0->state){
   case C_mode_connection_imap::ST_IMAP_SELECT:
      {
         C_mode_connection_imap::C_command_select *cmd_sel = (C_mode_connection_imap::C_command_select*)cmd0;
         cmd_sel->num_msgs = num_msgs;
      }
      break;
   case C_mode_connection_imap::ST_IMAP_IDLE:
      if(mod.msg_seq_map.GetMaxSeqNum()!=num_msgs){
         C_mode_connection_imap::C_command_idle *cmd_idle = (C_mode_connection_imap::C_command_idle*)cmd0;
         if(!cmd_idle->done_sent){
                           //stop IDLE mode
            ImapIdleSendDone(mod);
         }
      }
      break;
   }
}

//----------------------------

void C_mail_client::ImapIdleUpdateFlags(C_mode_connection_imap &mod, bool also_delete){

   if(mod.commands.size()==1 && mod.commands.front()->state==C_mode_connection_imap::ST_IMAP_IDLE){
      mod.action = also_delete ? mod.ACT_IMAP_IDLE_UPDATE_FLAGS_AND_DELETE : mod.ACT_IMAP_IDLE_UPDATE_FLAGS;

      ImapIdleSendDone(mod);
   }
}

//----------------------------

void C_mail_client::ImapIdleSendDone(C_mode_connection_imap &mod){

   if(mod.commands.size()==1 && mod.commands.front()->state==C_mode_connection_imap::ST_IMAP_IDLE){
      ((C_mode_connection_imap::C_command_idle*)(C_mode_connection_imap::C_command*)mod.commands.front())->done_sent = true;
      const char *s = "DONE\r\n";
      if(mod.capability&mod.CAPS_IN_COMPRESSION)
         SendCompressedData(mod.acc.socket, s, 6, mod.compress_out);
      else
         mod.SocketSendCString(s);

      mod.acc.socket->SetTimeout(0);
      ConnectionUpdateState(mod.acc, S_account::UPDATE_WORKING);
   }else
      assert(0);
}

//----------------------------

void C_mail_client::ImapProcessExpunge(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd0, dword msg_seq_no){

   dword uid = mod.msg_seq_map.FindAndRemoveUid(msg_seq_no);
   if(uid!=0xffffffff){
      C_vector<S_message> &messages = mod.GetMessages();
      for(int i=messages.size(); i--; ){
         if(messages[i].imap_uid==uid){
            {
               S_message &msg = messages[i];
               mod.GetContainer().DeleteMessageFiles(mail_data_path, msg);
               if(!msg.IsRead())
                  home_screen_notify.RemoveMailNotify(mod.acc, *mod.folder, msg);
            }
            messages.remove_index(i);
            mod.GetContainer().MakeDirty();

            for(C_mode *m=mode; m; m=m->GetParent()){
               switch(m->Id()){
               case C_mode_mailbox::ID:
                  {
                     C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*m;
                     if(&mod_mbox.acc==&mod.acc){
                        if(mod_mbox.selection>i)
                           --mod_mbox.selection;
                        Mailbox_RecalculateDisplayArea(mod_mbox);
                     }
                  }
                  break;
               }
            }
            UpdateUnreadMessageNotify();
            break;
         }
      }
      //if(mod.IsImapIdle())
      {
                              //refresh mailbox
         ImapIdleUpdateAfterOperation(mod, true);
      }
   }//else
      //assert(mod.action==mod.ACT_IMAP_PURGE);
}

//----------------------------

S_message *C_mail_client::C_mode_connection::FindImapMessage(dword imap_uid){

   C_vector<S_message> &messages = GetMessages();
   for(int i=messages.size(); i--; ){
      S_message &msg = messages[i];
      if(msg.imap_uid==imap_uid)
         return &msg;
   }
   return NULL;
}

//----------------------------

bool C_mail_client::ProcessLineImap(C_mode_connection_imap &mod, const C_socket::t_buffer &line, Cstr_w &err){

   if(mod.multiline.string_size){
      return ImapProcessMultiLine(mod, line, err);
   }
   const char *cp = line.Begin();

   C_mode_connection_imap::C_command *cmd0 = mod.commands.size() ? (C_mode_connection_imap::C_command*)mod.commands.front() : NULL;
   C_mode_connection_imap::E_STATE state = cmd0 ? cmd0->state : mod.ST_NULL;
   if(*cp=='+'){
                        //command continuation request
      switch(state){
      case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE:
         {
            C_mode_connection_imap_upload &mod_up = (C_mode_connection_imap_upload&)mod;
            if(mod.capability&mod.CAPS_IN_COMPRESSION){
               SendCompressedData(mod.acc.socket, mod_up.curr_header.begin(), mod_up.curr_header.size(), mod.compress_out);
            }else{
               mod_up.curr_header.push_back(0);
               mod.SocketSendCString(mod_up.curr_header.begin());
            }
            mod_up.curr_header.clear();
            mod_up.char_index = 0;
            mod.state = mod.ST_IMAP_UPLOAD_MESSAGE_SEND;
         }
         break;
      case C_mode_connection_imap::ST_IMAP_LOGIN:
         {
                        //send password
            Cstr_c s;
            s<<mod.acc.password <<"\r\n";
            mod.SocketSendString(s, 0, true);
         }
         break;
      case C_mode_connection_imap::ST_IMAP_IDLE:
         {
            ConnectionUpdateState(mod.acc, S_account::UPDATE_IDLING);
            mod.acc.socket->SetTimeout(-1);
                              //process possible pending IDLE data on the socket
            ProcessImapIdle(mod.acc, SOCKET_DATA_RECEIVED);
         }
         return false;
         /*
      case C_mode_connection_imap::ST_IMAP_GET_HDR:   //not working on servers
         {
            C_mode_connection_imap::C_command_get_hdr *cmd = (C_mode_connection_imap::C_command_get_hdr*)cmd0;
            assert(cmd->params.Length());
            mod.SocketSendString(cmd->params);
         }
         break;
         */
      default:
         assert(0);
      }
      return true;
   }

   bool fail = false;

   if(*cp=='*'){
                        //untagged response
      ++cp;
      text_utils::SkipWS(cp);
      if(text_utils::CheckStringBegin(cp, "ok")){
                        //success
         return ImapProcessOkUntagged(mod, cp, state, err);
      }else
      if(text_utils::CheckStringBegin(cp, "bad")){
         if(mod.commands.size() && cmd0->state==C_mode_connection_imap::ST_IMAP_GET_HDR){
                              //ignore
         }else{
            text_utils::SkipWS(cp);
            fail = true;
         }
      }else
      if(text_utils::CheckStringBegin(cp, "no")){
         //return ImapProcessNo(mod, cp, state, false, err);
      }else
      if(text_utils::CheckStringBegin(cp, "bye")){
         //RemoveDeletedMessages(mod);
         ConnectionDisconnect(mod);
         return false;
      }else
      if(text_utils::CheckStringBegin(cp, "preauth")){
                        //logged-in automatically, continue in work
         ConnectionImapSelectFolder(mod);
      }else
      if(text_utils::CheckStringBegin(cp, "capability")){
         ImapProcessCapability(mod, cp);
      }else
      if(text_utils::CheckStringBegin(cp, "flags")){
                           //during SELECT
      }else
      if(text_utils::CheckStringBegin(cp, "search")){
         if(mod.commands.size() && cmd0->state==C_mode_connection_imap::ST_IMAP_SEARCH_SINCE){
            C_mode_connection_imap::C_command_search_since_date *cmd_s = (C_mode_connection_imap::C_command_search_since_date*)cmd0;
            while(*cp){
               text_utils::SkipWS(cp);
               dword uid;
               if(!text_utils::ScanDecimalNumber(cp, (int&)uid))
                  break;
               cmd_s->seqs.push_back(uid);
            }
         }else
            assert(0);
      }else
      if(text_utils::CheckStringBegin(cp, "list")){
         if(mod.commands.size()){
            switch(cmd0->state){
            case C_mode_connection_imap::ST_IMAP_LIST:
               if(!ImapProcessList(mod, (C_mode_connection_imap::C_command_list*)cmd0, cp))
                  fail = true;
               break;
            case C_mode_connection_imap::ST_IMAP_CREATE_FOLDER:
               {
                  C_mode_connection_imap::C_command_create *cmd_create = (C_mode_connection_imap::C_command_create*)cmd0;
                  cmd_create->ParseFlags(cp);
               }
               break;
            default: assert(0);
            }
         }
      }else{
         int n;
         if(text_utils::ScanDecimalNumber(cp, n)){
            text_utils::SkipWS(cp);
            Cstr_c kw;
            if(text_utils::ReadToken(cp, kw, " \t\n")){
               text_utils::SkipWS(cp);
               kw.ToLower();
               if(kw=="exists"){
                  if(cmd0)
                     ImapProcessExists(mod, cmd0, n);
                  else assert(0);
               }else
               if(kw=="expunge"){
                  if(cmd0)
                     ImapProcessExpunge(mod, cmd0, n);
                  else assert(0);
               }else
               if(kw=="fetch"){
                  if(*cp!='('){
                     fail = true;
                  }else{
                     ++cp;
                     if(cmd0){
                        switch(cmd0->state){
                        case C_mode_connection_imap::ST_IMAP_GET_UIDS:
                           {
                              C_mode_connection_imap::C_command_get_uids *cmd_uids = (C_mode_connection_imap::C_command_get_uids*)cmd0;
                              cmd_uids->seq_num = n;
                              mod.headers.push_back(S_message_header_base());
                              if(!ParseImapFetch(mod, cmd0, cp))
                                 fail = true;
                           }
                           break;

                        case C_mode_connection_imap::ST_IMAP_GET_HDR:
                           {
                                       //start getting new header
                              C_mode_connection_imap::C_command_get_hdr *cmd_1 = (C_mode_connection_imap::C_command_get_hdr*)cmd0;
                              cmd_1->Reset();
                              cmd_1->seq_num = n;
                              if(!ParseImapFetch(mod, cmd_1, cp))
                                 fail = true;
                           }
                           break;

                        case C_mode_connection_imap::ST_IMAP_GET_BODY:
                           if(mod.commands.size()==1){
                              C_mode_connection_imap::C_command_get_body *cmd_body = (C_mode_connection_imap::C_command_get_body*)cmd0;
                                                   //start single fetch
                              cmd_body->Reset();
                              cmd_body->got_fetch = true;
                              cmd_body->seq_num = n;

                              switch(cmd_body->phase){
                              case C_mode_connection_imap::C_command_get_body::PHASE_GET_HDRS:
                                                   //store message sequence - add mime header
                                 cmd_body->mime_headers.push_back(C_mode_connection_imap::C_command_get_body::S_mime_header()).msg_sequence_number = n;
                                 break;
                              case C_mode_connection_imap::C_command_get_body::PHASE_GET_BODY_TEXT:
                                                   //fill in mime type
                                 {
                                    int i;
                                    for(i=cmd_body->mime_headers.size(); i--; ){
                                       const C_mode_connection_imap::C_command_get_body::S_mime_header &mime = cmd_body->mime_headers[i];
                                       if(mime.msg_sequence_number==n){
                                          cmd_body->retrieved_header.content = mime.content;
                                          cmd_body->retrieved_header.content_encoding = mime.content_encoding;
                                          cmd_body->retrieved_header.text_coding = mime.coding;
                                          cmd_body->retrieved_header.format_flowed = mime.format_flowed;
                                          cmd_body->retrieved_header.format_delsp = mime.format_delsp;
                                          if(mime.content.type==CONTENT_MULTIPART){
                                             cmd_body->CreateMultiPartInfo();
                                             cmd_body->multi_part->boundary = mime.multipart_boundary;
                                          }
                                          cmd_body->got_header = true;
                                          break;
                                       }
                                    }
                                    assert(i!=-1);
                                 }
                                          //flow...
                              case C_mode_connection_imap::C_command_get_body::PHASE_GET_BODY_FULL:
                                          //prepare for downloading message
                                 cmd_body->start_progress_pos = mod.progress.pos;
                                 if(mod.num_get_bodies){
                                          //draw progress
                                    ++mod.get_bodies_index;
                                    Cstr_w s;
                                    s<<GetText(TXT_PROGRESS_GETTING_BODY) <<L' ' <<mod.get_bodies_index <<L'/' <<mod.num_get_bodies;
                                    ConnectionDrawAction(mod, s);
                                 }
                                 break;
                              }
                              if(!ParseImapFetch(mod, cmd0, cp)){
                                 //fail = true;
                              }
                           }else
                              assert(0);
                           break;

                        case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
                           if(mod.commands.size()==1){
                              C_mode_connection_imap::C_command_get_attachment_body *cmd_body = (C_mode_connection_imap::C_command_get_attachment_body*)cmd0;
                              cmd_body->got_fetch = true;
                              cmd_body->uid = 0;
                              cmd_body->seq_num = n;
                              cmd_body->got_flags = false;
                              if(!ParseImapFetch(mod, cmd0, cp))
                                 fail = true;
                           }
                           break;

                        case C_mode_connection_imap::ST_IMAP_GET_MSG_HEADERS:
                           if(mod.commands.size()==1){
                              C_mode_connection_imap::C_command_get_msg_headers *cmd_body = (C_mode_connection_imap::C_command_get_msg_headers*)cmd0;
                              cmd_body->got_fetch = true;
                              cmd_body->seq_num = n;
                              cmd_body->got_flags = false;
                              if(!ParseImapFetch(mod, cmd0, cp))
                                 fail = true;
                           }
                           break;

                        default:
                           if(mod.msg_seq_map.is_synced){
                              if(cp[StrLen(cp)-1]!='}'){
                                 dword uid = mod.msg_seq_map.FindUid(n);
                                 if(uid!=0xffffffff){
                                                                  //work on temp command, don't process multiline
                                    C_mode_connection_imap::C_command_fetch_sync cmd;
                                    cmd.curr_msg = mod.FindImapMessage(uid);
                                    if(cmd.curr_msg){
                                       ParseImapFetch(mod, &cmd, cp);
                                    }
                                 }
                              }else
                                 assert(0);
                           }
                        }
                     }else{
                        assert(0);
                     }
                  }
               }else
#ifdef _DEBUG
               if(kw=="recent"){
               }else
#endif
                  assert(0);
            }else{
               err = GetText(TXT_ERR_CONNECT_FAIL);
               mod.acc.socket = NULL;
               return false;
            }
            /*
         }else{
            err = GetText(TXT_ERR_CONNECT_FAIL);
            mod.acc.socket = NULL;
            */
         }
      }
   }else{
                        //should be tag ID
      int tag;
      if(text_utils::ScanDecimalNumber(cp, tag)){
                              //find which command it is
         int i;
         for(i=mod.commands.size(); i--; ){
            if(mod.commands[i]->tag_id==tag)
               break;
         }
         if(i==-1){
            assert(0);
            return true;
         }
         C_smart_ptr<C_mode_connection_imap::C_command> cmd = mod.commands[i];
         mod.commands.remove_index(i);

         text_utils::SkipWS(cp);
         if(text_utils::CheckStringBegin(cp, "ok")){
            return ImapProcessOkTagged(mod, cp, cmd, err);
         }else
         if(text_utils::CheckStringBegin(cp, "bad")){
            switch(cmd->state){
            case C_mode_connection_imap::ST_IMAP_COMPRESS:
               AfterImapLogin(mod);
               break;
            case C_mode_connection_imap::ST_IMAP_ID:
               ImapLogin(mod);
               break;
            case C_mode_connection_imap::ST_IMAP_IDLE:
               cp = "IMAP IDLE not supported. Disable Push email option for this account.";
               fail = true;
               break;
            case C_mode_connection_imap::ST_IMAP_LOGIN:
               if(!mod.acc.password.Length()){
                  cp = "Your login password is empty, set the password in Account settings.";
               }else
                  text_utils::SkipWS(cp);
               fail = true;
               break;
            case C_mode_connection_imap::ST_IMAP_GET_HDR:
               {
                              //probably "command too long" error, try with optimized uid set
                  C_mode_connection_imap::C_command_get_hdr *cmd_hdr = (C_mode_connection_imap::C_command_get_hdr*)(C_mode_connection_imap::C_command*)cmd;
                  if(!cmd_hdr->optimized_uid_range && mod.acc.imap_last_x_days_limit)
                     StartGettingNewHeadersIMAP(mod, true);
                  else{
                     text_utils::SkipWS(cp);
                     fail = true;
                  }
               }
               break;
            default:
               text_utils::SkipWS(cp);
               fail = true;
            }
         }else
         if(text_utils::CheckStringBegin(cp, "no")){
            return ImapProcessNoTagged(mod, cp, cmd, err);
         }else{
            fail = true;
            assert(0);
         }
      }else{
         //fail = true;
                              //unknown command, ignore
      }
   }
   if(fail){
                        //failed, display error (copy response line)
      err.Copy(cp);
      mod.acc.socket = NULL;
      return false;
   }
   return true;
}

//----------------------------

void C_mail_client::StartMoveMessagesByRuleIMAP(C_mode_connection_imap &mod){

                              //one command for each folder move
   C_vector<C_mode_connection_imap::C_command_move_to_folder*> cmds;

                              //find all movable headers
   for(int i=mod.headers_to_move.size(); i--; ){
      const C_mode_connection_imap::S_message_header_imap_move &hdr = mod.headers_to_move[i];
      int j;
      for(j=cmds.size(); j--; ){
         if(cmds[j]->folder_name==hdr.move_folder_name)
            break;
      }
      if(j==-1){
         j = cmds.size();
         C_mode_connection_imap::C_command_move_to_folder *cmd = new(true) C_mode_connection_imap::C_command_move_to_folder(mod.ST_IMAP_MOVE_TO_FOLDER_BY_RULE);
         cmds.push_back(cmd);
         cmd->folder_name = hdr.move_folder_name;
         cmd->enc_folder_name = EncodeImapFolderName(hdr.move_folder_name);
      }
      cmds[j]->uids.push_back(hdr.imap_uid);
   }
   mod.headers_to_move.clear();
                              //start moving messages
   ConnectionDrawTitle(mod, GetText(TXT_MOVING_MESSAGES));
   for(int i=0; i<cmds.size(); i++){
      C_mode_connection_imap::C_command_move_to_folder *cmd = cmds[i];
      Cstr_c range = MakeImapSequenceString(cmd->uids);
      cmd->imap_cmd<<"UID COPY " <<range <<' ' <<cmd->enc_folder_name;
      SendImapCommand(mod, cmd->imap_cmd, cmd);
   }
}

//----------------------------

void C_mail_client::StartRetrievingMessageMarkedBodiesIMAP(C_mode_connection_imap &mod, const C_vector<dword> &msg_indexes){

   mod.headers_to_download.clear();

   C_vector<S_message> &messages = mod.GetMessages();
   C_mode_connection_imap::C_command_get_body *cmd = new(true) C_mode_connection_imap::C_command_get_body;
   for(int i=0; i<3; i++)
      cmd->uids_text[i].reserve(msg_indexes.size());
   cmd->uids_full.reserve(msg_indexes.size());
                              //convert message indexes into uids
   mod.progress.total = 0;

   for(int i=0; i<msg_indexes.size(); i++){
      const S_message &msg = messages[msg_indexes[i]];
      if(!(mod.acc.flags&S_account::ACC_IMAP_DOWNLOAD_ATTACHMENTS) && (msg.flags&msg.MSG_HAS_ATTACHMENTS) && !(msg.flags&msg.MSG_DRAFT)){
         dword text_subpart = (msg.flags>>msg.MSG_TEXT_PART_TYPE_SHIFT)&3;
         assert(text_subpart);
         if(!text_subpart) ++text_subpart;      //compatibility with already present headers; get them as part "1" (may fail on some servers since IMAP forbits to get "multipart/*" part body this way
         cmd->uids_text[text_subpart-1].push_back(msg.imap_uid);
         mod.progress.total += msg.imap_text_part_size;
      }else{
         cmd->uids_full.push_back(msg.imap_uid);
         mod.progress.total += msg.size;
      }
   }
   for(int i=0; i<3; i++)
      cmd->uids_text[i].resize(cmd->uids_text[i].size());
   cmd->uids_full.resize(cmd->uids_full.size());
                              //send command to get them all

   if(cmd->uids_text[0].size() || cmd->uids_text[1].size() || cmd->uids_text[2].size()){
      StartRetrievingBodiesTextIMAP(mod, cmd);
   }else
   if(cmd->uids_full.size())
      StartRetrievingBodiesFullIMAP(mod, cmd);
   ConnectionDrawProgress(mod);

   ConnectionDrawAction(mod, GetText(TXT_PROGRESS_GETTING_BODY));
}

//----------------------------
const int MAX_MSGS_FOR_CMD = 100;


//----------------------------

void C_mail_client::StartRetrievingBodiesTextIMAP(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_get_body *cmd){

                        //download without attachments - send command to get mime headers
   cmd->curr_text_subpart = 0;
   if(!cmd->uids_text[0].size()){
      ++cmd->curr_text_subpart;
      if(!cmd->uids_text[1].size()){
         ++cmd->curr_text_subpart;
         assert(cmd->uids_text[2].size());
      }
   }
   C_vector<dword> &uids = cmd->uids_text[cmd->curr_text_subpart];
   int n = Min(uids.size(), MAX_MSGS_FOR_CMD);
   assert(n);
   cmd->curr_uid_range.clear(); cmd->curr_uid_range.insert(cmd->curr_uid_range.end(), uids.begin(), uids.begin()+n);
   uids.erase(uids.begin(), uids.begin()+n);
   Cstr_c s;
   s.Format("UID FETCH % (body.peek[%.mime])") <<MakeImapSequenceString(cmd->curr_uid_range) <<subpart_names[cmd->curr_text_subpart];
   cmd->mime_headers.reserve(n);
   cmd->phase = cmd->PHASE_GET_HDRS;
   SendImapCommand(mod, s, cmd);
}

//----------------------------

void C_mail_client::StartRetrievingBodiesFullIMAP(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_get_body *cmd){

                           //simple peek of entire body
   int n = Min(cmd->uids_full.size(), MAX_MSGS_FOR_CMD);
   cmd->curr_uid_range.clear(); cmd->curr_uid_range.insert(cmd->curr_uid_range.end(), cmd->uids_full.begin(), cmd->uids_full.begin()+n);
   cmd->uids_full.erase(cmd->uids_full.begin(), cmd->uids_full.begin()+n);
   Cstr_c s;
   s.Format("UID FETCH % (UID body.peek[])") <<MakeImapSequenceString(cmd->curr_uid_range);
   //if(partial_body)
      //s.AppendFormat("<0.%>") <<(mod.acc.max_kb_to_retrieve*1024);
   cmd->phase = cmd->PHASE_GET_BODY_FULL;
   cmd->got_fetch = false;
   SendImapCommand(mod, s, cmd);
}

//----------------------------

void C_mail_client::BeginRetrieveMessageImap(C_mode_connection_imap &mod, dword msg_index){

   C_vector<dword> indexes;
   indexes.push_back(msg_index);
   StartRetrievingMessageMarkedBodiesIMAP(mod, indexes);
}

//----------------------------

void C_mail_client::BeginRetrieveMsgHeadersImap(C_mode_connection_imap &mod){

   const S_message &msg = mod.GetMessages()[mod.params.message_index];
   Cstr_c s;
   s.Format("UID FETCH % (body.peek[header])") <<msg.imap_uid;
   SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command_get_msg_headers(mod.params.message_index));
}

//----------------------------

bool C_mail_client::StartRetrievingMessageAttachment(C_mode_connection_imap &mod){

   mod.progress.pos = 0;
   const S_message &msg = mod.GetMessages()[mod.params.message_index];
   if(mod.action==C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL){
                              //count total size of non-downloaded attachments
      mod.progress.total = 0;
      for(dword i=0; i<msg.attachments.Size(); i++){
         const S_attachment &att = msg.attachments[i];
         if(!att.IsDownloaded())
            mod.progress.total += att.file_size;
      }
   }else{
      mod.progress.total = msg.attachments[mod.params.attachment_index].file_size;
   }
   return RetrieveNextMessageAttachment(mod);
}

//----------------------------

bool C_mail_client::RetrieveNextMessageAttachment(C_mode_connection_imap &mod){

   int attachment_index;
   if(mod.action==C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL){
                              //count total size of non-downloaded attachments
      const S_message &msg = mod.GetMessages()[mod.params.message_index];
      attachment_index = -1;
      for(dword i=0; i<msg.attachments.Size(); i++){
         const S_attachment &att = msg.attachments[i];
         if(!att.IsDownloaded()){
            attachment_index = i;
            break;
         }
      }
      if(attachment_index==-1)
         return false;
   }else{
      attachment_index = mod.params.attachment_index;
   }

   ConnectionDrawTitle(mod, GetText(TXT_DOWNLOAD));
   assert(mod.IsImap());
   C_vector<S_message> &messages = mod.GetMessages();
   const S_message &msg = messages[mod.params.message_index];
   const S_attachment &att = msg.attachments[attachment_index];

   ConnectionDrawAction(mod, att.suggested_filename);

   dword part_index = att.part_index!=0xffff ? att.part_index : attachment_index+2;
   Cstr_c s;
   s.Format("UID FETCH % (UID body.peek[%])") <<msg.imap_uid <<part_index;
   C_mode_connection_imap::C_command_get_attachment_body *cmd = new(true) C_mode_connection_imap::C_command_get_attachment_body(mod.params.message_index, attachment_index);
   cmd->retrieved_header.content_encoding = att.content_encoding;
   SendImapCommand(mod, s, cmd);

   Cstr_w mail_path = mod.GetContainer().GetMailPath(mail_data_path);
   Cstr_w ext = text_utils::GetExtension(att.suggested_filename);
   ext.ToLower();
   Cstr_w filename;
   file_utils::MakeUniqueFileName(mail_path, filename, ext);
   Cstr_w full_name;
   full_name<<mail_path <<filename;
   C_file::MakeSurePathExists(full_name);
   cmd->att_saving.Start(full_name);

   ConnectionDrawProgress(mod);
   return true;
}

//----------------------------

bool C_mail_client::StartUpdatingServerFlags(C_mode_connection_imap &mod, C_mode_connection_imap::E_STATE set_flags_state){

                              //find which flags are to be updated
   C_vector<S_message> &msgs = mod.GetMessages();
   C_vector<dword> change_flags[2][5];    //[0=clear, 1=set][0=read, 1=replied, 2=flagged, 3=deleted, 4=forwarded]
                              //collect messages with same changes
   for(int i=msgs.size(); i--; ){
      S_message &msg = msgs[i];
      if(msg.flags&msg.MSG_SERVER_SYNC){
         dword uid = msg.imap_uid;
         if(msg.flags&msg.MSG_IMAP_READ_DIRTY){
            change_flags[bool(msg.flags&msg.MSG_READ)][0].push_back(uid);
            msg.flags &= ~msg.MSG_IMAP_READ_DIRTY;
            mod.GetContainer().MakeDirty();
         }
         if(msg.flags&msg.MSG_IMAP_REPLIED_DIRTY){
            change_flags[bool(msg.flags&msg.MSG_REPLIED)][1].push_back(uid);
            msg.flags &= ~msg.MSG_IMAP_REPLIED_DIRTY;
            mod.GetContainer().MakeDirty();
         }
         if(msg.flags&msg.MSG_IMAP_FORWARDED_DIRTY){
            change_flags[bool(msg.flags&msg.MSG_FORWARDED)][4].push_back(uid);
            msg.flags &= ~msg.MSG_IMAP_FORWARDED_DIRTY;
            mod.GetContainer().MakeDirty();
         }
         if(msg.flags&msg.MSG_IMAP_FLAGGED_DIRTY){
            change_flags[bool(msg.flags&msg.MSG_FLAGGED)][2].push_back(uid);
            msg.flags &= ~msg.MSG_IMAP_FLAGGED_DIRTY;
            mod.GetContainer().MakeDirty();
         }
         if((msg.flags&msg.MSG_DELETED_DIRTY) && !(msg.flags&msg.MSG_DELETED)){
                              //Deleted flag may be only cleared this way
            change_flags[0][3].push_back(uid);
            msg.flags &= ~msg.MSG_DELETED_DIRTY;
            mod.GetContainer().MakeDirty();
         }
      }
   }
   bool any_cmd_set = false;
   for(int ci=0; ci<2; ci++){
      for(int fi=0; fi<5; fi++){
         const C_vector<dword> &lst = change_flags[ci][fi];
         if(lst.size()){
                              //update same flags on all messages
            Cstr_c cmd = "UID STORE ";
            for(int i=lst.size(); i--; ){
               cmd<<lst[i];
               if(i)
                  cmd<<",";
            }
            static const char *const flag_name[] = {
               "\\Seen", "\\Answered", "\\Flagged", "\\Deleted", "$Forwarded"
            };
            //static const dword flgs_dirty[] = { S_message::MSG_IMAP_READ_DIRTY, S_message::MSG_IMAP_REPLIED_DIRTY, S_message::MSG_IMAP_FLAGGED_DIRTY, };
            //static const dword flgs[] = { S_message::MSG_READ, S_message::MSG_REPLIED, S_message::MSG_FLAGGED };

            cmd<<' ' <<(!ci ? '-' : '+') <<"flags.silent (" <<flag_name[fi] <<')';
            SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command(set_flags_state));

            any_cmd_set = true;
         }
      }
   }
   if(any_cmd_set)
      ConnectionDrawAction(mod, GetText(TXT_PROGRESS_UPDATING_FLAGS));
   return any_cmd_set;
}

//----------------------------

void C_mail_client::StartDeletingMessagesImap(C_mode_connection_imap &mod, bool allow_imap_thrash_move){

   C_vector<S_message> &msgs = mod.GetMessages();
   if(mod.acc.imap_trash_folder.Length() && allow_imap_thrash_move && mod.folder && mod.acc.move_to_trash &&
      mod.acc.GetFullFolderName(*mod.folder)!=mod.acc.GetTrashFolderName()
      ){
                              //check which deleted messages have dirty flag
      C_vector<dword> move_uids;
      for(int i=mod.msgs_to_delete.size(); i--; ){
         dword uid = mod.msgs_to_delete[i];
         for(int j=msgs.size(); j--; ){
            S_message &msg = msgs[j];
            if(msg.imap_uid==uid){
               if(msg.flags&msg.MSG_DELETED_DIRTY){
                              //mark seen first
                  if(!(msg.flags&msg.MSG_READ)){
                     msg.flags |= msg.MSG_READ | msg.MSG_IMAP_READ_DIRTY;
                     mod.GetContainer().MakeDirty();
                  }
                  move_uids.push_back(uid);
               }
               break;
            }
         }
      }
      if(move_uids.size()){
                              //first need to update flags, in case that moved messages have them changed
         if(StartUpdatingServerFlags((C_mode_connection_imap&)mod))
            return;

         C_mode_connection_imap::C_command_move_to_folder *cmd_move = new(true) C_mode_connection_imap::C_command_move_to_folder(C_mode_connection_imap::ST_IMAP_MOVE_MESSAGES_TO_THRASH);
         cmd_move->uids = move_uids;

         cmd_move->enc_folder_name = EncodeImapFolderName(mod.acc.GetTrashFolderName());
         cmd_move->imap_cmd<<"UID COPY " <<MakeImapSequenceString(move_uids) <<' ' <<cmd_move->enc_folder_name;
         SendImapCommand(mod, cmd_move->imap_cmd, cmd_move);

         Cstr_w s;
         s<<GetText(TXT_MOVING) <<L" (" <<mod.acc.imap_trash_folder <<')';
         ConnectionDrawAction(mod, s);
         return;
      }
   }
                        //set all msgs as deleted
   Cstr_c cmd;
   cmd <<"UID STORE " <<MakeImapSequenceString(mod.msgs_to_delete) <<" +flags.silent (\\Deleted)";
   SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_DELETE_MESSAGES));
   mod.need_expunge = true;
   /*
                           //clear delete dirty flags
   for(int i=msgs.size(); i--; ){
      S_message &msg = msgs[i];
      if(msg.flags&msg.MSG_DELETED_DIRTY){
         msg.flags &= ~msg.MSG_DELETED_DIRTY;
         mod.GetContainer().MakeDirty();
      }
   }
   */
   Cstr_w s;
   s<<GetText(TXT_PROGRESS_DELETING) <<' ' <<mod.msgs_to_delete.size();
   ConnectionDrawAction(mod, s);
}

//----------------------------

void C_mail_client::ConnectionExpungeIMAP(C_mode_connection_imap &mod){

   SendImapCommand(mod, "EXPUNGE", new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_EXPUNGE), 5000);
}

//----------------------------

bool C_mail_client::ConnectionImapFolderClose(C_mode_connection_imap &mod){

   if(mod.acc.selected_folder){

      if(mod.need_expunge && config_mail.imap_auto_expunge){
         mod.need_expunge = false;
         mod.acc.selected_folder = NULL;
         SendImapCommand(mod, "CLOSE", new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_CLOSE));
         return true;
      }
      if(mod.action==mod.ACT_UPDATE_ACCOUNTS || mod.action==mod.ACT_UPDATE_IMAP_FOLDERS){
         if(!mod.acc.selected_folder->IsInbox())
            mod.acc.selected_folder = NULL;
      }
   }
   if(mod.folder && mod.folder!=mod.acc.selected_folder)
      mod.folder->SaveAndUnloadMessages(mail_data_path);
                              //no folder selected, or closing not needed, continue
                              //find next folder for update
   C_message_container *fld = FindImapFolderForUpdate(mod.acc, mod.folder, mod.params.imap_update_hidden);
   if(!fld){
                        //no more folders, disconnect
      ConnectionDisconnect(mod);
      return false;
   }
   ConnectionClearProgress(mod);
   mod.folder = fld;
   ConnectionImapSelectFolder(mod);
   return true;
}

//----------------------------

void C_mail_client::C_mode_connection_imap::C_command_list::AddFolder(const char *name, bool need_decode){

   //if(flags&FLAG_NOSELECT)
      //return;
   Cstr_c tmp;
   if(need_decode){
                              //name was sent in encoded form
      tmp = DecodeImapFolderName(name).ToUtf8();
      name = tmp;
   }
                              //store folder into hierarchy
   Cstr_c n = name;
   C_vector<S_folder> *hr = &folders;
   while(n.Length()){
      Cstr_c fn = n;
      int di = n.Find(delimiter);
      if(di!=-1){
         fn = n.Left(di);
         n = n.RightFromPos(di+1);
      }else
         n.Clear();
                              //find such folder
      int i;
      for(i=hr->size(); i--; ){
         S_folder &f = (*hr)[i];
         if(f.name==fn){
            if(!n.Length()){
                              //leaf, store flags
               f.flags = flags;
            }
            hr = &f.subfolders;
            break;
         }
      }
      if(i==-1){
                              //add such folder
         S_folder &f = hr->push_back(S_folder());
         if(!n.Length()){
                              //leaf, store flags
            f.flags = flags;
         }
         f.name = fn;
         hr = &f.subfolders;
      }
   }
}

//----------------------------

int CompareStrings(const void *a1, const void *a2, void *context);

//----------------------------

static dword GenerateMsgId(C_vector<dword> &folder_ids){
                              //generate unused msg id
   dword min_id = folder_ids.size()+1;
   if(min_id!=1){
      if(folder_ids.front()!=1){
         min_id = 1;
         folder_ids.push_front(1);
      }else{
         int insert_pos = folder_ids.size();
         for(int i=folder_ids.size()-1; i--; ){
            if(folder_ids[i]+1 != folder_ids[i+1]){
               min_id = folder_ids[i]+1;
               insert_pos = i+1;
            }
         }
         folder_ids.insert(folder_ids.begin()+insert_pos, min_id);
      }
   }else
      folder_ids.push_back(1);
   return min_id;
}

//----------------------------

void C_mail_client::CleanFoldersHierarchy(C_mode_connection_imap &mod, t_folders &flds, const C_vector<C_mode_connection_imap::C_command_list::S_folder> &flst, bool &changed,
   C_vector<dword> &folder_ids, C_message_container *parent){

                              //remove possible duplicates
   for(int i=flds.Size(); i--; ){
      C_message_container *fld = flds[i];
      for(int j=i; j--; ){
         C_message_container *fld1 = flds[j];
         if(fld->folder_name==fld1->folder_name){
                              //duplicate, remove one
            mod.acc.DeleteFolder(mail_data_path, fld, false);
            break;
         }
      }
   }

                              //temp list of folders, keeping existing and new
   C_vector<C_smart_ptr<C_message_container> > tmp_flds;
   tmp_flds.reserve(flds.Size()+flst.size());
                              //walk on all listed folders in hierarchy level, check if they exist locally, adopt them or create new
   for(int i=flst.size(); i--; ){
      const C_mode_connection_imap::C_command_list::S_folder &fdef = flst[i];
                              //check if such folder exists
      C_message_container *fld = NULL;
      for(int j=flds.Size(); j--; ){
         C_message_container *f = flds[j];
         if(f && f->folder_name==fdef.name){
                              //adopt the folder
            tmp_flds.push_back(f);
            flds[j] = NULL;
            fld = f;
         }
      }
      if(!fld){
                              //server folder doesn't exist locally, create now
         fld = new(true) C_message_container;
         fld->msg_folder_id = GenerateMsgId(folder_ids);
         fld->folder_name = fdef.name;
         fld->parent_folder = parent;

         tmp_flds.push_back(fld);
         fld->Release();
      }
      fld->flags &= ~(fld->FLG_NOINFERIORS | fld->FLG_NOSELECT);
      if(fdef.flags&C_mode_connection_imap::C_command_list::FLAG_NOINFERIORS)
         fld->flags |= fld->FLG_NOINFERIORS;
      if(fdef.flags&C_mode_connection_imap::C_command_list::FLAG_NOSELECT)
         fld->flags |= fld->FLG_NOSELECT;
                              //now walk on subfolders of folder
      CleanFoldersHierarchy(mod, fld->subfolders, fdef.subfolders, changed, folder_ids, fld);
   }
                              //delete dropped folders now
   for(int i=flds.Size(); i--; ){
      C_message_container *fld = flds[i];
      if(fld){
                              //remove foder id
         if(fld->msg_folder_id){
                              //remove msg id from ids so that it can be reused
            for(int j=folder_ids.size(); j--; ){
               if(folder_ids[j]==fld->msg_folder_id){
                  folder_ids.remove_index(j);
                  break;
               }
            }
         }
         mod.acc.DeleteFolder(mail_data_path, fld, false);
         changed = true;
      }
   }
   flds.Assign(tmp_flds.begin(), tmp_flds.end());
}

//----------------------------

void C_mail_client::CleanImapFolders(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_list &cmd){

   S_account &acc = mod.acc;
   C_mode_folders_list &mod_flds = (C_mode_folders_list&)*mod.GetParent();
   C_smart_ptr<C_message_container> sel_fld = mod_flds.GetSelectedFolder();
   bool changed = false;
   C_vector<dword> folder_ids;
   CollectContainerFolderIds(folder_ids);

   CleanFoldersHierarchy(mod, mod.acc._folders, cmd.folders, changed, folder_ids, NULL);
   if(acc.imap_folder_delimiter!=cmd.delimiter){
      acc.imap_folder_delimiter = cmd.delimiter;
      changed = true;
   }
   //if(changed)
   {
      assert(mod_flds.Id()==C_mode_folders_list::ID);
      acc.flags &= ~S_account::ACC_NEED_FOLDER_REFRESH;

      mod_flds.selection = 0;
      SortFolders(acc, &mod_flds, sel_fld);
      SaveAccounts();

      FoldersList_InitView(mod_flds);
   }
}

//----------------------------

void C_mail_client::Connection_MoveImapMessages(C_mode_connection_imap &mod){

                              //first update server flags
   if(StartUpdatingServerFlags(mod, mod.ST_IMAP_MOVE_MESSAGES_SET_FLAGS))
      return;

   ConnectionDrawTitle(mod, GetText(TXT_MOVING_MESSAGES));
   C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mod.GetParent();

   C_vector<S_message*> msgs;
   GetMovableMessages(mod_mbox, msgs);

   C_mode_connection_imap::C_command_move_to_folder *cmd = new(true) C_mode_connection_imap::C_command_move_to_folder(mod.ST_IMAP_MOVE_MESSAGES_COPY);

   cmd->uids.reserve(msgs.size());
   for(int i=msgs.size(); i--; )
      cmd->uids.push_back(msgs[i]->imap_uid);
   Cstr_c range = MakeImapSequenceString(cmd->uids);

   cmd->imap_cmd<<"UID COPY " <<range <<' ' <<mod.acc.GetImapEncodedName(*mod.params.imap_folder_move_dest);
   SendImapCommand(mod, cmd->imap_cmd, cmd);
}

//----------------------------

int C_mail_client::S_bodystructure::GetTextInBraces(const char *&cp, C_vector<char> &buf, int brace_count){

   if(!brace_count){
                              //must begin with open brace
      if(*cp!='(')
         return 0;
      ++cp;
      ++brace_count;
   }
   while(*cp){
      char c = *cp++;
      if(c=='\"'){
         --cp;
         Cstr_c str;
         ReadQuotedString(cp, str);
         buf.push_back('\"');
         buf.insert(buf.end(), str, str+str.Length());
         buf.push_back('\"');
      }else{
         if(c=='(')
            ++brace_count;
         else
         if(c==')'){
            if(!--brace_count)
               break;
         }
         buf.push_back(c);
      }
   }
   return brace_count;
}

//----------------------------

void C_mail_client::S_bodystructure::GetTextInBraces(const char *&cp, Cstr_c &str){

   str.Clear();
   C_vector<char> buf;
   int br;
   br = GetTextInBraces(cp, buf, 0);
   assert(!br);
   buf.push_back(0);
   str = buf.begin();
}

//----------------------------

void C_mail_client::S_bodystructure::Clear(){
   root_part = S_part();
   src_data.clear();
   src_open_braces_count = 0;
   has_text_part = false;
}

//----------------------------

void C_mail_client::S_bodystructure::SkipWord(const char *&cp){

   while(*cp){
      char c = *cp;
      if(c==' ' || c=='(' || c==')')
         break;
      if(c=='\"'){
         ++cp;
         while(*cp){
            if(*cp=='\\' && cp[1]=='\"')
               ++cp;
            else
            if(*cp=='\"')
               break;
            ++cp;
         }
      }
      ++cp;
   }
}

//----------------------------

void C_mail_client::S_bodystructure::SkipImapParam(const char *&cp){

   if(*cp=='('){
      int brace_count = 1;
      ++cp;
      while(*cp){
         char c = *cp++;
         if(c=='\"'){
            --cp;
            SkipWord(cp);
         }else{
            if(c=='(')
               ++brace_count;
            else
            if(c==')' && !--brace_count)
               break;
         }
      }
   }else{
      SkipWord(cp);
   }
}

//----------------------------

bool C_mail_client::S_bodystructure::ReadQuotedString(const char *&cp, Cstr_c &str){

   const char quote = '\"';
   if(*cp!=quote)
      return false;
   ++cp;
   int i = 0;
   while(cp[i] && cp[i]!=quote){
                              //ignore explicit quotes prefixed by '\'
      if(cp[i]=='\\' && cp[i+1]==quote)
         ++i;
      ++i;
   }
   str.Allocate(cp, i);
   cp += i;
   if(*cp)
      ++cp;
   return true;
}

//----------------------------

bool C_mail_client::S_bodystructure::ReadWord(const char *&cp, Cstr_c &str){
   if(ReadQuotedString(cp, str))
      return true;
   //return ::ReadWord(cp, str, " ()");
   str.Clear();
   while(*cp){
      char c = *cp;
      if(c==' ' || c=='(' || c==')')
         break;
      if(c=='\\'){
         ++cp;
         c = *cp;
      }
      str<<c;
      ++cp;
   }
   return (str.Length()!=0);
}

//----------------------------

bool C_mail_client::S_bodystructure::S_part::ParsePart(const char *&cp, const C_mail_client &app){

                           //read type and subtype
   Cstr_c type;
   if(!S_bodystructure::ReadWord(cp, type))
      return false;
   type.ToLower();
   if(type=="text") content_type.type = CONTENT_TEXT;
   else if(type=="image") content_type.type = CONTENT_IMAGE;
   else if(type=="video") content_type.type = CONTENT_VIDEO;
   else if(type=="audio") content_type.type = CONTENT_AUDIO;
   else if(type=="application") content_type.type = CONTENT_APPLICATION;
   else if(type=="multipart") content_type.type = CONTENT_MULTIPART;
   else if(type=="message") content_type.type = CONTENT_MESSAGE;
#ifdef _DEBUG_
   else if(type=="unknown"){}
   else
      assert(0);
#endif
   text_utils::SkipWS(cp);
   if(!S_bodystructure::ReadWord(cp, content_type.subtype))
      return false;
   content_type.subtype.ToLower();
   text_utils::SkipWS(cp);
                           //read params
   if(*cp=='('){
      ++cp;
      text_utils::SkipWS(cp);
      while(*cp!=')'){
         Cstr_c attr, val;
         if(!S_bodystructure::ReadWord(cp, attr))
            return false;
         text_utils::SkipWS(cp);
         if(!S_bodystructure::ReadWord(cp, val))
            return false;
         attr.ToLower();
         if(attr=="charset"){
            val.ToLower();
            encoding::CharsetToCoding(val, charset);
         }else if(attr=="name")
            app.DecodeEncodedText(val, name);
#ifdef _DEBUG_
         else if(attr=="format" || attr=="reply-type" || attr=="delsp" || attr=="method" || attr=="profile" || attr=="boundary"){
         }else{
            assert(attr[0]=='x');
         }
#endif

         text_utils::SkipWS(cp);
      }
      if(*cp!=')'){
         assert(0);
         return false;
      }
      ++cp;
      text_utils::SkipWS(cp);
   }else
   if(text_utils::CheckStringBegin(cp, "nil"))
      text_utils::SkipWS(cp);
   else
      assert(0);
                           //read body_id, body_description, body_encoding, body_size
   S_bodystructure::SkipImapParam(cp);
   text_utils::SkipWS(cp);
   S_bodystructure::SkipImapParam(cp);
   text_utils::SkipWS(cp);
   Cstr_c kw;
   if(!S_bodystructure::ReadWord(cp, kw))
      return false;
   kw.ToLower();
   if(kw=="7bit") content_encoding = ENCODING_7BIT;
   else if(kw=="8bit" || kw=="utf-8") content_encoding = ENCODING_8BIT;
   else if(kw=="binary") content_encoding = ENCODING_BINARY;
   else if(kw=="quoted-printable") content_encoding = ENCODING_QUOTED_PRINTABLE;
   else if(kw=="base64") content_encoding = ENCODING_BASE64;
#ifdef _DEBUG_
   else
      if(kw=="certification");
   else
      assert(0);
#endif
   text_utils::SkipWS(cp);
   if(!text_utils::ScanDecimalNumber(cp, (int&)size))
      return false;
   text_utils::SkipWS(cp);
   int par_i = 0;
   while(*cp && *cp!=')'){
      if(par_i==1){
                              //read content disposition params
         if(*cp=='('){
            Cstr_c str;
            S_bodystructure::GetTextInBraces(cp, str);
            const char *cp1 = str;
            text_utils::SkipWS(cp1);
            while(*cp1){
               Cstr_c attr, val;
               if(!S_bodystructure::ReadWord(cp1, attr))
                  break;
               text_utils::SkipWS(cp1);
               if(!S_bodystructure::ReadWord(cp1, val)){
                  S_bodystructure::GetTextInBraces(cp1, val);
                  if(!val.Length())
                     break;
               }
               attr.ToLower();
               if(attr=="attachment"){
                  const char *cp2 = val;
                  text_utils::SkipWS(cp2);
                  while(*cp2){
                     Cstr_c attr1, val1;
                     if(!S_bodystructure::ReadWord(cp2, attr1))
                        break;
                     text_utils::SkipWS(cp2);
                     if(!S_bodystructure::ReadWord(cp2, val1))
                        break;
                     attr1.ToLower();
                     if(attr1=="filename")
                        app.DecodeEncodedText(val1, name);
#ifdef _DEBUG_
                     else assert(0);
#endif
                  }
               }
#ifdef _DEBUG_
               else
               if(attr=="application" || attr=="text" || attr=="image" || attr=="inline"){
               }else
                  assert(0);
#endif
               text_utils::SkipWS(cp1);
            }
            text_utils::SkipWS(cp);
         }else
            S_bodystructure::SkipImapParam(cp);
      }else
         S_bodystructure::SkipImapParam(cp);
      text_utils::SkipWS(cp);
      ++par_i;
   }
   return true;
}

//----------------------------

bool C_mail_client::S_bodystructure::S_part::ParseParts(const char *&cp, const C_mail_client &app){

   text_utils::SkipWS(cp);

   if(*cp!='(')
      return ParsePart(cp, app);
                           //parse all parts
   while(*cp=='('){
      ++cp;
      S_bodystructure::S_part part;
      if(*cp=='('){
                           //parse nested parts
         if(!part.ParseParts(cp, app))
            return false;
      }else{
         if(!part.ParsePart(cp, app))
            return false;
      }
      nested_parts.push_back(part);
      if(*cp!=')'){
         assert(0);
         return false;
      }
      ++cp;
      text_utils::SkipWS(cp);
   }
                           //read multipart subtype
   content_type.type = CONTENT_MULTIPART;
   if(!S_bodystructure::ReadWord(cp, content_type.subtype))
      return false;
   content_type.subtype.ToLower();
   text_utils::SkipWS(cp);
                              //compute multipart size (sum of size of all parts)
   for(int i=nested_parts.size(); i--; )
      size += nested_parts[i].size;

                              //read multipart params
   if(*cp=='('){
      ++cp;
      text_utils::SkipWS(cp);
                              //parts params
      while(*cp!=')'){
         Cstr_c attr, val;
         if(!S_bodystructure::ReadWord(cp, attr))
            return false;
         text_utils::SkipWS(cp);
         if(!S_bodystructure::ReadWord(cp, val))
            return false;
         attr.ToLower();
         if(attr=="boundary")
            nested_multipart_boundary = val;
#ifdef _DEBUG_
         else
         if(attr=="type" || attr=="charset" || attr=="protocol" || attr=="micalg" || attr=="report-type" || attr=="name"){
         }else
            assert(0);
#endif

         text_utils::SkipWS(cp);
      }
      ++cp;
      text_utils::SkipWS(cp);
   }/*else
   if(text_utils::CheckStringBegin(cp, "nil"))
      text_utils::SkipWS(cp);
   else
      assert(0);
      */
   while(*cp && *cp!=')'){
      S_bodystructure::SkipImapParam(cp);
      text_utils::SkipWS(cp);
   }
   return true;
}

//----------------------------

bool C_mail_client::S_bodystructure::Parse(const C_mail_client &app){

                              //we should be cleared
   assert(!root_part.nested_parts.size());
   root_part = S_bodystructure::S_part();
   src_data.push_back(0);
   const char *cp = src_data.begin();
   bool ok = root_part.ParseParts(cp, app);
   //assert(ok);
   src_data.clear();
   src_open_braces_count = 0;

                              //determine if we have text part
   if(root_part.content_type.type==CONTENT_MULTIPART){
      dword n = root_part.nested_parts.size();
      if(n)
      switch(root_part.nested_parts.front().content_type.type){
      case CONTENT_TEXT:
      case CONTENT_MULTIPART: //assumption!
         has_text_part = true;
         break;
         /*
      case CONTENT_MULTIPART:
         {
            const S_bodystructure::S_part &p = root_part.nested_parts.front();
            n = p.nested_parts.size();
            if(n && p.nested_parts.front().content_type.type==CONTENT_TEXT)
               has_text_part = true;
         }
         break;
         */
      }
   }else
      has_text_part = (root_part.content_type.type==CONTENT_TEXT);
   return ok;
}

//----------------------------

void C_mail_client::S_bodystructure::AppendSourceData(const char *cp){
   src_data.insert(src_data.end(), cp, cp+StrLen(cp));
}

//----------------------------

int C_mail_client::S_bodystructure::GetNumAttachments() const{

   switch(root_part.content_type.type){
   case CONTENT_MULTIPART:
      if(root_part.content_type.subtype=="mixed"){
         dword n = root_part.nested_parts.size();
         if(has_text_part)
            --n;
         return n;
      }
      /*
      if(root_part.content_type.subtype=="signed"){
         dword n = root_part.nested_parts.size();
         if(n==2){
            --n;
                              //classic signed, one main part, other is signature
            const S_part &p = root_part.nested_parts[0];
            if(p.content_type.subtype=="mixed"){
               //n = p.nested_parts.size();
            }
            if(has_text_part)
               --n;
            return n;
         }
      }
      */
#ifdef _DEBUG
      if(!(
         root_part.content_type.subtype=="alternative" ||
         root_part.content_type.subtype=="related" ||
         root_part.content_type.subtype=="signed" ||
         root_part.content_type.subtype=="digest" ||
         root_part.content_type.subtype=="report"
         ))
         assert(0);
#endif
      break;
   case CONTENT_TEXT:
      break;
   default:
      return 1;
   }
   return 0;
}

//----------------------------

const C_mail_client::S_bodystructure::S_part *C_mail_client::S_bodystructure::GetFirstAttachment() const{

   switch(root_part.content_type.type){
   case CONTENT_MULTIPART:
      return root_part.nested_parts.begin()+int(has_text_part);
   default:
      return &root_part;
   }
}

//----------------------------

dword C_mail_client::S_bodystructure::GetSizeOfTextPart() const{

   const S_part *p = &root_part;
   if(p->content_type.type==CONTENT_MULTIPART && p->content_type.subtype=="mixed" && p->nested_parts.size())
      p = p->nested_parts.begin();
   return p->size;
}

//----------------------------

void C_mail_client::C_mode_connection_imap::S_msg_seq_map::Assign(dword seq, dword uid){

   for(int i=map.size(); i--; ){
      if(map[i].seq==seq){
         assert(map[i].uid == uid);
         map[i].uid = uid;
         return;
      }
   }
   S_value m = { seq, uid };
   map.push_back(m);
}

//----------------------------

dword C_mail_client::C_mode_connection_imap::S_msg_seq_map::FindUid(dword seq) const{

   for(int i=map.size(); i--; ){
      if(map[i].seq==seq){
         return map[i].uid;
      }
   }
   return 0xffffffff;
}

//----------------------------

dword C_mail_client::C_mode_connection_imap::S_msg_seq_map::FindAndRemoveUid(dword seq){

   dword ret = 0xffffffff;
   for(int i=map.size(); i--; ){
      S_value &val = map[i];
      if(val.seq==seq){
         ret = val.uid;
         map.remove_index(i);
      }else
      if(val.seq>seq){
         --val.seq;
      }
   }
   return ret;
}

//----------------------------

dword C_mail_client::C_mode_connection_imap::S_msg_seq_map::GetMaxSeqNum() const{

   dword max = 0;
   for(int i=map.size(); i--; )
      max = Max(max, map[i].seq);
   return max;
}

//----------------------------

bool C_mail_client::BeginImapIdle(C_mode_connection_imap &mod){

   if((mod.capability&mod.CAPS_IDLE) && mod.acc.use_imap_idle){
                              //begin IDLE command
      mod.action = mod.ACT_IMAP_IDLE;
      mod.acc.background_processor.mode = &mod;
      ConnectionUpdateState(mod.acc, S_account::UPDATE_WORKING);
      ConnectionDrawAction(mod, NULL);

      if(mod.folder && mod.folder==mod.acc.selected_folder && mod.msg_seq_map.is_synced){
                              //start IDLE on this folder
         SendImapCommand(mod, "IDLE", new(true) C_mode_connection_imap::C_command_idle, -1);
      }else{
         C_message_container *cnt = FindInbox(mod.acc);
         if(!cnt){
            assert(0);
            return false;
         }
         mod.folder = cnt;
         ConnectionImapSelectFolder(mod);
      }
      return true;
   }
   return false;
}

//----------------------------

void C_mail_client::ImapIdleUpdateAfterOperation(C_mode_connection_imap &mod, bool num_msgs_changed){

   if(num_msgs_changed){
      for(C_mode *m=mode; m; m=m->GetParent()){
         switch(m->Id()){
         case C_mode_mailbox::ID:
            {
               C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*m;
               if(&mod_mbox.acc==&mod.acc)
                  Mailbox_RecalculateDisplayArea(mod_mbox);
            }
            break;
         }
      }
   }
   ConnectionRedrawImapIdleFolder(mod.acc);
}

//----------------------------

void C_mail_client::ImapIdleAfterGotHeaders(C_mode_connection_imap &mod){

   if(mod.headers_to_move.size()){
      StartMoveMessagesByRuleIMAP(mod);
      return;
   }
   if(mod.msgs_to_delete.size()){
      Cstr_c range = MakeImapSequenceString(mod.msgs_to_delete);
      //mod.msgs_to_delete.clear();

      Cstr_c cmd;
      cmd <<"UID STORE " <<range <<" +flags (\\Deleted)";
      SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_DELETE_MESSAGES));
      return;
   }
   if(mod.headers_to_download.size()){
      C_vector<S_message> &msgs = mod.GetMessages();
      C_vector<dword> retr_msg_indexes;
      retr_msg_indexes.reserve(mod.headers_to_download.size());
      mod.num_get_bodies = 0;
      mod.get_bodies_index = 0;
      mod.progress.total = mod.progress.pos = 0;
      for(int i=0; i<mod.headers_to_download.size(); i++){
         const S_message_header_base &hdr = mod.headers_to_download[i];
         for(int j=msgs.size(); j--; ){
            S_message &msg = msgs[j];
            if(msg.MatchUID(hdr, mod.IsImap())){
               retr_msg_indexes.push_back(j);
               ++mod.num_get_bodies;
               break;
            }
         }
      }
      if(mod.num_get_bodies){
         StartRetrievingMessageMarkedBodiesIMAP((C_mode_connection_imap&)mod, retr_msg_indexes);
         return;
      }
   }
                              //continue in IDLE
   BeginImapIdle(mod);
}

//----------------------------

void C_mail_client::SetModeConnectionImapUpload(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params){

   C_mode_connection_imap_upload &mod = *new(true) C_mode_connection_imap_upload(*this, mode, acc, cnt, C_mode_connection::ACT_UPLOAD_SENT, params, action);
   ConnectionFinishInit(mod);

   int n = CountMessagesForUpload(acc, &mod.cnt_out, &mod.progress.total);
   if(n<0){
      n = -n;
      mod.action = mod.ACT_UPLOAD_DRAFTS;
   }
   mod.num_send_messages = n;

   //if(acc.background_processor.state)
   mod.AdoptOpenedConnection();

   if(mod.acc.socket){
                              //reuse opened socket
      //mod.socket = mod.acc.socket;
      mod.capability = mod.acc.imap_capability;
      mod.using_cached_socket = true;
      StartUploadingMessageToImap(mod);
   }else
      ConnectionInitSocket(mod);
}

//----------------------------

void C_mail_client::ImapUploadSocketEvent(C_mode_connection_imap_upload &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){

   switch(ev){
   case C_socket_notify::SOCKET_DATA_SENT:
      switch(mod.state){
      case C_mode_connection_imap::ST_IMAP_UPLOAD_MESSAGE_SEND:
         ConnectionDrawSocketEvent(mod, ev);
         //ConnectionDrawFloatDataCounters(mod);
         z_stream *zs = NULL;
         if(mod.capability&mod.CAPS_IN_COMPRESSION)
            zs = &mod.compress_out;
         if(SendNextMessageData(mod, mod, mod.acc.socket, mod.GetMessages()[mod.message_index], zs)){
                     //send last eol
            if(zs)
               SendCompressedData(socket, "\r\n", 2, *zs);
            else
               mod.SocketSendCString("\r\n");
            assert(mod.commands.size()==1);
            C_mode_connection_imap::C_command *cmd = mod.commands.front();
            mod.state = cmd->state = mod.ST_IMAP_UPLOAD_MESSAGE_DONE;
         }
         return;
      }
      break;
   }
   ConnectionSocketEvent(mod, ev, socket, redraw);
}

//----------------------------

void C_mail_client::ImapUploadCloseMode(C_mode_connection_imap_upload &mod){

   if(!mod.container_invalid)
      mod.GetContainer().SaveMessages(mail_data_path);

                              //if we were in idle, go back to it
   if(!mod.user_canceled && mod.next_action==mod.ACT_SEND_MAILS && mod.acc.background_processor.state){
      ConnectionDisconnect(mod);
      mod.cnt_out = NULL;
      return;
   }

                              //keep ref of mode
   C_smart_ptr<C_mode> keep_ref = &mod;
   //mod.socket = NULL;
   CloseMode(mod, false);

   if(!mod.user_canceled){
      ConnectionInit(mod.acc, mod.folder, mod.next_action, &mod.params);
   }else
      RedrawScreen();
}

//----------------------------

void C_mail_client::StartUploadingMessageToImap(C_mode_connection_imap_upload &mod){

   if(mod.commands.size()==1 && mod.commands.front()->state==mod.ST_IMAP_IDLE){
                              //terminate IDLE first
      ImapIdleSendDone(mod);
      return;
   }
   if(mod.GetContainer().IsTemp()){
                              //try to create folder first, it's too late to try after upload
      ConnectionDrawAction(mod, GetText(TXT_CREATING_IMAP_FOLDER));
      Cstr_c s;
      s<<"CREATE " <<mod.acc.GetImapEncodedName(mod.GetContainer());
      SendImapCommand(mod, s, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_UPLOAD_MESSAGE_CREATE_FOLDER));
      return;
   }

   const C_vector<S_message> &messages = mod.GetMessages();
   {
      dword FLG = mod.action==mod.ACT_UPLOAD_SENT ? S_message::MSG_SENT : S_message::MSG_DRAFT;
      for(; mod.message_index<dword(messages.size()); mod.message_index++){
         dword flg = messages[mod.message_index].flags;
         if((flg&FLG) && !(flg&S_message::MSG_SERVER_SYNC) && (flg&S_message::MSG_NEED_UPLOAD))
            break;
      }
   }
   if(mod.message_index<dword(messages.size())){
      const S_message &msg = messages[mod.message_index];

                              //count message size
      mod.send_recipients.clear();
      ParseRecipients(msg.to_emails, mod.send_recipients);
      ParseRecipients(msg.cc_emails, mod.send_recipients);
      ParseRecipients(msg.bcc_emails, mod.send_recipients);

                              //prepare headers
      mod.curr_header.clear();
      PrepareMailHeadersForSending(mod, mod, msg, mod.curr_header);
      int size = mod.curr_header.size();
      {
         C_connection_send::E_SEND_PHASE sp = mod.send_phase;
                              //count size of message body
         C_vector<char> buf;
         while(!PrepareNextMessageData(mod, mod, msg, buf, false)){
            size += buf.size();
            buf.clear();
         }
         mod.send_phase = sp;
         mod.txt_flowed_curr_quote_count = -1;  //need to reset this
      }
      Cstr_c cmd; cmd<<"APPEND " <<mod.acc.GetImapEncodedName(mod.GetContainer());
      cmd<<" (\\Seen";
      if(mod.action==mod.ACT_UPLOAD_DRAFTS)
         cmd<<" \\Draft";
      cmd<<") {" <<size <<'}';
      SendImapCommand(mod, cmd, new(true) C_mode_connection_imap::C_command(mod.ST_IMAP_UPLOAD_MESSAGE));

      ++mod.send_message_index;
      Cstr_w s;
      s.Format(L"% %/%") <<GetText(TXT_SAVING_TO_SERVER) <<mod.send_message_index <<mod.num_send_messages;
      ConnectionDrawAction(mod, s);
   }else{
      ImapUploadCloseMode(mod);
   }
}

//----------------------------

void C_mail_client::ConnectionCancelAllImapCommands(C_mode_connection_imap &mod){

                              //cancel all commands
   for(int i=mod.commands.size(); i--; ){
      C_mode_connection_imap::C_command *cmd = mod.commands[i];
      switch(cmd->state){
      case C_mode_connection_imap::ST_IMAP_GET_BODY:
         {
            C_mode_connection_imap::C_command_get_body *cmd_body = (C_mode_connection_imap::C_command_get_body*)cmd;
            //CancelMessageRetrieval(mod, cmd_body->message_index);
            mod.GetContainer().DeleteMessageFiles(mail_data_path, cmd_body->temp_msg);
         }
         break;
      }
   }
   mod.commands.clear();
}

//----------------------------

void C_mail_client::ConnectionErrorImap(C_mode_connection_imap &mod, const wchar *err){

   ConnectionCancelAllImapCommands(mod);
   ConnectionError(mod, err);
}

//----------------------------

void C_mail_client::ConnectionProcessInputImap(C_mode_connection_imap &mod, S_user_input &ui, bool &redraw){

#ifdef USE_MOUSE
   ProcessMouseInSoftButtons(ui, redraw);
#endif
   if(ui.key==K_RIGHT_SOFT || ui.key==K_BACK || ui.key==K_ESC){
      /*
      if(!mod.cancel_request){
         switch(mod.state){
         case C_mode_connection_imap::ST_IMAP_SELECT:
         case C_mode_connection_imap::ST_IMAP_GET_HDR:
         case C_mode_connection_imap::ST_IMAP_GET_BODY:
         case C_mode_connection_imap::ST_IMAP_GET_ATTACHMENT_BODY:
         case C_mode_connection_imap::ST_IMAP_GET_UIDS:
            mod.cancel_request = true;
            mod.rsk = TXT_NULL;
            DrawConnection(mod);
            ResetDirtyRect();
            const int sy = fdb.line_spacing + 1;
            AddDirtyRect(S_rect(0, 0, ScrnSX(), sy));
            UpdateScreen();
            AddDirtyRect(S_rect(0, ScrnSY()-sy, ScrnSX(), sy));
            UpdateScreen();
            return;
         }
      }
      */
      mod.cancel_request = true;
      S_account &acc = mod.acc;

      ConnectionCancelAllImapCommands(mod);
      mod.acc.CloseConnection();
      if(mod.action==mod.ACT_UPLOAD_SENT || mod.action==mod.ACT_UPLOAD_DRAFTS)
         ((C_mode_connection_imap_upload&)mod).user_canceled = true;
      mod.rsk = TXT_NULL;
      ConnectionDisconnect(mod);

                              //reconnect IDLE
      if((acc.flags&acc.ACC_USE_IMAP_IDLE) && acc.use_imap_idle)
         ConnectAccountInBackground(acc);
   }
}

void encr_segment_01b(){}
//----------------------------

void C_mail_client::ConnectionRedrawImapIdleFolder(const S_account &acc){

   switch(mode->Id()){
   case C_mode_accounts::ID:
      if(!mode->GetMenu())
         ((C_mode_accounts&)*mode).DrawAccount(&acc-accounts.Begin());
      else
         RedrawScreen();
      //UpdateScreen();
      break;
   case C_mode_folders_list::ID:
      if(!acc.IsImap()){
         RedrawScreen();
         //UpdateScreen();
      }else
      if(acc.selected_folder){
         C_mode_folders_list &mod = (C_mode_folders_list&)*mode;
         if(&mod.acc==&acc){
            if(!mode->GetMenu()){
               C_folders_iterator it(const_cast<S_account&>(acc)._folders);
               int i = 0;
               while(!it.IsEnd()){
                  const C_message_container *fld = it.Next();
                  if(fld==acc.selected_folder){
                     DrawFolder(mod, i);
                     break;
                  }
                  ++i;
               }
            }else
               RedrawScreen();
            //UpdateScreen();
         }
      }
      break;
   case C_mode_mailbox::ID:
      if(!acc.IsImap()){
         RedrawScreen();
         //UpdateScreen();
      }else
      if(acc.selected_folder){
         C_mode_mailbox &mod = (C_mode_mailbox&)*mode;
         if(mod.folder==acc.selected_folder){
            RedrawScreen();
            //UpdateScreen();
         }
      }
      break;
   }
}

//----------------------------
