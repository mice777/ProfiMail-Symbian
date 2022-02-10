#include "..\Main.h"
#include "Main_Email.h"
#include "Base64.h"

//----------------------------

#define MAX_NAME_LENGTH 100

extern const char send_key_name[];

//----------------------------

C_mail_client::C_mode_folders_list::~C_mode_folders_list(){

   if(//acc.IsImap() &&
      acc.background_processor.state)
      return;
   acc.socket = NULL;
}

//----------------------------

void C_folders_iterator::PrepareNext(){

restart:
   const t_folders *hr = &folders;
   for(int i=0; i<levels.size()-1; i++)
      hr = &(*hr)[levels[i]]->subfolders;

   if(levels.back()>=0 && (*hr)[levels.back()]->subfolders.Size()){
                              //check if we're stepping into subfolder
      const C_message_container *cnt = (*hr)[levels.back()];
      if((show_hidden || !cnt->IsHidden()) &&
         (show_collapsed || cnt->IsExpanded())){
                              //step into subfolders
         levels.push_back(0);
         if(!IsEnd() && !show_hidden){
            hr = &cnt->subfolders;
            const C_message_container *cnt1 = (*hr)[levels.back()];
            if(cnt1->IsHidden())
               PrepareNext();
         }
         return;
      }
   }
                              //go to next visible folder
   while(true){
                        //move to next folder
      if(++levels.back() < (int)hr->Size())
         break;
                        //go back one level
      levels.pop_back();
      if(!levels.size())
         break;
      hr = &folders;
      for(int i=0; i<levels.size()-1; i++)
         hr = &(*hr)[levels[i]]->subfolders;
   }
   if(!IsEnd() && !show_hidden){
      const C_message_container *cnt = (*hr)[levels.back()];
      if(cnt->IsHidden())
         //PrepareNext();
         goto restart;  //use goto, calling func uses too much stack on possibly big amount of items (resulting in stack overflow)
   }
}

//----------------------------

C_message_container *C_folders_iterator::PeekNext(){
   assert(!IsEnd());
   t_folders *hr = &folders;
   for(int i=0; i<levels.size()-1; i++)
      hr = &(*hr)[levels[i]]->subfolders;
   return (*hr)[levels.back()];
}

//----------------------------

C_message_container *C_folders_iterator::Next(){
   C_message_container *ret = PeekNext();
   PrepareNext();
   return ret;
}

//----------------------------

static void FlushImapBase64Utf7Buf(C_vector<byte> &utf16, C_vector<char> &buf){

   if(utf16.size()){
      C_vector<char> tmp;
      EncodeBase64(utf16.begin(), utf16.size(), tmp);
                              //remove last '='
      while(tmp.size() && tmp.back()=='=')
         tmp.pop_back();

      buf.push_back('&');
                              //convert '/' to ',' and store to dest buf
      for(int i=0; i<tmp.size(); i++){
         char c = tmp[i];
         if(c=='/')
            c = ',';
         buf.push_back(c);
      }
      buf.push_back('-');
      utf16.clear();
   }
}

//----------------------------

Cstr_c EncodeImapFolderName(const wchar *wp){

   C_vector<byte> utf16;      //temp buf with utf16 chars
   C_vector<char> buf;        //final buf
   buf.reserve(100);

   bool need_quotes = false;
   while(*wp){
      wchar c = *wp++;
      if(c=='&'){
         FlushImapBase64Utf7Buf(utf16, buf);
         buf.push_back('&');
         buf.push_back('-');
      }else
      if(!(c>=0x20 && c<=0x7e)){
         utf16.push_back(byte(c>>8));
         utf16.push_back(byte(c&0xff));
      }else{
         FlushImapBase64Utf7Buf(utf16, buf);
         buf.push_back(char(c));
         switch(c){
         case '\\':
            buf.push_back('\\');
                              //flow...
         case ' ':
            need_quotes = true;
            break;
         default:
            if(!text_utils::IsAlNum(c))
               need_quotes = true;
         }
      }
   }
   FlushImapBase64Utf7Buf(utf16, buf);
   if(need_quotes){
      buf.insert(buf.begin(), '\"');
      buf.push_back('\"');
   }
   buf.push_back(0);
   return buf.begin();
}

//----------------------------

Cstr_w DecodeImapFolderName(const char *cp1){

   Cstr_c tmp;
   text_utils::ReadWord(cp1, tmp, " ");
   char *cp = &tmp.At(0);

   C_vector<wchar> buf;
   buf.reserve(100);
   while(*cp){
      dword c = (byte)*cp++;
      if(!((c>=0x20 && c<=0x25) || (c>=0x27 && c<=0x7f))){
         switch(c){
         case '&':
            {
               if(*cp=='-'){
                  ++cp;
                  break;
               }
               char *beg = cp;
                              //modified Base64 encoded
                              // find end, and convert to standard Base64
               while(*cp){
                  c = *cp;
                  if(c=='-')
                     break;
                  if(c==',')
                     *cp = '/';
                  ++cp;
               }
               if(!*cp){
                              //coding error, ignore
                  assert(0);
                  continue;
               }
               C_vector<char> src;
               src.insert(src.end(), beg, cp);
               while(src.size()&3)
                  src.push_back('=');
               ++cp;             //skip '-'
               C_vector<byte> vtmp;
               if(DecodeBase64(src.begin(), src.size(), vtmp)){
                                 //decoded string is now utf16 in big-endian format
                                 // append to our string
                  int n = vtmp.size();
                  assert(!(n&1));
                  for(int i=0; i<n/2; i++){
                     wchar w = (vtmp[i*2]<<8) | vtmp[i*2+1];
                     buf.push_back(w);
                  }
               }
            }
            continue;
         }
      }else
      if(c=='\\'){
         if(*cp=='\\')
            ++cp;
      }
      buf.push_back(char(c));
   }
   buf.push_back(0);
   return buf.begin();
}

//----------------------------

Cstr_c C_mail_client::S_account::GetImapEncodedName(const C_message_container &cnt) const{
   Cstr_w name1;
   name1 = GetFullFolderName(cnt);
   return EncodeImapFolderName(name1);
}

//----------------------------

C_mail_client::C_mode_folders_list &C_mail_client::SetModeFoldersList(S_account &acc, bool allow_folders_refresh){

   C_mode_folders_list &mod = *new(true) C_mode_folders_list(*this, acc);

   InitLayoutFoldersList(mod);

   ActivateMode(mod);
                              //refresh folders if needed
   if(mod.acc.IsImap() && (mod.acc.flags&S_account::ACC_NEED_FOLDER_REFRESH) && allow_folders_refresh){
      //mod.acc.flags &= ~S_account::ACC_NEED_FOLDER_REFRESH;
      //SaveAccounts();
      SetModeConnection(mod.acc, NULL, C_mode_connection::ACT_REFRESH_IMAP_FOLDERS);
   }
   return mod;
}

//----------------------------

void C_mail_client::InitLayoutFoldersList(C_mode_folders_list &mod){

   mod.entry_height = fdb.line_spacing*2;
   mod.EnsureVisible();
   const int border = 2;
   const int top = GetTitleBarHeight();
   mod.rc = S_rect(border, top, ScrnSX()-border*2, ScrnSY()-top-GetSoftButtonBarHeight()-border);
                           //compute # of visible lines, and resize rectangle to whole lines
   //int num_lines = mod.rc.sy / mod.entry_height;
   mod.sb.visible_space = mod.rc.sy;//num_lines;
   //if(mod.IsPixelMode())
      //mod.sb.visible_space *= mod.entry_height;
   //mod.rc.y += (mod.rc.sy - num_lines*mod.entry_height)/2;
   //mod.rc.sy = num_lines * mod.entry_height;

   const int width = GetScrollbarWidth();
   mod.sb.rc = S_rect(mod.rc.Right()-width-1, mod.rc.y+1, width, mod.rc.sy-2);

   mod.max_name_width = mod.sb.rc.x - 2 - (mod.rc.x + fdb.letter_size_x);

   FoldersList_InitView(mod);

   if(mod.te_rename){
      S_rect trc = mod.te_rename->GetRect();
      trc.y = 
         mod.IsPixelMode() ?
         mod.rc.y + fdb.line_spacing/2 + mod.selection*mod.entry_height - mod.sb.pos :
         mod.rc.y + fdb.line_spacing/2 + mod.entry_height*(mod.selection-mod.top_line);
      mod.te_rename->SetRect(trc);
   }
}

//----------------------------

void C_mail_client::FoldersList_InitView(C_mode_folders_list &mod, const C_message_container *sel_fld){

   mod.sb.pos = 0;

   C_folders_iterator it(mod.acc._folders, mod.show_hidden, false);
   for(mod.num_entries=0; !it.IsEnd(); ++mod.num_entries){
      const C_message_container *fld = it.Next();
      if(sel_fld && sel_fld==fld){
                              //try to keep desired folder selected
         mod.selection = mod.num_entries;         
      }
   }
   mod.selection = Min(Max(mod.selection, 0), mod.num_entries-1);
   mod.sb.total_space = mod.num_entries;
   if(mod.IsPixelMode())
      mod.sb.total_space *= mod.entry_height;
   mod.sb.SetVisibleFlag();

   mod.EnsureVisible();
}

//----------------------------

struct S_sort_fld_help{
   C_smart_ptr<C_message_container> *folders;
   int *folder_index;
};

//----------------------------

static int CompareFolders(const void *f1, const void *f2, void *context){

   const C_smart_ptr<C_message_container> &fld1 = *(C_smart_ptr<C_message_container>*)f1,
      &fld2 = *(C_smart_ptr<C_message_container>*)f2;

                              //hidden folders are always at end
   if((fld1->flags&C_message_container::FLG_HIDDEN) != (fld2->flags&C_message_container::FLG_HIDDEN))
      return (fld1->flags&C_message_container::FLG_HIDDEN) ? 1 : -1;

                              //INBOX is always at top
   if(fld1->IsInbox())
      return -1;
   if(fld2->IsInbox())
      return 1;
   //return text_utils::CompareStringsNoCase(fld1->display_name.FromUtf8(), fld2->display_name.FromUtf8());
   return text_utils::CompareStringsNoCase(fld1->folder_name, fld2->folder_name);
}

//----------------------------

static void SwapFolders(void *f1, void *f2, dword w, void *context){

   if(f1!=f2){
      C_smart_ptr<C_message_container> *fld1 = (C_smart_ptr<C_message_container>*)f1,
         *fld2 = (C_smart_ptr<C_message_container>*)f2;
      Swap(*fld1, *fld2);
                              //adjust account index
      S_sort_fld_help &ah = *(S_sort_fld_help*)context;
      if(ah.folder_index){
         int i1 = fld1 - ah.folders;
         int i2 = fld2 - ah.folders;
         if(i1==*ah.folder_index) *ah.folder_index = i2;
         else
         if(i2==*ah.folder_index) *ah.folder_index = i1;
      }
   }
}

//----------------------------

void C_mail_client::SortFoldersHierarchy(t_folders &flds){

   S_sort_fld_help fh = { flds.Begin(), NULL };
   QuickSort(fh.folders, flds.Size(), sizeof(C_smart_ptr<C_message_container>), &CompareFolders, &fh, &SwapFolders);
   for(int i=flds.Size(); i--; ){
      C_message_container *fld = flds[i];
      if(fld->subfolders.Size())
         SortFoldersHierarchy(fld->subfolders);
   }
}

//----------------------------

void C_mail_client::SortFolders(S_account &acc, C_mode_folders_list *mod, const C_message_container *sel_fld){

   if(mod && !sel_fld){
      C_folders_iterator it(acc._folders, mod->show_hidden, false);
      int sel = mod->selection;
      while(!it.IsEnd()){
         const C_message_container *f = it.Next();
         if(!sel--){
            sel_fld = f;
            break;
         }
      }
   }
   SortFoldersHierarchy(acc._folders);
   if(mod && sel_fld){
      mod->selection = 0;
      C_folders_iterator it(acc._folders, mod->show_hidden, false);
      while(!it.IsEnd()){
         if(sel_fld==it.Next())
            break;
         ++mod->selection;
      }
   }
}

//----------------------------

C_message_container *C_mail_client::C_mode_folders_list::GetSelectedFolder(){
   C_folders_iterator it(acc._folders, show_hidden, false);
   for(int i=0; i<selection && !it.IsEnd(); i++){
      it.Next();
   }
   if(it.IsEnd())
      return NULL;
   return it.Next();
}

//----------------------------

void C_mail_client::FoldersList_DeleteFolder(C_mode_folders_list &mod){

   if(mod.acc.IsImap())
      SetModeConnection(mod.acc, mod.GetSelectedFolder(), C_mode_connection::ACT_DELETE_IMAP_FOLDER);
   else{
      FoldersList_FinishDelete(mod);
      RedrawScreen();
   }
}

//----------------------------

void C_mail_client::FoldersList_StartRename(C_mode_folders_list &mod){

   const C_message_container &fld = *mod.GetSelectedFolder();
   if(fld.IsInbox() || fld.IsTemp())
      return;

   mod.EnsureVisible();

   mod.creating_new = false;
   mod.te_rename = CreateTextEditor(TXTED_ACTION_ENTER|TXTED_ALLOW_PREDICTIVE, UI_FONT_BIG, FF_BOLD, NULL, MAX_NAME_LENGTH);
   C_text_editor &te = *mod.te_rename;
   te.Release();

   S_rect trc = S_rect(mod.rc.x+fdb.letter_size_x,
      mod.IsPixelMode() ?
      mod.rc.y + fdb.line_spacing/2 + mod.selection*mod.entry_height - mod.sb.pos :
      mod.rc.y + fdb.line_spacing/2 + mod.entry_height*(mod.selection-mod.top_line),
      mod.max_name_width, fdb.cell_size_y+1);
   for(const C_message_container *p=fld.parent_folder; p && trc.sx>fdb.cell_size_x*10; p=p->parent_folder){
      trc.x += fdb.cell_size_x;
      trc.sx -= fdb.cell_size_x;
   }
   if(fld.subfolders.Size()){
      trc.x += fdb.cell_size_x;
      trc.sx -= fdb.cell_size_x;
   }
   te.SetRect(trc);

   te.SetInitText(fld.folder_name.FromUtf8());
   te.SetCursorPos(te.GetCursorPos());
   MakeSureCursorIsVisible(te);
   RedrawScreen();
}

//----------------------------

void C_mail_client::FoldersList_FinishRename(C_mode_folders_list &mod){

                              //accept renamed name
   C_message_container *fld = mod.GetSelectedFolder();

   if(mod.te_rename){
                              //renamed by typing new name
      fld->folder_name = Cstr_w(mod.te_rename->GetText()).ToUtf8();
      mod.te_rename = NULL;
   }else{
                              //moved to different parent folder
      fld->AddRef();
                              //first remove from previous parent
      {
         t_folders &hr = fld->parent_folder ? fld->parent_folder->subfolders : mod.acc._folders;
         int i;
         for(i=hr.Size(); i--; ){
            if((C_message_container*)hr[i]==fld){
               hr[i] = hr.Back();
               hr.Resize(hr.Size()-1);
               break;
            }
         }
         assert(i!=-1);
      }
                              //now assign to new parent
      {
         fld->parent_folder = mod.folder_move_target;
         t_folders &hr = mod.folder_move_target ? mod.folder_move_target->subfolders : mod.acc._folders;
         hr.Resize(hr.Size()+1);
         hr.Back() = fld;

         fld->flags &= ~C_message_container::FLG_EXPANDED;
                              //make all parents epxanded, so that we're seen
         for(C_message_container *p = fld->parent_folder; p; p=p->parent_folder)
            p->flags |= C_message_container::FLG_EXPANDED;
      }

      fld->Release();
      mod.folder_move_target = NULL;
   }

   SortFolders(mod.acc, &mod, fld);
   SaveAccounts();

   FoldersList_InitView(mod);

   mod.EnsureVisible();
}

//----------------------------

void C_mail_client::FoldersList_FinishDelete(C_mode_folders_list &mod){

   mod.acc.DeleteFolder(mail_data_path, mod.GetSelectedFolder());
   SaveAccounts();
   FoldersList_InitView(mod);
}

//----------------------------

void C_mail_client::FoldersList_StartCreate(C_mode_folders_list &mod, bool ask_subfolder, bool in_root){

   if(ask_subfolder && mod.acc.IsImap()){
                              //check if selected folder can have children
      C_message_container *prnt = mod.GetSelectedFolder();
      if(!(prnt->flags&prnt->FLG_NOINFERIORS) && mod.acc.imap_folder_delimiter){
         Cstr_w s;
         s.Format(GetText(TXT_Q_CREATE_FOLDER_AS_SUBFOLDER)) <<mod.acc.GetFullFolderName(*prnt);

         class C_question: public C_question_callback{
            C_mail_client &app;
            C_mode_folders_list &mod;
            virtual void QuestionConfirm(){
               app.FoldersList_StartCreate(mod, false, false);
            }
            virtual void QuestionReject(){
               app.FoldersList_StartCreate(mod, false, true);
            }
         public:
            C_question(C_mail_client &a, C_mode_folders_list &m): app(a), mod(m){}
         };

         CreateQuestion(*this, TXT_CREATE_FOLDER, s, new(true) C_question(*this, mod), true);
         return;
      }
   }
   if(!mod.show_hidden){
      mod.show_hidden = true;
      FoldersList_InitView(mod);
   }
   C_message_container *fld = new(true) C_message_container;
   fld->msg_folder_id = GetMsgContainerFolderId();
   fld->is_imap = mod.acc.IsImap();
   if(!in_root){
      C_message_container *prnt = mod.GetSelectedFolder();
                              //expand parent
      if(!prnt->IsExpanded()){
         prnt->flags |= prnt->FLG_EXPANDED;
      }
                              //collapse all its children
      for(int i=prnt->subfolders.Size(); i--; ){
         prnt->subfolders[i]->flags &= ~C_message_container::FLG_EXPANDED;
      }
      int n = prnt->subfolders.Size();
      mod.selection += n+1;
      prnt->subfolders.Resize(n+1);

      prnt->subfolders[n] = fld;
      fld->parent_folder = prnt;
   }else{
      int n = mod.acc._folders.Size();
      mod.acc._folders.Resize(n+1);
      mod.acc._folders[n] = fld;
      mod.selection = mod.GetNumEntries();
   }
   FoldersList_InitView(mod);
   FoldersList_StartRename(mod);
   mod.creating_new = true;
   fld->Release();
}

//----------------------------

void C_mail_client::ImapFolders_FinishCreate(C_mode_folders_list &mod){

   FoldersList_FinishRename(mod);
}

//----------------------------

void C_mail_client::OpenImapFolder(C_mode_folders_list &mod, bool &redraw){

   C_message_container *fld = mod.GetSelectedFolder();
   if(!(fld->flags&fld->FLG_NOSELECT))
      SetModeMailbox(mod.acc, fld);
   else if(fld->subfolders.Size()){
      S_user_input ui;
      ui.Clear();
      ui.key = fld->IsExpanded() ? K_CURSORLEFT : K_CURSORRIGHT;
      FoldersListProcessInput(mod,  ui, redraw);
   }
}

//----------------------------

C_message_container *C_mail_client::FindImapFolderForUpdate(S_account &acc, const C_message_container *curr_fld, bool allow_hidden){

   C_folders_iterator it(acc._folders, allow_hidden, true);
   if(curr_fld){
      while(!it.IsEnd()){
         if(curr_fld==it.Next())
            break;
      }
   }
   while(!it.IsEnd()){
      C_message_container *fld = it.Next();//acc.folders[i];
      if(fld->IsTemp() || (fld->flags&fld->FLG_NOSELECT))
         continue;
      if(acc.flags&acc.ACC_IMAP_UPDATE_INBOX_ONLY){
         if(!fld->IsInbox())
            continue;
      }
      LoadMessages(*fld);
      return fld;
   }
   return NULL;
}

//----------------------------

void C_mail_client::ImapFolders_UpdateFolders(C_mode_folders_list &mod, bool auto_update){

   S_connection_params con_params;
   con_params.auto_update = auto_update;
   if(mod.acc.IsImap()){
      C_message_container *fld = FindImapFolderForUpdate(mod.acc, NULL, mod.show_hidden);
      if(fld){
         con_params.imap_update_hidden = mod.show_hidden;
         SetModeConnection(mod.acc, fld, C_mode_connection::ACT_UPDATE_IMAP_FOLDERS, &con_params);
      }
   }else{
      SetModeConnection(mod.acc, FindInbox(mod.acc), C_mode_connection::ACT_UPDATE_MAILBOX, &con_params);
   }
}

//----------------------------

C_message_container *C_mail_client::FindFolder(S_account &acc, const wchar *name) const{

   Cstr_c n_utf8 = Cstr_w(name).ToUtf8();
   t_folders *hr = &acc._folders;
   while(true){
      Cstr_c fn = n_utf8;
      if(acc.IsImap() && acc.imap_folder_delimiter){
         int di = n_utf8.Find(acc.imap_folder_delimiter);
         if(di!=-1){
            fn = n_utf8.Left(di);
            n_utf8 = n_utf8.RightFromPos(di+1);
         }else
            n_utf8.Clear();
      }else
         n_utf8.Clear();
      C_message_container *f = NULL;
      for(int i=hr->Size(); i--; ){
         C_message_container *ff = (*hr)[i];
         if(ff->folder_name==fn){
            f = ff;
            break;
         }
      }
      if(!f)
         return NULL;
      if(!n_utf8.Length())
         return f;
      hr = &f->subfolders;
   }
   return NULL;
}

//----------------------------

C_message_container *C_mail_client::FindInbox(S_account &acc) const{

   Cstr_c n_utf8 = Cstr_w(acc.inbox_folder_name).ToUtf8();
                              //note: inbox should be sorted as first
   for(dword i=0; i<acc._folders.Size(); i++){
      C_message_container *fld = acc._folders[i];
      if(!text_utils::CompareStringsNoCase(fld->folder_name, n_utf8))
         return fld;
   }
   return NULL;
}

//----------------------------

C_message_container *C_mail_client::FindOrCreateImapFolder(S_account &acc, const wchar *name, bool &created) const{

   created = false;
   C_message_container *fld = FindFolder(acc, name);
   if(!fld){
      created = true;
      Cstr_c n_utf8 = Cstr_w(name).ToUtf8();
      t_folders *hr = &acc._folders;
      C_message_container *prnt = NULL;
      while(true){
         Cstr_c fn = n_utf8;
         if(acc.IsImap() && acc.imap_folder_delimiter){
            int di = n_utf8.Find(acc.imap_folder_delimiter);
            if(di!=-1){
               fn = n_utf8.Left(di);
               n_utf8 = n_utf8.RightFromPos(di+1);
            }else
               n_utf8.Clear();
         }else
            n_utf8.Clear();
         C_message_container *f = NULL;
         for(int i=hr->Size(); i--; ){
            C_message_container *ff = (*hr)[i];
            if(ff->folder_name==fn){
               f = ff;
               break;
            }
         }
         if(!f){
            int indx = hr->Size();
            hr->Resize(indx+1);
            f = new(true) C_message_container;
            (*hr)[indx] = f;
            f->Release();
            f->is_imap = acc.IsImap();
            f->folder_name = fn;
            f->parent_folder = prnt;
            f->msg_folder_id = GetMsgContainerFolderId();
         }
         if(!n_utf8.Length()){
            f->flags = f->FLG_TEMP;
            fld = f;
            break;
         }
         prnt = f;
         hr = &f->subfolders;
      }
   }else
      LoadMessages(*fld);
   return fld;
}

//----------------------------

bool C_mail_client::CleanEmptyTempImapFolders(S_account &acc, int *selection){

   bool deleted = false;
   t_folders &flds = acc._folders;
   int sz = flds.Size();
   for(int i=sz; i--; ){
      C_message_container &fld = *flds[i];
      if(fld.IsTemp() && fld.IsEmpty(mail_data_path)){
         fld.DeleteContainerFiles(mail_data_path);
         if(selection && *selection>=i)
            --selection;
         for(int j=i+1; j<sz; j++)
            flds[j-1] = flds[j];
         --sz;
         deleted = true;
      }
   }
   if(deleted){
      flds.Resize(sz);
      SaveAccounts();
   }
   return deleted;
}

//----------------------------

void C_mail_client::FoldersListClose(C_mode_folders_list &mod){

   CleanEmptyTempImapFolders(mod.acc);
   if(mod.acc.background_processor.state==S_account::UPDATE_IDLING){
      assert(mod.acc.IsImap());
                              //make sure that inbox will be connected when we leave folder view
      if(mod.acc.socket && mod.acc.selected_folder){
         if(!mod.acc.selected_folder->IsInbox()){
            C_message_container *cnt = FindInbox(mod.acc);
            if(cnt)
               SetModeConnection(mod.acc, cnt, C_mode_connection::ACT_IMAP_IDLE);
         }
      }else
         assert(0);
   }
   CloseMode(mod);
}

//----------------------------

void C_mail_client::FoldersList_MoveToFolder(C_mode_folders_list &mod, bool ask, C_message_container *target){

   C_message_container *fld = mod.GetSelectedFolder();
   if(fld){
      if(ask){
         if(mod.acc.IsImap() && !fld->IsInbox())
            SetModeFolderSelector(mod.acc, (C_mode_folder_selector::t_FolderSelected)&C_mail_client::FoldersList_MoveToFolder1, fld, true, false, (fld->parent_folder!=NULL));
      }else{
         S_connection_params p;
         if(target){
            p.text<<mod.acc.GetFullFolderName(*target) <<mod.acc.imap_folder_delimiter;
         }
         p.text<<fld->folder_name.FromUtf8();
         mod.folder_move_target = target;
         SetModeConnection(mod.acc, fld, C_mode_connection::ACT_RENAME_IMAP_FOLDER, &p);
      }
   }
}

//----------------------------

void C_mail_client::FoldersListProcessMenu(C_mode_folders_list &mod, int itm, dword menu_id){

   switch(itm){
   case TXT_OPEN:
      if(mod.num_entries){
         bool redraw;
         OpenImapFolder(mod, redraw);
      }
      break;

   case TXT_FOLDER:
      {
         const C_message_container *fld = mod.num_entries ? mod.GetSelectedFolder() : NULL;

         mod.menu = mod.CreateMenu();
         if(mod.acc.IsImap()){
            mod.menu->AddItem(TXT_REFRESH_LIST);
            mod.menu->AddSeparator();
         }
         mod.menu->AddItem(TXT_NEW);
         bool can_ren_del = false;
         if(fld && !fld->IsTemp()){
            if(!fld->IsInbox())
               can_ren_del = true;
         }
         mod.menu->AddItem(TXT_DELETE, can_ren_del ? 0 : C_menu::DISABLED);
         mod.menu->AddItem(TXT_RENAME, can_ren_del ? 0 : C_menu::DISABLED);
         if(mod.acc.IsImap() && mod.acc.NumFolders()>1){
                           //move to other folder
            mod.menu->AddItem(TXT_MOVE_TO_FOLDER, fld && fld->IsInbox() ? C_menu::DISABLED : 0, "[1]", "[M]");
         }
         if(mod.acc.NumFolders()){
            mod.menu->AddSeparator();
            if(mod.num_entries)
               mod.menu->AddItem((fld->flags&fld->FLG_HIDDEN) ? TXT_UNHIDE : TXT_HIDE, (fld->IsTemp() || !can_ren_del) ? C_menu::DISABLED : 0, "[8]", "[H]");
            mod.menu->AddItem(TXT_SHOW_HIDDEN, mod.show_hidden ? C_menu::MARKED : 0, "[0]", "[W]");
         }
         PrepareMenu(mod.menu);
      }
      break;

   case TXT_MOVE_TO_FOLDER:
      FoldersList_MoveToFolder(mod, true);
      break;

   case TXT_NEW:
      FoldersList_StartCreate(mod, true);
      break;

   case TXT_DELETE:
      {
         class C_question: public C_question_callback{
            C_mail_client &app;
            C_mode_folders_list &mod;
            virtual void QuestionConfirm(){
               app.FoldersList_DeleteFolder(mod);
            }
         public:
            C_question(C_mail_client &a, C_mode_folders_list &m): app(a), mod(m){}
         };
         CreateQuestion(*this, mod.acc.IsImap() ? TXT_Q_DELETE_FOLDER : TXT_Q_DELETE_DIR,
            GetText(mod.acc.IsImap() ? TXT_Q_FOLDER_DEL_WARNING : TXT_Q_ARE_YOU_SURE), new(true) C_question(*this, mod), true);
      }
      break;

   case TXT_RENAME:
      FoldersList_StartRename(mod);
      break;

   case TXT_HIDE:
   case TXT_UNHIDE:
      {
         mod.GetSelectedFolder()->flags ^= C_message_container::FLG_HIDDEN;
         SortFolders(mod.acc, NULL);
         SaveAccounts();
         FoldersList_InitView(mod);
         RedrawScreen();
      }
      break;

   case TXT_REFRESH_LIST:
      SetModeConnection(mod.acc, NULL, C_mode_connection::ACT_REFRESH_IMAP_FOLDERS);
      break;

   case TXT_SHOW_HIDDEN:
      {
         const C_message_container *sel_fld = mod.GetSelectedFolder();
         mod.show_hidden = !mod.show_hidden;
         FoldersList_InitView(mod, sel_fld);
         RedrawScreen();
      }
      break;

   case TXT_IMAP_IDLE:
      {
         mod.menu = mod.CreateMenu();
         bool con = (mod.acc.use_imap_idle);
         mod.menu->AddItem(TXT_CONNECT, !con ? 0 : C_menu::DISABLED, !con ? "[5]" : NULL, !con ? "[I]" : NULL);
         mod.menu->AddItem(TXT_DISCONNECT, con ? 0 : C_menu::DISABLED, con ? "[5]" : NULL, con ? "[I]" : NULL);
         PrepareMenu(mod.menu);
      }
      break;

   case TXT_CONNECT:
      ConnectAccountInBackground(mod.acc);
      break;

   case TXT_DISCONNECT:
      mod.acc.CloseIdleConnection();
      break;

   case TXT_UPDATE_MAILBOXES:
   case TXT_UPDATE_MAILBOX:
      ImapFolders_UpdateFolders(mod);
      break;

   case TXT_NEW_MSG:
      SetModeWriteMail(NULL, NULL, false, false, false);
      break;

   case TXT_BACK:
      FoldersListClose(mod);
      break;

   case TXT_EXIT:
      Exit();
      break;

   case TXT_COLLAPSE:
   case TXT_EXPAND:
      {
         const C_message_container &fld = *mod.GetSelectedFolder();
         S_user_input ui;
         ui.Clear();
         ui.key = fld.IsExpanded() ? K_CURSORLEFT : K_CURSORRIGHT;
         bool redraw;
         FoldersListProcessInput(mod, ui, redraw);
      }
      break;
   }
}

//----------------------------

void C_mail_client::FoldersListProcessInput(C_mode_folders_list &mod, S_user_input &ui, bool &redraw){

   if(!ProcessMouseInSoftButtons(ui, redraw)){
      if(mod.te_rename){
#ifdef USE_MOUSE
         if(ProcessMouseInTextEditor(*mod.te_rename, ui))
            redraw = true;
#endif
      }else{
         mod.ProcessInputInList(ui, redraw);
#ifdef USE_MOUSE
         int but = TickBottomButtons(ui, redraw);
         if(but!=-1){
            switch(but){
            case 0: ImapFolders_UpdateFolders(mod); break;
            case 1: SetModeWriteMail(NULL, NULL, false, false, false); break;
            }
            return;
         }
         if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
            if(ui.CheckMouseInRect(mod.rc) && mod.num_entries){
               const C_message_container &fld = *mod.GetSelectedFolder();
               bool can_ren_del = (!fld.IsTemp() & !fld.IsInbox());
               mod.menu = CreateTouchMenu();
               mod.menu->AddItem(TXT_NEW);
               mod.menu->AddItem(TXT_DELETE, can_ren_del ? 0 : C_menu::DISABLED, 0, 0, BUT_DELETE);
               mod.menu->AddItem((fld.flags&fld.FLG_HIDDEN) ? TXT_UNHIDE : TXT_HIDE, can_ren_del ? 0 : C_menu::DISABLED);
               mod.menu->AddItem(TXT_SHOW_HIDDEN, mod.show_hidden ? C_menu::MARKED : 0);
               mod.menu->AddItem(TXT_RENAME, can_ren_del ? 0 : C_menu::DISABLED);
               PrepareTouchMenu(mod.menu, ui);
            }
         }
         if((ui.mouse_buttons&MOUSE_BUTTON_1_UP) && ui.key==K_ENTER){
            const C_message_container *fld = mod.GetSelectedFolder();
            int x = mod.rc.x;
            for(const C_message_container *p=fld->parent_folder; p; p=p->parent_folder){
               x += fdb.cell_size_x;
            }
            if(fld->subfolders.Size())
               x += fdb.line_spacing*2;
            if(ui.mouse.x < x){
               if(fld->subfolders.Size()){
                  ui.key = fld->IsExpanded() ? K_CURSORLEFT : K_CURSORRIGHT;
               }
            }
            //else ui.key = K_ENTER;
         }
#endif
      }
   }
   MapScrollingKeys(ui);
   if(mod.te_rename){
      C_text_editor &te = *mod.te_rename;
      C_message_container &fld = *mod.GetSelectedFolder();
      switch(ui.key){
      case K_RIGHT_SOFT:
      case K_ESC:
         if(!mod.creating_new){
                              //cancel rename
         }else{
                              //cancel create, delete new folder
            t_folders &hr = fld.parent_folder ? fld.parent_folder->subfolders : mod.acc._folders;
            hr.Resize(hr.Size()-1);
            mod.selection = Max(0, mod.selection-1);
            FoldersList_InitView(mod);
         }
         mod.te_rename = NULL;
         redraw = true;
         break;
      case K_ENTER:
      case K_LEFT_SOFT:
         {
            Cstr_w n = te.GetText();
            if(n.Length()){
               t_folders &hr = fld.parent_folder ? fld.parent_folder->subfolders : mod.acc._folders;
                              //check name duplication
               for(int i=hr.Size(); i--; ){
                  const C_message_container *f = hr[i];
                  if(f==&fld)
                     continue;
                  if(!text_utils::CompareStringsNoCase(n, f->folder_name.FromUtf8())){
                     n.Clear();
                     break;
                  }
               }
            }
            if(!n.Length())
               break;
            if(mod.acc.IsImap()){
                              //check if name contains hierarchy delimiter
               if(mod.acc.imap_folder_delimiter){
                  for(int i=n.Length(); i--; ){
                     if(n[i]==mod.acc.imap_folder_delimiter){
                        n.Clear();
                        break;
                     }
                  }
               }
               if(!n.Length()){
                  break;
               }
               S_connection_params p;
               if(fld.parent_folder){
                  p.text<<mod.acc.GetFullFolderName(*fld.parent_folder) <<mod.acc.imap_folder_delimiter;
               }
               p.text<<n;
               SetModeConnection(mod.acc, &fld, mod.creating_new ? C_mode_connection::ACT_CREATE_IMAP_FOLDER : C_mode_connection::ACT_RENAME_IMAP_FOLDER, &p);
               return;
            }else{
               FoldersList_FinishRename(mod);
               redraw = true;
            }
         }
         break;
      }
   }else
   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      FoldersListClose(mod);
      return;

   case '8':
   case 'h':
      {
         C_message_container &fld = *mod.GetSelectedFolder();
         if(!fld.IsInbox() || (fld.flags&C_message_container::FLG_HIDDEN)){
            fld.flags ^= C_message_container::FLG_HIDDEN;
            SortFolders(mod.acc, NULL);
            SaveAccounts();
            FoldersList_InitView(mod);
            redraw = true;
         }
      }
      break;

   case '0':
   case 'w':
      {
         const C_message_container *sel_fld = mod.GetSelectedFolder();
         mod.show_hidden = !mod.show_hidden;
         FoldersList_InitView(mod, sel_fld);
         redraw = true;
      }
      break;

   case '5':
   case 'i':
#ifdef _DEBUG
      if(ui.key_bits&GKEY_CTRL){
         ConnectAccountInBackground(mod.acc);
         break;
      }
#endif
      if(mod.acc.IsImap()){
         if(mod.acc.use_imap_idle)
            mod.acc.CloseIdleConnection();
         else
            ConnectAccountInBackground(mod.acc);
         redraw = true;
      }
      break;

   case '1':
   case 'm':
      FoldersList_MoveToFolder(mod, true);
      break;

#ifdef _DEBUG
      /*
   case '0':
      if(mod.num_entries){
         DeleteIMAPFolder(mod.acc, mod.selection);
         SaveAccounts();
         FoldersList_InitView(mod);
         redraw = true;
      }
      break;
      */
   case 'l':
      if(mod.acc.IsImap()){
         SetModeConnection(mod.acc, NULL, C_mode_connection::ACT_REFRESH_IMAP_FOLDERS);
         return;
      }
      break;

   case '2':
      FoldersList_StartRename(mod);
      return;

   case '7':
      if(ConnectionImapStartUploadingSent(mod.acc, NULL, C_mode_connection::ACT_UPLOAD_SENT, NULL))
         return;
      break;
#endif//_DEBUG

   case 'n':
      SetModeWriteMail(NULL, NULL, false, false, false);
      return;

   case 'u':
   case K_SEND:
      ImapFolders_UpdateFolders(mod);
      return;

   case K_CURSORLEFT:
      if(mod.num_entries){
         C_message_container &fld = *mod.GetSelectedFolder();
         if(fld.subfolders.Size() && fld.IsExpanded()){
            fld.flags ^= fld.FLG_EXPANDED;
            int tl = mod.sb.pos;
            FoldersList_InitView(mod);
            mod.sb.pos = tl;
         }else if(fld.parent_folder){
            while(mod.selection){
               --mod.selection;
               if(mod.GetSelectedFolder()==fld.parent_folder)
                  break;
            }
         }else{
            ui.key = K_CURSORUP;
            FoldersListProcessInput(mod, ui, redraw);
            return;
         }
         mod.EnsureVisible();
         redraw = true;
      }
      break;
   case K_CURSORRIGHT:
      if(mod.num_entries){
         C_message_container &fld = *mod.GetSelectedFolder();
         if(fld.subfolders.Size() && !fld.IsExpanded()){
            fld.flags ^= fld.FLG_EXPANDED;
            int tl = mod.sb.pos;
            FoldersList_InitView(mod);
            mod.sb.pos = tl;
                              //make sure most possible entires are visible
            int num_c = fld.subfolders.Size();
            int num_lines = mod.sb.visible_space;
            if(mod.IsPixelMode())
               num_lines /= mod.entry_height;
            int want_t = mod.selection + num_c - num_lines + 1;
            if(mod.IsPixelMode())
               want_t *= mod.entry_height;
            if(want_t > mod.top_line)
               mod.sb.pos = want_t;
            mod.EnsureVisible();

            redraw = true;
         }else{
            ui.key = K_CURSORDOWN;
            FoldersListProcessInput(mod, ui, redraw);
            return;
         }
      }
      break;

   case K_ENTER:
      if(mod.num_entries)
         OpenImapFolder(mod, redraw);
      return;

   case K_LEFT_SOFT:
   case K_MENU:
      mod.menu = mod.CreateMenu();
      if(mod.num_entries){
         C_message_container &fld = *mod.GetSelectedFolder();
         if(!(fld.flags&fld.FLG_NOSELECT))
            mod.menu->AddItem(TXT_OPEN, 0, ok_key_name);
         if(fld.subfolders.Size())
            mod.menu->AddItem(fld.IsExpanded() ? TXT_COLLAPSE : TXT_EXPAND, 0, fld.IsExpanded() ? "[<- Cursor]" : "[Cursor ->]");
      }
      mod.menu->AddItem(mod.acc.IsImap() ? TXT_UPDATE_MAILBOXES : TXT_UPDATE_MAILBOX, (!mod.num_entries ? C_menu::DISABLED : 0), send_key_name, "[U]");
      mod.menu->AddItem(TXT_FOLDER, C_menu::HAS_SUBMENU);
      if(mod.acc.IsImap() && (mod.acc.flags&S_account::ACC_USE_IMAP_IDLE)){
         mod.menu->AddItem(TXT_IMAP_IDLE, C_menu::HAS_SUBMENU);
      }
      mod.menu->AddItem(TXT_NEW_MSG, 0, NULL, "[N]", BUT_NEW);
      mod.menu->AddSeparator();
      mod.menu->AddItem(config_mail.tweaks.exit_in_menus ? TXT_EXIT : TXT_BACK);
      PrepareMenu(mod.menu);
      return;
   }
}

//----------------------------

void C_mail_client::DrawFolder(const C_mode_folders_list &mod, int fi){

                              //compute item rect
   S_rect rc_item = mod.rc;
   rc_item.sx = mod.GetMaxX()-mod.rc.x;
   rc_item.sy = mod.entry_height;
   if(mod.IsPixelMode()){
                              //pixel offset
      rc_item.y = mod.rc.y + fi*mod.entry_height - mod.sb.pos;
   }
   if(rc_item.y>=mod.rc.Bottom() || rc_item.Bottom()<=mod.rc.y)
      return;
   const int max_x = mod.GetMaxX();
   const S_account &acc = mod.acc;

   C_folders_iterator it(mod.acc._folders, mod.show_hidden, false);
   for(int i=0; i<fi && !it.IsEnd(); i++)
      it.Next();
   if(it.IsEnd())
      return;
   const C_message_container &fld = *it.Next();

   S_rect rc_fill(2, rc_item.y, max_x-rc_item.x, mod.entry_height);
   dword flags = FF_BOLD, color = GetColor(COL_TEXT);
   if(fi==mod.selection){
      DrawSelection(rc_fill, true);
      color = GetColor(COL_TEXT_HIGHLIGHTED);
   }else
      ClearWorkArea(rc_fill);

                        //draw separator
   if(rc_item.y!=mod.rc.y && (fi<mod.selection || fi>(mod.selection+1))){
      const int OFFS = fdb.letter_size_x;
      DrawSeparator(rc_item.x+OFFS, max_x-rc_item.x-OFFS*2, rc_item.y);
   }
   int x = rc_item.x;
   if(acc.background_processor.state && (acc.selected_folder==&fld || (!acc.IsImap() && fld.IsInbox()))){
                           //draw connected mailbox
      int w = DrawConnectIconType(x+fdb.cell_size_x/4, rc_item.y+fdb.line_spacing, acc.background_processor.state);
      x += w;
      //max_width -= w;
   }
                              //offset by level
   for(const C_message_container *p=fld.parent_folder; p; p=p->parent_folder){
      x += fdb.cell_size_x;
   }
   if(fld.subfolders.Size()){
      x += fdb.cell_size_x/4;
      int sz = fdb.line_spacing/2;
      x += DrawPlusMinus(x, rc_item.y+(mod.entry_height-sz)/2, sz, !fld.IsExpanded());
      x += fdb.cell_size_x/8;
      /*
      if(fi==mod.selection){
         const int SZ = fds.line_spacing/2;
         DrawArrowHorizontal(x, rc_item.CenterY()-SZ/2, SZ, color, !fld.IsExpanded());
         x += SZ;
      }
      */
   }else
      x += fdb.cell_size_x/2;
   //max_width -= fdb.cell_size_x;

   if(fld.flags&fld.FLG_HIDDEN)
      color = MulAlpha(color, 0x6000);
   {
                              //draw folder icon
      int fi1 = 1;
      if(acc.IsImap()){
         Cstr_w n_fld = acc.GetFullFolderName(fld);
         if(n_fld==acc.GetTrashFolderName())
            fi1 = 2;
         else if(n_fld==acc.GetDraftFolderName())
            fi1 = 3;
         else if(n_fld==acc.GetSentFolderName())
            fi1 = 4;
      }
      x += DrawImapFolderIcon(x+2, rc_item.y + fdb.line_spacing/4, fi1);
      x += fdb.cell_size_x;
   }
                        //draw name
   if(fi==mod.selection && mod.te_rename)
      DrawEditedText(*mod.te_rename);
   else{
      if(fld.IsTemp()){
         flags |= FF_ITALIC;
         flags &= ~FF_BOLD;
         color = MulAlpha(color, 0xc000);
      }else if(fld.flags&fld.FLG_NOSELECT){
         flags |= FF_ITALIC;
         color = MulAlpha(color, 0xc000);
      }
      Cstr_w n = fld.folder_name.FromUtf8();
      const wchar *status = NULL;
      if(acc.background_processor.state && acc.selected_folder==&fld && acc.background_processor.status_text.Length())
         status = acc.background_processor.status_text;

      const dword *stats = fld.GetMessagesStatistics();
      dword tmp_stats[C_message_container::STAT_LAST];
      if(fld.subfolders.Size() && !fld.IsExpanded()){
         MemSet(tmp_stats, 0, sizeof(tmp_stats));
         fld.CollectHierarchyStatistics(tmp_stats, mod.show_hidden);
         stats = tmp_stats;
      }
      DrawAccountNameAndIcons(n, x, rc_item.y, rc_item, max_x-x, flags, color, stats, status);
   }
}

//----------------------------

void C_mail_client::DrawFoldersListContents(const C_mode_folders_list &mod){

   if(mod.num_entries){
      S_rect rc_item;
      int item_index = -1;
      while(mod.BeginDrawNextItem(rc_item, item_index))
         DrawFolder(mod, item_index);
      mod.EndDrawItems();

      DrawScrollbar(mod.sb, SCROLLBAR_DRAW_BACKGROUND);
                              //fill empty space
      int rest = mod.rc.Bottom() - rc_item.Bottom();
      if(rest>0){
         S_rect rc(2, rc_item.Bottom(), ScrnSX()-4, rest);
         ClearWorkArea(rc);
      }
   }
}

//----------------------------

void C_mail_client::DrawFoldersList(const C_mode_folders_list &mod){

   SetScreenDirty();
   {
      Cstr_w s;
      s.Format(L"% - % %")
         <<mod.acc.name <<(mod.acc.IsImap() ? L"IMAP" : L"POP3") <<GetText(TXT_FOLDERS);
      DrawTitleBar(s, mod.rc.y);
   }
   ClearSoftButtonsArea(mod.rc.Bottom() + 2);

   DrawEtchedFrame(mod.rc);
   DrawFoldersListContents(mod);
   {
      E_TEXT_ID lsk, rsk;
      if(mod.te_rename){
         lsk = TXT_OK, rsk = TXT_CANCEL;
      }else
         lsk = TXT_MENU, rsk = TXT_BACK;
      DrawSoftButtonsBar(mod, lsk, rsk, mod.te_rename);
   }
#ifdef USE_MOUSE
   if(!mod.te_rename){
      static const char but_defs[] = { BUT_UPDATE_MAILBOX, BUT_NEW, BUT_NO, BUT_NO };
      static const dword tids[] = { TXT_UPDATE_MAILBOX, TXT_NEW, 0, 0 };
      DrawBottomButtons(mod, but_defs, tids);
   }
#endif
}

//----------------------------
//----------------------------

static void SkipIgnoredFolders(C_folders_iterator &it, const C_message_container *ignore_folder){

   while(!it.IsEnd()){
      const C_message_container *fld = it.PeekNext();
      if(//fld!=ignore_folder && 
         !fld->IsTemp())
         break;
      it.Next();
   }
}

//----------------------------

void C_mail_client::SetModeFolderSelector(S_account &acc, C_mode_folder_selector::t_FolderSelected fsel, const C_message_container *ignore_folder, bool allow_nonselectable,
   bool allow_noninferiors, bool show_root){

   C_mode_folder_selector &mod = *new(true) C_mode_folder_selector(*this, acc, fsel, ignore_folder, allow_nonselectable, allow_noninferiors, show_root);

                              //save expand/collapse flags
   C_folders_iterator it(mod.acc._folders);
   while(!it.IsEnd())
      mod.save_expand_flag.push_back(it.Next()->flags&C_message_container::FLG_EXPANDED);

   InitLayoutImapFolderSelector(mod);
   FolderSelector_InitView(mod, ignore_folder);
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::InitLayoutImapFolderSelector(C_mode_folder_selector &mod){

   mod.entry_height = fdb.line_spacing;
   mod.redraw_bgnd = true;

   const dword sx = ScrnSX();
   const int sz_x = Min(int(sx) - fdb.letter_size_x * 4, fdb.letter_size_x*30);
   const int title_sy = GetDialogTitleHeight()+2;
   //int num_lines = Min(mod.num_entries, int(ScrnSY()-GetSoftButtonBarHeight()*2-title_sy-fdb.line_spacing)/fdb.line_spacing);
   int num_lines = (ScrnSY()-GetSoftButtonBarHeight()*4-title_sy-fdb.line_spacing)/fdb.line_spacing;
                              //initial rect (horizontal position is correct)
   mod.rc_outline = S_rect((sx-sz_x)/2, 0, sz_x, title_sy + num_lines*fdb.line_spacing + fdb.line_spacing/2);
   mod.rc_outline.y = (ScrnSY()-mod.rc_outline.sy)/2;

   mod.rc = S_rect(mod.rc_outline.x+fdb.letter_size_x, mod.rc_outline.y+title_sy, mod.rc_outline.sx-fdb.letter_size_x*2, mod.entry_height * num_lines);

   mod.sb.visible_space = num_lines;
   if(mod.IsPixelMode())
      mod.sb.visible_space *= mod.entry_height;

   mod.sb.SetVisibleFlag();
   const int width = GetScrollbarWidth();
   mod.sb.rc = S_rect(mod.rc.Right()-width-1, mod.rc.y+3, width, mod.rc.sy-6);
   mod.EnsureVisible();
}

//----------------------------

void C_mail_client::FolderSelector_InitView(C_mode_folder_selector &mod, const C_message_container *sel_fld){

   mod.sb.pos = 0;

   mod.num_entries = 0;
   if(mod.show_root)
      ++mod.num_entries;
   C_folders_iterator it(mod.acc._folders, true, false);
   for(; !it.IsEnd(); it.Next(), ++mod.num_entries){
      SkipIgnoredFolders(it, mod.ignore_folder);
      if(it.IsEnd())
         break;
      if(sel_fld && it.PeekNext()==mod.ignore_folder)
         mod.selection = mod.num_entries;
   }
   mod.selection = Min(Max(mod.selection, 0), mod.num_entries-1);
   mod.sb.total_space = mod.num_entries;
   if(mod.IsPixelMode())
      mod.sb.total_space *= mod.entry_height;
   mod.sb.SetVisibleFlag();

   mod.EnsureVisible();
}

//----------------------------

C_message_container *C_mail_client::C_mode_folder_selector::GetSelectedFolder(){
   int i = 0;
   if(show_root){
      if(!selection)
         return NULL;
      ++i;
   }
   C_folders_iterator it(acc._folders, true, false);
   for( ; i<selection && !it.IsEnd(); it.Next(), i++){
      SkipIgnoredFolders(it, ignore_folder);
   }
   if(it.IsEnd())
      return NULL;
   SkipIgnoredFolders(it, ignore_folder);
   return it.Next();
}

//----------------------------

bool C_mail_client::C_mode_folder_selector::IsValidTraget(const C_message_container *fld) const{

   if(!fld)
      return true;
   if(fld==ignore_folder)
      return false;
   if((fld->flags&fld->FLG_NOSELECT) && !allow_nonselectable)
      return false;
   if(!allow_noninferiors){
      if(fld->flags&fld->FLG_NOINFERIORS)
         return false;
      if(ignore_folder){
                              //fld also can't be child of ignored folder
         Cstr_w n_fld = acc.GetFullFolderName(*fld);
         Cstr_w n_ign = acc.GetFullFolderName(*ignore_folder);
         if(n_ign.Length() < n_fld.Length() && n_fld.Left(n_ign.Length())==n_ign)
            return false;
      }
   }
   return true;
}

//----------------------------

void C_mail_client::CloseModeFolderSelector(C_mode_folder_selector &mod, bool redraw){

                              //restore expand/collapse flags
   C_folders_iterator it(mod.acc._folders);
   for(int i = 0; !it.IsEnd(); ++i){
      C_message_container *fld = it.Next();
      fld->flags &= ~C_message_container::FLG_EXPANDED;
      if(mod.save_expand_flag[i])
         fld->flags |= C_message_container::FLG_EXPANDED;
   }
   CloseMode(mod, redraw);
}

//----------------------------

void C_mail_client::ImapFolderSelectorProcessInput(C_mode_folder_selector &mod, S_user_input &ui, bool &redraw){

   mod.ProcessInputInList(ui, redraw);
#ifdef USE_MOUSE
   if(!ProcessMouseInSoftButtons(ui, redraw)){
      if((ui.mouse_buttons&MOUSE_BUTTON_1_UP) && ui.key==K_ENTER){
         const C_message_container *fld = mod.GetSelectedFolder();
         int x = mod.rc.x;
         if(fld){
            for(const C_message_container *p=fld->parent_folder; p; p=p->parent_folder)
               x += fdb.cell_size_x;
            if(fld->subfolders.Size())
               x += fdb.line_spacing;
         }
         if(ui.mouse.x < x){
            if(fld->subfolders.Size()){
               ui.key = fld->IsExpanded() ? K_CURSORLEFT : K_CURSORRIGHT;
            }
         }//else
            //ui.key = K_ENTER;
      }
   }
#endif
   if(redraw)
      mod.redraw_bgnd = true;
   MapScrollingKeys(ui);
   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      CloseModeFolderSelector(mod);
      return;
   case K_ENTER:
   case K_LEFT_SOFT:
      if(mod.num_entries){
                              //get selected IMAP folder
         C_message_container *fld = mod.GetSelectedFolder();
         if(!mod.IsValidTraget(fld)){
            if(fld && fld->subfolders.Size()){
               S_user_input ui1;
               ui1.Clear();
               ui1.key = fld->IsExpanded() ? K_CURSORLEFT : K_CURSORRIGHT;
               ImapFolderSelectorProcessInput(mod,  ui1, redraw);
               return;
            }
            break;
         }
         C_mode_folder_selector::t_FolderSelected FolderSelected = mod.FolderSelected;
         CloseModeFolderSelector(mod, false);
         (this->*FolderSelected)(fld);
         return;
      }
      break;

   case K_CURSORLEFT:
      if(mod.num_entries){
         C_message_container *fld = mod.GetSelectedFolder();
         if(fld && fld->subfolders.Size() && fld->IsExpanded()){
            fld->flags ^= fld->FLG_EXPANDED;
            int tl = mod.sb.pos;
            FolderSelector_InitView(mod);
            mod.sb.pos = tl;
         }else if(fld && fld->parent_folder){
            while(mod.selection){
               --mod.selection;
               if(mod.GetSelectedFolder()==fld->parent_folder)
                  break;
            }
         }else{
            ui.key = K_CURSORUP;
            ImapFolderSelectorProcessInput(mod, ui, redraw);
            return;
         }
         mod.EnsureVisible();
         redraw = true;
      }
      break;
   case K_CURSORRIGHT:
      if(mod.num_entries){
         C_message_container *fld = mod.GetSelectedFolder();
         if(fld && fld->subfolders.Size() && !fld->IsExpanded()){
            fld->flags ^= fld->FLG_EXPANDED;
            int tl = mod.sb.pos;
            FolderSelector_InitView(mod);
            mod.sb.pos = tl;
                              //make sure most possible entires are visible
            int num_c = fld->subfolders.Size();
            int num_lines = mod.sb.visible_space;
            if(mod.IsPixelMode())
               num_lines /= mod.entry_height;
            int want_t = mod.selection + num_c - num_lines + 1;
            if(mod.IsPixelMode())
               want_t *= mod.entry_height;
            if(want_t > mod.top_line)
               mod.sb.pos = want_t;
            mod.EnsureVisible();

            redraw = true;
         }else{
            ui.key = K_CURSORDOWN;
            ImapFolderSelectorProcessInput(mod, ui, redraw);
            return;
         }
      }
      break;
   }
}

//----------------------------

void C_mail_client::DrawImapFolderSelectorContets(const C_mode_folder_selector &mod){

   DrawDialogBase(mod.rc_outline, false, &mod.rc);
   dword col_text = GetColor(COL_TEXT_POPUP);

   const int max_x = mod.GetMaxX();

   S_rect rc_item;
   int item_index = -1;
   if(mod.BeginDrawNextItem(rc_item, item_index)){
      int line = 0;
      if(mod.show_root)
         ++line;
      C_folders_iterator it(mod.acc._folders, true, false);
      for( ; line<item_index && !it.IsEnd(); line++){
         SkipIgnoredFolders(it, mod.ignore_folder);
         it.Next();
      }
      do{
         SkipIgnoredFolders(it, mod.ignore_folder);
         const C_message_container *fld;
         if(mod.show_root && !item_index)
            fld = NULL;
         else
            fld = it.Next();

         dword color = col_text;
         if(item_index==mod.selection){
            //S_rect rc(mod.rc.x+1, y, sel_w, mod.entry_height);
            if(!mod.IsValidTraget(fld)){
               S_rect rc = rc_item;
               rc.Compact();
               DrawOutline(rc, 0xffff0000);
            }else{
               DrawSelection(rc_item);
               color = GetColor(COL_TEXT_HIGHLIGHTED);
            }
         }
         dword flags = 0;
         int x = mod.rc.x;
         if(fld){
            if(fld->flags&fld->FLG_HIDDEN)
               color = MulAlpha(color, 0x8000);
            if(fld->flags&fld->FLG_NOSELECT)
               flags |= FF_ITALIC;
         }
         if(fld){
                              //offset by level
            for(const C_message_container *p=fld->parent_folder; p; p=p->parent_folder)
               x += fdb.cell_size_x;
         }
         if(fld && fld->subfolders.Size()){
            x += fdb.cell_size_x/4;
            int sz = mod.entry_height/3;
            x += DrawPlusMinus(x, rc_item.y+(mod.entry_height-sz)/2, sz, !fld->IsExpanded());
            x += fdb.cell_size_x/8;
         }else
            x += fdb.cell_size_x/2;

         Cstr_w s;
         if(fld)
            s = fld->folder_name.FromUtf8();
         else
            s = GetText(TXT_ROOT_FOLDER);
         DrawString(s, x, rc_item.y, UI_FONT_BIG, flags, color, -(max_x-x));
      }while(mod.BeginDrawNextItem(rc_item, item_index));
   }
   mod.EndDrawItems();
   DrawScrollbar(mod.sb);
}

//----------------------------

void C_mail_client::DrawImapFolderSelector(const C_mode_folder_selector &mod){

   if(mod.redraw_bgnd){
      mod.redraw_bgnd = false;
      mod.DrawParentMode(true);

      DrawDialogBase(mod.rc_outline, true);
      DrawOutline(mod.rc, 0x80000000, 0xc0ffffff);

      DrawDialogTitle(mod.rc_outline, GetText(TXT_MOVE_TO_FOLDER));
      DrawSoftButtonsBar(mod, TXT_OK, TXT_CANCEL);
      SetScreenDirty();
   }
   DrawImapFolderSelectorContets(mod);
}

//----------------------------
