#ifdef __SYMBIAN32__
#include <E32Std.h>
#endif

#include "..\Main.h"
#include "Main_Email.h"
#include <C_file.h>
#include <Directory.h>
#include <Base64.h>
#include <UI\TextEntry.h>

//----------------------------
                              //version of messages binary file (equals to program version * 100)
const int MESSAGES_MIN_VER = 282, MESSAGES_SAVE_VERSION = 336;

#if defined __SYMBIAN_3RD__ && !defined _DEBUG
                              //must be in lower-case!
const wchar MAIL_PATH[] = L"\\system\\data\\profimail\\mail\\";
#else
const wchar MAIL_PATH[] = L"mail\\";
#endif
#define USE_COMPRESSED_MESSAGES_BIN //compress account data which store messages

//----------------------------

static void ScanDir(const wchar *dir, C_vector<Cstr_w> &files, C_vector<Cstr_w> &dirs){

   C_dir d;
   if(d.ScanBegin(dir)){
      dword atts;
      const wchar *fn;
      while((fn = d.ScanGet(&atts), fn)){
         Cstr_w s;
         s<<dir <<fn;
         if(atts&C_file::ATT_DIRECTORY){
            s<<'\\';
            dirs.push_back(s);
         }else{
#ifndef UNIX_FILE_SYSTEM
            s.ToLower();
#endif
            files.push_back(s);
         }
      }
   }
}

//----------------------------

void S_attachment::Save(C_file &fl) const{

   file_utils::WriteString(fl, filename);
   file_utils::WriteStringAsUtf8(fl, suggested_filename);
   file_utils::WriteString(fl, content_id);
      
   fl.WriteDword(file_size);
   fl.WriteByte(char(content_encoding));
   fl.WriteWord(part_index);
}

//----------------------------

bool S_attachment::Load(C_file &fl, dword save_version){

   byte b;
   if(file_utils::ReadString(fl, filename) &&
      file_utils::ReadStringAsUtf8(fl, suggested_filename) &&
      file_utils::ReadString(fl, content_id) &&
      fl.ReadDword(file_size) &&
      fl.ReadByte(b) &&
      fl.ReadWord(part_index)){

      content_encoding = (E_CONTENT_ENCODING)b;
      return true;
   }
   return false;
}

//----------------------------

bool S_message_header::operator ==(const S_message_header &hdr) const{
                              //not complete compare, just suitable for our needs
   return (
      pop3_uid==hdr.pop3_uid &&
      imap_uid==hdr.imap_uid &&
      size==hdr.size &&
      subject==hdr.subject &&
      sender.email==hdr.sender.email &&
      sender.display_name==hdr.sender.display_name &&
      message_id==hdr.message_id &&
      date==hdr.date &&
      1);
}

//----------------------------

void S_message::Save(C_file &fl, bool is_imap) const{

   fl.WriteDword(size);
   fl.WriteDword(flags);
   fl.WriteDword(date);
   dword num_a = attachments.Size();
   dword num_ai = inline_attachments.Size();
   fl.WriteWord(word(num_a));
   fl.WriteWord(word(num_ai));
   fl.WriteByte(byte(body_coding&0xff));

   if(is_imap){
      fl.WriteDword(imap_uid);
      fl.WriteDword(imap_text_part_size);
   }else{
      file_utils::WriteString(fl, pop3_uid);
      fl.WriteWord(word(pop3_server_msg_index));
   }
   file_utils::WriteString(fl, subject);
   file_utils::WriteString(fl, sender.email);
   file_utils::WriteString(fl, sender.display_name);
   file_utils::WriteString(fl, to_emails);
   file_utils::WriteString(fl, to_names);
   file_utils::WriteString(fl, cc_emails);
   file_utils::WriteString(fl, bcc_emails);
   file_utils::WriteString(fl, body_filename);
   file_utils::WriteString(fl, reply_to_email);
   file_utils::WriteString(fl, message_id);
   if(flags&(MSG_TO_SEND|MSG_NEED_UPLOAD|MSG_DRAFT))
      file_utils::WriteString(fl, our_message_id);
   file_utils::WriteString(fl, references);
   fl.WriteByte(thread_level);
                           //write attachments
   for(dword i=0; i<num_a; i++)
      attachments[i].Save(fl);

   for(dword i=0; i<num_ai; i++)
      inline_attachments[i].Save(fl);
}

//----------------------------

bool S_message::Load(C_file &fl, dword save_version, bool is_imap){

   if(!fl.ReadDword(size)) return false;
   if(!fl.ReadDword(flags)) return false;
   if(!fl.Read(&date, sizeof(date))) return false;

   word num_a;
   if(!fl.ReadWord(num_a)) return false;
   word num_ai;
   if(!fl.ReadWord(num_ai)) return false;
   byte b;
   if(!fl.ReadByte(b)) return false;
   body_coding = (E_TEXT_CODING)b;

   if(is_imap){
      if(!fl.ReadDword(imap_uid)) return false;
      if(!fl.ReadDword((dword&)imap_text_part_size)) return false;
   }else{
      if(!file_utils::ReadString(fl, pop3_uid)) return false;
      short sh;
      if(!fl.ReadWord((word&)sh)) return false;
      pop3_server_msg_index = sh;
   }
   
   if(!file_utils::ReadString(fl, subject)) return false;
   if(!file_utils::ReadString(fl, sender.email)) return false;
   if(!file_utils::ReadString(fl, sender.display_name)) return false;
   if(!file_utils::ReadString(fl, to_emails)) return false;
   if(!file_utils::ReadString(fl, to_names)) return false;
   if(!file_utils::ReadString(fl, cc_emails)) return false;
   if(!file_utils::ReadString(fl, bcc_emails)) return false;
   if(!file_utils::ReadString(fl, body_filename)) return false;
   if(!file_utils::ReadString(fl, reply_to_email)) return false;
   if(!file_utils::ReadString(fl, message_id)) return false;
   if(save_version>=315){
      if(flags&(MSG_TO_SEND|MSG_NEED_UPLOAD|MSG_DRAFT))
      if(!file_utils::ReadString(fl, our_message_id)) return false;
   }
   if(save_version>=336){
      if(!file_utils::ReadString(fl, references)) return false;
      if(!fl.ReadByte(thread_level)) return false;
   }

   if(save_version<305){
                              //fix bad fields in drafts/sent
      if((flags&(MSG_DRAFT|MSG_TO_SEND|MSG_SENT)) && sender.email.Length()){
         to_emails = sender.email; sender.email.Clear();
         to_names = sender.display_name; sender.display_name.Clear();
      }
   }
   attachments.Resize(num_a);
   for(dword i=0; i<num_a; i++){
      if(!attachments[i].Load(fl, save_version))
         return false;
   }
   inline_attachments.Resize(num_ai);
   for(dword i=0; i<num_ai; i++){
      if(!inline_attachments[i].Load(fl, save_version))
         return false;
   }
   return true;
}

//----------------------------

static bool CopyFile(const C_zip_package *dta, const wchar *src, const wchar *dst){

   C_file_zip ck_src;
   if(!ck_src.Open(src, dta))
      return false;
   C_file ck_dst;
   if(!ck_dst.Open(dst, C_file::FILE_WRITE))
      return false;

   C_buffer<byte> buf;
   int sz = ck_src.GetFileSize();
   buf.Resize(sz);
   ck_src.Read(buf.Begin(), sz);
   ck_dst.Write(buf.Begin(), sz);

   return true;
}

//----------------------------

void C_mail_client::CreateDemoMail(C_message_container &cnt, const C_zip_package *dta, const char *subject, dword size, dword date, const wchar *fnames) const{

   //int n = cnt.messages.size();
   cnt.messages.push_back(S_message());
   S_message &msg = cnt.messages.back();
   msg.subject = subject;
   msg.sender.display_name = "Lonely Cat Games";
   msg.sender.email = "noreply@lonelycatgames.com";
   msg.size = size;
   msg.date = date;
   msg.flags = msg.MSG_HTML;

   Cstr_w mail_path = cnt.GetMailPath(mail_data_path);
   C_file::MakeSurePathExists(mail_path);
   {
      Cstr_w src; src<<L"Demo\\" <<fnames;
      Cstr_w dst_name = fnames;
      file_utils::MakeUniqueFileName(mail_path, dst_name, L"");
      Cstr_w full_name; full_name<<mail_path <<dst_name;
      ::CopyFile(dta, src, full_name);
      msg.body_filename = dst_name.ToUtf8();
      fnames += StrLen(fnames) + 1;
   }
                              //copy attachments
   int cid = -1;
   while(*fnames){
      bool is_inline = false;
      if(*fnames=='*'){
         ++fnames;
         is_inline = true;
         ++cid;
      }
      Cstr_w dst_name;
      file_utils::MakeUniqueFileName(mail_path, dst_name, text_utils::GetExtension(fnames));
      Cstr_w full_name; full_name<<mail_path <<dst_name;
      Cstr_w src; src<<L"Demo\\" <<fnames;
      ::CopyFile(dta, src, full_name);
      C_buffer<S_attachment> &atts = is_inline ? msg.inline_attachments : msg.attachments;
      atts.Resize(atts.Size() + 1);
      S_attachment &att = atts.Back();
      att.filename = full_name.ToUtf8();
      att.suggested_filename = fnames;
      att.content_id<<cid;

      fnames += StrLen(fnames) + 1;
   }
}

//----------------------------

void C_mail_client::CreateDemoMail(C_message_container &cnt){

   const C_smart_ptr<C_zip_package> dta = CreateDtaFile();

   S_date_time_x date; date.GetCurrent();
   //date.minute -= 2;
   CreateDemoMail(cnt, dta, "Welcome to ProfiMail!", 58000, date.sort_value, L"demo.html\0*lcg.jpg\0*jukebox.jpg\0*smartmovie.jpg\0*xplore.jpg\0");
   /*
   ++date.minute;
   CreateDemoMail(cnt, dta, "Learn about SmartMovie", 37200, date.sort_value, L"demo2.html\0SM_Player1.jpg\0SM_Converter.jpg\0more.html\0SM_Player2.jpg\0On Phone.jpg\0");
   ++date.minute;
   CreateDemoMail(cnt, dta, "LCG Jukebox - your music player", 18930, date.sort_value, L"demo3.html\0*LCGJukeBox_Screen.png\0LCGJukeBox_Screen.png\0LCGJukeBox_mini.png\0");
   */
   cnt.SaveMessages(mail_data_path, true);
}

//----------------------------

C_message_container::C_message_container():
   is_imap(true),
   msg_folder_id(0),
   flags(0),
   last_msg_cleanup_day(0),
   loaded(false),
   need_save(false),
   stats_dirty(false),
   parent_folder(NULL),
   imap_uid_validity(0)
{
   MemSet(stats, 0, sizeof(stats));
}

//----------------------------

void C_message_container::MakeDirty(){
   need_save = true;
   stats_dirty = true;
}

//----------------------------

bool C_mail_client::LoadMessages(C_message_container &cnt) const{

   bool ret = cnt.LoadMessages(mail_data_path);
   if(ret){
      if(cnt.flags&cnt.FLG_NEED_SORT){
         cnt.flags &= ~cnt.FLG_NEED_SORT;
         cnt.MakeDirty();
         SortMessages(cnt.messages, cnt.is_imap);
      }
   }else
      cnt.ResetStats();
   return ret;
}

//----------------------------

bool C_message_container::LoadMessages(const Cstr_w &mail_data_path){

   if(loaded)
      return true;
   Cstr_w fn = GetMailPath(mail_data_path); fn<<L"messages.bin";
#ifdef USE_COMPRESSED_MESSAGES_BIN
   C_file_raw_zip_read fl;
#else
   C_file fl;
#endif
   do{
      if(!fl.Open(fn)){
#ifdef USE_COMPRESSED_MESSAGES_BIN
                              //try uncompressed version
         if(!fl.C_file::Open(fn))
#endif
         break;
      }
      dword ver;
      if(!fl.ReadDword(ver))
         break;
      if(ver<MESSAGES_MIN_VER || ver>MESSAGES_SAVE_VERSION)
         break;
      if(!fl.ReadDword(last_msg_cleanup_day))
         break;
      dword num;
      if(!fl.ReadDword(num))
         break;
      messages.resize(num);
      dword i;
      for(i=0; i<num; ){
         S_message &msg = messages[i];
         if(!msg.Load(fl, ver, is_imap))
            break;
                              //don't keep deleted local messages
         if(msg.IsDeleted() && !msg.IsServerSynced()){
            msg = S_message();
         }else
            i++;
      }
      messages.resize(i);
      loaded = true;
                              //keep stats in sync with reality - reset now so that they're rebuilt
      ResetStats();
      return true;
   }while(false);
   loaded = true;
   return false;
}

//----------------------------

void C_message_container::SaveMessages(const Cstr_w &mail_data_path, bool force){

   if(!need_save && !force)
      return;
   BuildMessagesStatistics();
   Cstr_w mail_path = GetMailPath(mail_data_path);
   Cstr_w fn = mail_path; fn<<L"messages.bin";
   if(!messages.size()){
      C_file::DeleteFile(fn);
      C_dir::RemoveDirectory(mail_path);
   }else{
#ifdef USE_COMPRESSED_MESSAGES_BIN
      C_file_raw_zip_write fl;
      if(fl.Open(fn, C_file_raw_zip_write::COMPRESSION_FAST))
#else
      C_file fl;
      if(fl.Open(fn, C_file::FILE_WRITE|C_file::FILE_WRITE_CREATE_PATH))
#endif
      {
         fl.WriteDword(MESSAGES_SAVE_VERSION);

         fl.WriteDword(last_msg_cleanup_day);
         dword num = messages.size();
         fl.WriteDword(num);
         for(dword i=0; i<num; i++)
            messages[i].Save(fl, is_imap);
         fl.WriteFlush();
         fl.Close();
      }
   }
   need_save = false;
}

//----------------------------

void C_message_container::SaveAndUnloadMessages(const Cstr_w &mail_data_path){

   if(loaded){
      if(stats_dirty){
         BuildMessagesStatistics();
      }
                              //check if it's time to cleanup folder
      S_date_time td;
      td.GetCurrent();
      int d = td.GetSeconds() / (60*60*24);
#ifndef _DEBUG
      if(Abs(int(last_msg_cleanup_day)-d) >= 7)
#endif
      {
         last_msg_cleanup_day = d;
         CleanupMailFiles(mail_data_path);
         need_save = true;
      }
   }
   if(need_save){
      SaveMessages(mail_data_path);
   }
   assert(!need_save);
   loaded = false;
   messages.clear();
}

//----------------------------

void C_message_container::CleanupMailFiles(const Cstr_w &mail_data_path){

   assert(loaded);
   Cstr_w mail_path = GetMailPath(mail_data_path);
   //LOG_RUN("Cleanup");
   //LOG_RUN(mail_path.ToUtf8());
                              //get contents of folder
   C_vector<Cstr_w> cnt_files, cnt_dirs;
   ScanDir(mail_path, cnt_files, cnt_dirs);
   //LOG_RUN_N("Scan files", cnt_files.size());
                              //can't have any sub-dirs
   for(int i=cnt_dirs.size(); i--; ){
      const Cstr_w &dir = cnt_dirs[i];
#if defined _DEBUG_ && defined WIN32
      Cstr_w s;
      s.Format(L"Unused dir: %") <<dir;
      Info(s);
#endif
      LOG_RUN("Unused dir"); LOG_RUN(dir.ToUtf8());
      C_dir::RemoveDirectory(dir, true);
   }
   cnt_dirs.clear();
                              //check all messages
   for(int msg_i=messages.size(); msg_i--; ){
      const S_message &msg = messages[msg_i];
      if(!msg.HasBody())
         continue;
      int num_a = msg.attachments.Size(), num_ai = msg.inline_attachments.Size();
                        //check all message's files
      for(int att_i=num_a+num_ai+1; att_i--; ){
         Cstr_w s = (att_i<num_a) ? msg.attachments[att_i].filename.FromUtf8() :
            att_i-num_a < num_ai ? msg.inline_attachments[att_i-num_a].filename.FromUtf8() : msg.body_filename.FromUtf8();
         if(!s.Length() && att_i==num_a+num_ai)
            continue;
         if((msg.flags&(msg.MSG_DRAFT|msg.MSG_TO_SEND)) && att_i<num_a+num_ai)
            continue;
                        //allow empty files
         if(!s.Length())
            continue;
#ifndef UNIX_FILE_SYSTEM
         s.ToLower();
#endif
         if(att_i==num_a+num_ai){
            Cstr_w full_name; full_name<<mail_path <<s;
            s = full_name;
         }
         int i;
         for(i=cnt_files.size(); i--; ){
            if(cnt_files[i]==s){
               cnt_files.remove_index(i);
               break;
            }
         }
                        //file must exist
         if(i==-1){
#if defined _DEBUG_ && defined WIN32
            Cstr_w ss;
            ss.Format(L"File missing: '%'") <<s;
            Info(ss);
#endif
            LOG_RUN("File missing"); LOG_RUN(s.ToUtf8());
         }
      }
   }
   Cstr_w bin; bin<<mail_path <<L"messages.bin";
                        //remove unused files
   for(int i=cnt_files.size(); i--; ){
      const Cstr_w &fn = cnt_files[i];
      if(fn==bin)
         continue;
#if defined _DEBUG_ && defined WIN32
      Cstr_w s;
      s.Format(L"Unused file: %") <<fn;
      Info(s);
#endif
      LOG_RUN("Unused file"); LOG_RUN(fn.ToUtf8());
      C_file::DeleteFile(fn);
   }
}

//----------------------------

bool C_message_container::DeleteMessageFiles(const Cstr_w &mail_data_path, S_message &msg) const{

   bool mod = DeleteMessageBody(mail_data_path, msg);
   mod = (DeleteMessageAttachments(msg, true) || mod);
   return mod;
}

//----------------------------

bool C_message_container::DeleteMessageBody(const Cstr_w &mail_data_path, S_message &msg) const{

   bool mod = false;
   if(msg.HasBody()){
      Cstr_w body_fname;
      body_fname<<GetMailPath(mail_data_path) <<msg.body_filename.FromUtf8();
      C_file::DeleteFile(body_fname);
      msg.body_filename.Clear();
      mod = true;
   }
   int num_a = msg.inline_attachments.Size();
   if(num_a){
      while(num_a--)
         C_file::DeleteFile(msg.inline_attachments[num_a].filename.FromUtf8());
      msg.inline_attachments.Clear();
      mod = true;
   }
   if(msg.flags&msg.MSG_PARTIAL_DOWNLOAD){
      msg.flags &= ~msg.MSG_PARTIAL_DOWNLOAD;
      mod = true;
   }
   return mod;
}

//----------------------------

bool C_message_container::DeleteMessageAttachments(S_message &msg, bool files_only) const{

   int num_a = msg.attachments.Size();
   if(num_a){
                              //delete attachment files for "normal" messages (not drafts, because they just reference attachment files)
      if(!(msg.flags&(msg.MSG_DRAFT|msg.MSG_TO_SEND))){
         while(num_a--){
            S_attachment &att = msg.attachments[num_a];
            if(att.IsDownloaded()){
               C_file::DeleteFile(att.filename.FromUtf8());
               att.filename.Clear();
            }
         }
      }
      if(!files_only)
         msg.attachments.Clear();
      return true;
   }
   return false;
}

//----------------------------

void S_message::MoveMessageFiles(const Cstr_w &mail_data_path, C_message_container &from, C_message_container &to){

   Cstr_w from_dir = from.GetMailPath(mail_data_path), to_dir = to.GetMailPath(mail_data_path);
   C_file::MakeSurePathExists(to_dir);
   if(HasBody()){
      Cstr_w s, d;
      s<<from_dir<<body_filename.FromUtf8();
      d<<to_dir<<body_filename.FromUtf8();
      C_file::RenameFile(s, d);
   }
   if(!(flags&(MSG_DRAFT|MSG_TO_SEND))){
      for(int ai=0; ai<2; ai++){
         C_buffer<S_attachment> &atts = !ai ? attachments : inline_attachments;
         for(int i=atts.Size(); i--; ){
            Cstr_w s = atts[i].filename.FromUtf8();
            if(s.Length()){
               Cstr_w d; d<<to_dir <<file_utils::GetFileNameNoPath(s);
               C_file::RenameFile(s, d);
               atts[i].filename = d.ToUtf8();
            }
         }
      }
   }
}

//----------------------------

bool S_message::UpdateFlags(dword new_flags){

   bool need_update = false;
   dword FLAGS_MASK =
      S_message::MSG_READ | S_message::MSG_REPLIED | S_message::MSG_DELETED | S_message::MSG_FLAGGED | S_message::MSG_DRAFT | S_message::MSG_FORWARDED;
      //| S_message::MSG_RECENT;

   if(flags&S_message::MSG_IMAP_REPLIED_DIRTY) FLAGS_MASK &= ~S_message::MSG_REPLIED;
   if(flags&S_message::MSG_IMAP_READ_DIRTY)    FLAGS_MASK &= ~S_message::MSG_READ;
   if(flags&S_message::MSG_IMAP_FLAGGED_DIRTY) FLAGS_MASK &= ~S_message::MSG_FLAGGED;
   if(flags&S_message::MSG_DELETED_DIRTY) FLAGS_MASK &= ~S_message::MSG_DELETED;
   if(flags&S_message::MSG_IMAP_FORWARDED_DIRTY) FLAGS_MASK &= ~S_message::MSG_FORWARDED;

   new_flags &= FLAGS_MASK;
   if((flags&FLAGS_MASK) != new_flags){
      flags &= ~FLAGS_MASK;
      flags |= new_flags;
      need_update = true;
   }
   return need_update;
}

//----------------------------

bool S_message::HasMultipleRecipients(const S_account_settings *acc) const{
   //check if there're multiple recipients
   C_vector<Cstr_c> addresses;
   GetMessageRecipients(*this, acc ? acc->primary_identity.email : Cstr_c(), addresses);
   return (addresses.size()!=0);
}

//----------------------------

void C_message_container::DeleteContainerFiles(const Cstr_w &mail_data_path){

   assert(msg_folder_id);
   Cstr_w dir;
   dir<<mail_data_path <<MAIL_PATH <<msg_folder_id;
   C_dir::RemoveDirectory(dir, true);

   messages.clear();
   msg_folder_id = 0;
}

//----------------------------

Cstr_w C_message_container::GetMailPath(const Cstr_w &mail_data_path) const{

   assert(msg_folder_id);
   Cstr_w d;
   d<<mail_data_path;
#ifndef UNIX_FILE_SYSTEM
   d.ToLower();             //can't be on some file systems
#endif
   d<<MAIL_PATH <<msg_folder_id <<'\\';
   return d;
}

//----------------------------

void C_message_container::ResetStats(){
   MemSet(stats, 0, sizeof(stats));
   stats_dirty = true;
}

//----------------------------

void C_message_container::BuildMessagesStatistics(){

   ResetStats();
   for(int i=messages.size(); i--; ){
      dword st = messages[i].flags;
      if(st&S_message::MSG_HIDDEN)
         continue;
      if(!(st&S_message::MSG_DELETED)){
         if(st&S_message::MSG_DRAFT)
            ++stats[STAT_DRAFTS];
         else
         if(st&S_message::MSG_TO_SEND)
            ++stats[STAT_TO_SEND];
         else
         if(st&S_message::MSG_SENT)
            ++stats[STAT_SENT];
         else
         if(st&S_message::MSG_READ)
            ++stats[STAT_READ];
         else
            ++stats[STAT_UNREAD];

         if((st&S_message::MSG_RECENT) && !(st&S_message::MSG_READ))
            ++stats[STAT_RECENT];
      }
   }
   stats_dirty = false;
}

//----------------------------

const dword *C_message_container::GetMessagesStatistics() const{

   if(stats_dirty)
      const_cast<C_message_container&>(*this).BuildMessagesStatistics();
   return stats;
}

//----------------------------

void C_message_container::CollectHierarchyStatistics(dword stats_sum[], bool show_hidden) const{

   for(int i=STAT_LAST; i--; )
      stats_sum[i] += stats[i];

   if(!IsExpanded())
   for(int i=subfolders.Size(); i--; ){
      const C_message_container &fld = *subfolders[i];
      if(!show_hidden && fld.IsHidden())
         continue;
      fld.CollectHierarchyStatistics(stats_sum, show_hidden);
   }
}

//----------------------------

static bool CopyFile(const wchar *src, const wchar *dst){

   C_file ck_src;
   if(!ck_src.Open(src))
      return false;
   C_file ck_dst;
   if(!ck_dst.Open(dst, C_file::FILE_WRITE))
      return false;

   const int BUF_SIZE = 0x4000;
   byte *buf = new(true) byte[BUF_SIZE];
   int sz = ck_src.GetFileSize();
   C_file::E_WRITE_STATUS err = C_file::WRITE_OK;
   while(sz){
      int copy_sz = Min(sz, BUF_SIZE);
      ck_src.Read(buf, copy_sz);
      err = ck_dst.Write(buf, copy_sz);
      if(err)
         break;
      sz -= copy_sz;
   }
   delete[] buf;
   if(err || ck_dst.WriteFlush()){
      ck_dst.Close();
      C_file::DeleteFile(dst);
      return false;
   }
   return true;
}

//----------------------------

void C_message_container::MoveMessageTo(const Cstr_w &mail_data_path, S_message &src, const C_message_container &dst_cnt, S_message &dst) const{

   const Cstr_w dst_mail_path = dst_cnt.GetMailPath(mail_data_path);
                     //move all files
   if(src.body_filename.Length()){
      Cstr_w old = GetMailPath(mail_data_path); old<<src.body_filename.FromUtf8();
      //Cstr_w nw = dst_mail_path; nw<<src.body_filename.FromUtf8();
      Cstr_w fn, nw;
      file_utils::MakeUniqueFileName(dst_mail_path, fn, L"txt");
      nw<<dst_mail_path <<fn;
      C_file::MakeSurePathExists(nw);
      C_file::RenameFile(old, nw);
      src.body_filename = fn.ToUtf8();
   }
   for(int i=src.attachments.Size(); i--; ){
      S_attachment &att = src.attachments[i];
      Cstr_w old = att.filename.FromUtf8();
      //if(copy_files){
         Cstr_w fn, nw;
         file_utils::MakeUniqueFileName(dst_mail_path, fn, text_utils::GetExtension(att.filename.FromUtf8()));
         nw<<dst_mail_path <<fn;
         if(::CopyFile(att.filename.FromUtf8(), nw))
            att.filename = nw.ToUtf8();
         else
            att.filename.Clear();
      /*}else{
         int ai = old.FindReverse('\\');
         if(ai!=-1){
            //Cstr_w nw = dst_mail_path; nw<<old.RightFromPos(ai+1);
            Cstr_w nw;
            {
               Cstr_w fn;
               file_utils::MakeUniqueFileName(dst_mail_path, fn, text_utils::GetExtension(old.RightFromPos(ai+1)));
               nw<<dst_mail_path <<fn;
            }
            C_file::MakeSurePathExists(nw);
            C_file::RenameFile(old, nw);
            old = nw;
         }
      }*/
   }
   dst = src;
   dst.flags &= ~(S_message::MSG_SERVER_SYNC | S_message::MSG_HIDDEN);
   src.body_filename.Clear();
   src.attachments.Clear();
   src.inline_attachments.Clear();
   if(src.flags&S_message::MSG_SERVER_SYNC){
      src.flags |= S_message_header::MSG_DELETED|S_message_header::MSG_DELETED_DIRTY;
   }else{
                     //todo: test
      //assert(0);
      //DeleteMessage(acc_saved, mod.selection);
      //Mailbox_RecalculateDisplayArea(mod);
   }
}

//----------------------------

struct S_sort_msg_help{
   S_message *msgs;           //just used with selection to detect message index
   bool is_imap;
   int *selection;
   const C_mail_client *_this;
};

//----------------------------

int C_mail_client::CompareMessages(const void *m1, const void *m2, void *context){

   const S_sort_msg_help *hlp = (S_sort_msg_help*)context;
   const C_mail_client *_this = hlp->_this;
   const S_message &msg1 = *(S_message*)m1;
   const S_message &msg2 = *(S_message*)m2;

   bool hidden1 = (msg1.flags&msg1.MSG_HIDDEN);
   bool hidden2 = (msg2.flags&msg1.MSG_HIDDEN);
                              //hidden messages are always at end
   if(hidden1 != hidden2)
      return hidden1 ? 1 : -1;

   if(_this->config_mail.tweaks.show_only_unread_msgs){
      //Fatal("!");
                              //unread messages are treated as hidden
      hidden1 = (msg1.IsRead() && !(msg1.flags&S_message::MSG_DRAFT) && !(msg1.flags&S_message::MSG_TO_SEND));
      hidden2 = (msg2.IsRead() && !(msg2.flags&S_message::MSG_DRAFT) && !(msg2.flags&S_message::MSG_TO_SEND));
                              //hidden messages are always at end
      if(hidden1 != hidden2)
         return hidden1 ? 1 : -1;
   }

   int cmp;
   switch(_this->config_mail.sort_mode){
   case S_config_mail::SORT_BY_SENDER:
      {
         const char *s1 = msg1.sender.display_name, *s2 = msg2.sender.display_name;
         if(!*s1) s1 = msg1.sender.email;
         if(!*s2) s2 = msg2.sender.email;
         cmp = text_utils::CompareStringsNoCase(s1, s2);
         if(!cmp){
            if(msg1.date < msg2.date) cmp = -1;
            else if(msg1.date > msg2.date) cmp = 1;
         }
      }
      break;
   case S_config_mail::SORT_BY_SUBJECT:
      {
         const char *s1 = msg1.subject, *s2 = msg2.subject;
         SkipReFwd(s1);
         SkipReFwd(s2);
         cmp = text_utils::CompareStringsNoCase(s1, s2);
         if(!cmp){
            if(msg1.date < msg2.date) cmp = -1;
            else if(msg1.date > msg2.date) cmp = 1;
         }
      }
      break;
   case S_config_mail::SORT_BY_RECEIVE_ORDER:
      if(hlp->is_imap){
         cmp = msg1.imap_uid<msg2.imap_uid ? -1 : msg1.imap_uid>msg2.imap_uid ? 1 : 0;
      }else{
         cmp = msg1.pop3_server_msg_index<msg2.pop3_server_msg_index ? -1 : msg1.pop3_server_msg_index>msg2.pop3_server_msg_index ? 1 : 0;
         if(!cmp)
            cmp = StrCmp(msg1.pop3_uid, msg2.pop3_uid);
      }
      break;
   default:                   //date
      cmp = 0;
      if(msg1.date < msg2.date) cmp = -1;
      else if(msg1.date > msg2.date) cmp = 1;
   }
   if(_this->config_mail.sort_descending)
      cmp = -cmp;
   return cmp;
}

//----------------------------

static void SwapMessages(void *m1, void *m2, dword w, void *context){

   if(m1!=m2){
      S_message &msg1 = *(S_message*)m1;
      S_message &msg2 = *(S_message*)m2;
      Swap(msg1, msg2);
                              //adjust selection index
      const S_sort_msg_help &hlp = *(S_sort_msg_help*)context;
      int i1 = &msg1 - hlp.msgs;
      int i2 = &msg2 - hlp.msgs;
      if(i1==*hlp.selection)
         *hlp.selection = i2;
      else
      if(i2==*hlp.selection)
         *hlp.selection = i1;
   }
}

//----------------------------

typedef dword t_hash;
//----------------------------

#if 0

class C_hash_table{
public:
   C_hash_table()
   {}

   struct S_pair{
      Cstr_c key;
      dword index;
   };
   C_vector<S_pair> vals;

   static t_hash GetHash(const char *s, dword length){
      return 0;
   }

   void Insert(t_hash hash, const char *key, dword key_len, int index){
      S_pair p;
      p.index = index;
      p.key.Allocate(key, key_len);
      vals.push_back(p);
   }
   int Find(dword hash, const char *key, dword key_len) const{
      for(int i=vals.size(); i--; ){
         const S_pair &p = vals[i];
         if(p.key.Length()==key_len && !MemCmp(p.key, key, key_len))
            return p.index;
      }
      return -1;
   }
};

#else
//----------------------------
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

#define hash_mix(a, b, c) { \
  a -= c; a ^= rot(c, 4); c += b; \
  b -= a; b ^= rot(a, 6); a += c; \
  c -= b; c ^= rot(b, 8); b += a; \
  a -= c; a ^= rot(c,16); c += b; \
  b -= a; b ^= rot(a,19); a += c; \
  c -= b; c ^= rot(b, 4); b += a; \
}

#define hash_final(a, b, c) { \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c, 4); \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}


class C_hash_table{

   static const int TABLE_SIZE_BITS = 16;

   struct S_entry{
      dword hash;
      S_entry *next;
      const char *key;        //key is string - just pointer to other stored string, and it length
      dword key_len;
      int value;              //-1 = invalid

      S_entry():
         next(NULL),
         value(-1)
      {}
      inline bool IsEmpty() const{ return (value==-1); }
   };
   S_entry *base_tab;
   struct S_alloc_entry: public S_entry{
      S_alloc_entry *next_alloc;
   };
   S_alloc_entry *alloced;    //linked allocated entries
   dword alloced_pos;

//----------------------------
#ifdef _DEBUG
                              //some stats
   int num_alloc_entry;
#endif
public:
//----------------------------
   C_hash_table():
#ifdef _DEBUG
      num_alloc_entry(0),
#endif
      alloced_pos(0),
      alloced(NULL)
   {
      base_tab = new(true) S_entry[1<<TABLE_SIZE_BITS];
   }
//----------------------------
   ~C_hash_table(){
      delete[] base_tab;
      for(S_alloc_entry *ae=alloced; ae; ){
         S_alloc_entry *next = ae->next_alloc;
         delete[] ae;
         ae = next;
      }
   }
//----------------------------
   static dword GetHash(const char *key, dword length){
      dword a, b, c;
         //Set up the internal state
      a = b = c = 0xdeadbeef + ((dword)length);
      if(!(dword(key)&3)){

         const dword *k = (const dword*)key;         //read 32-bit chunks

         //------ all but last block: aligned reads and affect 32 bits of (a,b,c)
         while(length>12){
            a += k[0];
            b += k[1];
            c += k[2];
            hash_mix(a, b, c);
            length -= 12;
            k += 3;
         }
         //handle the last (probably partial) block
         switch(length){
         case 12: c += k[2]; b += k[1]; a += k[0]; break;
         case 11: c += k[2]&0xffffff; b += k[1]; a += k[0]; break;
         case 10: c += k[2]&0xffff; b += k[1]; a += k[0]; break;
         case 9: c += k[2]&0xff; b += k[1]; a += k[0]; break;
         case 8: b += k[1]; a += k[0]; break;
         case 7: b += k[1]&0xffffff; a += k[0]; break;
         case 6: b += k[1]&0xffff; a += k[0]; break;
         case 5: b += k[1]&0xff; a += k[0]; break;
         case 4: a += k[0]; break;
         case 3: a += k[0]&0xffffff; break;
         case 2: a += k[0]&0xffff; break;
         case 1: a += k[0]&0xff; break;
         case 0: return c;              /* zero length strings require no mixing */
         }
      }else
      if(!(dword(key)&1)){
         const word *k = (const word*)key;         //read 16-bit chunks
               //--------------- all but last block: aligned reads and different mixing
         while(length>12){
            a += k[0] + (dword(k[1])<<16);
            b += k[2] + (dword(k[3])<<16);
            c += k[4] + (dword(k[5])<<16);
            hash_mix(a, b, c);
            length -= 12;
            k += 6;
         }
                              //handle the last (probably partial) block
         const byte *k8 = (const byte*)k;
         switch(length){
         case 12:
            c += k[4] + (dword(k[5])<<16);
            b += k[2] + (dword(k[3])<<16);
            a += k[0] + (dword(k[1])<<16);
            break;
         case 11:
            c += dword(k8[10])<<16;     //fall through
         case 10:
            c += k[4];
            b += k[2]+(dword(k[3])<<16);
            a += k[0]+(dword(k[1])<<16);
            break;
         case 9:
            c += k8[8];                      //fall through
         case 8:
            b += k[2]+(dword(k[3])<<16);
            a += k[0]+(dword(k[1])<<16);
            break;
         case 7:
            b += dword(k8[6])<<16;      //fall through
         case 6:
            b += k[2];
            a += k[0]+(dword(k[1])<<16);
            break;
         case 5:
            b += k8[4];                      //fall through
         case 4:
            a += k[0]+(dword(k[1])<<16);
            break;
         case 3:
            a += dword(k8[2])<<16;      //fall through
         case 2:
            a += k[0];
            break;
         case 1:
            a += k8[0];
            break;
         case 0:
            return c;                     //zero length requires no mixing
         }
      }else{
         const byte *k = (const byte*)key;
                              //all but the last block: affect some 32 bits of (a,b,c)
         while(length>12){
            a += k[0];
            a += dword(k[1])<<8;
            a += dword(k[2])<<16;
            a += dword(k[3])<<24;
            b += k[4];
            b += dword(k[5])<<8;
            b += dword(k[6])<<16;
            b += dword(k[7])<<24;
            c += k[8];
            c += dword(k[9])<<8;
            c += dword(k[10])<<16;
            c += dword(k[11])<<24;
            hash_mix(a,b,c);
            length -= 12;
            k += 12;
         }
                              //last block: affect all 32 bits of (c)
         switch(length){      //all the case statements fall through
         case 12: c += dword(k[11])<<24;
         case 11: c += dword(k[10])<<16;
         case 10: c += dword(k[9])<<8;
         case 9: c += k[8];
         case 8: b += dword(k[7])<<24;
         case 7: b += dword(k[6])<<16;
         case 6: b += dword(k[5])<<8;
         case 5: b += k[4];
         case 4: a += dword(k[3])<<24;
         case 3: a += dword(k[2])<<16;
         case 2: a += dword(k[1])<<8;
         case 1: a += k[0];
            break;
         case 0:
            return c;
         }
      }
      hash_final(a, b, c);
      return c;
   }
//----------------------------
   static inline dword GetHashIndex(dword hash){
      return hash & ((1<<TABLE_SIZE_BITS)-1);
   }
//----------------------------
   void Insert(dword hash, const char *key, dword key_len, int value){
      S_entry *e = &base_tab[GetHashIndex(hash)];
      if(!e->IsEmpty()){
                              //add new entry and insert to list
         if(!alloced_pos){
                              //alloc new pool of additional entries
            alloced_pos = 50;
            S_alloc_entry *pool = new(true) S_alloc_entry[alloced_pos];
            pool->next_alloc = alloced;
            alloced = pool;
         }
         S_entry *new_entry = alloced + (--alloced_pos);
         new_entry->next = e->next;
         e->next = new_entry;
         e = new_entry;
#ifdef _DEBUG
         ++num_alloc_entry;
#endif
      }
      e->hash = hash;
      e->key = key;
      e->key_len = key_len;
      e->value = value;
   }
//----------------------------
   int Find(dword hash, const char *key, dword key_len) const{
      const S_entry *e = &base_tab[GetHashIndex(hash)];
      if(!e->IsEmpty()){
                              //lookup correct node in linked nodes
         do{
            if(e->hash==hash){
               if(e->key_len==key_len){
                  if(!MemCmp(e->key, key, key_len))
                     return e->value;
               }
            }
            e = e->next;
         }while(e);
      }
      return -1;
   }
};
#endif

//----------------------------
#if defined _DEBUG || 0
//#define SORT_PROFILE        //measure time spent
//#define DUMP_HIERARCHY
#endif

//----------------------------

struct S_thread_sort_container{
#ifdef _DEBUG
   int id;
#endif
   int parent, child, sibling;//indexes to other containers
   //int msg_index;             //-1 = null
   const S_message *msg;

   S_thread_sort_container():
      msg(NULL),
      parent(0),              //point to root by default
      child(-1), sibling(-1)
   {}
   inline bool IsEmpty() const{ return (msg==NULL); }
};

//----------------------------

class C_thread_sort_help{
   S_sort_msg_help hlp;
   int *selection;
   S_message *dst_messages;
   bool sort_by_date;
public:
   bool descending;
   int dst_i;
   C_vector<S_thread_sort_container> containers;
   C_vector<S_message> src_messages;

   C_thread_sort_help(const C_mail_client *app, S_message *_dst, int *sel, bool by_date, bool _descending, bool is_imap):
      selection(sel),
      sort_by_date(by_date),
      descending(_descending),
      dst_messages(_dst),
      dst_i(0)
   {
      hlp.msgs = NULL;
      hlp.selection = NULL;
      hlp._this = app;
      hlp.is_imap = is_imap;
   }

//----------------------------

   void Init(const S_message *src_msgs, dword num_messages){
                              //make copy of messages
      src_messages.insert(src_messages.begin(), src_msgs, src_msgs+num_messages);
      containers.reserve(num_messages*10/8);

                           //make root container
      containers.push_back(S_thread_sort_container()).parent = -1;
   }

//----------------------------
// Check if child container is somehow linked to parent.
   bool ContainerCheckLink(int parent, int child) const{

      int orig_child = child;
      while(child>0){
         const S_thread_sort_container &c = containers[child];
         if(c.parent==parent)
            return true;
         child = c.parent;
         assert(child!=orig_child);
      }
      return false;
   }

//----------------------------

   void SetContanerParent(int parent, int child){

                                 //avoid linking if they're already linked
      if(child==parent ||
         ContainerCheckLink(child, parent) ||
         ContainerCheckLink(parent, child)){
         return;
      }

      int &childs_parent = containers[child].parent;
      if(childs_parent!=0){
         if(childs_parent!=parent){
            return;
            /*
                                 //if new parent leads to our current parent, then adopt the parent
            if(ContainerCheckLink(containers, childs_parent, parent)){
                                 //remove child from current parent
               for(int ci=containers[childs_parent].child; ci!=-1; ){
                  if(ci==child){
                  }
               }
               childs_parent = parent;
            }else
               assert(0);
               */
         }
      }else
         childs_parent = parent;
      int &parent_child = containers[parent].child;
      if(parent_child!=-1){
         if(parent_child!=child){
            int sib = parent_child;
            while(containers[sib].sibling!=-1)
               sib = containers[sib].sibling;
            containers[sib].sibling = child;         
         }
      }else
         parent_child = child;
   }

//----------------------------

   void CreateContainers(){
#ifdef _DEBUG
      int num_dups = 0, num_zero_id = 0;
#endif
#ifdef SORT_PROFILE
      dword beg_t = GetTickTime();
#endif
      C_hash_table hash_tab;
#ifdef SORT_PROFILE
      LOG_RUN_N("-------\nHash tab", GetTickTime()-beg_t); beg_t = GetTickTime();
#endif

      C_vector<int> ref_cnt_index;
      ref_cnt_index.reserve(20);
      for(int mi=0; mi<src_messages.size(); mi++){
         const S_message &msg = src_messages[mi];
         t_hash msg_id_hash = hash_tab.GetHash(msg.message_id, msg.message_id.Length());
#ifdef _DEBUG
         if(!msg.message_id.Length())
            ++num_zero_id;
#endif
                              //find container for this msg
         int msg_ci = hash_tab.Find(msg_id_hash, msg.message_id, msg.message_id.Length());
         if(msg_ci!=-1){
                              //such container exists
            S_thread_sort_container &c = containers[msg_ci];
            if(c.IsEmpty()){
                              //utilize empty container
               c.msg = &msg;
            }else{
                              //2 messages with same id
                              // make new container (but don't insert to hash table)
               msg_ci = -2;
#ifdef _DEBUG
               ++num_dups;
#endif
            }
         }
         if(msg_ci<0){
                              //create new container for this message
            S_thread_sort_container c;
            c.msg = &msg;
            int cnt_index = containers.size();
            if(msg_ci==-1)
               hash_tab.Insert(msg_id_hash, msg.message_id, msg.message_id.Length(), cnt_index);
#ifdef _DEBUG
            c.id = cnt_index;
#endif
            msg_ci = cnt_index;
            containers.push_back(c);
         }
                              //process message's references
         if(msg.references.Length()){
            ref_cnt_index.clear();

            const char *cp = msg.references;
            while(*cp){
                           //parse single reference inside of < >
               text_utils::SkipWS(cp);
               if(*cp++!='<')
                  break;
               const char *end = cp;
               while(*end){
                  if(*++end=='>')
                     break;
               }
               if(!*end)
                  break;
               dword key_len = end-cp;
               if(!key_len)
                  break;
                           //find its message
               t_hash ref_hash = hash_tab.GetHash(cp, key_len);
               int ref_ci = hash_tab.Find(ref_hash, cp, key_len);
               if(ref_ci==-1){
                           //such message doesn't exist, so make empty container
                  ref_ci = containers.size();
                  hash_tab.Insert(ref_hash, cp, key_len, ref_ci);
                                 //make empty reference
                  S_thread_sort_container c;
#ifdef _DEBUG
                  c.id = containers.size();
#endif
                  containers.push_back(c);
               }
               //assert(containers[ref_ci].msg_id==ref);
               ref_cnt_index.push_back(ref_ci);
               cp = end+1;
            }
            dword num_refs = ref_cnt_index.size();
            if(num_refs){
                           //set parent-child links between neighbour references
               for(dword ri=0; ri<num_refs-1; ri++)
                  SetContanerParent(ref_cnt_index[ri], ref_cnt_index[ri+1]);
                              //set this message's parent to be last ref
               SetContanerParent(ref_cnt_index[num_refs-1], msg_ci);
            }
         }
      }
#ifdef SORT_PROFILE
      LOG_RUN_N("Containers:", GetTickTime()-beg_t); beg_t = GetTickTime();
#endif
                           //make all top-level containers to be siblings
      int last_top_level = -1;
      for(int i=containers.size(); --i > 0; ){
         S_thread_sort_container &c = containers[i];
         if(c.parent==0){
            c.sibling = last_top_level;
            last_top_level = i;
         }
      }
      containers.front().child = last_top_level;
   }

//----------------------------

   void RemoveEmptyContainers(int ci0){

#ifdef SORT_PROFILE
      dword beg_t = GetTickTime();
#endif
                              //process all children of c, remove empty ones
      S_thread_sort_container &c0 = containers[ci0];
      S_thread_sort_container *prev_sibling = NULL;
      for(int ci1 = c0.child; ci1!=-1; ){
         S_thread_sort_container &c1 = containers[ci1];
         if(c1.child!=-1)
            RemoveEmptyContainers(ci1);
         if(c1.IsEmpty()){
                              //remove this container
            int ci2 = c1.child;
            /*
            if(!ci0 && containers[ci2].sibling!=-1){
                           //speciality on level 0 = make all siblings to be children of oldest message, so that we get nice thread also with missing parent
                           // technically, find oldest and put it in place of removed empty container
               dword min_date = 0xffffffff;
               int min_cnt = -1;
               for(int ci=ci2; ci!=-1; ){
                  const S_thread_sort_container &c = containers[ci];
                  assert(!c.IsEmpty());
                  if(c.msg.date<min_date)
                  {
                     min_date = c.msg.date;
                     min_cnt = ci;
                  }
                  ci = c.sibling;
               }
               assert(min_cnt!=-1);
               S_thread_sort_container &c_min = containers[min_cnt];
                           //now remove this child from chain, and make it its parent
               for(int ci=ci2; ci!=-1; ){
                  S_thread_sort_container &c = containers[ci];
                  if(ci!=min_cnt){
                     if(c.sibling==min_cnt){
                        c.sibling = c_min.sibling;
                     }
                     c.parent = min_cnt;
                  }
                  ci = c.sibling;
                  if(ci==-1){
                           //last child, link it to present children of c_min
                     c.sibling = c_min.child;
                     c_min.child = -1;
                  }
               }
               assert(c_min.child==-1);
               c_min.child = min_cnt==ci2 ? c_min.sibling : ci2;
               c_min.sibling = -1;
               ci2 = min_cnt;
            }
            /**/
            if(ci2!=-1){
                           //first child in chain, link to left sibling or parent
               if(!prev_sibling){
                  assert(c0.child==ci1);
                  c0.child = ci2;
               }else{
                  prev_sibling->sibling = ci2;
               }
                        //put all its children in its place
               do{
                  S_thread_sort_container &c2 = containers[ci2];
                  assert(c2.parent==ci1);
                  c2.parent = ci0;
                  prev_sibling = &c2;
                  ci2 = c2.sibling;
               }while(ci2!=-1);
                           //last child, link to next sibling on level 1
               prev_sibling->sibling = c1.sibling;
            }else{
                           //no child
               if(!prev_sibling){
                  assert(c0.child==ci1);
                  c0.child = c1.sibling;
               }else{
                  prev_sibling->sibling = c1.sibling;
               }
               //prev_sibling = &c1;
            }
                              //kill it
            c1.parent = c1.child = -1;
         }else{
            prev_sibling = &c1;
         }
         ci1 = c1.sibling;
      }
#ifdef SORT_PROFILE
      LOG_RUN_N("RemoveEmptyContainers:", GetTickTime()-beg_t);
#endif
   }

//----------------------------
   const S_thread_sort_container *GetNonEmptyChild(const S_thread_sort_container *c) const{
      while(c->IsEmpty()){
         if(c->child==-1)
            return NULL;
         c = &containers[c->child];
      }
      return c;
   }
//----------------------------
   const S_thread_sort_container *GetYoungestChild(const S_thread_sort_container *c) const{
      const S_thread_sort_container *ret = c;
      while(c->child!=-1){
         c = &containers[c->child];
         if(!c->IsEmpty()){
            if(ret->msg->date < c->msg->date)
               ret = c;
         }
      }
      return ret;
   }
//----------------------------
   int CompareContainers(const S_thread_sort_container *c1, const S_thread_sort_container *c2){
                              //skip empty containers, compare by first child
      //c1 = GetNonEmptyChild(c1);
      //c2 = GetNonEmptyChild(c2);
      assert(!c1->IsEmpty());
      assert(!c2->IsEmpty());
      //assert(c1 && c2);
      if(sort_by_date){
         c1 = GetYoungestChild(c1);
         c2 = GetYoungestChild(c2);
      }
                              //now normally compare 
      return C_mail_client::CompareMessages(c1->msg, c2->msg, &hlp);
   }

   static int CompareContainers1(const void *_c1, const void *_c2, void *_this){
      return ((C_thread_sort_help*)_this)->CompareContainers(*(S_thread_sort_container**)_c1, *(S_thread_sort_container**)_c2);
   }

   //----------------------------

   void ProcessContainerChild(const S_thread_sort_container &c, int thread_level){
      assert(!c.IsEmpty());
      //if(!c.IsEmpty())
      {
                              //fix selection index
         if(selection && *selection==(c.msg-src_messages.begin())){
            *selection = dst_i;
                              //don't further process index
            selection = NULL;
         }
                              //add this message
         S_message &dst_msg = dst_messages[dst_i];
#ifdef THREAD_SORT_DESCENDING_ALTERNATIVE
         if(descending)
            --dst_i;
         else
#endif
            ++dst_i;
         dst_msg = *c.msg;
         dst_msg.thread_level = byte(Min(thread_level, 255));
         //++thread_level;
      }
                              //process children
      if(c.child!=-1)
         ProcessContainerMessages(c, thread_level+1);
   }

//----------------------------
// Final pass for writing messages from tree of containers to continuous message array.
   void ProcessContainerMessages(const S_thread_sort_container &c0, int thread_level){

                                 //count all children
      dword level_size = 0;
      for(int ci1 = c0.child; ci1!=-1; ){
         ++level_size;
         ci1 = containers[ci1].sibling;
      }
      assert(level_size);
      if(level_size==1){
                              //just one child, process it fast
         ProcessContainerChild(containers[c0.child], thread_level);
      }else{
                              //sort and process sorted messages
         C_vector<const S_thread_sort_container*> tmp_buf;
         tmp_buf.reserve(level_size);

                                    //put all containers on this level to temp buf
         for(int ci1 = c0.child; ci1!=-1; ){
            const S_thread_sort_container *c1 = &containers[ci1];
            tmp_buf.push_back(c1);
            ci1 = c1->sibling;
         }
         assert(tmp_buf.size()==level_size);
                                    //now sort these containers (on same level)
         QuickSort(tmp_buf.begin(), level_size, sizeof(S_thread_sort_container*), &CompareContainers1, (void*)this);

                                    //finally write their messages to dest buffer, and also process their sub-levels
         for(dword i=0; i<level_size; i++){
            const S_thread_sort_container &c1 = *tmp_buf[i];
            ProcessContainerChild(c1, thread_level);
         }
      }
   }
//----------------------------
#ifdef DUMP_HIERARCHY
   void DumpHierarchy(int ci0=0, int level=0) const{

      if(!level)
         LOG_RUN("-------------");
      for(int ci1 = containers[ci0].child; ci1!=-1; ){
         const S_thread_sort_container &c1 = containers[ci1];
         Cstr_c s;
         for(int i=0; i<level; i++){
            s<<' ';
         }
         s.AppendFormat("(%) ") <<ci1;
         if(c1.IsEmpty())
            s<<".";
         else
            s<<src_messages[c1.msg_index].subject;
         LOG_RUN(s);
         DumpHierarchy(ci1, level+1);
         ci1 = c1.sibling;
      }
   }
#endif
};

//----------------------------

void C_mail_client::SortByThread(S_message *messages, dword num_messages, bool is_imap, int *selection) const{

   if(!num_messages)
      return;

   C_thread_sort_help hlp(this, messages, selection, (config_mail.sort_mode==S_config_mail::SORT_BY_DATE), config_mail.sort_descending, is_imap);
   hlp.Init(messages, num_messages);

   hlp.CreateContainers();

                           //now collect containers' messages on all levels
#ifdef DUMP_HIERARCHY
   hlp.DumpHierarchy();
#endif
   hlp.RemoveEmptyContainers(0);
#ifdef DUMP_HIERARCHY
   hlp.DumpHierarchy();
#endif

#ifdef SORT_PROFILE
   dword beg_t = GetTickTime();
#endif

#ifdef THREAD_SORT_DESCENDING_ALTERNATIVE
   config.flags &= ~S_config_mail::CONF_SORT_DESCENDING;
   if(hlp.descending)
      hlp.dst_i = num_messages-1;
#endif
   hlp.ProcessContainerMessages(hlp.containers[0], 0);
#ifdef THREAD_SORT_DESCENDING_ALTERNATIVE
   if(hlp.descending){
      config.flags |= S_config_mail::CONF_SORT_DESCENDING;
      assert(hlp.dst_i==-1);
   }else
#endif
   assert(hlp.dst_i==num_messages);

#ifdef SORT_PROFILE
   LOG_RUN_N("Sort done:", GetTickTime()-beg_t);
#endif
}

//----------------------------

void C_mail_client::SortMessages(C_vector<S_message> &_messages, bool is_imap, int *selection) const{

   dword num_messages = _messages.size();
   S_message *messages = _messages.begin();

#ifdef _DEBUG_
   {
      int tmp = 0;
      const S_sort_msg_help hlp = { messages, is_imap, selection ? selection : &tmp, this };
      QuickSort(hlp.msgs, num_messages, sizeof(S_message), &CompareMessages, (void*)&hlp, &SwapMessages);
   }
#endif

   if(config_mail.sort_by_threads && !config_mail.tweaks.show_only_unread_msgs){
                              //fast pre-sort - put hidden to back
      dword lo = 0, hi = num_messages;
      while(lo!=hi){
         if(!messages[lo].IsHidden())
            ++lo;
         else
         if(messages[hi-1].IsHidden())
            --hi;
         else
            Swap(messages[lo], messages[hi-1]);
      }
                              //find number of non-hidden messages
      int num_visible = lo;

      SortByThread(messages, num_visible, is_imap, selection);
                              //sort hidden now
      SortByThread(messages+num_visible, num_messages - num_visible, is_imap, NULL);
   }else{
#ifdef SORT_PROFILE
      dword beg_t = GetTickTime();
#endif
      {
         int tmp = 0;
         const S_sort_msg_help hlp = { messages, is_imap, selection ? selection : &tmp, this };
         QuickSort(hlp.msgs, num_messages, sizeof(S_message), &CompareMessages, (void*)&hlp, &SwapMessages);
      }
#ifdef SORT_PROFILE
      LOG_RUN_N("QuickSort", GetTickTime()-beg_t); beg_t = GetTickTime();
#endif
                              //reset thread levels for safety
      for(int i=num_messages; i--; )
         messages[i].thread_level = 0;
   }
}

//----------------------------

void C_mail_client::SortAllAccounts(const C_message_container *curr_cnt, int *selection){

   for(int i=NumAccounts(); i--; ){
      S_account &acc = accounts[i];
      C_folders_iterator it(acc._folders);
      while(!it.IsEnd()){
         C_message_container &cnt = *it.Next();
         if(cnt.loaded){
            SortMessages(cnt.messages, cnt.is_imap, &cnt==curr_cnt ? selection : NULL);
            cnt.MakeDirty();
         }else
            cnt.flags |= cnt.FLG_NEED_SORT;
      }
   }
}

//----------------------------

void C_mail_client::CleanupMailFiles(){

   LOG_RUN("CleanupMailFiles");
                              //collect all files in root
   C_vector<Cstr_w> files, dirs;
   Cstr_w base;
   base<<mail_data_path;
#ifndef UNIX_FILE_SYSTEM
   base.ToLower();
#endif
   base<<MAIL_PATH;
   ScanDir(base, files, dirs);
                              //no files can be in root
   for(int i=files.size(); i--; ){
      const Cstr_w &fn = files[i];
#if defined _DEBUG
      Cstr_w s;
      s.Format(L"Unused file: %") <<fn;
      Info(s);
#endif
      LOG_RUN("Unused file"); LOG_RUN(fn.ToUtf8());
      C_file::DeleteFile(fn);
   }
                              //check all accounts
   for(int acc_i=NumAccounts(); acc_i--; ){
      S_account &acc = accounts[acc_i];
      //bool is_imap = acc.IsImap();
                              //process all containers
      C_folders_iterator it(acc._folders);
      while(!it.IsEnd()){
                              //get messages
         C_message_container &cnt = *it.Next();
                              //find container's folder
         assert(cnt.msg_folder_id);
         Cstr_w mail_path = cnt.GetMailPath(mail_data_path);
                              //check if its path exists
         for(int pi=dirs.size(); pi--; ){
            if(dirs[pi]==mail_path){
               dirs.remove_index(pi);
               break;
            }
         }
      }
   }
                              //delete unused dirs
   for(int i=dirs.size(); i--; ){
      const Cstr_w &dir = dirs[i];
#if defined _DEBUG_
      Cstr_w s;
      s.Format(L"Unused dir: %") <<dir;
      Info(s);
#endif
      LOG_RUN("Unused dir"); LOG_RUN(dir.ToUtf8());
      Cstr_w fn;
      C_file::GetFullPath(dir.Left(dir.Length()-1), fn);
      C_dir::RemoveDirectory(fn, true);
   }
}

//----------------------------

bool C_mail_client::DecodeMessageTextLine(const char *cp, dword sz, E_CONTENT_ENCODING content_encoding, C_file &fl, S_download_body_data *body_data){

   bool add_eol = true;
   switch(content_encoding){
   case ENCODING_QUOTED_PRINTABLE:
      {
                     //grammar:
                     //quoted-printable = qp-line *(CRLF qp-line)
                     //qp-line = *(qp-segment transport-padding CRLF) qp-part transport-padding
                     //qp-part = qp-section 
                     //qp-segment = qp-section *(SPACE | TAB) "="
                     //qp-section = [*(ptext | SPACE / TAB) ptext]
                     //ptext = hex-octet | safe-char
                     //safe-char = <any octet with decimal value of 33 through 60 inclusive, and 62 through 126>
                     //hex-octet = "=" 2(DIGIT / "A" / "B" / "C" / "D" / "E" / "F")
                     //transport-padding = *LWSP-char

                     //by RFC 2045/6.7, strip all spaces at back
         while(sz){
            char c = cp[sz-1];
            if(c==' ' || c=='\t')
               --sz;
            else
               break;
         }
         for(int i=sz; i--; ){
            dword c = (byte)*cp++;
            if(c=='='){
               if(i>=2){
                  dword chex;
                  if(text_utils::ScanHexByte(cp, chex)){
                     c = chex;
                     i -= 2;
                  }else
                  if(*cp=='\n'){
                     ++cp;
                     --i;
                     c = '\n';
                  }else
                  if(*cp=='\r'){
                     ++cp;
                     --i;
                     c = '\n';
                     if(*cp=='\n'){
                        ++cp;
                        --i;
                     }
                  }
               }else
               if(i==0){
                     //'=' at end of line means soft-break, so ignore this as well as \n
                  add_eol = false;
                  continue;
               }else{
                     //error
                  assert(0);
               }
            }
            if(fl.WriteByte(byte(c))!=fl.WRITE_OK)
               return false;
         }
      }
      break;
   case ENCODING_BASE64:
      {
         C_vector<byte> buf;
         if(DecodeBase64(cp, sz, buf)){
            const byte *cp1 = buf.begin();
            for(int i=buf.size(); i--; ){
               byte c = *cp1++;
                              //watch for encoded CRLF and convert these to single '\n'
               switch(c){
               case '\r':
                  if(!i || *cp1=='\n')
                     continue;
                              //stand-alone '\r' not followed by '\n' ?
                  //assert(0);
                  break;
               case '\n':
                  //StripTrailingSpace(dst);
                  break;
               }
               if(fl.WriteByte(byte(c))!=fl.WRITE_OK)
                  return false;
            }
         }
         add_eol = false;
      }
      break;
   default:
      if(body_data && body_data->flowed_text.enable_decoding && body_data->retrieved_header.format_flowed){
                              //decode flowed format here
         S_download_body_data::S_flowed_text &ft = body_data->flowed_text;
         add_eol = false;
                              //count quotes
         int qc;
         for(qc=0; qc<int(sz) && cp[qc]=='>'; qc++);
         int write_quotes = qc;

         if(ft.prev_soft_line_quote_count!=-1){
                              //previous line was soft-break
            if(ft.prev_soft_line_quote_count==qc){
                              //connect with previous line, remove quotes
               cp += qc;
               sz -= qc;
               write_quotes = 0;
            }else{
                              //error, different quote count with previous flowed line, force hard break of previous line
               //add_eol = true;
               if(fl.WriteByte('\n')!=fl.WRITE_OK)
                  return false;
            }
         }
         if(fl.Write(cp, write_quotes)!=fl.WRITE_OK)
            return false;
         cp += write_quotes;
         sz -= write_quotes;
                              //detect space stuffing
         if(sz && *cp==' '){
                              //remove space stuffing
            ++cp;
            --sz;
         }
                              //check for usenet signature line
         bool is_signature = (sz==3 && cp[0]=='-' && cp[1]=='-' && cp[2]==' ');

         if(sz && qc && *cp!=' '){
                              //add aesthetic space 
            if(fl.WriteByte(' ')!=fl.WRITE_OK)
               return false;
         }
         if(sz && cp[sz-1]==' ' && !is_signature){
                              //flowed line (don't insert eol)
            ft.prev_soft_line_quote_count = qc;
            if(body_data->retrieved_header.format_delsp){
               //assert(0);     //test it
               --sz;
            }
         }else{
                              //fixed line
            ft.prev_soft_line_quote_count = -1;
            add_eol = true;
         }
      }
      if(fl.Write(cp, sz)!=fl.WRITE_OK)
         return false;
   }
   if(add_eol){
      if(fl.WriteByte('\n')!=fl.WRITE_OK)
         return false;
   }
   return true;
}

//----------------------------

bool C_mail_client::DecodeMessageAttachmentLine(const char *cp, dword sz, E_CONTENT_ENCODING content_encoding, C_file &fl){

   switch(content_encoding){
   case ENCODING_BASE64:
      {
                              //fix common problems
                              //remove spaces at end
         while(sz && cp[sz-1]==' ')
            --sz;
         C_vector<byte> buf;
         DecodeBase64(cp, sz, buf);
         return (fl.Write(buf.begin(), buf.size())==fl.WRITE_OK);
      }

   case ENCODING_QUOTED_PRINTABLE:
      return DecodeMessageTextLine(cp, sz, content_encoding, fl);
   }
                              //save as text (attach LFCR, which is part of data if no coding is used)
   return
      (fl.Write(cp, sz)==fl.WRITE_OK &&
      fl.WriteByte('\r')==fl.WRITE_OK &&
      fl.WriteByte('\n')==fl.WRITE_OK);
}

//----------------------------

bool C_mail_client::SaveMessageBody(C_message_container &cnt, S_message &msg, const char *body, dword len, const char *ext) const{

   S_download_body_data body_data;
   if(!BeginMessageBodyRetrieval(cnt, body_data, ext ? ext : "txt", NULL))
      return false;
   if(body_data.body_saving.fl.Write(body, len)!=C_file::WRITE_OK)
      return false;
   body_data.retrieved_header.text_coding = COD_UTF_8;
   return FinishMessageBodyRetrieval(cnt, body_data, body_data.retrieved_header, msg);
}

//----------------------------

static Cstr_w GetPartialInfoText(const wchar *t){
   
   Cstr_w str = t;
   for(int i=str.Length(); i--; ){
      wchar &c = str.At(i);
      if(c=='|')
         c = '\n';
   }
   return str;
}

//----------------------------

bool C_mail_client::OpenMessageBody(const C_message_container &cnt, const S_message &msg, S_text_display_info &tdi, bool preview_mode) const{

   Cstr_w body_fname = cnt.GetMailPath(mail_data_path);
   body_fname<<msg.body_filename.FromUtf8();
   tdi.body_c.coding = msg.body_coding;

   C_client_viewer::S_file_open_help oh;
   oh.preview_mode = preview_mode;
   oh.detect_phone_numbers = config_mail.tweaks.detect_phone_numbers;

   if(!C_client_viewer::OpenTextFile(this, body_fname, msg.flags&msg.MSG_HTML, tdi, oh))
      return false;
   if(!preview_mode && (msg.flags&msg.MSG_PARTIAL_DOWNLOAD)){
                              //add partial message text
      Cstr_w txt = GetPartialInfoText(GetText(TXT_PARTIAL_MSG_INFO));
      if(tdi.body_w.Length()){
         assert(0);
      }else{
         C_vector<char> buf;
         S_text_style ts;
         ts.AddWideStringToBuf(buf, txt);
         buf.push_back(0);
         tdi.body_c<<buf.begin();
      }
   }
   return true;
}

//----------------------------

void C_mail_client::StartAlertSoundsPlayback(){

   if(alert_manager.alerts_to_play.size())
      PlayNextAlertSound();
   if(alert_manager.vibrate){
      alert_manager.vibrate = false;
#ifdef USE_SYSTEM_VIBRATE
      if(C_phone_profile::IsVibrationEnabled())
#else
      if(config.flags&config_mail.CONF_VIBRATE_ALERT)
#endif
      {
         MakeVibration();
      }
   }
}

//----------------------------

void C_mail_client::PlayNextAlertSound(){

                              //check alert manager
   for(int i=alert_manager.alerts_to_play.size(); i--; ){
      C_file fl;
      if(!fl.Open(alert_manager.alerts_to_play[i].alert_name))
         alert_manager.alerts_to_play.remove_index(i);
   }
   simple_snd_plr = NULL;
#ifdef __SYMBIAN32__
   dword did = system::GetDeviceId();
   if(did==0x20024eec || did==0x20024eed){
                              //UID_SONYERICSSON_VIVAZ_U5i, audio hangs system
      alert_manager.alerts_to_play.clear();
   }
#endif
   if(alert_manager.alerts_to_play.size()){
                              //delete old first, so that audio resources are freed
      simple_snd_plr = C_simple_sound_player::Create(alert_manager.alerts_to_play.front().alert_name, alert_manager.alerts_to_play.front().volume);
      if(simple_snd_plr)
         simple_snd_plr->Release();
      alert_manager.alerts_to_play.pop_front();
   }
   ManageTimer();
}

//----------------------------

void C_mail_client::PlayNewMailSound(){

   if(config_mail.alert_volume)
   {
      Cstr_w fn;
#ifdef USE_SYSTEM_PROFILES
      if(!C_phone_profile::GetEmailAlertTone(fn))
#endif
      if(!C_phone_profile::IsSilentProfile())
         fn = config_mail.alert_sound;
      if(fn.Length()){
         alert_manager.AddAlert(fn
            , config_mail.alert_volume
            );
         PlayNextAlertSound();
      }
   }
}

//----------------------------

void C_mail_client::C_alert_manager::AddAlert(const Cstr_w &alert_name, int volume){

#ifdef USE_SYSTEM_PROFILES
                              //multiply volume by profile volume
   volume = volume*C_phone_profile::GetProfileVolume()/100;
   volume = Max(1, volume);
#endif
   for(int i=alerts_to_play.size(); i--; ){
      S_alert &a = alerts_to_play[i];
      if(a.alert_name==alert_name){
         a.volume = Max(a.volume, volume);
         return;
      }
   }
   S_alert &a = alerts_to_play.push_back(S_alert());
   a.alert_name = alert_name;
   a.volume = volume;
}

//----------------------------
//----------------------------

class C_mode_read_mail: public C_mail_client::C_mode_read_mail_base, public C_mail_client::C_text_viewer{
   virtual bool WantInactiveTimer() const{ return false; }

   virtual dword GetSecondarySoftKey() const{
      if(ctrl_copy)
         return TXT_CANCEL;
      return C_mode::GetSecondarySoftKey();
   }

   class C_title_bar: public C_ctrl_title_bar{
      C_mode_read_mail &Mode(){ return (C_mode_read_mail&)mod; }
      const C_mode_read_mail &Mode() const{ return (C_mode_read_mail&)mod; }
      virtual void InitLayout(){
         C_ctrl_title_bar::InitLayout();
         if(!Mode().ctrl_copy){
            C_application_ui &app = App();
            //rc = S_rect(0, 0, app.ScrnSX(), app.GetTitleBarHeight());
            rc.sy = app.fdb.cell_size_y*2 + app.fdb.line_spacing - 2;
         }
      }
   //----------------------------
      virtual void Draw() const{
         if(Mode().ctrl_copy)
            C_ctrl_title_bar::Draw();
         else
            Mode().DrawHeaderFields();
      }
   public:
      C_title_bar(C_mode *m):
         C_ctrl_title_bar(m)
      {}
   };
//----------------------------
   virtual C_ctrl_title_bar *CreateTitleBar(){
      return new(true) C_title_bar(this);
   }
//----------------------------
   void DrawInfoInSoftbar() const;

//----------------------------
   virtual void OnSoftBarButtonPressed(dword index){
      C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
      switch(index){
      case 2:
         {
            app.CloseMode(*this, false);
            bool redraw;
            app.DeleteMarkedOrSelectedMessages(mod_mbox, true, redraw);
            app.RedrawScreen();
         }
         break;
      case 0:
      case 1:
         app.CloseMode(*this, false);
         mod_mbox.GetContainer().SaveMessages(app.GetMailDataPath());

         S_message &msg1 = mod_mbox.GetMessage(mod_mbox.selection);
         bool r = (index==0), f = (index==1);
         if(r && msg1.HasMultipleRecipients(acc)){
            app.ReplyWithQuestion(mod_mbox.GetContainer(), msg1);
         }else
            app.SetModeWriteMail(&mod_mbox.GetContainer(), &msg1, r, f, false);
         break;
      }
   }
//----------------------------
   void InitBottomButtons(){
      C_ctrl_softkey_bar &skb = *GetSoftkeyBar();
      skb.ResetAllButtons();
      if(acc){
         skb.InitButton(0, app.BUT_REPLY, TXT_REPLY);
         skb.InitButton(1, app.BUT_FORWARD, TXT_FORWARD);
         skb.InitButton(2, app.BUT_DELETE, TXT_DELETE);
      }
   }
//----------------------------

   virtual void InitMenu(){
      if(ctrl_copy){
         bool has_sel = (ctrl_copy->GetCursorPos()!=ctrl_copy->GetCursorSel());
         //menu->AddItem(_TXT_COPY, (has_sel || text_copy_select_pos!=-1) ? 0 : C_menu::DISABLED, (has_sel || text_copy_select_pos!=-1) ? app.ok_key_name : NULL);
         menu->AddItem(_TXT_COPY, (has_sel) ? 0 : C_menu::DISABLED, (has_sel) ? app.ok_key_name : NULL);
         //menu->AddItem(TXT_SELECT, text_copy_select_pos!=-1 ? C_menu::MARKED : 0, !(has_sel || text_copy_select_pos!=-1) ? app.ok_key_name : NULL);
         menu->AddItem(TXT_MARK_ALL, 0, "[4]", "[L]");
         menu->AddSeparator();
         menu->AddItem(TXT_CANCEL);
      }else{
         kinetic_movement.Reset();
         auto_scroll_time = 0;
         if(msg.IsDeleted()){
            menu->AddItem(TXT_UNDELETE, 0, app.delete_key_name);
            menu->AddItem(TXT_HIDE, 0, "[8]", "[h]");
            menu->AddSeparator();
         }else
         if(msg.flags&(S_message::MSG_DRAFT|S_message::MSG_TO_SEND)){
            menu->AddItem(TXT_DELETE, 0, app.delete_key_name);
            menu->AddItem(TXT_EDIT);
            menu->AddSeparator();
         }else{
            menu->AddItem(TXT_MESSAGE, C_menu::HAS_SUBMENU);
            if(acc){
               menu->AddItem(TXT_MARK, C_menu::HAS_SUBMENU);
            }
            if(msg.attachments.Size())
               menu->AddItem(TXT_ATTACHMENT, C_menu::HAS_SUBMENU);
            if(//!(app.config.flags&app.config_mail.CONF_DOWNLOAD_HTML_IMAGES) && text_info.images.size()
               img_loader.entries.size()
               ){
               for(int i=text_info.images.size(); i--; ){
                  const S_image_record &ir = text_info.images[i];
                  if(!ir.img){
                     menu->AddItem(TXT_CFG_DOWNLOAD_HTML_IMAGES, img_loader.socket ? C_menu::MARKED : 0);
                     break;
                  }
               }
            }
            menu->AddSeparator();
            const S_hyperlink *href = text_info.GetActiveHyperlink();
            if(href){
               //dword flg = tdi.active_object_offs!=-1 ? 0 : C_menu::DISABLED;
               if(href->link.Length()){
                  Cstr_c sc = app.GetShiftShortcut("[%+OK]");
                  menu->AddItem(TXT_OPEN_LINK, 0, app.config_mail.tweaks.open_links_by_system ? (const char*)sc : app.ok_key_name);
                  menu->AddItem(TXT_OPEN_BY_SYSTEM, 0, app.config_mail.tweaks.open_links_by_system ? app.ok_key_name : (const char*)sc);
                  menu->AddItem(TXT_COPY_LINK, 0, NULL, "[Y]");
               }         
               if(href->image_index!=-1)
                  menu->AddItem(TXT_VIEW_IMAGE, 0, href->link.Length() ? NULL : app.ok_key_name);

               if(!app.HasMouse())
                  menu->AddItem(TXT_SHOW_LINK, 0, "[0]", "[K]");
               if(href->link.Length())
                  menu->AddItem(TXT_SAVE_TARGET);
               menu->AddSeparator();
            }
            menu->AddItem(TXT_COPY_FROM_TEXT, 0, "[4]", "[C]");
            if(!app.HasMouse()){
               menu->AddItem(TXT_SCROLL, C_menu::HAS_SUBMENU);
               menu->AddSeparator();
            }
         }
         if(acc){
            menu->AddItem(TXT_NEXT, GetMailbox().selection<int(GetMailbox().num_vis_msgs)-1 ? 0 : C_menu::DISABLED, "[3]", "[N]");
            menu->AddItem(TXT_PREVIOUS, GetMailbox().selection ? 0 : C_menu::DISABLED, "[2]", "[P]");
         }
         menu->AddSeparator();
         menu->AddItem(app.config_mail.tweaks.exit_in_menus ? TXT_EXIT : TXT_BACK);
      }
   }

//----------------------------

   virtual void ReplyToMessage(bool reply, bool forward, bool reply_all, bool detect_reply_all){
      if(acc){
         C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
         app.CloseMode(*this, false);
         mod_mbox.GetContainer().SaveMessages(app.GetMailDataPath());

         S_message &msg1 = mod_mbox.GetMessage(mod_mbox.selection);
         if(reply && detect_reply_all && msg1.HasMultipleRecipients(acc)){
            app.ReplyWithQuestion(mod_mbox.GetContainer(), msg1);
         }else
            app.SetModeWriteMail(&mod_mbox.GetContainer(), &msg1, reply, forward, reply_all);
      }
   }

//----------------------------

   virtual void ShowMessageDetails(){
      app.ShowMessageDetails(msg);
   }

public:
   C_attach_browser attach_browser;

   S_message &msg;
   const C_mail_client::S_account *acc;   //if NULL, msg has no account (e.g. it is opened from a file)

   C_ctrl_text_entry *ctrl_copy;
   void CloseCopy(){
      RemoveControl(ctrl_copy);
      ctrl_copy = NULL;
      SetTitle(NULL);
      InitBottomButtons();
      InitLayout();
   }
   void DrawHeaderFields() const;

   //int text_copy_select_pos;
   bool att_browser_focused;
   int drag_x_pos;
   C_kinetic_movement kinetic_movement;

   C_mode_read_mail(C_mail_client &_app, S_message &m, const C_mail_client::S_account *a):
      C_mode_read_mail_base(_app),
      attach_browser(_app),
      msg(m),
      acc(a),
      drag_x_pos(0),
      ctrl_copy(NULL),
      att_browser_focused(true)
   {
      mode_id = ID;
      img_loader.mod_socket_notify = this;
      InitBottomButtons();
   }
   virtual C_mail_client::C_mode_mailbox &GetMailbox(){ return (C_mail_client::C_mode_mailbox&)*saved_parent; }
   inline const C_mail_client::C_mode_mailbox &GetMailbox() const{ return (C_mail_client::C_mode_mailbox&)*saved_parent; }

   virtual void ResetTouchInput(){
      attach_browser.ResetTouchInput();
      //C_text_viewer::ResetTouchInput();
      mouse_drag = false;
   }
   virtual void InitLayout();
   virtual void ProcessInput(S_user_input &ui, bool &redraw);
   virtual void ProcessMenu(int itm, dword menu_id);
   virtual void Tick(dword time, bool &redraw);
   virtual void Draw() const;
   virtual void SocketEvent(C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){
      C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
      if(app.TickViewerImageLoader(*this, ev, *this, &mod_mbox.GetContainer(), msg.inline_attachments, redraw))
         mod_mbox.GetContainer().MakeDirty();
   }

   void CopyLink();
   virtual void StartCopyText();
   void Scroll(bool top);
   void FinishInit();
   void DrawTextBody() const;
   void DrawAttachments() const{
      attach_browser.Draw(msg.attachments, C_fixed::Percent(att_browser_focused ? 100 : 30));
   }
//----------------------------
   void DeleteMessage(bool &redraw){
      bool server_sync = (msg.flags&S_message::MSG_SERVER_SYNC);
      C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
      AddRef();
      app.CloseMode(*this, false);
      app.DeleteMarkedOrSelectedMessages(mod_mbox, true, redraw);
      if(server_sync){
         /*
         if(!msg.IsDeleted())
            OpenMessage(mod_mbox, true);
            */
      }
      Release();
   }
//----------------------------
   bool NavigateToNext(bool right){

      C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
      if(mod_mbox.num_vis_msgs){
         int ns = mod_mbox.selection;
         if(!right){
            if(ns)
               --ns;
         }else
         if(ns<int(mod_mbox.num_vis_msgs)-1)
            ++ns;
         if(ns!=mod_mbox.selection){
            app.SetMailboxSelection(mod_mbox, ns);
            app.CloseMode(*this, false);
            app.OpenMessage(mod_mbox, true);
            return true;
         }
      }
      return false;
   }
//----------------------------
   void HideMessage(){

      C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
      app.CloseMode(*this, false);
      mod_mbox.MarkMessages(true, S_message::MSG_HIDDEN);
      app.InitLayoutMailbox(mod_mbox);
      /*
      if(mod_mbox.num_vis_msgs)
         OpenMessage(mod_mbox, true);
      else
      */
         app.RedrawScreen();
   }
//----------------------------
   bool GetPreviousNextFile(bool next, Cstr_w *filename, Cstr_w *title);
//----------------------------
// Download selected attachment (and possibly open).
   void DownloadAttachment(bool open_after_download, bool all){

      SuspendLoader();

                                 //download the attachment
      C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();

      C_mail_client::S_connection_params p;
      p.message_index = mod_mbox.GetRealMessageIndex(mod_mbox.selection);
      p.attachment_index = attach_browser.selection;

      app.SetModeConnection(mod_mbox.acc, mod_mbox.folder,
         all ? C_mail_client::C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL :
         open_after_download ? C_mail_client::C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT_AND_OPEN :
         C_mail_client::C_mode_connection::ACT_DOWNLOAD_IMAP_ATTACHMENT, &p);
   }
};

//----------------------------
//----------------------------

void C_mail_client::SetModeReadMail(C_mode_mailbox &mod_mbox){

   S_account &acc = mod_mbox.acc;
   S_message &msg = mod_mbox.GetMessage(mod_mbox.selection);
   bool deleted = msg.IsDeleted();
   if(!deleted){
      assert(msg.HasBody());
      if(!(msg.flags&msg.MSG_READ)){
         msg.flags |= msg.MSG_READ | msg.MSG_IMAP_READ_DIRTY;
         mod_mbox.GetContainer().MakeDirty();
         UpdateUnreadMessageNotify();
         if(config_mail.tweaks.show_only_unread_msgs){
            mod_mbox.GetContainer().flags |= C_message_container::FLG_NEED_SORT;
         }
      }
                              //clear recent flag
      if(msg.IsRecent()){
         msg.flags &= ~msg.MSG_RECENT;
         mod_mbox.GetContainer().MakeDirty();
      }
   }
   assert(&mod_mbox==mode);
   C_mode_read_mail &mod = *new(true) C_mode_read_mail(*this, msg, &acc);

   mod.InitLayout();
   if(!deleted){
      OpenMessageBody(mod_mbox.GetContainer(), msg, mod.text_info, false);
      OpenHtmlImages(msg.inline_attachments, &msg.attachments, (config.flags&config_mail.CONF_DOWNLOAD_HTML_IMAGES), mod.text_info, mod.img_loader);
   }else
      mod.text_info.bgnd_color = GetColor(COL_LIGHT_GREY);
   mod.FinishInit();
}

//----------------------------

void C_mode_read_mail::FinishInit(){

   if(text_info.HasDefaultBgndColor())
      text_info.bgnd_color = app.GetColor(app.COL_WHITE) & 0xffffff;

   app.CreateTimer(*this, 40);
   text_info.ts.font_index = app.config.viewer_font_index;
   app.CountTextLinesAndHeight(text_info, app.config.viewer_font_index);
   //GoToNextActiveObject(text_info);

   sb.total_space = text_info.total_height;
   sb.SetVisibleFlag();

   app.ActivateMode(*this);
}

//----------------------------

                           //reading message from a file
class C_mode_read_mail_file: public C_mode_read_mail{
protected:
   virtual void ReplyToMessage(bool reply, bool forward, bool reply_all, bool detect_reply_all){
      if(!app.SafeReturnToAccountsMode())
         return;
      if(!app.mode || !app.NumAccounts())
         return;
      C_mail_client::C_mode_accounts &mod_acc = (C_mail_client::C_mode_accounts&)*app.mode;
      app.OpenMailbox(mod_acc, true);

      app.SetModeWriteMail(fld, &msg, reply, forward, reply_all);
                              //keep temp files only if forwarding, and there're some attachments
                              // this will leave temp mail folder, which will be deleted sometimes in future (cleanup phase)
      if(forward && msg.attachments.Size())
         keep_tmp_files = true;
   }

//----------------------------

   virtual void ShowMessageDetails(){
                              //we know full headers
      app.ShowMessageDetails(msg, full_hdrs.begin());
   }
public:
   C_smart_ptr<C_message_container> fld;
   S_message msg;
   Cstr_w mail_data_path;
   bool keep_tmp_files;
   C_vector<char> full_hdrs;

   C_mode_read_mail_file(C_mail_client &_app, const Cstr_w &_mail_data_path):
      C_mode_read_mail(_app, msg, NULL),
      keep_tmp_files(false),
      mail_data_path(_mail_data_path)
   {
      fld = new(true) C_message_container;
      fld->Release();
   }
   ~C_mode_read_mail_file(){
      if(!keep_tmp_files)
         fld->DeleteContainerFiles(mail_data_path);
   }
};

//----------------------------

bool C_mail_client::SetModeReadMail(const wchar *fn){

                              //open EML message from file
   C_file fl;
   if(!fl.Open(fn))
      return false;

   C_mode_read_mail_file &mod = *new(true) C_mode_read_mail_file(*this, mail_data_path);
                              //decode the message into temp account (with own folder)
   S_account acc;
   mod.fld->msg_folder_id = 0xffffffff;
   C_mode_connection_in &con = *new(true) C_mode_connection_in(*this, NULL, acc, mod.fld, C_mode_connection_base::ACT_GET_BODY, NULL);
   con.progress_drawn = true;
   S_download_body_data *body_data = new(true) S_download_body_data;
   bool is_hdr = true;
   while(!fl.IsEof()){
      Cstr_c s = fl.GetLine();
      C_buffer<char> line;
      line.Assign((const char*)s, (const char*)s+s.Length()+1);
      if(is_hdr){
                              //save headers
         if(s.Length()){
            mod.full_hdrs.insert(mod.full_hdrs.end(), line.Begin(), line.Begin()+s.Length());
            mod.full_hdrs.push_back('\n');
         }else
            is_hdr = false;
      }
      Cstr_w err;
      if(!AddRetrievedMessageLine(con, *body_data, mod.msg, line, err))
         break;
      if(err.Length()){
         mod.Release();
         con.Release();
         delete body_data;
         return false;
      }
   }
   mod.full_hdrs.push_back(0);
   //LOG_RUN("a");
   FinishBodyRetrieval(con, *body_data, mod.msg);
   con.Release();
                              //adopt headers
   dword flgs = mod.msg.flags;
   (S_message_header&)mod.msg = body_data->retrieved_header;
   delete body_data;
   mod.msg.flags = flgs;
   mod.InitLayout();
                              //finally open message body
   if(!OpenMessageBody(*mod.fld, mod.msg, mod.text_info, false)){
      mod.Release();
      return false;
   }
   mod.FinishInit();
   return true;
}

//----------------------------

void C_mode_read_mail::InitLayout(){

   C_mode::InitLayout();
   const int border = 1;

   C_text_viewer::title_height = app.fdb.cell_size_y*2 + app.fdb.line_spacing - 2;
   rc = S_rect(border, C_text_viewer::title_height, app.ScrnSX()-border*2, app.ScrnSY()-C_text_viewer::title_height-app.GetSoftButtonBarHeight()-border);

   dword num_att = msg.IsDeleted() ? 0 : msg.attachments.Size();
   int att_sy = app.icons_file->SizeY() + 8;
   //att_sy = Max(att_sy, app.fds.cell_size_y+app.fdb.cell_size_y+2);
   att_sy = Max(att_sy, (int)app.C_client::GetSoftButtonBarHeight());

                           //compute # of visible lines, and resize rectangle to whole lines
   //const int line_size = app.fds.line_spacing;
   if(num_att)
      rc.sy -= att_sy;
   //int vis_lines = rc.sy / line_size;
   //rc.sy = vis_lines * line_size;

   if(num_att){
      attach_browser.rc = S_rect(1, rc.Bottom()+1, app.ScrnSX()-2, att_sy);
      attach_browser.Init();
   }

   const int sb_width = app.GetScrollbarWidth();

   text_info.rc = rc;
   text_info.rc.x += app.fdb.letter_size_x/2;
   text_info.rc.sx -= sb_width + app.fdb.letter_size_x;

   sb.visible_space = rc.sy;
   sb.rc = S_rect(rc.Right()-sb_width-1, rc.y+1, sb_width, rc.sy-2);
   sb.SetVisibleFlag();

   attach_browser.ResetAfterScreenResize();

   if(ctrl_copy){
      S_rect trc = GetClientRect();
      trc.Compact();
      ctrl_copy->SetRect(trc);
   }

   const S_font &fdl = app.font_defs[app.UI_FONT_SMALL];
   rc_link_show = S_rect(rc.x+fdl.letter_size_x, rc.y-fdl.line_spacing-3, rc.sx-(sb.rc.sx+2)-fdl.letter_size_x*2, fdl.cell_size_y+1);
}

//----------------------------

void C_mail_client::CallNumberConfirm(const Cstr_w &txt){

#ifdef __SYMBIAN32__
   Cstr_c s;
   s.Copy(txt);
   SymbianMakePhoneCall(s);
#endif
}

//----------------------------

bool C_mail_client::Viewer_OpenLink(C_text_viewer &viewer, C_mode_mailbox *parent_mode, bool open_in_system_browser, bool open_image){

   const S_text_display_info &tdi = viewer.text_info;
   const S_hyperlink *href = tdi.GetActiveHyperlink();
   if(!href)
      return false;

   const char *link = href->link;
   if(!*link && href->image_index!=-1)
      open_image = true;

   if(open_image){
      if(href->image_index!=-1 && href->image_index<tdi.images.size()){
         const S_image_record &ir = tdi.images[href->image_index];
         if(ir.filename.Length()){
            Cstr_w title = ir.alt;
            if(!title.Length())
               title.Format(L"Image %") <<(href->image_index+1);
            //file_utils::GetFileNameNoPath(ir.filename)
            if(C_client_viewer::OpenFileForViewing(this, ir.filename, title)){
               viewer.SuspendLoader();
               return true;
            }
         }
      }
      return false;
   }

   if(text_utils::CheckStringBegin(link, "mailto:")){
#ifdef WINDOWS_MOBILE
                              //run standard browser for making phonecall
      if(open_in_system_browser){
         StartBrowser(link-7);
         return true;
      }
#endif
      if(!parent_mode)
         return false;
      Cstr_c cmd = link;
      mode = parent_mode;
      Cstr_c to;
      Cstr_w subject;

                              //parse command
      char *cp = &cmd.At(0);
      const char *pars = NULL;
      text_utils::SkipWS((const char*&)cp);
      for(int i=0; cp[i]; i++){
         if(cp[i]=='?'){
            cp[i] = 0;
            pars = cp+i+1;
            break;
         }
      }
      to = cp;
      if(pars){
                              //read additional params after '?'
         while(true){
            text_utils::SkipWS(pars);
            Cstr_c cmd1, val;
            if(!text_utils::ReadWord(pars, cmd1, "&= "))
               break;
            text_utils::SkipWS(pars);
            if(*pars!='=')
               break;
            ++pars;
            if(!text_utils::ReadWord(pars, val, "&"))
               break;
            cmd1.ToLower();
            if(cmd1=="subject"){
               subject.Copy(val);
            }else
            if(cmd1=="body"){
            }else{
               assert(0);
            }
            text_utils::SkipWS(pars);
            if(*pars!='&')
               break;
            ++pars;
         }
      }
      SetModeWriteMail(to, NULL, NULL, subject, NULL);
      return true;
   }
   if(text_utils::CheckStringBegin(link, text_utils::HTTP_PREFIX, true, false) ||
      text_utils::CheckStringBegin(link, text_utils::HTTPS_PREFIX, true, false)){
      if(open_in_system_browser){
         StartBrowser(link);
      }else{
         {
            viewer.SuspendLoader();
            SetModeDownload(link);
         }
      }
      return true;
   }
   if(text_utils::CheckStringBegin(link, "tel:", false, false)){
#ifdef WINDOWS_MOBILE
                              //run standard browser for making phonecall
      StartBrowser(link);
      return true;
#else
      Cstr_w tmp;
      tmp.Copy(link+4);
      //s<<GetText(TXT_Q_ARE_YOU_SURE) <<'\n' <<tmp;
      CreateTextEntryMode(*this, TXT_CALL_NUMBER, new(true) C_text_entry_call(*this), true, 80, tmp);
      return true;
#endif
   }
   if(link[0]=='#'){
                              //local bookmark
      int want_offs = -1;
      if(!link[1]){
         want_offs = 0;
      }else{
         for(int i=0; i<tdi.bookmarks.size(); i++){
            const S_text_display_info::S_bookmark &b = tdi.bookmarks[i];
            if(b.name==link+1){
               want_offs = b.byte_offset;
               break;
            }
         }
      }
      if(want_offs>=0){
         S_text_display_info &tdi1 = viewer.text_info;
                           //now we want to have bookmark offset at top of screen
         bool up = (tdi1.byte_offs > dword(want_offs));
         if(up){
                              //scroll down
            while(ScrollText(tdi1, -fds.line_spacing)){
               if(tdi1.byte_offs < dword(want_offs)){
                  ScrollText(tdi1, tdi1.pixel_offs);
                  break;
               }
            }
         }else{
                              //scroll up
            while(ScrollText(tdi1, fds.line_spacing)){
               if(tdi1.byte_offs > dword(want_offs)){
                  while(tdi1.byte_offs > dword(want_offs))
                     ScrollText(tdi1, -1);
                  ScrollText(tdi1, -font_defs[tdi1.ts.font_index].line_spacing);
                  break;
               }
            }
         }
         if(!HasMouse())
            ViewerGoToVisibleHyperlink(viewer, up);
         viewer.sb.pos = tdi1.top_pixel;
         RedrawScreen();
         return true;
      }
      return false;
   }
   //assert(0);
   return false;
}

//----------------------------

bool C_mail_client::Viewer_SaveTargetAsCallback(const Cstr_w *file, const C_vector<Cstr_w> *files){

   C_mode_read_mail &mod = (C_mode_read_mail&)*mode;

   const S_text_display_info &tdi = mod.text_info;
   const S_hyperlink *href = tdi.GetActiveHyperlink();
   if(href){
      const char *link = href->link;
      if(text_utils::CheckStringBegin(link, text_utils::HTTP_PREFIX, true, false)){
         SetModeDownload(link, file);
         return true;
      }
   }
   return false;
}

//----------------------------

void C_mail_client::Viewer_SaveTargetAs(C_text_viewer &viewer){

   const S_text_display_info &tdi = viewer.text_info;
   const S_hyperlink *href = tdi.GetActiveHyperlink();
   if(!href)
      return;
   const char *link = href->link;

   if(text_utils::CheckStringBegin(link, text_utils::HTTP_PREFIX)){
      viewer.SuspendLoader();
      Cstr_w fname;
      fname.Copy(file_utils::GetFileNameFromUrl(link));
      if(fname.Length())
         C_client_file_mgr::SetModeFileBrowser_GetSaveFileName(this, fname, (C_client_file_mgr::C_mode_file_browser::t_OpenCallback)&C_mail_client::Viewer_SaveTargetAsCallback);
   }
}

//----------------------------

bool C_mail_client::TickViewerImageLoader(C_mode &mod, C_socket_notify::E_SOCKET_EVENT ev, C_text_viewer &viewer, C_message_container *cnt, C_buffer<S_attachment> &atts, bool &redraw){

   C_multi_item_loader &ldr = viewer.img_loader;
   C_application_http::E_HTTP_LOADER lr = ((C_application_http*)this)->TickDataLoader(ldr, ev, redraw);
   switch(lr){
   case C_application_http::LDR_FAILED:
      {
                              //load failed, cancel and go to next image
         S_text_display_info &tdi = viewer.text_info;
         const Cstr_c &e = ldr.entries.front();
         for(int i=tdi.images.size(); i--; ){
            S_image_record &ir = tdi.images[i];
            if(ir.src==e)
               ir.invalid = true;
         }
         ldr.BeginNextEntry();
         ldr.prog_curr.pos = 0;
         ldr.prog_curr.total = 1;
      }
      //break;
                              //flow...
   case C_application_http::LDR_DONE:
   case C_application_http::LDR_NOT_FOUND:
      if(lr!=C_application_http::LDR_FAILED)
         FinishImageDownload(viewer, cnt, atts);
      if(ldr.entries.size()){
                              //connect to next entry
         ((C_application_http*)this)->Loader_ConnectTo(ldr, ldr.entries.front());
      }
      return true;
   }
   return false;
}

//----------------------------

bool C_mode_read_mail::GetPreviousNextFile(bool next, Cstr_w *filename, Cstr_w *title){

   const C_buffer<S_attachment> &atts = msg.attachments;
   int sel = attach_browser.selection;
   const S_attachment &att = atts[sel];

   Cstr_w ext = text_utils::GetExtension(att.suggested_filename);
   ext.ToLower();
   C_client::E_FILE_TYPE ft = app.DetermineFileType(ext);

   while(true){
      if(!next){
         if(!sel)
            break;
         --sel;
      }else{
         if(sel==int(atts.Size()-1))
            break;
         ++sel;
      }
      const S_attachment &att1 = atts[sel];
      if(att1.IsDownloaded()){
                              //check its file type
         ext = text_utils::GetExtension(att1.suggested_filename);
         ext.ToLower();
         if(app.DetermineFileType(ext)==ft){
                                 //found it
            if(filename){
               *filename = att1.filename.FromUtf8();
               *title = att1.suggested_filename;
               attach_browser.selection = sel;
                              //make it visible
               attach_browser.MakeSelectionVisible();
            }
            return true;
         }
      }
   }
   return false;
}

//----------------------------

bool C_mail_client::ReadMail_SaveAttachments(const Cstr_w &file, bool all){

   if(all)
      C_client_file_mgr::FileBrowser_Close(this, (C_client_file_mgr::C_mode_file_browser&)*mode);
   C_mode_read_mail &mod = (C_mode_read_mail&)*mode;
   for(dword i=0; i<mod.msg.attachments.Size(); i++){
      if(!all && i!=mod.attach_browser.selection)
         continue;
      const S_attachment &att = mod.msg.attachments[i];
      Cstr_w fn_dst;
      if(!all)
         fn_dst = file;
      else
         fn_dst<<file <<att.suggested_filename;
      if(C_file::WRITE_OK!=C_file::CopyFile(att.filename.FromUtf8(), fn_dst)){
         ShowErrorWindow(TXT_ERR_WRITE, fn_dst);
         return false;
      }
   }
   RedrawScreen();
   return true;
}

//----------------------------

void C_mail_client::ConvertFormattedTextToPlainText(const S_text_display_info &td, C_vector<wchar> &body){

   const void *vp = td.is_wide ? (const char*)(const wchar*)td.body_w : (const char*)td.body_c;
   body.reserve(td.Length());
   while(true){
      dword c = text_utils::GetChar(vp, td.is_wide);
      if(!c)
         break;
      if(S_text_style::IsControlCode(c)){
         const char *&cp = (const char*&)vp;
         if(c!=CC_WIDE_CHAR){
            --cp;
            S_text_style::SkipCode(cp);
            continue;
         }
         c = S_text_style::ReadWideChar(cp);
      }else
         c = encoding::ConvertCodedCharToUnicode(c, td.body_c.coding);
      body.push_back(wchar(c));
   }
   body.push_back(0);
}

//----------------------------

void C_mode_read_mail::StartCopyText(){

   SetTitle(app.GetText(TXT_COPY_FROM_TEXT));
   GetSoftkeyBar()->ResetAllButtons();

   ctrl_copy = new(true) C_ctrl_text_entry(this, 250000, TXTED_READ_ONLY, app.config.viewer_font_index);
   AddControl(ctrl_copy);
   SetFocus(ctrl_copy);
   InitLayout();

   C_vector<wchar> body;
   app.ConvertFormattedTextToPlainText(text_info, body);

   ctrl_copy->SetText(body.begin());
   ctrl_copy->SetCursorPos(0);
   //text_copy_select_pos = -1;
}

//----------------------------

void C_mode_read_mail::Scroll(bool top){

   int n = sb.total_space;
   if(top)
      n = -n;
   app.ScrollText(text_info, n);
   sb.pos = text_info.top_pixel;
}

//----------------------------

void C_mode_read_mail::CopyLink(){

   const S_hyperlink *href = text_info.GetActiveHyperlink();
   if(href){
      if(href->link.Length()){
         Cstr_w s;
         s.Copy(href->link);
         C_text_editor::ClipboardCopy(s);
      }
   }
}

//----------------------------

void C_mode_read_mail::ProcessMenu(int itm, dword menu_id){

   switch((menu_id<<16)|itm){
   case TXT_REPLY:
   case TXT_FORWARD:
   case TXT_REPLY_ALL:
      ReplyToMessage((itm==TXT_REPLY), (itm==TXT_FORWARD), (itm==TXT_REPLY_ALL), false);
      break;

   case TXT_SHOW_DETAILS:
      ShowMessageDetails();
      break;

   case TXT_MARK_FLAGGED:
   case TXT_MARK_UNFLAGGED:
      msg.flags ^= S_message::MSG_FLAGGED;
      msg.flags |= S_message::MSG_IMAP_FLAGGED_DIRTY;
      GetMailbox().GetContainer().MakeDirty();
      app.MailboxUpdateImapIdleFolder(GetMailbox(), false);
      break;

   case TXT_DELETE:
   case TXT_UNDELETE:
      {
         bool redraw;
         DeleteMessage(redraw);
         if(redraw)
            app.RedrawScreen();
      }
      break;

   case TXT_HIDE:
      HideMessage();
      break;

   case TXT_ATTACHMENT:
      {
         S_attachment &att = msg.attachments[attach_browser.selection];

         menu = CreateMenu(1);
         if(att.IsDownloaded()){
            C_file fl;
            if(!fl.Open(att.filename.FromUtf8())){
               att.filename.Clear();
               if(acc->IsImap())
                  menu->AddItem(TXT_DOWNLOAD);
            }else{
               menu->AddItem(TXT_OPEN);
               menu->AddItem(TXT_OPEN_BY_SYSTEM);
               menu->AddItem(TXT_SAVE);
               //menu->AddItem(TXT_DELETE_FROM_PHONE);
            }
         }else{
            menu->AddItem(TXT_DOWNLOAD);
         }
                           //if at least 2 attachments are not yet downloaded, add 'Download all' option
         int num_n = 0;
         for(int i=msg.attachments.Size(); i--; ){
            if(!msg.attachments[i].IsDownloaded())
               ++num_n;
         }
         if(msg.attachments.Size()>=2){
            if(num_n){
               menu->AddSeparator();
               menu->AddItem(TXT_DOWNLOAD_ALL);
            }else
            if(!num_n){
               menu->AddSeparator();
               menu->AddItem(TXT_SAVE_ALL);
            }
         }
         app.PrepareMenu(menu);
      }
      break;

   case 0x10000|TXT_SAVE:
   case 0x10000|TXT_SAVE_ALL:
      {
         assert(msg.attachments.Size());
         if(itm==TXT_SAVE){
            const S_attachment &att = msg.attachments[attach_browser.selection];
            C_client_file_mgr::SetModeFileBrowser_GetSaveFileName(&app, att.suggested_filename,
               (C_client_file_mgr::C_mode_file_browser::t_OpenCallback)&C_mail_client::ReadMail_SaveAttachmentOne);
         }else{
            C_client_file_mgr::SetModeFileBrowser(&app, C_client_file_mgr::C_mode_file_browser::MODE_EXPLORER, true,
               (C_client_file_mgr::C_mode_file_browser::t_OpenCallback)&C_mail_client::ReadMail_SaveAttachmentAll, TXT_SAVE_AS, NULL, C_client_file_mgr::GETDIR_DIRECTORIES);
         }
      }
      break;

   case 0x10000|TXT_OPEN:
      {
         const S_attachment &att = msg.attachments[attach_browser.selection];
         if(!att.IsDownloaded() && acc->IsImap())
            DownloadAttachment(true, false);
         else{
            C_client_viewer::OpenFileForViewing(&app, att.filename.FromUtf8(), att.suggested_filename, NULL, NULL, NULL, this);
         }
      }
      break;

   case 0x10000|TXT_OPEN_BY_SYSTEM:
      {
         const S_attachment &att = msg.attachments[attach_browser.selection];
         if(!att.IsDownloaded())
            DownloadAttachment(true, false);
         else
            app.OpenFileBySystem(att.filename.FromUtf8());
      }
      break;

   case TXT_EDIT:
      {
         C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
         app.CloseMode(*this, false);
         app.OpenMessage(mod_mbox);
      }
      break;
      /*
   case TXT_DELETE_FROM_PHONE:
      {
         S_attachment &att = mod.msg.attachments[mod.attach_browser.selection];
         assert(att.IsDownloaded());
         C_file fl;
         if(fl.Open(att.filename)){
            att.file_size = fl.GetFileSize();
            fl.Close();
         }
         C_file::DeleteFile(att.filename);
         att.filename.Clear();
         need_save_messages = true;
      }
      break;
      */
   case TXT_PREVIOUS:
   case TXT_NEXT:
      NavigateToNext((itm==TXT_NEXT));
      break;

   case 0x10000|TXT_DOWNLOAD:
      DownloadAttachment(false, false);
      break;

   case TXT_DOWNLOAD:
      {
         assert(msg.flags&S_message::MSG_PARTIAL_DOWNLOAD);
         C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
         app.CloseMode(*this);
         C_mail_client::S_connection_params p; p.message_index = mod_mbox.GetRealMessageIndex(mod_mbox.selection);
         app.SetModeConnection(mod_mbox.acc, mod_mbox.folder, C_mail_client::C_mode_connection::ACT_GET_BODY, &p);
      }
      break;

   case 0x10000|TXT_DOWNLOAD_ALL:
      DownloadAttachment(false, true);
      break;

   case TXT_SCROLL:
      menu = CreateMenu();
      menu->AddItem(TXT_PAGE_UP, 0, "[*]", "[Q]");
      menu->AddItem(TXT_PAGE_DOWN, 0, "[#]", "[A]");
      menu->AddItem(TXT_TOP, 0, app.GetShiftShortcut("[%+Up]"));
      menu->AddItem(TXT_BOTTOM, 0, app.GetShiftShortcut("[%+Down]"));
      app.PrepareMenu(menu);
      break;

   case TXT_PAGE_UP:
   case TXT_PAGE_DOWN:
      auto_scroll_time = (sb.visible_space-app.font_defs[app.config.viewer_font_index].line_spacing) << 8;
      if(itm==TXT_PAGE_UP)
         auto_scroll_time = -auto_scroll_time;
      break;

   case TXT_TOP:
   case TXT_BOTTOM:
      Scroll(itm==TXT_TOP);
      break;

   case TXT_SHOW_LINK:
      app.ViewerShowLink(*this);
      break;

   case TXT_OPEN_LINK:
      app.Viewer_OpenLink(*this, !acc ? NULL : &GetMailbox(), false);
      break;

   case TXT_OPEN_BY_SYSTEM:
      app.Viewer_OpenLink(*this, &GetMailbox(), true);
      break;

   case TXT_COPY_LINK:
      CopyLink();
      break;

   case TXT_VIEW_IMAGE:
      app.Viewer_OpenLink(*this, !acc ? NULL : &GetMailbox(), false, true);
      break;

   case TXT_SAVE_TARGET:
      app.Viewer_SaveTargetAs(*this);
      break;

   case TXT_COPY_FROM_TEXT:
      StartCopyText();
      break;

   case TXT_BACK:
      if(acc){
         C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
         app.SetMailboxSelection(mod_mbox, mod_mbox.selection);
      }
      app.CloseMode(*this);
      break;

   case TXT_EXIT:
      app.Exit();
      break;

   case TXT_MESSAGE:
      {
         menu = CreateMenu();
         if(acc){
            if(msg.flags&S_message::MSG_PARTIAL_DOWNLOAD){
               menu->AddItem(TXT_DOWNLOAD, NULL, app.send_key_name);
               menu->AddSeparator();
            }
            menu->AddItem(TXT_DELETE, 0, app.delete_key_name, NULL, app.BUT_DELETE);
         }
         menu->AddItem(TXT_REPLY, 0, "[6]", "[R]", app.BUT_REPLY);
         if(msg.HasMultipleRecipients(acc))
            menu->AddItem(TXT_REPLY_ALL, 0, 0, "[L]");
         menu->AddItem(TXT_FORWARD, 0, "[7]", "[F]", app.BUT_FORWARD);
         menu->AddSeparator();
         menu->AddItem(TXT_SHOW_DETAILS, 0, "[5]", "[E]");
         if(acc){
            if(acc->NumFolders()>1)
               menu->AddItem(TXT_MOVE_TO_FOLDER, 0, "[1]", "[M]");
         }
         app.PrepareMenu(menu);
      }
      break;

   case TXT_MARK:
      menu = CreateMenu();
      //menu->AddItem((msg.flags&S_message::MSG_READ) ? TXT_MARK_UNREAD : TXT_MARK_READ, 0, "[9]", "[K]");
      menu->AddItem((msg.flags&S_message::MSG_FLAGGED) ? TXT_MARK_UNFLAGGED : TXT_MARK_FLAGGED, 0, "[9]", "[G]");
      if(acc){
         if(!(msg.flags&S_message::MSG_HIDDEN))
            menu->AddItem(TXT_HIDE, 0, "[8]", "[H]");
      }
      app.PrepareMenu(menu);
      break;

   case TXT_MOVE_TO_FOLDER:
      if(acc){
         C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
         app.CloseMode(*this, false);
         app.SetModeFolderSelector(mod_mbox.acc, (C_mail_client::C_mode_folder_selector::t_FolderSelected)&C_mail_client::MailboxMoveMessagesFolderSelected, mod_mbox.folder);
      }
      break;

   case _TXT_COPY:
      if(ctrl_copy){
         ctrl_copy->Copy();
         CloseCopy();
      }
      break;

      /*
   case TXT_SELECT:
      if(ctrl_copy){
         if(text_copy_select_pos==-1)
            text_copy_select_pos = ctrl_copy->GetCursorPos();
         else
            text_copy_select_pos = -1;
         ctrl_copy->SetCursorPos(ctrl_copy->GetCursorPos());
      }
      break;
      */

   case TXT_MARK_ALL:
      if(ctrl_copy)
         ctrl_copy->SetCursorPos(ctrl_copy->GetTextLength(), 0);
      break;

   case TXT_CANCEL:
      if(ctrl_copy)
         CloseCopy();
      break;

   case TXT_CFG_DOWNLOAD_HTML_IMAGES:
      if(!img_loader.socket)
         app.OpenHtmlImages(msg.inline_attachments, &msg.attachments, true, text_info, img_loader);
      else
         img_loader.Suspend();
      break;
   }
}

//----------------------------

void C_mode_read_mail::ProcessInput(S_user_input &ui, bool &redraw){

   C_mode::ProcessInput(ui, redraw);
   S_text_display_info &tdi = text_info;
   int scroll_pixels = 0;      //positive = down; negative = up

   bool redraw_body = false;
   bool redraw_atts = false;
   const int lsc = link_show_count;

#ifdef USE_MOUSE
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){
      if(!ctrl_copy){
         C_scrollbar::E_PROCESS_MOUSE pm = app.ProcessScrollbarMouse(sb, ui);
         switch(pm){
         case C_scrollbar::PM_PROCESSED:
            redraw_body = true;
            break;
         case C_scrollbar::PM_CHANGED:
            scroll_pixels = sb.pos-tdi.top_pixel;
            kinetic_movement.Reset();
            break;
         default:
            kinetic_movement.ProcessInput(ui, rc, 0, -1);
            if(app.ViewerProcessMouse(*this, ui, scroll_pixels, redraw_body)){
               if(app.Viewer_OpenLink(*this, !acc ? NULL : &GetMailbox(), app.config_mail.tweaks.open_links_by_system))
                  return;
            }else if(ui.CheckMouseInRect(rc)){
               if((ui.mouse_buttons&MOUSE_BUTTON_1_DOWN) && acc){
                              //make touch menu
                  menu = app.CreateTouchMenu();
                  const S_hyperlink *href = text_info.GetActiveHyperlink();
                  if(href){
                     const char *link = href->link;
                     if(*link){
                        menu->AddItem(TXT_OPEN_LINK);
                        menu->AddItem(TXT_COPY_LINK);
                     }else{
                        menu->AddSeparator();
                        menu->AddSeparator();
                     }
                     menu->AddSeparator();
                     if(href->image_index!=-1){
                              //image link
                        if(*link)
                           menu->AddItem(TXT_OPEN_BY_SYSTEM);
                        else
                           menu->AddSeparator();
                        menu->AddItem(TXT_VIEW_IMAGE);
                     }else{
                        menu->AddSeparator();
                        if(*link)
                           menu->AddItem(TXT_OPEN_BY_SYSTEM);
                     }
                  }else if(acc){
                     {
                        menu->AddItem(TXT_REPLY, 0, 0, 0, app.BUT_REPLY);
                        menu->AddItem(TXT_DELETE, 0, 0, 0, app.BUT_DELETE);
                        if(!(msg.flags&S_message::MSG_HIDDEN))
                           menu->AddItem(TXT_HIDE);
                        else
                           menu->AddSeparator();
                        if(acc->NumFolders()>1)
                           menu->AddItem(TXT_MOVE_TO_FOLDER);
                        else
                           menu->AddSeparator();
                     }
                     menu->AddItem(TXT_SHOW_DETAILS);
                  }
                  app.PrepareTouchMenu(menu, ui);
               }
            }
            if((ui.mouse_buttons&MOUSE_BUTTON_1_DRAG) && mouse_drag && acc){
                           //check horizontal drag for moving to prev/next message
               int dx = ui.mouse.x - drag_mouse_begin.x;
               int dy = ui.mouse.y - drag_mouse_begin.y;
               if(Abs(dx)>=Abs(dy)){
                  dx = ui.mouse.x - drag_mouse_begin.x;
                  if(Abs(dx) > app.fdb.cell_size_x*2){
                     drag_x_pos = dx;
                     redraw_body = true;
                  }else if(drag_x_pos){
                     drag_x_pos = 0;
                     redraw_body = true;
                  }
               }else{
                           //more movement vertically, reset X dragging
                  //drag_mouse_begin.x = ui.mouse.x;
                  if(drag_x_pos){
                     drag_x_pos = 0;
                     redraw_body = true;
                  }
               }
            }
            if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
               if(drag_x_pos){
                  if(Abs(drag_x_pos) >= rc.sx/4){
                     ui.key = (drag_x_pos<0) ? 'n' : 'p';
                  }
                  drag_x_pos = 0;
                  redraw_body = true;
               }
            }
         }
      }
   }
#endif

   if(ctrl_copy){
                              //text copy mode
      //const bool skip_word = (ui.key_bits&(GKEY_CTRL|GKEY_SHIFT));
      switch(ui.key){
      case K_RIGHT_SOFT:
      case K_BACK:
      case K_ESC:
         /*
         if(text_copy_select_pos!=-1 || ctrl_copy->GetCursorPos()!=ctrl_copy->GetCursorSel()){
            text_copy_select_pos = -1;
            ctrl_copy->SetCursorPos(ctrl_copy->GetCursorPos());
         }else
         */
            CloseCopy();
         redraw = true;
         break;

      case K_ENTER:
         /*
         if(text_copy_select_pos==-1 && ctrl_copy->GetCursorPos()==ctrl_copy->GetCursorSel())
            text_copy_select_pos = ctrl_copy->GetCursorPos();
         else{
         */
            ctrl_copy->Copy();
            CloseCopy();
         //}
         redraw = true;
         break;
         /*
      case K_CURSORLEFT:
      case K_CURSORRIGHT:
         {
            dword pos = ctrl_copy->GetCursorPos();
            const wchar *txt = ctrl_copy->GetText();
            if(ui.key==K_CURSORLEFT){
               bool had_char = false;
               while(pos){
                  --pos;
                  if(!skip_word || !pos)
                     break;
                  wchar c = txt[pos-1];
                  if(c=='\n') break;
                  if(c!=' '){
                     had_char = true;
                  }else{
                     if(had_char)
                        break;
                  }
               }
            }else{
               bool had_space = (txt[pos]=='\n');
               while(pos<ctrl_copy->GetTextLength()){
                  ++pos;
                  if(!skip_word)
                     break;
                  wchar c = txt[pos];
                  if(c=='\n') break;
                  if(c==' '){
                     had_space = true;
                  }else{
                     if(had_space)
                        break;
                  }
               }
            }
            ctrl_copy->SetCursorPos(pos, (text_copy_select_pos!=-1 || (ui.key_bits&GKEY_SHIFT)) ? ctrl_copy->GetCursorSel() : pos);

            mod_copy.adjust_scroll = true;
            redraw = true;
         }
         break;
         */

      case '4':
      case 'l':
         {
            ctrl_copy->SetCursorPos(ctrl_copy->GetTextLength(), 0);
            redraw = true;
         }
         break;

         /*
      case '#':               //scroll page
      case '*':
      case 'q':
      case 'a':
      case K_PAGE_UP:
      case K_PAGE_DOWN:
         {
            if(ui.key=='*' || ui.key=='q' || ui.key==K_PAGE_UP)
               ui.key = K_CURSORUP;
            else
               ui.key = K_CURSORDOWN;
            const int line_size = app.font_defs[app.config.viewer_font_index].line_spacing;
            dword num_lines = rc.sy / line_size;
            while(num_lines--)
               mod_copy.Tick(ui, false);
            redraw = true;
         }
         break;
         */
      }
   }else{
      app.MapScrollingKeys(ui);
      switch(ui.key){
      case K_RIGHT_SOFT:
      case K_BACK:
      case K_ESC:
         if(acc){
            C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
            app.CloseMode(*this, false);
            app.SetMailboxSelection(mod_mbox, mod_mbox.selection);
            redraw = true;
         }else
            app.CloseMode(*this);
         return;

      case K_DEL:
#ifdef _WIN32_WCE
      case 'd':
#endif
         DeleteMessage(redraw);
         return;

      case 'k':
      case '0':
         app.ViewerShowLink(*this);
         redraw_body = true;
         break;

      case '1':
      case 'm':
         if(acc && acc->IsImap()){
            C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
            app.CloseMode(*this, false);
            app.SetModeFolderSelector(mod_mbox.acc, (C_mail_client::C_mode_folder_selector::t_FolderSelected)&C_mail_client::MailboxMoveMessagesFolderSelected, mod_mbox.folder);
         }
         return;

      case '4':
      case 'c':
         StartCopyText();
         redraw = true;
         break;

      case 'e':
      case '5':                  //details
         ShowMessageDetails();
         return;

      case 'r':
      case 'f':
      case '6':                  //reply
      case '7':                  //forward
      case 'l':                  //reply all
         ReplyToMessage((ui.key=='6' || ui.key=='r'), (ui.key=='7' || ui.key=='f'), (ui.key=='l'), true);
         return;

      case 'g':
      case '9':
         if(acc){
            ProcessMenu(TXT_MARK_FLAGGED, 0);
            redraw = true;
         }
         break;

      case 'y':
         CopyLink();
         break;

      case '8':
      case 'h':
         if(acc){
            HideMessage();
            return;
         }
         break;

      case K_HOME:
      case K_END:
         Scroll(ui.key==K_HOME);
         redraw = true;
         break;

      case K_ENTER:
                              //perform action
                              // note: on sent messages we don't open them, because they were just referenced, and may be deleted
         if(msg.attachments.Size() && att_browser_focused){
                              //open selected attachment
            S_attachment &att = msg.attachments[attach_browser.selection];
            if(!att.IsDownloaded()){
               DownloadAttachment(true, false);
               return;
            }
            if(C_client_viewer::OpenFileForViewing(&app, att.filename.FromUtf8(), att.suggested_filename, NULL, NULL, NULL, this)){
               return;
            }
            if(app.OpenFileBySystem(att.filename.FromUtf8()))
               return;
                              //can't be opened, check if file exists
                              //check if file really exists
            C_file fl;
            if(!fl.Open(att.filename.FromUtf8())){
               att.filename.Clear();
               if(acc->IsImap()){
                  DownloadAttachment(true, false);
                  return;
               }
            }
         }
         if(app.Viewer_OpenLink(*this, &GetMailbox(), app.config_mail.tweaks.open_links_by_system != bool(ui.key_bits&GKEY_SHIFT))){
            return;
         }
         break;

      case K_SEND:
         if(msg.flags&S_message::MSG_PARTIAL_DOWNLOAD){
            C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
            app.CloseMode(*this);
            C_mail_client::S_connection_params p; p.message_index = mod_mbox.GetRealMessageIndex(mod_mbox.selection);
            app.SetModeConnection(mod_mbox.acc, mod_mbox.folder, C_mail_client::C_mode_connection::ACT_GET_BODY, &p);
            return;
         }
         break;

      case K_CURSORUP:
      case K_CURSORDOWN:
         if(ui.key_bits&GKEY_SHIFT){
            Scroll(ui.key==K_CURSORUP);
            redraw = true;
            /*
         }else
         if(app.HasMouse()){
            auto_scroll_time = (sb.visible_space/2) << 8;
            if(ui.key==K_CURSORUP)
               auto_scroll_time = -auto_scroll_time;
               */
         }else{
            kinetic_movement.Reset();
            int li = text_info.active_link_index;
            app.ViewerProcessKeys(*this, ui, scroll_pixels, redraw_body);
            if(text_info.active_link_index!=li)
               att_browser_focused = false;
         }
         break;

      case '#':               //scroll page
      case '*':
      case 'q':
      case 'a':
      case K_PAGE_UP:
      case K_PAGE_DOWN:
      case ' ':
         kinetic_movement.Reset();
         auto_scroll_time = (sb.visible_space-app.font_defs[app.config.viewer_font_index].line_spacing) << 8;
         if(ui.key=='*' || ui.key=='q' || ui.key==K_PAGE_UP)
            auto_scroll_time = -auto_scroll_time;
         break;

      case '2':
      case '3':
      case 'p':
      case 'n':
         if(acc)
         if(NavigateToNext((ui.key=='3' || ui.key=='n')))
            return;
         break;

      default:
         dword num_att = msg.attachments.Size();
         bool att_sel_changed = false;
         if(num_att){
            if(!att_browser_focused && (ui.key==K_CURSORLEFT || ui.key==K_CURSORRIGHT)){
               att_sel_changed = true;
               redraw_atts = true;
            }else{
               bool popup_touch_menu;
               if(attach_browser.Tick(ui, num_att, redraw_atts, att_sel_changed, popup_touch_menu)){
#ifdef USE_MOUSE
                  const S_attachment &att = msg.attachments[attach_browser.selection];
                  if(!att.IsDownloaded() && acc->IsImap()){
                     DownloadAttachment(true, false);
                     return;
                  }
                  if(C_client_viewer::OpenFileForViewing(&app, att.filename.FromUtf8(), att.suggested_filename, NULL, NULL, NULL, this))
                     return;
                  if(app.OpenFileBySystem(att.filename.FromUtf8()))
                     return;
#endif
               }
#ifdef USE_MOUSE
               if(popup_touch_menu){
                  menu = app.CreateTouchMenu(NULL, 1);
                  S_attachment &att = msg.attachments[attach_browser.selection];
                  bool is_dl = att.IsDownloaded();
                  if(is_dl){
                     C_file fl;
                     if(!fl.Open(att.filename.FromUtf8())){
                        att.filename.Clear();
                        is_dl = false;
                     }
                  }
                  if(acc && !att.IsDownloaded()){
                     menu->AddSeparator();
                     if(acc->IsImap()){
                        menu->AddItem(TXT_DOWNLOAD);
                     }else
                        menu->AddSeparator();
                     menu->AddSeparator();
                  }else{
                     menu->AddItem(TXT_OPEN_BY_SYSTEM);
                     menu->AddSeparator();
                     menu->AddItem(TXT_SAVE);
                  }
                  int num_dl = 0;
                  for(int i=msg.attachments.Size(); i--; ){
                     if(msg.attachments[i].IsDownloaded())
                        ++num_dl;
                  }
                  if((msg.attachments.Size()-num_dl)>1)
                     menu->AddItem(TXT_DOWNLOAD_ALL);
                  else
                     menu->AddSeparator();
                  menu->AddItem(TXT_OPEN);
                  app.PrepareTouchMenu(menu, ui);
               }
#endif
            }
         }
         if(acc){
            if(att_sel_changed){
               att_browser_focused = true;
            }else{
               if(ui.key==K_CURSORLEFT || ui.key==K_CURSORRIGHT){
                  //navigate to left/right mail
                  if(NavigateToNext((ui.key==K_CURSORRIGHT)))
                     return;
               }
            }
         }
      }
   }
   if(scroll_pixels && app.ScrollText(tdi, scroll_pixels)){
      sb.pos = tdi.top_pixel;
      redraw_body = true;
   }
   if(redraw_body){
      if(lsc!=link_show_count)
         redraw = true;
      else
         DrawTextBody();
   }
   if(redraw_atts)
      DrawAttachments();
}

//----------------------------

void C_mode_read_mail::Tick(dword time, bool &redraw){

   if(!IsActive())
      return;
   int li = text_info.active_link_index;
   const int lsc = link_show_count;

   bool redraw_text = false;
   app.ViewerTickCommon(*this, time, redraw_text);

   if(kinetic_movement.IsAnimating()){
      S_point p;
      kinetic_movement.Tick(time, &p);
      if(app.ScrollText(text_info, p.y)){
         sb.pos = text_info.top_pixel;
         //redraw = true;
         app.ViewerGoToVisibleHyperlink(*this, (p.y<0));
         DrawTextBody();
      }else
         kinetic_movement.Reset();
   }

   if(redraw_text){
      if(menu || lsc!=link_show_count)
         redraw = true;
      else
         DrawTextBody();
   }

   if(text_info.active_link_index!=li)
      att_browser_focused = false;

   for(int i=text_info.last_rendered_images.size(); i--; ){
      C_image *img = (C_image*)text_info.last_rendered_images[i];
      if(img->Tick(time))
         redraw = true;
   }
}

//----------------------------

void C_mode_read_mail::DrawTextBody() const{

   const S_text_display_info &tdi = text_info;
   if(drag_x_pos){
      app.FillRect(rc, 0xffffffff);
      app.ClearToBackground(tdi.rc);
      if(Abs(drag_x_pos) >= rc.sx/4){
         dword color = app.GetColor(app.COL_TEXT);
                              //draw next/prev info
         int y = tdi.rc.CenterY();
         int ARROW_SZ = app.fdb.cell_size_y;

         const C_mail_client::C_mode_mailbox &mod_mbox = GetMailbox();
         int sel = mod_mbox.selection;
         Cstr_w cnt;
         if(drag_x_pos<0){
                              //next
            int x = tdi.rc.Right()-1;
            if(sel>=int(mod_mbox.num_vis_msgs)-1)
               color = MulAlpha(color, 0x8000);
            else{
               cnt.Format(L"%/%") <<(sel+2) <<(int)mod_mbox.num_vis_msgs;
               app.DrawString(cnt, x, y-app.fdb.line_spacing*2, app.UI_FONT_BIG, FF_RIGHT, color);
            }
            app.DrawString(app.GetText(TXT_NEXT), x, y, app.UI_FONT_BIG, FF_RIGHT, color);
            app.DrawArrowHorizontal(x-ARROW_SZ, y-ARROW_SZ, ARROW_SZ, color, true);
         }else{
                              //previous
            int x = tdi.rc.x+1;
            if(!sel)
               color = MulAlpha(color, 0x8000);
            else{
               cnt.Format(L"%/%") <<(sel) <<(int)mod_mbox.num_vis_msgs;
               app.DrawString(cnt, x, y-app.fdb.line_spacing*2, app.UI_FONT_BIG, 0, color);
            }
            app.DrawString(app.GetText(TXT_PREVIOUS), x, y, app.UI_FONT_BIG, 0, color);
            app.DrawArrowHorizontal(x, y-ARROW_SZ, ARROW_SZ, color, false);
         }
      }

      S_rect &trc = const_cast<S_text_display_info&>(tdi).rc;
      S_rect rcx = trc;
      trc.x += drag_x_pos;
      rcx.SetIntersection(rcx, trc);
      app.DrawFormattedText(tdi, &rcx);
      trc.x -= drag_x_pos;
   }else
      app.DrawFormattedText(tdi, &rc);
   app.DrawScrollbar(sb);//, text_info.HasDefaultBgndColor() ? 0xffffffff : text_info.ts.bgnd_color);
   if(img_loader.IsActive()){
      app.DrawOutline(img_loader.prog_curr.rc, 0xff000000);
      app.DrawOutline(img_loader.prog_all.rc, 0xff000000);
      app.DrawProgress(img_loader.prog_curr, 0xffff0000, false, 0x40000000);
      app.DrawProgress(img_loader.prog_all, 0xff00ff00, false, 0x40000000);
   }
}

//----------------------------

void C_mode_read_mail::DrawHeaderFields() const{

   app.ClearTitleBar(rc.sy);
                        //draw header fields
   dword col_text = app.GetColor(app.COL_TEXT_TITLE);
   int x = app.fdb.letter_size_x / 2;
   int y = app.fdb.cell_size_y / 4;

   bool is_sent_folder = false;
   if(acc && acc->IsImap() && !GetMailbox().folder->IsInbox()){
      is_sent_folder = (acc->GetFullFolderName(*GetMailbox().folder) == acc->GetSentFolderName());
   }
   bool is_sent_msg = is_sent_folder;
   if(!is_sent_msg && acc)
      is_sent_msg = (msg.sender.email==acc->primary_identity.email && msg.to_emails!=acc->primary_identity.email);

   const dword sx = app.ScrnSX();
   int right_space = 0;//(fds.letter_size_x+1)*10;
   {
      int xx = sx-1, yy = y+1;
      dword flg = FF_RIGHT;
#ifdef S60
      if(app.GetScreenRotation()!=app.ROTATION_NONE && !app.HasMouse()){
         xx = app.fds.letter_size_x/3;
         yy = app.ScrnSY() - app.fds.line_spacing;
         flg = 0;
      }
#endif
      Cstr_w dstr, tstr;
      S_date_time dt; dt.SetFromSeconds(msg.date);
      app.GetDateString(dt, dstr);
      tstr = text_utils::GetTimeString(dt, false);
      dstr<<' ' <<tstr;
      right_space = app.DrawString(dstr, xx, yy, app.UI_FONT_SMALL, flg, MulAlpha(col_text, 0x8000));
#ifdef S60
      if(app.GetScreenRotation()!=app.ROTATION_NONE && !app.HasMouse())
         right_space = app.GetTextWidth(app.GetText(TXT_BACK), app.UI_FONT_BIG, FF_BOLD);
#endif
   }
                        //draw from
   Cstr_w s;
   if(is_sent_msg || (msg.flags&S_message::MSG_SENT)){
      if(msg.to_names.Length())
         s = msg.to_names.FromUtf8();
      else
         s.Copy(msg.to_emails);
      if(!is_sent_folder){
                           //also draw "To:"
         x += app.DrawString(app.GetText(TXT_TO), x, y, app.UI_FONT_BIG, 0, MulAlpha(col_text, 0x8000)) + app.fdb.cell_size_x/2;
      }
   }else
   if(msg.sender.display_name.Length())
      s = msg.sender.display_name.FromUtf8();
   else
      s.Copy(msg.sender.email);
   app.DrawString(s, x, y, app.UI_FONT_BIG, FF_BOLD, col_text, -int(sx - x-right_space));

   y += app.fdb.line_spacing;

   int max_subj_w = sx - x;
   if(msg.IsFlagged()){
      const C_image *img = app.msg_icons[app.MESSAGE_ICON_FLAG];
      int w = img->SizeX();
      img->Draw(sx-w, y);
      max_subj_w -= w + app.fdb.cell_size_x/2;
   }
                           //draw subject
   app.DrawString(msg.subject.FromUtf8(), x, y, app.UI_FONT_BIG, 0, col_text, -max_subj_w);
}

//----------------------------

void C_mode_read_mail::DrawInfoInSoftbar() const{
   if(acc && !app.HasMouse()){
                              //draw info about previous/next message
      dword col = app.GetColor(app.COL_TEXT_SOFTKEY);
      Cstr_w s;
      s.Format(L"%/%") <<(GetMailbox().selection+1) <<GetMailbox().num_vis_msgs;
      int ws = app.GetTextWidth(s, app.UI_FONT_BIG);

      const dword *stats = GetMailbox().GetContainer().GetMessagesStatistics();
      int x = rc.CenterX();
      int y = app.ScrnSY() - app.fdb.line_spacing;
      dword num_unread = stats[C_message_container::STAT_UNREAD];
      x -= ws/2;
      int r = x + ws;
      if(num_unread){
         Cstr_w nu;
         nu<<num_unread;
         int wu = app.GetTextWidth(nu, app.UI_FONT_BIG);

         const C_image *img = app.small_msg_icons[app.MESSAGE_ICON_NEW];
         const int ssx = img->SizeX();
         x -= (ssx+wu+app.fdb.cell_size_x)/2;
         r -= (ssx+wu-app.fdb.cell_size_x/2)/2;

         img->Draw(r, y);
         r += ssx + app.fdb.cell_size_x/2;
         r += app.DrawString(nu, r, y, app.UI_FONT_BIG, 0, col);
      }
      r += app.fdb.cell_size_x;
      dword left_arrow_col = col, right_arrow_col = left_arrow_col;
      if(!GetMailbox().selection)
         left_arrow_col &= 0x40ffffff;
      if(GetMailbox().selection >= int(GetMailbox().num_vis_msgs)-1)
         right_arrow_col &= 0x40ffffff;

      app.DrawString(s, x, y, app.UI_FONT_BIG, 0, col);
      int arrow_sz = app.fdb.cell_size_y/2;
      app.DrawArrowHorizontal(r, y+arrow_sz/2, arrow_sz, right_arrow_col, true);
      app.DrawArrowHorizontal(x-app.fdb.cell_size_x-arrow_sz, y+arrow_sz/2, arrow_sz, left_arrow_col, false);
   }
}

//----------------------------

void C_mode_read_mail::Draw() const{

   C_mode::Draw();

   const dword c0 = app.GetColor(app.COL_SHADOW), c1 = app.GetColor(app.COL_HIGHLIGHT);
   if(ctrl_copy){
      app.DrawOutline(ctrl_copy->GetRect(), c0, c1);
      //app.DrawShadow(ctrl_copy->GetRect());
      /*
      if(text_copy_select_pos!=-1){
         int y = app.ScrnSY() - app.fds.line_spacing;
         app.DrawString(app.GetText(TXT_SELECT), rc.CenterX(), y, app.UI_FONT_SMALL, FF_CENTER|FF_ITALIC, app.GetColor(app.COL_TEXT_SOFTKEY));
      }
      */
   }else{
      app.DrawOutline(rc, c0, c1);
      {
         S_rect rc1 = rc;
         if(msg.attachments.Size())
            rc1.SetUnion(rc1, attach_browser.rc);
         app.DrawShadow(rc1, true);
      }
      if(msg.IsDeleted()){
         app.FillRect(rc, 0xffffffff);
         app.DrawString(app.GetText(TXT_DELETED_MESSAGE), rc.CenterX(), rc.y, app.UI_FONT_BIG, FF_CENTER, 0xff808080);
      }else{
         DrawTextBody();
         DrawAttachments();
         if(link_show_count){
            const S_rect &lrc = rc_link_show;
            app.DrawOutline(lrc, 0xff000000);
            app.FillRect(lrc, 0xffffffff);
            app.DrawSimpleShadow(lrc, true);
            //DrawShadow(lrc, true);
            app.DrawNiceURL(link_show, lrc.x+app.fds.letter_size_x/2, lrc.y, app.UI_FONT_SMALL, 0xff000000, lrc.sx-app.fds.letter_size_x);
         }
      }
      DrawInfoInSoftbar();
   }
}

//----------------------------

void C_mail_client::OpenHtmlImages(const C_buffer<S_attachment> &attachments, const C_buffer<S_attachment> *attachments1, bool allow_download,
   S_text_display_info &tdi, C_multi_item_loader &ldr, bool allow_progress){

                              //progress indicator stuff
   int last_progress_time = GetTickTime();
   bool progress_inited = false;

   C_progress_indicator progress;
   progress.total = tdi.images.size();
   ldr.entries.clear();
   S_rect rc_dialog;

   const dword sx = ScrnSX();

   for(int i=0; i<tdi.images.size(); i++){
      S_image_record &ir = tdi.images[i];

                        //reuse image, if it was already opened
      int j;
      for(j=i; j--; ){
         const S_image_record &ir1 = tdi.images[j];
         if(ir1.src==ir.src){
            ir.img = ir1.img;
            ir.filename = ir1.filename;
            ir.sx = ir1.sx;
            ir.sy = ir1.sy;
            ir.invalid = ir1.invalid;
            break;
         }
      }
      if(j!=-1)
         continue;

      const char *cp = ir.src;
      if(text_utils::CheckStringBegin(cp, "cid:")){
         text_utils::SkipWS(cp);
         Cstr_c img_src = cp;
         Cstr_w img_src_w; img_src_w.Copy(img_src);
                              //find among attachments
         int num_a = attachments.Size(), num_a1 = attachments1 ? attachments1->Size() : 0;
         int ai;
         for(ai=0; ai<(num_a+num_a1); ai++){
            const S_attachment &att = ai<num_a ? attachments[ai] : (*attachments1)[ai-num_a];
            if(!text_utils::CompareStringsNoCase(att.content_id, img_src) || !text_utils::CompareStringsNoCase(att.suggested_filename, img_src_w)){
               C_image *img = OpenHtmlImage(att.filename.FromUtf8(), tdi, ir);
               img->Release();
               break;
            }
         }
         if(ai==-1)
            ir.invalid = true;
      }else
      if(text_utils::CheckStringBegin(cp, text_utils::HTTP_PREFIX) || text_utils::CheckStringBegin(cp, text_utils::HTTPS_PREFIX)){
                              //external source
                              // search in cache first
#if 1
         int ai;
         for(ai=attachments.Size(); ai--; ){
            const S_attachment &att = attachments[ai];
            if(att.content_id == ir.src){
               C_image *img = OpenHtmlImage(att.filename.FromUtf8(), tdi, ir);
               if(!ir.img)
                  ir.invalid = true;
               img->Release();
               break;
            }
         }
         if(ai==-1)
#endif
         {
            //if(config.flags&app.config_mail.CONF_DOWNLOAD_HTML_IMAGES)
            //if(allow_download)
            {
                              //not found in cache, setup loader
               if(!ldr.entries.size())
                  ldr.entries.reserve(tdi.images.size()-i);
               ldr.entries.push_back(ir.src);
            }
         }
      }else{
         ir.invalid = true;
      }
                              //draw progress indicator
      if(allow_progress && i<progress.total-1){
         if(!progress_inited){
            int t = GetTickTime();
            if(t-last_progress_time >= 250){
               int sz_x = fdb.letter_size_x * 20;
               int sy = fdb.line_spacing*3;

               DrawWashOut();
               rc_dialog = S_rect((sx-sz_x)/2, (ScrnSY()-sy)/2, sz_x, sy);
               DrawDialogBase(rc_dialog, true);
               DrawDialogTitle(rc_dialog, GetText(TXT_OPENING));

               progress.rc = S_rect(rc_dialog.x+fdb.letter_size_x, rc_dialog.y+fdb.line_spacing*7/4, rc_dialog.sx-fdb.letter_size_x*2, fdb.line_spacing);
               progress_inited = true;
               UpdateScreen();
            }
         }
         ++progress.pos;
         if(progress_inited){
            S_rect rc = progress.rc; rc.Expand();
            DrawDialogBase(rc_dialog, false, &rc);
            DrawProgress(progress);
            UpdateScreen();
         }
      }
   }
   if(ldr.entries.size() && allow_download){
                              //init image loading progress bars
      const int border = 12;
      ldr.prog_all.rc = S_rect(border, tdi.rc.Bottom()-6, sx-border*2, 3);
      ldr.prog_all.pos = 0;
      ldr.prog_all.total = ldr.entries.size();

      ldr.prog_curr.rc = S_rect(ldr.prog_all.rc.x, ldr.prog_all.rc.y-4, ldr.prog_all.rc.sx, 3);
      ldr.phase = ldr.NOT_CONNECTED;
      ldr.dst_filename = mail_data_path;
      ldr.dst_filename<<MAIL_PATH;

      ((C_application_http*)this)->Loader_ConnectTo(ldr, ldr.entries.front());
   }else
      ldr.phase = ldr.CLOSED;
}

//----------------------------

void C_mail_client::FinishImageDownload(C_text_viewer &viewer, C_message_container *cnt, C_buffer<S_attachment> &atts){

   C_multi_item_loader &ldr = viewer.img_loader;
                              //permanently store downloaded image
   dword ai = atts.Size();
   atts.Resize(ai+1);
   S_attachment &att = atts[ai];
   att.filename = ldr.data_saving.filename.ToUtf8();
   if(cnt){
                              //move file to container's folder
      int ai1 = ldr.data_saving.filename.FindReverse('\\');
      if(ai1!=-1){
         Cstr_w filename = cnt->GetMailPath(mail_data_path);
         filename = cnt->GetMailPath(mail_data_path);
         filename<<ldr.data_saving.filename.RightFromPos(ai1+1);
         att.filename = filename.ToUtf8();
         C_file::RenameFile(ldr.data_saving.filename, filename);
      }
   }
      
   const Cstr_c &e = ldr.entries.front();
   att.content_id = e;
   ldr.data_saving.Finish();

   S_text_display_info &tdi = viewer.text_info;
   bool resized = false;

   C_image *img = NULL;
   S_image_record *ir_1 = NULL;
                              //replace in all images that use it
   for(int i=tdi.images.size(); i--; ){
      S_image_record &ir = tdi.images[i];
      if(ir.src==e){
         if(!img){
            int sx = ir.sx, sy = ir.sy;
            img = OpenHtmlImage(att.filename.FromUtf8(), tdi, ir);
            ir_1 = &ir;
            if(sx!=ir.sx || sy!=ir.sy)
               resized = true;
         }else{
            if(ir.sx!=ir_1->sx || ir.sy!=ir_1->sy){
               //assert(!ir.size_set);
                              //adopt 1st image's size
               resized = true;
            }
            ir = *ir_1;
         }
      }
   }
   if(img)
      img->Release();

   if(resized){
                              //loaded image(s) was resized, need to recompute screen
      tdi.byte_offs = 0;
      tdi.top_pixel = 0;
      tdi.pixel_offs = 0;
      CountTextLinesAndHeight(tdi, config.viewer_font_index);

      C_scrollbar &sb = viewer.sb;
                              //scroll back to where we were (the func takes care of validity checking)
      ScrollText(tdi, sb.pos);

      sb.pos = tdi.top_pixel;
      sb.total_space = tdi.total_height;
      sb.SetVisibleFlag();
   }

                              //go to next image
   ldr.BeginNextEntry();
   ldr.prog_curr.pos = 0;
   ldr.prog_curr.total = 1;
}

//----------------------------

void C_mail_client::OpenHtmlImages1(C_client_viewer::C_mode_this &mod, C_client_viewer::C_mode_this *mod_reuse){

   C_buffer<S_attachment> &atts = mod.html_images;
   if(mod_reuse){
      atts = mod_reuse->html_images;
      mod_reuse->html_images.Clear();
      {
                              //make the parent mode's images to be reloaded
         C_vector<S_image_record> &imgs = mod_reuse->text_info.images;
         for(int i=imgs.size(); i--; )
            imgs[i].img = NULL;
         OpenHtmlImages(mod_reuse->html_images, NULL, true, mod_reuse->text_info, mod_reuse->img_loader);
      }
   }
   OpenHtmlImages(atts, NULL, true, mod.text_info, mod.img_loader);
   if(mod_reuse){
                              //clean cached images - keep only used ones
      for(int i=atts.Size(); i--; ){
         S_attachment &att = atts[i];
         const C_vector<S_image_record> &imgs = mod.text_info.images;
         int j;
         for(j=imgs.size(); j--; ){
            if(imgs[j].src==att.content_id)
               break;
         }
         if(j==-1){
                        //img unused, delete and clear
            C_file::DeleteFile(att.filename.FromUtf8());
            att = atts.Back();
            atts.Resize(atts.Size()-1);
         }
      }
   }
}

//----------------------------

void C_mail_client::SetModeDownload(const char *url, const Cstr_w *dst_filename, const wchar *custom_msg){

   C_mode_download &mod = *new(true) C_mode_download(*this, mode, custom_msg);

   mod.url = url;
   if(dst_filename)
      mod.ldr.dst_filename = *dst_filename;
   ((C_application_http*)this)->Loader_ConnectTo(mod.ldr, url);

   InitLayoutDownload(mod);
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::InitLayoutDownload(C_mode_download &mod){

   mod.draw_bgnd = true;
   mod.data_cnt_bgnd = NULL;

   const int sz_x = Min(fdb.letter_size_x*30, ScrnSX() - fdb.letter_size_x*4);
   const int title_sy = GetDialogTitleHeight();

   mod.rc = S_rect(0, 0, sz_x, title_sy + fdb.line_spacing*5/2);
   mod.rc.PlaceToCenter(ScrnSX(), ScrnSY());

   mod.ldr.prog_curr.rc = S_rect(mod.rc.x+fdb.letter_size_x, mod.rc.y+title_sy+fdb.line_spacing, mod.rc.sx-fdb.letter_size_x*2, fdb.line_spacing);
}

//----------------------------

void C_mail_client::DownloadProcessInput(C_mode_download &mod, S_user_input &ui, bool &redraw){

   ProcessMouseInSoftButtons(ui, redraw);

   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      CloseMode(mod);
      return;
   }
}

//----------------------------

void C_mail_client::DownloadSocketEvent(C_mode_download &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){

   C_application_http::C_http_data_loader &ldr = mod.ldr;
   C_application_http::E_HTTP_LOADER lr = ((C_application_http*)this)->TickDataLoader(ldr, ev, redraw);
   switch(lr){
   case C_application_http::LDR_CLOSED:
      CloseMode(mod);
      break;
   case C_application_http::LDR_PROGRESS:
      break;
   case C_application_http::LDR_FAILED:
   case C_application_http::LDR_NOT_FOUND:
      {
         mod.AddRef();
         CloseMode(mod, false);
         Cstr_w err;
         if(!mod.custom_msg.Length())
            //err.Copy(ldr.entries.back());
            err.Copy(ldr.curr_file);
         ShowErrorWindow(lr==C_application_http::LDR_NOT_FOUND ? TXT_ERR_NOT_FOUND : TXT_ERR_CONNECTION_FAILED, err);
         mod.Release();
      }
      break;
   case C_application_http::LDR_DONE:
      if(!ldr.dst_filename.Length()){
                              //invoke viewer on downloaded file
         ldr.socket = NULL;
                           //keep this mode in stack, because it takes care of file deleting when destroyed
                           //downloaded, decide what to do with the file
         ldr.phase = ldr.CLOSED;
         Cstr_w show_name;
         //show_name.Copy(ldr.entries.back());
         show_name.Copy(ldr.curr_file);

         Cstr_w fname = ldr.data_saving.filename;
         Cstr_c domain = ldr.domain;

                              //important - pause timer on this mode; when resumed, it will erase viewed file
         //mod.timer->Pause();
         ldr.data_saving.Finish();
         CloseMode(mod);
#ifdef MAIL
         //C_client_viewer::C_mode_this *mod_v_prnt = (*mod.saved_parent==C_mode::VIEWER) ? &(C_client_viewer::C_mode_this&)*mod.saved_parent : NULL;
         if(!C_client_viewer::OpenFileForViewing(this, fname, show_name, NULL, domain))
#endif
         {
            if(!OpenFileBySystem(fname))
               ShowErrorWindow(TXT_ERR_NO_VIEWER, show_name);
         }
      }else{
                              //file downloaded successfully
                              // leave it in dest location and launch viewer on it
         ldr.data_saving.Finish();
         config.last_browser_path = ldr.dst_filename;
         CloseMode(mod, false);
         C_client_file_mgr::SetModeFileBrowser(this);
      }
      break;
   case C_application_http::LDR_CORRUPTED:
      return;
   }
}

//----------------------------

void C_mail_client::DrawDownload(const C_mode_download &mod){

   if(mod.draw_bgnd){
      mod.draw_bgnd = false;
      mod.DrawParentMode(true);

      DrawDialogBase(mod.rc, true);
      DrawDialogTitle(mod.rc, GetText(TXT_PROGRESS_DOWNLOADING));

      dword col_text = GetColor(COL_TEXT_POPUP);

      //DrawDownload(mod);
      {
         int xx = mod.rc.x+fdb.letter_size_x/2, yy = mod.rc.y+GetTitleBarHeight();
         int max_w = mod.rc.sx-fdb.letter_size_x;
         if(mod.custom_msg.Length())
            DrawString(mod.custom_msg, xx, yy, UI_FONT_SMALL, 0, col_text, -max_w);
         else
            DrawNiceURL(mod.url, xx, yy, UI_FONT_SMALL, col_text, max_w);
      }
      //DrawFloatDataCounters(mod.data_cnt_bgnd);
      DrawSoftButtonsBar(mod, TXT_NULL, TXT_CANCEL);
   }

   { S_rect rc = mod.ldr.prog_curr.rc; rc.Expand(); DrawDialogBase(mod.rc, false, &rc); }
   DrawProgress(mod.ldr.prog_curr);
   //DrawFloatDataCounters(mod.data_cnt_bgnd);
}

//----------------------------

void C_mail_client::ShowMessageDetails(const S_message &msg, const char *full_headers){

   bool is_received = !(msg.flags&(msg.MSG_DRAFT | msg.MSG_TO_SEND));

   Cstr_w tmp;
   C_vector<char> buf;
   S_text_style ts;
   ts.font_index = UI_FONT_BIG;
   const dword col_mark = 0xff0000c0;
                     //from
   if(is_received){
      ts.AddWideStringToBuf(buf, GetText(TXT_FROM), FF_BOLD); ts.AddWideStringToBuf(buf, L" ");
      ts.AddWideStringToBuf(buf, msg.sender.display_name.FromUtf8(), -1, col_mark); ts.AddWideStringToBuf(buf, L" ");
      Cstr_w t;
      t.Copy(msg.sender.email);
      tmp.Format(L"<%>\n") <<t;
      ts.AddWideStringToBuf(buf, tmp, 0, 0xff404040);

      if(msg.reply_to_email.Length()){
         ts.AddWideStringToBuf(buf, L"Reply-to: ", FF_BOLD);
         tmp.Copy(msg.reply_to_email);
         ts.AddWideStringToBuf(buf, tmp, 0, 0xff404040);
         ts.AddWideStringToBuf(buf, L"\n");
      }
   }
                     //subject
   ts.AddWideStringToBuf(buf, GetText(TXT_SUBJECT), FF_BOLD); ts.AddWideStringToBuf(buf, L" ");
   ts.AddWideStringToBuf(buf, msg.subject.FromUtf8(), -1, col_mark); ts.AddWideStringToBuf(buf, L"\n");

                     //date
   S_date_time_x dt;
   dt.SetFromSeconds(msg.date);
   GetDateString(dt, tmp);
   ts.AddWideStringToBuf(buf, GetText(TXT_DATE), FF_BOLD); ts.AddWideStringToBuf(buf, L" ");
   ts.AddWideStringToBuf(buf, tmp, -1, col_mark); ts.AddWideStringToBuf(buf, L"\n");
                     //time
   tmp = text_utils::GetTimeString(dt, true);
   ts.AddWideStringToBuf(buf, GetText(TXT_TIME), FF_BOLD); ts.AddWideStringToBuf(buf, L" ");
   ts.AddWideStringToBuf(buf, tmp, -1, col_mark); ts.AddWideStringToBuf(buf, L"\n");

                     //to
   bool has_to = false;
   if(msg.to_names.Length()){
      ts.AddWideStringToBuf(buf, GetText(TXT_TO), FF_BOLD); ts.AddWideStringToBuf(buf, L" ");
      ts.AddWideStringToBuf(buf, msg.to_names.FromUtf8(), -1, col_mark);
      has_to = true;
   }else
   if(msg.to_emails.Length()){
      ts.AddWideStringToBuf(buf, GetText(TXT_TO), FF_BOLD); ts.AddWideStringToBuf(buf, L" ");
      Cstr_w tmp1; tmp1.Copy(msg.to_emails.FromUtf8());
      ts.AddWideStringToBuf(buf, tmp1, -1, col_mark);
      has_to = true;
   }
   if(msg.cc_emails.Length()){
      if(has_to)
         ts.AddWideStringToBuf(buf, L"\n");
      ts.AddWideStringToBuf(buf, L"Cc: ", FF_BOLD);
      Cstr_w tmp1; tmp1.Copy(msg.cc_emails.FromUtf8());
      ts.AddWideStringToBuf(buf, tmp1, -1, col_mark);
      has_to = true;
   }
   if(msg.bcc_emails.Length()){
      if(has_to)
         ts.AddWideStringToBuf(buf, L"\n");
      ts.AddWideStringToBuf(buf, L"Bcc: ", FF_BOLD);
      Cstr_w tmp1; tmp1.Copy(msg.bcc_emails.FromUtf8());
      ts.AddWideStringToBuf(buf, tmp1, -1, col_mark);
   }
   bool show_hdrs = false;
   if(full_headers){
      ts.AddWideStringToBuf(buf, L"\n");
      ts.AddWideStringToBuf(buf, L"\n");
      ts.AddWideStringToBuf(buf, GetText(TXT_HEADERS), FF_BOLD);
      ts.AddWideStringToBuf(buf, L":\n");
      int font_index = UI_FONT_SMALL;
      ts.WriteCode(CC_TEXT_FONT_SIZE, &ts.font_index, &font_index, 1, buf);
      while(*full_headers){
         int eol;
         for(eol=0; full_headers[eol]; ){
            if(full_headers[eol++]=='\n')
               break;
         }
         if(*full_headers==' ' || *full_headers=='\t'){
            const char *beg = full_headers;
            text_utils::SkipWS(full_headers);
            --full_headers;
            *((char*)full_headers) = char(0xa0);
            eol -= full_headers-beg;
         }else{
            int ii;
            for(ii=0; ii<eol; ){
               if(full_headers[ii++]==':')
                  break;
            }
            if(ii!=eol){
                                 //keyword in different color
               ts.WriteCode(CC_TEXT_COLOR, &ts.text_color, &col_mark, 3, buf);
               buf.insert(buf.end(), full_headers, full_headers+ii);
               ts.WriteCode(CC_TEXT_COLOR, &col_mark, &ts.text_color, 3, buf);
               full_headers += ii;
               eol -= ii;
            }
         }
         buf.insert(buf.end(), full_headers, full_headers+eol);
         full_headers += eol;
      }
      //ts.WriteCode(CC_TEXT_FONT_SIZE, &font_index, &ts.font_index, 1, buf);
   }else
   if(msg.flags&msg.MSG_SERVER_SYNC){
      show_hdrs = true;
   }
   class C_question: public C_question_callback{
      C_mail_client &app;
      virtual void QuestionConfirm(){
         app.MsgDetailsHeaders();
      }
   public:
      C_question(C_mail_client &_a): app(_a){}
   };
   CreateFormattedMessage(*this, GetText(TXT_MESSAGE_DETAILS), buf,
      show_hdrs ? new(true) C_question(*this) : NULL, true,
      show_hdrs ? TXT_HEADERS : TXT_NULL, TXT_BACK);
   //mode->ProcessInput = (t_ProcessInput)&C_mail_client::MsgDetailsProcessInput;
}

//----------------------------

void C_mail_client::SaveMessageToFile(const S_message &msg, const wchar *fn){

}

//----------------------------
#ifdef __SYMBIAN32__
#ifdef __SYMBIAN_3RD__
#include <Etel3rdParty.h>

void C_mail_client::SymbianMakePhoneCall(const char *num){

   class C_phonecall: public C_unknown, public CActive{
      CTelephony *telephony;
      CTelephony::TCallId call_id;
      CTelephony::TCallParamsV1 call_params;
      CTelephony::TCallParamsV1Pckg pckg;
      C_smart_ptr<C_unknown> *this_ptr;

      void DoCancel(){
         telephony->CancelAsync(CTelephony::EDialNewCallCancel);
      }
      void RunL(){
         //Info("R", iStatus.Int());
         *this_ptr = NULL;
      }
      C_phonecall(C_smart_ptr<C_unknown> *p):
         CActive(EPriorityNormal),
         pckg(call_params),
         telephony(NULL),
         this_ptr(p)
      {
         CActiveScheduler::Add(this);
      }

      void ConstructL(const char *num){
         telephony = CTelephony::NewL();
         CTelephony::TTelNumber buf;
         buf.Copy(TPtrC8((byte*)num, StrLen(num)));
         call_params.iIdRestrict = CTelephony::EIdRestrictDefault;
         telephony->DialNewCall(iStatus, pckg, buf, call_id);
         SetActive();
      }
      ~C_phonecall(){
         Cancel();
         delete telephony;
      }
   public:
      static bool Create(const char *num, C_smart_ptr<C_unknown> *p){
         if(StrLen(num)>CTelephony::KMaxTelNumberSize)
            return false;
         C_phonecall *_this = new(true) C_phonecall(p);
         _this->ConstructL(num);
         *p = _this;
         _this->Release();
         return true;
      }
   };
   C_phonecall::Create(num, phone_call.Ptr());
}

//----------------------------
#else
#include <Etel.h>

void C_mail_client::SymbianMakePhoneCall(const char *num){
   class C_phonecall: public C_unknown, public CActive{
      RTelServer svr;
      RCall call;
      RTelServer::TPhoneInfo phone_info;
      RPhone phone;
      RPhone::TLineInfo	iLineInfo;
      RLine	line;

      C_smart_ptr<C_unknown> *this_ptr;
      void DoCancel(){
      }
      void RunL(){
         //Info("R", iStatus.Int());
         *this_ptr = NULL;
      }
      C_phonecall(C_smart_ptr<C_unknown> *p):
         CActive(EPriorityNormal),
         this_ptr(p)
      {
         CActiveScheduler::Add(this);
      }
      bool ConstructL(const char *num){
         svr.Connect();
         svr.LoadPhoneModule(_L("phonetsy.tsy"));
         TInt num_ph = 0;
         if(svr.EnumeratePhones(num_ph) || num_ph!=1)
            return false;
         if(svr.GetPhoneInfo(0, phone_info))
            return false;
         if(phone.Open(svr, phone_info.iName))
            return false;
         TInt num_lines = 0;
         if(phone.EnumerateLines(num_lines))
            return false;
         bool foundLine = false;
         for(TInt i=0;i<num_lines;i++){
            phone.GetLineInfo(i, iLineInfo);
            if(iLineInfo.iLineCapsFlags&RLine::KCapsVoice){
	            foundLine = true;
	            break;
            }
         }
         if(!foundLine)
            return false;
         if(line.Open(phone, iLineInfo.iName))
            return false;
         if(call.OpenNewCall(line))
            return false;

         TBuf<50> buf;
         buf.Copy(TPtrC8((byte*)num, StrLen(num)));
         call.Dial(iStatus, buf);
         SetActive();
         return true;
      }
      ~C_phonecall(){
         Cancel();
         call.Close();
         line.Close();
         phone.Close();
         svr.UnloadPhoneModule(_L("phonetsy.tsy"));
         svr.Close();
      }
   public:
      static bool Create(const char *num, C_smart_ptr<C_unknown> *p){
         if(StrLen(num)>50)
            return false;
         C_phonecall *_this = new(true) C_phonecall(p);
         bool ret = _this->ConstructL(num);
         if(ret)
            *p = _this;
         _this->Release();
         return ret;
      }
   };
   C_phonecall::Create(num, phone_call.Ptr());
}

#endif
#endif
//----------------------------
