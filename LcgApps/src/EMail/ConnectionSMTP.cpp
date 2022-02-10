#include "..\Main.h"
#include "Main_Email.h"
#include <Md5.h>
#include <Base64.h>

//----------------------------

static void hmac_md5(const char *secret_key, int key_length, const byte *data, int data_length, char out[33]){

   const int MD5_BLOCKSIZE = 64;
   char tmp_key[16];
   if(key_length > MD5_BLOCKSIZE){
      C_md5 md5;
      md5.Update((const byte*)secret_key, key_length);
      md5.Finish((byte*)tmp_key);
      secret_key = tmp_key;
      key_length = 16;
   }
   byte buf[MD5_BLOCKSIZE];
   byte imd5[16];
   {
                              //inner digest
                              //pad the key for inner digest
      for(int i=0; i<key_length; ++i)
         buf[i] = secret_key[i] ^ 0x36;
      MemSet(buf+key_length, 0x36, MD5_BLOCKSIZE-key_length);
      C_md5 md5;
      md5.Update(buf, MD5_BLOCKSIZE);
      md5.Update(data, data_length);
      md5.Finish(imd5);
   }
   {
                              //outter digest
                              //pad the key for outter digest
      for(int i=0; i<key_length; ++i)
         buf[i] = secret_key[i] ^ 0x5c;
      MemSet(buf+key_length, 0x5c, MD5_BLOCKSIZE-key_length);
      C_md5 md5;
      md5.Update(buf, MD5_BLOCKSIZE);
      md5.Update(imd5, 16);
      md5.FinishToString(out);
   }
}

//----------------------------

void C_mail_client::SmtpError(C_mode_connection_smtp &mod, const wchar *err){

   if(mod.acc.background_processor.state){
      ConnectionUpdateState(mod.acc, S_account::UPDATE_ERROR, err);
   }else
   if(!mod.params.auto_update){
      CloseMode(mod, false);
      ShowErrorWindow(TXT_PROGRESS_SENDING, err);
   }else{
      //mod.cancel_request = true;
      SmtpCloseMode(mod);
   }
}

//----------------------------

void C_mail_client::SetModeConnectionSmtp(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params){

   C_mode_connection_smtp &mod = *new(true) C_mode_connection_smtp(*this, mode, acc, cnt, action, params);
   ConnectionFinishInit(mod);

   mod.progress.total = 0;
   mod.num_send_messages = CountMessagesForSending(acc, cnt, action, &mod.cnt_out, &mod.progress.total);

   bool use_ssl = (mod.acc.secure_out==S_account_settings::SECURE_SSL);
   bool def_ssl = (mod.acc.secure_out==S_account_settings::SECURE_STARTTLS);
   mod.socket = CreateSocket(connection, &mod, use_ssl, def_ssl
//#ifdef _DEBUG                 //logging not in release, so that users don't make mistakes overwriting log
      , socket_log ? NETWORK_LOG_FILE : NULL
//#endif
      );
   mod.socket->Release();
   word port = mod.acc.port_out;
   if(!port)
      port = C_socket::PORT_SMTP;
   mod.socket->Connect(mod.acc.smtp_server, port);
}

//----------------------------

bool C_mail_client::BeginSMTPAuthentication(C_mode_connection_smtp &mod){

   if(mod.smtp_caps&mod.SMTP_AUTH_CRAM_MD5){
      mod.smtp_caps &= ~mod.SMTP_AUTH_CRAM_MD5;
      mod.socket->SendCString("AUTH CRAM-MD5\r\n");
      mod.state = mod.ST_SMTP_AUTH_CRAM_MD5;
      return true;
   }
   if(mod.smtp_caps&mod.SMTP_AUTH_LOGIN){
      mod.smtp_caps &= ~mod.SMTP_AUTH_LOGIN;
      mod.socket->SendCString("AUTH LOGIN\r\n");
      mod.state = mod.ST_SMTP_AUTH_LOGIN;
      return true;
   }
   if(mod.smtp_caps&mod.SMTP_AUTH_PLAIN){
      mod.smtp_caps &= ~mod.SMTP_AUTH_PLAIN;
      Cstr_c cmd, b64 = SmtpGetAuthLoginBase64Param(mod);
      cmd.Format("AUTH PLAIN %\r\n") <<b64;
      //mod.socket->SendCString("AUTH PLAIN\r\n");
      mod.socket->SendCString(cmd);
      mod.state = mod.ST_SMTP_AUTH_PLAIN;
      return true;
   }
   return false;
}

//----------------------------

void C_mail_client::AfterSmtpConnected(C_mode_connection_smtp &mod){
                              //success, send Hello
   /*
   Cstr_c username;
   int ai = mod.acc.email.Find('@');
   if(ai!=-1){
      username = mod.acc.email.Left(ai);
               //remove all non-alphanum characters
      for(dword i=0; i<username.Length(); i++){
         char c = username[i];
         if(!text_utils::IsAlNum(c)){
            Cstr_c r = username.Right(username.Length()-i-1);
            username = username.Left(i);
            username<<r;
         }
      }
   }
   if(!username.Length())
      username = "ProfiMail";
      */
   Cstr_c domain;
   dword ip = mod.socket->GetLocalIp();
   domain.Format("[%.%.%.%]") <<int((ip>>24)&0xff) <<int((ip>>16)&0xff) <<int((ip>>8)&0xff) <<int((ip>>0)&0xff);

   Cstr_c cmd;
   cmd.Format("EHLO %\r\n")
      <<domain;
      //<<username;
   mod.socket->SendString(cmd);
   mod.state = mod.ST_SMTP_EHLO_TEST;
}

//----------------------------

bool C_mail_client::SmtpAfterMailSent(C_mode_connection_smtp &mod){

   C_mode_mailbox *mod_mbox = NULL;
   if(mod.GetParent() && mod.GetParent()->Id()==C_mode_mailbox::ID)
      mod_mbox = &(C_mode_mailbox&)*mod.GetParent();

   if(mod.acc.save_sent_messages ||
      (mod.acc.IsImap() && (mod.acc.flags&S_account::ACC_IMAP_UPLOAD_SENT))){
               //move it to Sent box
      C_message_container *cnt;
      {
                              //find or create sent folder
         Cstr_w saved_name;
         if(mod.acc.IsImap())
            saved_name = mod.acc.GetSentFolderName();
         else
            saved_name = S_account::default_sent_folder_name;
         bool created;
         C_message_container *fld = FindOrCreateImapFolder(mod.acc, saved_name, created);
         if(created){
            C_mode_folders_list *mod_flds;
            if(mod_mbox)
               mod_flds = &(C_mode_folders_list&)*mod_mbox->GetParent();
            else
               mod_flds = &(C_mode_folders_list&)*mod.GetParent();
            FoldersList_InitView(*mod_flds);
            SaveAccounts();
         }
         cnt = fld;
      }
      LoadMessages(*cnt);
      C_vector<S_message> &sent_messages = cnt->messages;
      sent_messages.push_back(S_message());

      C_vector<S_message> &messages = mod.GetMessages();
      S_message &src = messages[mod.message_index];
      S_message &dst = sent_messages.back();
      assert(!(src.flags&(src.MSG_SERVER_SYNC|src.MSG_DRAFT)));
      assert(src.flags&src.MSG_TO_SEND);

      mod.GetContainer().MoveMessageTo(mail_data_path, src, *cnt, dst);

      dst.flags &= ~dst.MSG_TO_SEND;
      dst.flags |= dst.MSG_NEED_UPLOAD;
      dst.flags |= dst.MSG_SENT | dst.MSG_READ;

      if(!mod.acc.IsImap() || !(mod.acc.flags&S_account::ACC_IMAP_UPLOAD_SENT)){
         dst.sender = mod.acc.primary_identity;
      }

      DeleteMessage(mod.GetContainer(), mod.message_index, false);
      SortMessages(sent_messages, mod.acc.IsImap(), mod_mbox ? &mod_mbox->selection : NULL);
      cnt->SaveMessages(mail_data_path, true);
   }else
      DeleteMessage(mod.GetContainer(), mod.message_index, true);

               //move index back, so that next msg is not skipped
   --mod.message_index;

   ConnectionDrawAction(mod, NULL);

   if(mod_mbox)
      Mailbox_RecalculateDisplayArea(*mod_mbox);

   mod.GetContainer().MakeDirty();
   if(StartSendingNextMessage(mod))
      return true;
                              //finished sending all messages
   mod.progress.pos = mod.progress.total;
   ConnectionDrawProgress(mod);

   mod.GetContainer().SaveMessages(mail_data_path);
                              //try to clean empty Outbox
   if(CleanEmptyTempImapFolders(mod.acc)){
      mod.container_invalid = true;
      for(C_mode *mp = mod.GetParent(); mp; mp=mp->GetParent()){
         if(mp->Id()==C_mode_folders_list::ID)
            FoldersList_InitView((C_mode_folders_list&)*mp);
      }
   }
                              //no more messages to send, quit
   SmtpCloseMode(mod);
   return false;
}

//----------------------------
                    
void C_mail_client::SmtpCloseMode(C_mode_connection_smtp &mod){

   if(!mod.container_invalid)
      mod.GetContainer().SaveMessages(mail_data_path);
                              //keep ref of mode
   C_smart_ptr<C_mode> keep_ref = &mod;
   if(&mod==mode){
      CloseMode(mod, false);
   }

   if(mod.state != mod.ST_USER_CANCELED)
      ConnectionInit(mod.acc, mod.folder, mod.action, &mod.params);
   else
      RedrawScreen();
   /*
   switch(mod.action){
   case C_mode_connection::ACT_UPDATE_ACCOUNTS:
      if(!mod.canceled_connection){
      }else{
         C_mode_folders_list &mod_flds = (C_mode_folders_list&)*mode;
         mode = mod_flds.mod_upd_mboxes;
         if(mod.canceled_connection){
            CloseMode(*mode);
         }else{
            mode->timer->Resume();
         }
      }
      break;
   default:
      if(mode->Id()==C_mode_mailbox::ID){
         C_mode_mailbox &mod_mbox = (C_mode_mailbox&)*mode;
         SetMailboxSelection(mod_mbox, mod_mbox.selection);
      }
      RedrawScreen();
   }
   */
}

//----------------------------

Cstr_c C_mail_client::C_mode_connection_smtp::GetSmtpUsername() const{

   return acc.smtp_username.Length() ? acc.smtp_username : acc.username;
}

//----------------------------

Cstr_c C_mail_client::SmtpGetAuthLoginBase64Param(C_mode_connection_smtp &mod){
   const Cstr_c un = mod.GetSmtpUsername();
   const Cstr_c &pw = mod.acc.smtp_password.Length() ? mod.acc.smtp_password : mod.acc.password;
   Cstr_c cmd;
   cmd.Format("%#%#%#%") <<char(0) <<un <<char(0) <<pw;
   C_vector<char> b64;
   EncodeBase64((byte*)(const char*)cmd, cmd.Length(), b64);
   b64.push_back(0);
   return b64.begin();
}

//----------------------------

bool C_mail_client::ProcessLineSmtp(C_mode_connection_smtp &mod, const C_socket::t_buffer &line){

                              //examine returned line
   const char *cp = line.Begin();
   bool fail = false;
   if(StrLen(cp) < 3)
      fail = true;
   else{
                              //Reply codes:
                              //1xx = positive preliminary reply
                              //2xx = positive completion reply
                              //3xx = positive intermediate reply
                              //4xx = transient negative completion
                              //5xx = permanent negative completion
      int d0 = cp[0] - '0';
                        //x0x = syntax
                        //x1x = information
                        //x2x = connection
                        //x5x = mail system
      //int d1 = cp[1] - '0';

      //int d2 = cp[2] - '0';
      cp += 3;
      switch(mod.state){
      case C_mode_connection_smtp::ST_WAIT_CONNECT_OK:
         if(d0==2){
            if(*cp=='-') break;  //multi-line?
            AfterSmtpConnected(mod);
         }else
            fail = true;
         break;

      case C_mode_connection_smtp::ST_SMTP_EHLO_TEST:
         if(d0!=2){
                           //try HELO
            /*
            Cstr_c username;
            int ai = mod.acc.email.Find('@');
            if(ai!=-1)
               username = mod.acc.email.Left(ai);
            else
               username = "ProfiMail";
               */
            Cstr_c domain;
            dword ip = mod.socket->GetLocalIp();
            domain.Format("[%.%.%.%]") <<int((ip>>24)&0xff) <<int((ip>>16)&0xff) <<int((ip>>8)&0xff) <<int((ip>>0)&0xff);

            Cstr_c cmd;
            cmd.Format("HELO %\r\n")
               //<<username;
               <<domain;
            mod.socket->SendString(cmd);
            mod.state = mod.ST_SMTP_HELO_TEST;
            break;
         }
                        //detect available commands
         {
            const char *cp1 = cp + 1;
            if(text_utils::CheckStringBegin(cp1, "auth")){
               text_utils::SkipWS(cp1);
                        //some servers use '='
               if(*cp1=='='){
                  ++cp1;
                  text_utils::SkipWS(cp1);
               }
               while(*cp1){
                  Cstr_c t_mode;
                  if(!text_utils::ReadWord(cp1, t_mode, " "))
                     break;
                  t_mode.ToLower();
                  if(t_mode=="plain")
                     mod.smtp_caps |= mod.SMTP_AUTH_PLAIN;
                  else
                  if(t_mode=="login")
                     mod.smtp_caps |= mod.SMTP_AUTH_LOGIN;
                  else
                  if(t_mode=="cram-md5")
                     mod.smtp_caps |= mod.SMTP_AUTH_CRAM_MD5;
                  text_utils::SkipWS(cp1);
               }
            }else
            if(text_utils::CheckStringBegin(cp1, "starttls")){
               mod.smtp_caps |= mod.SMTP_ALLOW_STARTTLS;
            }
         }
                           //check for multi-line response
         if(*cp=='-') break;
                           //flow...
      case C_mode_connection_smtp::ST_SMTP_HELO_TEST:
         if(d0!=2){
            fail = true;
            break;
         }
         {
            if(*cp=='-')
               break;  //multi-line?
            if(mod.acc.secure_out==S_account_settings::SECURE_STARTTLS && !(mod.smtp_caps&mod.SMTP_IN_TLS)){
                        //SSL connection required, check if available
               if(mod.smtp_caps&mod.SMTP_ALLOW_STARTTLS){
                  mod.socket->SendString("STARTTLS\r\n");
                  mod.state = mod.ST_SMTP_STARTTLS;
               }else{
                  SmtpError(mod, L"SMTP server doesn't support STARTTLS command. Disable this option is account settings.");
                  return false;
               }
               break;
            }
            if(mod.acc.flags&S_account::ACC_USE_SMTP_AUTH){
                        //authentization

               ConnectionDrawAction(mod, GetText(TXT_PROGRESS_LOGGING_IN));
               if(BeginSMTPAuthentication(mod))
                  break;
                        //no authentization method available, give up
            }
            //DrawOutlineRect(mod.rc, COL_LIGHT_GREY, 0xffa0ffa0);
            ConnectionDrawTitle(mod, GetText(TXT_PROGRESS_SENDING));
            if(!StartSendingNextMessage(mod)){
                           //no message to send, quit
               assert(0);
               SmtpCloseMode(mod);
               return false;
            }
         }
         break;

      case C_mode_connection_smtp::ST_SMTP_STARTTLS:
         if(d0!=2){
            fail = true;
            break;
         }
                        //begin SSL mode
         if(mod.socket->BeginSSL()){
                        //reset as if it was after welcome state
            mod.state = mod.ST_WAIT_CONNECT_OK;
            mod.smtp_caps = mod.SMTP_IN_TLS;
            return false;
         }else{
            SmtpError(mod, L"Cannot begin SSL connection.");
            return false;
         }
         break;

      case C_mode_connection_smtp::ST_SMTP_AUTH_PLAIN:
         if(d0==2){
                        //auth ok, start sending
            ConnectionDrawTitle(mod, GetText(TXT_PROGRESS_SENDING));
            if(!StartSendingNextMessage(mod)){
                           //no message to send, quit
               assert(0);
               SmtpCloseMode(mod);
               return false;
            }
         }else
         if(d0==3){
            Cstr_c cmd, b64 = SmtpGetAuthLoginBase64Param(mod);
            cmd.Format("%\r\n") <<b64;
            mod.socket->SendString(cmd);
            mod.state = mod.ST_SMTP_AUTH;
         }else
         if(!BeginSMTPAuthentication(mod))
            fail = true;
         break;

      case C_mode_connection_smtp::ST_SMTP_AUTH_CRAM_MD5:
         if(d0!=2){
            bool ok = false;
            if(d0==3){
               const Cstr_c un = mod.GetSmtpUsername();
               const Cstr_c &pw = mod.acc.smtp_password.Length() ? mod.acc.smtp_password : mod.acc.password;

               text_utils::SkipWS(cp);
               C_vector<byte> buf;
               if(DecodeBase64(cp, StrLen(cp), buf)){
                  char digest[33];
                  hmac_md5(pw, pw.Length(), buf.begin(), buf.size(), digest);

                  Cstr_c s;
                  s<<un <<' ' <<digest;
                  Cstr_c s_b64 = text_utils::StringToBase64(s);
                  s_b64<<"\r\n";
                  mod.socket->SendString(s_b64);
                  ok = true;
               }
            }
            if(!ok && !BeginSMTPAuthentication(mod))
               fail = true;
            break;
         }
                        //flow...
      case C_mode_connection_smtp::ST_SMTP_AUTH_LOGIN:
      case C_mode_connection_smtp::ST_SMTP_AUTH_LOGIN_PHASE2:
         if(d0!=2){
            bool ok = false;
            if(d0==3){
               C_vector<char> b64;
#if 1
               {
                  if(mod.state!=C_mode_connection_smtp::ST_SMTP_AUTH_LOGIN_PHASE2){
                                 //username
                     const Cstr_c un = mod.GetSmtpUsername();
                     EncodeBase64((byte*)(const char*)un, un.Length(), b64);
                     mod.state = C_mode_connection_smtp::ST_SMTP_AUTH_LOGIN_PHASE2;
                  }else{
                     const Cstr_c &pw = mod.acc.smtp_password.Length() ? mod.acc.smtp_password : mod.acc.password;
                     EncodeBase64((byte*)(const char*)pw, pw.Length(), b64);
                  }
#else
               text_utils::SkipWS(cp);
               Cstr_c q;
               C_vector<byte> buf;
               if(text_utils::ReadToken(cp, q, " "))
               if(DecodeBase64(q, q.Length(), buf)){
                  buf.push_back(0);
                  Cstr_c s;
                  s = (char*)buf.begin();
                  s.ToLower();
                  if(s=="username:"){
                     const Cstr_c un = mod.GetSmtpUsername();
                     EncodeBase64((byte*)(const char*)un, un.Length(), b64);
                  }else
                  if(s=="password:"){
                     const Cstr_c &pw = mod.acc.smtp_password.Length() ? mod.acc.smtp_password : mod.acc.password;
                     EncodeBase64((byte*)(const char*)pw, pw.Length(), b64);
                  }else
                     assert(0);
#endif
                  if(b64.size()){
                     b64.push_back(0);
                     Cstr_c s;
                     s.Format("%\r\n") <<b64.begin();
                     mod.socket->SendString(s, 0, true);
                     ok = true;
                  }
               }
            }
            if(!ok && !BeginSMTPAuthentication(mod))
               fail = true;
            break;
         }
                        //flow...
      case C_mode_connection_smtp::ST_SMTP_AUTH:
         if(d0==2){
                        //auth ok, start sending
            //DrawOutlineRect(mod.rc, COL_LIGHT_GREY, 0xffa0ffa0);
            ConnectionDrawTitle(mod, GetText(TXT_PROGRESS_SENDING));
            if(!StartSendingNextMessage(mod)){
                           //no message to send, quit
               assert(0);
               SmtpCloseMode(mod);
               return false;
            }
         }else
         if(!BeginSMTPAuthentication(mod))
            fail = true;
         break;

      case C_mode_connection_smtp::ST_SMTP_MAIL:
      case C_mode_connection_smtp::ST_SMTP_RCPT:
         if(d0==2){
            if(*cp=='-') break;  //multi-line?

            if(mod.rcpt_index < dword(mod.send_recipients.size())){
                           //add next recipient
               Cstr_c cmd;
               cmd.Format("RCPT To:<%>\r\n") <<mod.send_recipients[mod.rcpt_index++];
               mod.socket->SendString(cmd);
               mod.state = mod.ST_SMTP_RCPT;
            }else{
                           //start sending data
               mod.socket->SendCString("DATA\r\n");
               mod.state = mod.ST_SMTP_DATA_BEGIN;
            }
         }else
            fail = true;
         break;

      case C_mode_connection_smtp::ST_SMTP_DATA_BEGIN:
         if(d0==3){
            if(*cp=='-') break;  //multi-line?

            SmtpPrepareAndSendMessageHeaders(mod);
            mod.state = mod.ST_SMTP_DATA_SEND;
         }
         break;

      case C_mode_connection_smtp::ST_SMTP_DATA_END:
         if(d0==2){
                        //mail successfully sent
            return SmtpAfterMailSent(mod);
         }else
            fail = true;
         break;
      }
   }
   if(fail){
      Cstr_w err; err.Copy(line.Begin());
      SmtpError(mod, err);
      return false;
   }
   return true;
}

//----------------------------

void C_mail_client::SmtpProcessInput(C_mode_connection_smtp &mod, S_user_input &ui, bool &redraw){

#ifdef USE_MOUSE
   ProcessMouseInSoftButtons(ui, redraw);
#endif
   if(ui.key==K_RIGHT_SOFT || ui.key==K_BACK || ui.key==K_ESC){
      mod.cancel_request = true;
      mod.state = mod.ST_USER_CANCELED;
      SmtpCloseMode(mod);
   }
}

//----------------------------

bool C_mail_client::StartSendingNextMessage(C_mode_connection_smtp &mod){

   const C_vector<S_message> &messages = mod.GetMessages();
   while(++mod.message_index < dword(messages.size())){
      const S_message &msg = messages[mod.message_index];
      if(msg.flags&msg.MSG_TO_SEND){
         mod.send_recipients.clear();
         if(ParseRecipients(msg.to_emails, mod.send_recipients) &&
            ParseRecipients(msg.cc_emails, mod.send_recipients) &&
            ParseRecipients(msg.bcc_emails, mod.send_recipients) &&
            mod.send_recipients.size()){

            if(mod.acc.send_msg_copy_to.Length()){
               ParseRecipients(mod.acc.send_msg_copy_to, mod.send_recipients);
            }
                              //start sending MAIL
            Cstr_c cmd;
            cmd.Format("MAIL From:<%>\r\n") <<mod.acc.primary_identity.email;
            mod.socket->SendString(cmd);
            mod.state = mod.ST_SMTP_MAIL;
            mod.rcpt_index = 0;

            ++mod.send_message_index;
            {
               Cstr_w str;
               str<<GetText(TXT_SENDING) <<L' ' <<mod.send_message_index <<L'/' <<mod.num_send_messages;
               ConnectionDrawAction(mod, str);
            }
            return true;
         }
      }
   }
   return false;
}

//----------------------------

void C_mail_client::SmtpPrepareAndSendMessageHeaders(C_mode_connection_smtp &mod){

   C_vector<S_message> &messages = mod.GetMessages();
   const S_message &msg = messages[mod.message_index];
   C_vector<char> buf;

   PrepareMailHeadersForSending(mod, mod, msg, buf);
   mod.socket->SendData(buf.begin(), (dword)buf.size());
}

//----------------------------

void C_mail_client::SmtpSocketEvent(C_mode_connection_smtp &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){

   ConnectionDrawSocketEvent(mod, ev);
   //ConnectionDrawFloatDataCounters(mod);

   switch(ev){
   case C_socket_notify::SOCKET_CONNECTED:
      mod.state = mod.ST_WAIT_CONNECT_OK;
      //mod.socket->Receive();
      break;
   case C_socket_notify::SOCKET_DATA_SENT:
      if(mod.state==mod.ST_SMTP_DATA_SEND){
         if(!SendNextMessageData(mod, mod, mod.socket, mod.GetMessages()[mod.message_index]))
            break;
                              //send next data now
         mod.socket->SendData(".\r\n", 3);
         mod.state = mod.ST_SMTP_DATA_END;
         break;
      }
      //mod.socket->_Receive();
      break;
   case C_socket_notify::SOCKET_SSL_HANDSHAKE:
      AfterSmtpConnected(mod);
      break;
   case C_socket_notify::SOCKET_DATA_RECEIVED:
      {
         C_socket::t_buffer line;
         while(mod.socket->GetLine(line)){
            if(!ProcessLineSmtp(mod, line))
               break;
#ifndef _DEBUG
            if(&mod!=mode)
               return;
#endif
         }
      }
      break;
   case C_socket_notify::SOCKET_ERROR:
      SmtpError(mod, GetErrorName(mod.socket->GetSystemErrorText()));
      break;
   }
}

//----------------------------
