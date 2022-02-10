#include "..\Main.h"
#include "Main_Email.h"
#include <Xml.h>

#define MAX_SIG_TEXT_LEN 12000

//----------------------------

const int SUBJ_SCROLL_DELAY = 2000; //delay to scroll long subject

const int SIGNATURES_VERSION = 292;

#define MAX_SIGNATURES 40
#define MAX_NAME_LENGTH 24

//----------------------------

Cstr_w C_mail_client::GetSignaturesFilename() const{
   Cstr_w fn; fn<<mail_data_path <<DATA_PATH_PREFIX <<L"Signatures.xml";
   return fn;
}

//----------------------------

void C_mail_client::LoadSignatures(){

   signatures.Clear();
   C_file fl;
   if(fl.Open(GetSignaturesFilename())){
      C_xml xml;
      if(xml.Parse(fl) && xml.GetRoot()=="Signatures"){
         C_vector<S_signature> tmp;
         for(const C_xml::C_element *el=xml.GetFirstChild(); el; el=el->GetNextSibling()){
            if(*el=="Signature"){
               const char *name = el->FindAttributeValue("name");
               if(name){
                  S_signature &s = tmp.push_back(S_signature());
                  s.name = xml.DecodeString(name);
                  s.body = xml.DecodeString(el->GetContent());
               }
            }else assert(0);
         }
         signatures.Assign(tmp.begin(), tmp.end());
      }
   }
}

//----------------------------

void C_mail_client::SaveSignatures() const{

   C_file fl;
   if(fl.Open(GetSignaturesFilename(), C_file::FILE_WRITE|C_file::FILE_WRITE_CREATE_PATH)){
      C_xml_build xml("Signatures");
      xml.root.AddAttributeValue("version", SIGNATURES_VERSION);
      for(dword i=0; i<signatures.Size(); i++){
         const S_signature &s = signatures[i];
         C_xml_build::C_element &el = xml.root.AddChild("Signature");
         el.AddAttributeValue("name", s.name);
         el.SetContent(s.body);
      }
      xml.Write(fl, false, true);
   }else
      LOG_RUN("Can't save signatures");
}

//----------------------------

class C_mode_edit_signatures: public C_mode_list<C_mail_client>{
   enum{
      BUT_BROWSE_NEW = 0,
      BUT_BROWSE_DEL = 3,
   };

   void SetSelection(int sel);
   void StartRename();
   void Delete();
   void Edit();
   void AddNew();
   void BodyEditDone(bool save_text);
                           //signature body writer
   C_ctrl_text_entry *ctrl_writer;

                           //editor used for renaming
   C_smart_ptr<C_text_editor> te_rename;
   int max_name_width;

   virtual int GetNumEntries() const{
      return app.signatures.Size();
   }
   virtual bool IsPixelMode() const{ return true; }

   virtual void SelectionChanged(int prev_selection){
      SetSelection(-selection);
      ctrl_writer->Draw();
   }

   bool CheckValidName(const wchar *name) const{
                              //can't be empty
      if(!*name)
         return false;
                              //can't be same as in other signatures
      for(int i=app.signatures.Size(); i--; ){
         if(i==selection)
            continue;
         if(app.signatures[i].name==name)
            return false;
      }
      return true;
   }

   enum E_STATE{
      STATE_NO,
      STATE_BROWSE,
      STATE_RENAME,
      STATE_EDIT_BODY,
   } state;

//----------------------------

   virtual void OnSoftBarButtonPressed(dword index){
      switch(state){
      case STATE_BROWSE:
         switch(index){
         case BUT_BROWSE_NEW: AddNew(); break;
         case BUT_BROWSE_DEL: Delete(); break;
         }
         DrawContents();
         ctrl_writer->MarkRedraw();
         break;
      }
   }

//----------------------------

   void SetState(E_STATE s){

      state = s;
      C_ctrl_softkey_bar &skb = *GetSoftkeyBar();
      skb.ResetAllButtons();
      switch(state){
      case STATE_BROWSE:
         skb.InitButton(BUT_BROWSE_NEW, app.BUT_NEW, TXT_NEW);
         skb.InitButton(BUT_BROWSE_DEL, app.BUT_DELETE, TXT_DELETE);
         skb.EnableButton(BUT_BROWSE_NEW, (app.signatures.Size()<MAX_SIGNATURES));
         if(!app.signatures.Size())
            skb.EnableButton(BUT_BROWSE_DEL, false);
         break;
      case STATE_RENAME:
         break;
      }
   }

//----------------------------

   virtual dword GetMenuSoftKey() const{
      switch(state){
      case STATE_RENAME: return TXT_OK; break;
      default:
         return C_mode::GetMenuSoftKey();
      }
   }
//----------------------------

   virtual dword GetSecondarySoftKey() const{
      switch(state){
      case STATE_EDIT_BODY: return TXT_DONE; break;
      case STATE_RENAME: return TXT_CANCEL; break;
      default:
         return C_mode::GetSecondarySoftKey();
      }
   }
public:
   C_mode_edit_signatures(C_mail_client &_app):
      C_mode_list<C_mail_client>(_app),
      state(STATE_NO)
   {
      ctrl_writer = new(true) C_ctrl_text_entry(this, MAX_SIG_TEXT_LEN, TXTED_ALLOW_PREDICTIVE, app.config.viewer_font_index);
      AddControl(ctrl_writer);
      SetTitle(app.GetText(TXT_SIGNATURES_EDITOR));
      SetState(STATE_BROWSE);

      InitLayout();
      SetSelection(0);
   }
   virtual void InitLayout();
   virtual void ProcessInput(S_user_input &ui, bool &redraw);
   virtual void ProcessMenu(int itm, dword menu_id);
   virtual void Draw() const;
   virtual void DrawContents() const;  //draws list of signatures

   virtual void InitMenu(){
      int num_sigs = app.signatures.Size();
      switch(state){
      case STATE_BROWSE:
         menu->AddItem(TXT_EDIT, num_sigs ? 0 : C_menu::DISABLED, app.ok_key_name);
         menu->AddItem(TXT_RENAME, num_sigs ? 0 : C_menu::DISABLED);
         menu->AddSeparator();
         menu->AddItem(TXT_NEW, num_sigs<MAX_SIGNATURES ? 0 : C_menu::DISABLED, NULL, NULL, app.BUT_NEW);
         menu->AddItem(TXT_DELETE, num_sigs ? 0 : C_menu::DISABLED, NULL, NULL, app.BUT_DELETE);
         menu->AddSeparator();
         if(app.signatures.Size() && app.NumAccounts()){
            menu->AddItem(TXT_AUTOMATICALLY_INSERT, C_menu::HAS_SUBMENU);
            menu->AddSeparator();
         }
         menu->AddItem(TXT_BACK);
         break;
      case STATE_EDIT_BODY:
         menu->AddItem(TXT_DONE);
         app.AddEditSubmenu(menu);
         menu->AddSeparator();
         menu->AddItem(TXT_CANCEL);
         break;
      }
   }
};

//----------------------------

void C_mode_edit_signatures::InitLayout(){

   C_mode::InitLayout();
   const int top = app.GetTitleBarHeight();
   rc = S_rect(0, top, app.ScrnSX(), 0);

                              //compute # of visible lines, and resize rectangle to whole lines
   entry_height = app.fdb.line_spacing*3/2;
   {
      int sy = app.ScrnSY()-top;
      sy -= app.GetSoftButtonBarHeight();
                              //compute size of preview
      const int prv_sy = sy*4/10;
      sb.visible_space = sy - prv_sy;
      rc.sy = sy - prv_sy;
      {
         S_rect rc1(0, rc.Bottom()+2, rc.sx, 0);
         rc1.sy = GetClientRect().Bottom() - rc1.y;
         ctrl_writer->SetRect(rc1);
      }
   }

                           //init scrollbar
   const int width = app.GetScrollbarWidth();
   sb.rc = S_rect(rc.Right()-width-1, rc.y+3, width, rc.sy-6);
   sb.total_space = app.signatures.Size()*entry_height;
   sb.SetVisibleFlag();

   max_name_width = sb.rc.x - (rc.x + app.fdb.letter_size_x*3/2);
   EnsureVisible();

   if(te_rename){
      S_rect trc = te_rename->GetRect();
      trc.y = rc.y + app.fdb.line_spacing/3 + (entry_height*selection-sb.pos);
      trc.sx = max_name_width;
      te_rename->SetRect(trc);
   }
}

//----------------------------

void C_mode_edit_signatures::SetSelection(int sel){

   selection = Abs(sel);
   if(sel>=0)
      EnsureVisible();

   if(app.signatures.Size()){
      const S_signature &sig = app.signatures[selection];
      ctrl_writer->SetText(sig.body);
      ctrl_writer->SetCursorPos(0);
   }
}

//----------------------------

void C_mode_edit_signatures::StartRename(){

   EnsureVisible();

   const S_signature &sig = app.signatures[selection];

   te_rename = app.CreateTextEditor(0, app.UI_FONT_BIG, FF_BOLD, NULL, MAX_NAME_LENGTH);
   GetSoftkeyBar()->SetActiveTextEditor(te_rename);
   C_text_editor &te = *te_rename;
   te.Release();
   te.SetRect(S_rect(rc.x+app.fdb.letter_size_x, rc.y + app.fdb.line_spacing/3 + (entry_height*selection-sb.pos), max_name_width, app.fdb.cell_size_y+1));

   te.SetInitText(sig.name);
   te.SetCursorPos(te.GetCursorPos());
   app.MakeSureCursorIsVisible(te);
   SetState(STATE_RENAME);
}

//----------------------------

void C_mode_edit_signatures::Delete(){

   int num = app.signatures.Size();
   assert(num && selection < num);
   for(int i=selection+1; i<num; i++)
      app.signatures[i-1] = app.signatures[i];
   app.signatures.Resize(--num);
   if(state==STATE_BROWSE)
      GetSoftkeyBar()->EnableButton(BUT_BROWSE_NEW, (app.signatures.Size()<MAX_SIGNATURES));
   sb.total_space = num*entry_height;
   sb.SetVisibleFlag();
   SetSelection(Min(selection, num-1));
}

//----------------------------

void C_mode_edit_signatures::Edit(){

   if(!app.signatures.Size())
      return;
   SetState(STATE_EDIT_BODY);
   SetFocus(ctrl_writer);
}

//----------------------------

void C_mode_edit_signatures::AddNew(){

                              //add signature at end
   int num_sigs = app.signatures.Size();
   app.signatures.Resize(++num_sigs);
   sb.total_space = num_sigs*entry_height;
   sb.SetVisibleFlag();
   SetSelection(num_sigs-1);
   StartRename();
}

//----------------------------

void C_mode_edit_signatures::BodyEditDone(bool save_text){

   SetFocus(NULL);
                  //finished editing text, save it
   if(save_text){
      app.signatures[selection].body = ctrl_writer->GetText();
   }
   SetState(STATE_BROWSE);
   SetSelection(selection);
}

//----------------------------

void C_mode_edit_signatures::ProcessMenu(int itm, dword menu_id){

   switch(itm){
   case TXT_NEW:
      AddNew();
      break;

   case TXT_RENAME:
      StartRename();
      break;

   case TXT_DELETE:
      Delete();
      break;

   case TXT_EDIT:
      if(state==STATE_EDIT_BODY){
         menu = app.CreateEditCCPSubmenu(ctrl_writer->GetTextEditor(), menu);
         app.PrepareMenu(menu);
      }else
         Edit();
      break;

   case TXT_AUTOMATICALLY_INSERT:
      {
         Cstr_c sig = app.signatures[selection].name.ToUtf8();
         menu = CreateMenu();
         for(dword i=0; i<app.NumAccounts(); i++){
            const S_account_settings &acc = app.accounts[i];
            menu->AddItem(acc.name, acc.signature_name==sig ? C_menu::MARKED : 0);
         }
         app.PrepareMenu(menu);
      }
      break;

   case TXT_BACK:
      app.SaveSignatures();
      app.CloseMode(*this);
      break;

   case TXT_CANCEL:
   case TXT_DONE:
      BodyEditDone((itm==TXT_DONE));
      break;

   case C_application_ui::SPECIAL_TEXT_CUT: ctrl_writer->Cut(); break;
   case C_application_ui::SPECIAL_TEXT_COPY: ctrl_writer->Copy(); break;
   case C_application_ui::SPECIAL_TEXT_PASTE: ctrl_writer->Paste(); break;
   default:
      {
         dword ai = itm-0x10000;
         if(ai<app.NumAccounts()){
            Cstr_c sig = app.signatures[selection].name.ToUtf8();
            S_account_settings &acc = app.accounts[ai];
            if(acc.signature_name==sig)
               acc.signature_name.Clear();
            else
               acc.signature_name = sig;
            app.SaveAccounts();
         }
      }
   }
}

//----------------------------

void C_mode_edit_signatures::ProcessInput(S_user_input &ui, bool &redraw){

   C_mode::ProcessInput(ui, redraw);
   int num_sigs = app.signatures.Size();

   if(state==STATE_BROWSE)
      ProcessInputInList(ui, redraw);

#ifdef USE_MOUSE
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){

      if(te_rename){
         if(app.ProcessMouseInTextEditor(*te_rename, ui))
            redraw = true;
      }else{
         dword but_on = 0;
         if(state==STATE_BROWSE){
            if(num_sigs<MAX_SIGNATURES)
               but_on |= 1;
            if(num_sigs)
               but_on |= 8;
         }
         C_scrollbar::E_PROCESS_MOUSE pm = app.ProcessScrollbarMouse(sb, ui);
         switch(pm){
         case C_scrollbar::PM_PROCESSED: redraw = true; break;
         case C_scrollbar::PM_CHANGED:
            redraw = true;
            break;
         default:
            if(state==STATE_BROWSE && ui.CheckMouseInRect(rc)){
               int line = (ui.mouse.y - rc.y + sb.pos) / entry_height;
               int num = app.signatures.Size();
               if(line < num){
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                     menu = app.CreateTouchMenu();
                     menu->AddItem(TXT_EDIT);
                     menu->AddItem(TXT_DELETE);
                     menu->AddItem(TXT_RENAME);
                     app.PrepareTouchMenu(menu, ui);
                  }
               }
            }
         }
      }
   }
#endif

   switch(state){
   case STATE_BROWSE:
      switch(ui.key){
      case K_RIGHT_SOFT:
      case K_BACK:
      case K_ESC:
         app.SaveSignatures();
         app.CloseMode(*this);
         return;

      case K_ENTER:
         Edit();
         redraw = true;
         break;
      }
      break;

   case STATE_RENAME:
      {
         C_text_editor &te = *te_rename;
         S_signature &sig = app.signatures[selection];
         switch(ui.key){
         case K_RIGHT_SOFT:
         case K_ESC:
            te_rename = NULL;
            GetSoftkeyBar()->SetActiveTextEditor(NULL);
            SetState(STATE_BROWSE);
            if(!sig.name.Length() && !sig.body.Length())
               Delete();
            redraw = true;
            break;

         case K_ENTER:
         case K_LEFT_SOFT:
            {
               const wchar *name = te.GetText();
               if(!CheckValidName(name))
                  break;
               if(sig.name.Length()){
                  Cstr_c sig8 = sig.name.ToUtf8();
                              //rename also account signatures
                  bool save = false;
                  for(dword i=0; i<app.NumAccounts(); i++){
                     S_account_settings &acc = app.accounts[i];
                     if(acc.signature_name==sig8){
                        acc.signature_name = Cstr_w(name).ToUtf8();
                        save = true;
                     }
                  }
                  if(save)
                     app.SaveAccounts();
               }
               sig.name = name;

               te_rename = NULL;
               GetSoftkeyBar()->SetActiveTextEditor(NULL);
               SetState(STATE_BROWSE);
                              //switch to body field if it is empty
               if(!sig.body.Length())
                  ProcessMenu(TXT_EDIT, 0);
               redraw = true;
            }
            break;
         }
      }
      break;

   case STATE_EDIT_BODY:
      {
         switch(ui.key){
         case K_BACK:
         case K_ESC:
            BodyEditDone(false);
            redraw = true;
            break;

         case K_RIGHT_SOFT:
            BodyEditDone(true);
            redraw = true;
            break;
         }
      }
      break;
   }
}

//----------------------------

void C_mode_edit_signatures::DrawContents() const{

   app.ClearWorkArea(rc);
   dword col_text = app.GetColor(app.COL_TEXT);
                              //draw entries
   const int max_x = GetMaxX();
   const int max_width = max_x-rc.x - app.fdb.letter_size_x;
   S_rect rc_item;
   int item_index = -1;
   while(BeginDrawNextItem(rc_item, item_index)){
      int x = rc_item.x;
      int y = rc_item.y;

      const S_signature &sig = app.signatures[item_index];

      dword color = col_text;
      if(item_index==selection){
                           //draw selection
         app.DrawSelection(rc_item);
         color = app.GetColor(app.COL_TEXT_HIGHLIGHTED);
      }
                        //draw separator
      if(item_index && (item_index<selection || item_index>selection+1) && y>rc.y)
         app.DrawSeparator(x+app.fdb.letter_size_x*2, max_width-app.fdb.letter_size_x*4, y);

      int tx = x + app.fdb.letter_size_x;
      int ty = y + app.fdb.line_spacing/3;
      bool is_default = false;
      if(!(item_index==selection && state==STATE_RENAME)){
         app.DrawString(sig.name, tx, ty, app.UI_FONT_BIG, is_default ? FF_BOLD : 0, color, -max_name_width);

         Cstr_c sig8 = sig.name.ToUtf8();
         for(dword i=0; i<app.NumAccounts(); i++){
            S_account_settings &acc = app.accounts[i];
            if(acc.signature_name==sig8){
               is_default = true;
               break;
            }
         }
      }
      if(is_default){
         const int SZ = app.fdb.cell_size_y;
         app.DrawCheckbox(rc.x+max_name_width-SZ-app.fdb.cell_size_x/2, y+(entry_height-SZ)/2, SZ, true, false);
      }
      if(item_index==selection && state==STATE_RENAME)
         app.DrawEditedText(*te_rename);
   }
   EndDrawItems();
   app.DrawScrollbar(sb);
}

//----------------------------
/*
void C_mode_edit_signatures::DrawBody() const{

   if(!ctrl_writer){
      app.ClearToBackground(S_rect(0, writer.rc.Bottom(), app.ScrnSX(), 2));
      app.DrawThickSeparator(0, app.ScrnSX(), writer.rc.Bottom());
      if(app.signatures.Size()){
         writer.Draw(app.GetColor(state==STATE_EDIT_BODY ? app.COL_WHITE : app.COL_LIGHT_GREY), (state==STATE_EDIT_BODY));
      }
   }
}
*/
//----------------------------

void C_mode_edit_signatures::Draw() const{

   C_mode::Draw();
   
   const dword c0 = app.GetColor(app.COL_SHADOW), c1 = app.GetColor(app.COL_HIGHLIGHT);
   app.DrawOutline(rc, c0, c1);
   {
      S_rect rc1 = rc;
      rc1.Expand();
      app.DrawOutline(rc1, c1, c0);
   }
   DrawContents();
   /*
   {
      E_TEXT_ID lsk, rsk;
      const C_text_editor *te = NULL;
      switch(state){
      case STATE_RENAME:
         te = te_rename;
         lsk = CheckValidName(te->GetText()) ? TXT_OK : TXT_NULL;
         rsk = TXT_CANCEL;
         break;
      case STATE_EDIT_BODY:
         lsk = TXT_MENU, rsk = TXT_DONE; te = ctrl_writer->GetTextEditor();
         break;
      }
      app.DrawSoftButtonsBar(*this, lsk, rsk, te);
   }
   */
}

//----------------------------

void C_mail_client::SetModeEditSignatures(){

   if(!signatures.Size())
      LoadSignatures();

#ifdef ANDROID_
   if(use_system_ui){
      PC_jni_ProfiMailApplication japp = PC_jni_ProfiMailApplication::Wrap(android_globals.java_app);
      japp->EditSignatures();
      return;
   }
#endif
   C_mode_edit_signatures &mod = *new(true) C_mode_edit_signatures(*this);
   mod.Activate();
}

//----------------------------
