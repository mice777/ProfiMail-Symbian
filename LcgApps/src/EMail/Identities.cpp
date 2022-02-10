#include "..\Main.h"
#include "Main_Email.h"

//----------------------------

#define MAX_IDENTITIES 20

//----------------------------

class C_mode_edit_identities: public C_mode_list<C_mail_client>{
   void SetSelection(int sel);
   void StartEdit();
   void Delete();
   void AddNew();
   bool modified;

   C_mail_client::S_account &acc;

   C_vector<S_identity> identities;
   int default_identity;

   virtual int GetNumEntries() const{
      return identities.size();
   }
   virtual bool IsPixelMode() const{ return true; }

   struct C_config;
   friend struct C_config;
   struct C_config: public C_mode_configuration{
      C_mode_edit_identities &mod_i;
   public:
      C_config(C_application_ui &_app, C_mode_edit_identities &_mod_i):
         C_mode_configuration(_app, NULL),
         mod_i(_mod_i)
      {}
      virtual void Close(bool redraw){
         C_mode_configuration::Close(redraw);
         const S_identity &idn = mod_i.identities[mod_i.selection];
         if(!idn.display_name.Length() && !idn.email.Length() && mod_i.identities.size()>1){
                              //remove empty identity
            mod_i.Delete();
            app.RedrawScreen();
         }else{
            C_vector<Cstr_c> adds;
            if(!ParseRecipients(idn.email, adds) || adds.size()!=1){
               mod_i.StartEdit();
               ((C_client&)app).ShowErrorWindow(TXT_ERROR, TXT_ERR_ACC_EMAIL_INVALID);
            }
         }
      }
   };
public:
   C_mode_edit_identities(C_mail_client &_app, C_mail_client::S_account &_acc):
      C_mode_list<C_mail_client>(_app),
      acc(_acc),
      default_identity(0),
      modified(false)
   {
      identities.push_back(acc.primary_identity);
      identities.insert(identities.end(), acc.identities.Begin(), acc.identities.End());

      InitLayout();
      SetSelection(0);
   }
   virtual void InitLayout();
   virtual void ProcessInput(S_user_input &ui, bool &redraw);
   virtual void ProcessMenu(int itm, dword menu_id);
   virtual void Draw() const;
   virtual void DrawContents() const;

   void Save();
};

//----------------------------

void C_mail_client::SetModeEditIdentities(S_account &acc){

   C_mode_edit_identities &mod = *new(true) C_mode_edit_identities(*this, acc);
   ActivateMode(mod);
}

//----------------------------

static int CompareIdentities(const void *i1, const void *i2, void *context){
   const S_identity &id1 = *(S_identity*)i1,
      &id2 = *(S_identity*)i2;
   return StrCmp(id1.display_name, id2.display_name);
}

//----------------------------

void C_mode_edit_identities::Save(){
   if(modified){
      acc.primary_identity = identities[default_identity];
      identities.erase(&identities[default_identity]);
                              //sort the rest
      QuickSort(identities.begin(), identities.size(), sizeof(S_identity), &CompareIdentities);
      acc.identities.Assign(identities.begin(), identities.end());
      app.SaveAccounts();
   }
}

//----------------------------

void C_mode_edit_identities::InitLayout(){

   const int top = app.GetTitleBarHeight();
   entry_height = app.fdb.line_spacing*2;
   rc = S_rect(0, top, app.ScrnSX(), app.ScrnSY()-top-app.GetSoftButtonBarHeight());
                           //init scrollbar
   const int width = app.GetScrollbarWidth();
   sb.rc = S_rect(rc.Right()-width-1, rc.y+3, width, rc.sy-6);
   sb.total_space = identities.size()*entry_height;
   sb.visible_space = rc.sy;
   sb.SetVisibleFlag();

   EnsureVisible();
}

//----------------------------

void C_mode_edit_identities::SetSelection(int sel){

   selection = Abs(sel);
   if(sel>=0)
      EnsureVisible();
}

//----------------------------

static const S_config_item config_items[] = {
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_DISPLAY_NAME, 40, OffsetOf(S_identity, display_name), C_text_editor::CASE_CAPITAL, 2 },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_ACC_EMAIL, 100, OffsetOf(S_identity, email), C_text_editor::CASE_LOWER },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_REPLY_TO, 100, OffsetOf(S_identity, reply_to_email), C_text_editor::CASE_LOWER },
};

//----------------------------

void C_mode_edit_identities::StartEdit(){

   EnsureVisible();

   C_config &mod_cfg = *new(true) C_config(app, *this);
   mod_cfg.Init(config_items, sizeof(config_items)/sizeof(*config_items), &identities[selection], TXT_EDIT_IDENTITY);
   mod_cfg.InitLayout();
   app.ActivateMode(mod_cfg);
   modified = true;
}

//----------------------------

void C_mode_edit_identities::Delete(){

   identities.erase(&identities[selection]);
   sb.total_space = identities.size();
   sb.SetVisibleFlag();
   SetSelection(Min(selection, identities.size()-1));
   if(default_identity>=identities.size())
      default_identity = 0;
   modified = true;
}

//----------------------------

void C_mode_edit_identities::AddNew(){

                              //add one at end
   identities.push_back(S_identity());
   sb.total_space = identities.size();
   sb.SetVisibleFlag();
   SetSelection(identities.size()-1);
   StartEdit();
   modified = true;
}

//----------------------------

void C_mode_edit_identities::ProcessMenu(int itm, dword menu_id){

   switch(itm){
   case TXT_NEW:
      AddNew();
      break;

   case TXT_EDIT:
      StartEdit();
      break;

   case TXT_DELETE:
      Delete();
      break;

   case TXT_DEFAULT:
      default_identity = selection;
      modified = true;
      break;

   case TXT_BACK:
      Save();
   case TXT_CANCEL:
      app.CloseMode(*this);
      break;
   }
}

//----------------------------

void C_mode_edit_identities::ProcessInput(S_user_input &ui, bool &redraw){

   ProcessInputInList(ui, redraw);

#ifdef USE_MOUSE
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){

      C_scrollbar::E_PROCESS_MOUSE pm = app.ProcessScrollbarMouse(sb, ui);
      switch(pm){
      case C_scrollbar::PM_PROCESSED:
      case C_scrollbar::PM_CHANGED:
         redraw = true;
         break;
      default:
         if(ui.CheckMouseInRect(rc)){
            int line = (ui.mouse.y - rc.y + sb.pos) / entry_height;
            int num = GetNumEntries();
            if(line < num){
               if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                  if(selection != line){
                     SetSelection(-line);
                     redraw = true;
                  }else
                     touch_down_selection = line;
                  menu = app.CreateTouchMenu();
                  menu->AddItem(TXT_EDIT);
                  menu->AddItem(TXT_DELETE);
                  menu->AddSeparator();
                  menu->AddItem(TXT_DEFAULT, selection==default_identity ? C_menu::MARKED : 0);
                  app.PrepareTouchMenu(menu, ui);
               }
               if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
                  if(touch_down_selection==line)
                     ui.key = K_ENTER;
                  touch_down_selection = -1;
               }
            }
         }
      }
   }
#endif

   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      Save();
      app.CloseMode(*this);
      return;

   case K_ENTER:
      StartEdit();
      break;

   case K_LEFT_SOFT:
   case K_MENU:
      {
         menu = CreateMenu();
         menu->AddItem(TXT_EDIT);
         menu->AddItem(TXT_NEW, identities.size()<MAX_IDENTITIES ? 0 : C_menu::DISABLED, NULL, NULL, app.BUT_NEW);
         menu->AddItem(TXT_DELETE, identities.size()>1 ? 0 : C_menu::DISABLED, NULL, NULL, app.BUT_DELETE);
         menu->AddItem(TXT_DEFAULT, selection==default_identity ? C_menu::MARKED : 0);
         menu->AddSeparator();
         if(modified)
            menu->AddItem(TXT_CANCEL);
         menu->AddItem(TXT_BACK);
         app.PrepareMenu(menu);
      }
      return;
   }
}

//----------------------------

void C_mode_edit_identities::DrawContents() const{

   app.ClearWorkArea(rc);
   dword col_text = app.GetColor(app.COL_TEXT);
                              //draw entries
   const int max_x = GetMaxX();
   const int max_width = max_x-rc.x - app.fdb.letter_size_x;
   S_rect rc_item;
   int item_index = -1;
   while(BeginDrawNextItem(rc_item, item_index)){
      int x = rc_item.x + app.fdb.cell_size_x/2;
      int y = rc_item.y;
      const S_identity &idn = identities[item_index];

      dword color = col_text;
      if(item_index==selection){
                           //draw selection
         app.DrawSelection(rc_item);
         color = app.GetColor(app.COL_TEXT_HIGHLIGHTED);
      }
                        //draw separator
      if(item_index && (item_index<selection || item_index>selection+1) && y>rc.y)
         app.DrawSeparator(x+app.fdb.letter_size_x*2, max_width-app.fdb.letter_size_x*4, y);

      app.DrawString(idn.display_name.FromUtf8(), x, y+2, app.UI_FONT_BIG, item_index==default_identity ? FF_BOLD : 0, color, -(max_x - x));
      app.DrawStringSingle(idn.email, x, rc_item.CenterY(), app.UI_FONT_SMALL, 0, color, -(max_x - x));
      if(default_identity==item_index){
         const int SZ = app.fdb.cell_size_y;
         app.DrawCheckbox(max_x-SZ-app.fdb.cell_size_x/2, y+(entry_height-SZ)/2, SZ, true, false);
      }
   }
   EndDrawItems();
   app.DrawScrollbar(sb);
}

//----------------------------

void C_mode_edit_identities::Draw() const{

   app.DrawTitleBar(app.GetText(TXT_ACC_IDENTITIES));
   
   const dword c0 = app.GetColor(app.COL_SHADOW), c1 = app.GetColor(app.COL_HIGHLIGHT);
   app.DrawOutline(rc, c0, c1);
   {
      S_rect rc1 = rc;
      rc1.Expand();
      app.DrawOutline(rc1, c1, c0);
   }
   DrawContents();
   app.ClearSoftButtonsArea(rc.Bottom() + 2);
   {
      E_TEXT_ID lsk = TXT_MENU, rsk = TXT_BACK;
      app.DrawSoftButtonsBar(*this, lsk, rsk);
   }
}

//----------------------------
