#include "..\Main.h"
#include "Main_Email.h"
#include "..\FileBrowser.h"
#include <Xml.h>

//----------------------------

const int RULES_SAVE_VERSION = 292;

//----------------------------

const dword C_mail_client::rule_colors[NUM_RULE_COLORS] = {
   0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff
};

//----------------------------

Cstr_w C_mail_client::GetRulesFilename() const{
   Cstr_w fn; fn<<mail_data_path <<DATA_PATH_PREFIX <<L"Rules.xml";
   return fn;
}

//----------------------------

void C_mail_client::SaveRules() const{

   C_file fl;
   if(fl.Open(GetRulesFilename(), C_file::FILE_WRITE|C_file::FILE_WRITE_CREATE_PATH)){
      C_xml_build xml("Rules");
      xml.root.AddAttributeValue("version", RULES_SAVE_VERSION);
      for(dword i=0; i<rules.Size(); i++){
         const S_rule &r = rules[i];
         C_xml_build::C_element &el = xml.root.AddChild("Rule");
         el.AddAttributeValue("name", r.name);
         el.AddAttributeValue("operation", (r.flags&r.FLG_OP_OR) ? "OR" : "AND");
         if(!(r.flags&r.FLG_ACTIVE))
            el.AddChild("disabled");

         for(dword j=0; j<r.NumConds(); j++){
            const S_rule::S_condition &c = r.conds[j];
            C_xml_build::C_element &elc = el.AddChild("Condition");
            elc.AddAttributeValue("type", c.cond);
            if(c.param.Length())
               elc.AddAttributeValue("string", c.param);
            if(c.size)
               elc.AddAttributeValue("int", c.size);
         }
         C_xml_build::C_element &el_a = el.AddChild("Action");
         el_a.AddAttributeValue("type", r.action);
         if(r.action_param.Length())
            el_a.AddAttributeValue("string", r.action_param);
         if(r.action_param_i)
            el_a.AddAttributeValue("int", r.action_param_i);
      }
      xml.Write(fl, false, true);
   }
}

//----------------------------

void C_mail_client::LoadRules(){

   C_file fl;
   if(fl.Open(GetRulesFilename())){
      C_xml xml;
      if(xml.Parse(fl) && xml.GetRoot()=="Rules"){
         Cstr_c ver = xml.GetRoot().FindAttributeValue("version");
         int ver_i;
         if(ver.Length() && ver>>ver_i && ver_i>=RULES_SAVE_VERSION){
            C_vector<S_rule> tmp;
            for(const C_xml::C_element *el=xml.GetFirstChild(); el; el=el->GetNextSibling()){
               if(*el=="Rule"){
                  S_rule r;
                  r.flags = r.FLG_ACTIVE;
                  r.action_param_i = 0;
                  const C_vector<C_xml::C_element::S_attribute> &atts = el->GetAttributes();
                  for(int i=0; i<atts.size(); i++){
                     const C_xml::C_element::S_attribute &att = atts[i];
                     if(att=="name"){
                        r.name = xml.DecodeString(att.value);
                     }else
                     if(att=="operation"){
                        if(!StrCmp(att.value, "OR"))
                           r.flags |= r.FLG_OP_OR;
                     }else assert(0);
                  }
                  const C_vector<C_xml::C_element> &chlds = el->GetChildren();
                  for(int i=0; i<chlds.size(); i++){
                     const C_xml::C_element &c = chlds[i];
                     if(c=="disabled"){
                        r.flags &= ~r.FLG_ACTIVE;
                     }else if(c=="Condition"){
                        if(r.NumConds() < r.MAX_CONDS){
                           S_rule::S_condition &cond = r.conds[r.NumConds()];
                           ++r.flags;
                           cond.size = 0;
                           const C_vector<C_xml::C_element::S_attribute> &atts1 = c.GetAttributes();
                           for(int j=0; j<atts1.size(); j++){
                              const C_xml::C_element::S_attribute &att = atts1[j];
                              if(att=="type"){
                                 int ii = att.IntVal();
                                 if(ii<cond.LAST)
                                    cond.cond = (S_rule::S_condition::E_CONDITION)ii;
                              }else if(att=="string"){
                                 cond.param = xml.DecodeString(att.value);
                              }else if(att=="int"){
                                 cond.size = att.IntVal();
                              }else assert(0);
                           }
                        }
                     }else if(c=="Action"){
                        const C_vector<C_xml::C_element::S_attribute> &atts1 = c.GetAttributes();
                        for(int j=0; j<atts1.size(); j++){
                           const C_xml::C_element::S_attribute &att = atts1[j];
                           if(att=="type"){
                              int ii = att.IntVal();
                              if(ii<r.ACT_LAST)
                                 r.action = (S_rule::E_ACTION)ii;
                           }else if(att=="string"){
                              r.action_param = xml.DecodeString(att.value);
                           }else if(att=="int"){
                              r.action_param_i = att.IntVal();
                           }else assert(0);
                        }
                     }else assert(0);
                  }
                  if(r.name.Length())
                     tmp.push_back(r);
               }else assert(0);
            }
            rules.Assign(tmp.begin(), tmp.end());
         }
      }
   }else{
                              //add predefined rules
      static const struct S_init{
         const wchar *name;
         C_mail_client::S_rule::E_ACTION action;
         int action_param;
         C_mail_client::S_rule::S_condition::E_CONDITION cond;
         int cond_param;
         bool active;
      } inits[] = {
         { L"Truncate over 20KB", S_rule::ACT_DOWNLOAD_PARTIAL_BODY, 20, S_rule::S_condition::SIZE_MORE, 20, false },
         { L"Download small", S_rule::ACT_DOWNLOAD_BODY, 0, S_rule::S_condition::SIZE_LESS, 10, false },
      };
      int n = sizeof(inits)/sizeof(*inits);
      rules.Resize(n);
      for(int i=0; i<n; i++){
         S_rule &r = rules[i];
         const S_init &ii = inits[i];
         r.name = ii.name;
         r.action = ii.action;
         r.action_param_i = ii.action_param;
         r.conds[0].cond = ii.cond;
         r.conds[0].size = ii.cond_param;
         if(!ii.active)
            r.flags &= ~S_rule::FLG_ACTIVE;
      }
   }
}

//----------------------------

void C_mail_client::S_rule::S_condition::Save(C_file &ck) const{

   ck.WriteByte(byte(cond));
   ck.WriteDword(size);
   file_utils::WriteString(ck, param);
}

//----------------------------

bool C_mail_client::S_rule::S_condition::Load(C_file &ck){

   byte b;
   if(!ck.ReadByte(b))
      return false;
   cond = (E_CONDITION)b;
   if(!ck.ReadDword((dword&)size))
      return false;
   return file_utils::ReadString(ck, param);
}

//----------------------------

void C_mail_client::S_rule::SetActionDefaults(){

   switch(action){
   case ACT_MOVE_TO_FOLDER:
      action_param.Clear();
      break;
   case ACT_PLAY_SOUND:
      action_param.Clear();
      action_param_i = 5;     //volume
      break;
   case ACT_DOWNLOAD_PARTIAL_BODY:
      action_param_i = 5;
      break;
   case ACT_SET_COLOR:
      action_param_i = 0;
      break;
   }
}

//----------------------------

bool C_mail_client::S_rule::S_condition::CheckStringMatch(const Cstr_w &what, const Cstr_w &where, bool beg_only){

   int num_c = where.Length() - what.Length();
   if(num_c < 0 || !where.Length())
      return false;
   const wchar *wp = where;
   const wchar *p = what;

   for(int i=0; i<=num_c; i++, ++wp){
                              //compare the strings
      const wchar *s1 = p, *s2 = wp;
      while(true){
         dword c1 = text_utils::LowerCase(*s1++);
         if(!c1)
            return true;
         dword c2 = text_utils::LowerCase(*s2++);
         if(c1!=c2)
            break;
      }
      if(beg_only)
         break;
   }
   return false;
}

//----------------------------

bool C_mail_client::S_rule::S_condition::CheckStringMatch(const Cstr_w &where, bool beg_only) const{

                              //avoid detecting string match with empty string
   if(!param.Length())
      return false;
   return CheckStringMatch(param, where, beg_only);
}

//----------------------------

bool C_mail_client::S_rule::S_condition::CheckStringMatch(const Cstr_c &where, bool beg_only) const{
   Cstr_w tmp; tmp.Copy(where);
   return CheckStringMatch(tmp, beg_only);
}

//----------------------------

void C_mail_client::SetModeRulesBrowser(){

   if(!rules.Size())
      LoadRules();

   C_mode_rules_browser &mod = *new(true) C_mode_rules_browser(*this);
   InitLayoutRulesBrowser(mod);
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::InitLayoutRulesBrowser(C_mode_rules_browser &mod){

   const int border = 2;
   const int top = GetTitleBarHeight();
   mod.rc = S_rect(border, top, ScrnSX()-border*2, ScrnSY()-top-GetSoftButtonBarHeight()-border);
                              //compute # of visible lines, and resize rectangle to whole lines
   C_scrollbar &sb = mod.sb;
   const int entry_size = fdb.line_spacing*2;
   sb.visible_space = mod.rc.sy / entry_size;
   mod.rc.y += (mod.rc.sy - sb.visible_space*entry_size)/2;
   mod.rc.sy = sb.visible_space * entry_size;

   sb.total_space = rules.Size();
                              //init scrollbar
   const int width = GetScrollbarWidth();
   sb.rc = S_rect(mod.rc.Right()-width-3, mod.rc.y+3, width, mod.rc.sy-6);
   sb.SetVisibleFlag();

   mod.max_text_x = sb.rc.x - fdb.cell_size_y - 10;

   mod.EnsureVisible();

   if(mod.te){
      int x = fdb.letter_size_x*2;
      mod.te->SetRect(S_rect(x, mod.rc.y + 1 + entry_size*(mod.selection-mod.top_line), mod.max_text_x-x, fdb.cell_size_y+1));
   }
}

//----------------------------

void C_mail_client::Rules_DeleteSelected(C_mode_rules_browser &mod){

   int sz = rules.Size();
   for(int i=mod.selection; ++i < sz; )
      rules[i-1] = rules[i];
   --sz;
   rules.Resize(sz);
   C_scrollbar &sb = mod.sb;
   --sb.total_space;
   sb.SetVisibleFlag();
   mod.selection = Min(mod.selection, sz-1);
   SaveRules();
   mod.EnsureVisible();
}

//----------------------------

void C_mail_client::Rules_ToggleSelected(C_mode_rules_browser &mod){

   rules[mod.selection].flags ^= S_rule::FLG_ACTIVE;
   SaveRules();
}

//----------------------------

bool C_mail_client::Rules_MoveSelected(C_mode_rules_browser &mod, bool up){

   if(up){
      if(!mod.selection)
         return false;
      --mod.selection;
      Swap(rules[mod.selection], rules[mod.selection+1]);
   }else{
      if(mod.selection>=int(rules.Size()-1))
         return false;
      Swap(rules[mod.selection], rules[mod.selection+1]);
      ++mod.selection;
   }
   SaveRules();
   mod.EnsureVisible();
   return true;
}

//----------------------------

void C_mail_client::RulesProcessMenu(C_mode_rules_browser &mod, int itm, dword menu_id){

   switch(itm){
   case TXT_ENABLE:
   case TXT_DISABLE:
      if(rules.Size())
         Rules_ToggleSelected(mod);
      break;

   case TXT_NEW:
      {
                           //add one entry at end
         int sz = rules.Size();
         rules.Resize(sz+1);
         C_scrollbar &sb = mod.sb;
         ++sb.total_space;
         sb.SetVisibleFlag();
         mod.selection = sz;
         mod.EnsureVisible();
         mod.te = CreateTextEditor(0, UI_FONT_BIG, FF_BOLD, NULL, 100); mod.te->Release();
         mod.te->SetInitText(Cstr_w());
         mod.adding_new = true;

         int x = fdb.letter_size_x*2;
         const int entry_size = fdb.line_spacing*2;
         mod.te->SetRect(S_rect(x, mod.rc.y + 1 + entry_size*(mod.selection-mod.top_line), mod.max_text_x-x, fdb.cell_size_y+1));
      }
      break;

   case TXT_RENAME:
      {
         mod.te = CreateTextEditor(0, UI_FONT_BIG, FF_BOLD, NULL, 100); mod.te->Release();
         mod.adding_new = false;

         int x = fdb.letter_size_x*2;
         const int entry_size = fdb.line_spacing*2;
         mod.te->SetRect(S_rect(x, mod.rc.y + 1 + entry_size*(mod.selection-mod.top_line), mod.max_text_x-x, fdb.cell_size_y+1));

         mod.te->SetInitText(rules[mod.selection].name);
         MakeSureCursorIsVisible(*mod.te);
      }
      break;

   case TXT_EDIT:
      SetModeRuleEditor(rules[mod.selection]);
      break;

   case TXT_DELETE:
      if(rules.Size())
         CreateQuestion(*this, TXT_Q_DELETE_RULE, rules[mod.selection].name, new(true) C_question_del_rule(*this, mod), true);
      break;

   case TXT_MOVE_UP:
      Rules_MoveSelected(mod, true);
      break;

   case TXT_MOVE_DOWN:
      Rules_MoveSelected(mod, false);
      break;

   case TXT_BACK:
      CloseMode(mod);
      break;
   }
}

//----------------------------

void C_mail_client::RulesBrowserProcessInput(C_mode_rules_browser &mod, S_user_input &ui, bool &redraw){

#ifdef USE_MOUSE
   if(!ProcessMouseInSoftButtons(ui, redraw)){
      if(mod.te){
         if(ProcessMouseInTextEditor(*mod.te, ui))
            redraw = true;
      }else{
         C_scrollbar::E_PROCESS_MOUSE pm = ProcessScrollbarMouse(mod.sb, ui);
         switch(pm){
         case C_scrollbar::PM_PROCESSED: redraw = true; break;
         case C_scrollbar::PM_CHANGED:
            //mod.top_line = mod.sb.pos;
            redraw = true;
            break;
         default:
            if(ui.CheckMouseInRect(mod.rc)){
               const int entry_size = fdb.line_spacing*2;
               int line = (ui.mouse.y - mod.rc.y) / entry_size;
               line += mod.top_line;
               if(line < int(rules.Size())){
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                     if(mod.selection != line){
                        mod.selection = line;
                        redraw = true;
                     }else
                        mod.touch_down_selection = line;

                     mod.menu = CreateTouchMenu();
                     mod.menu->AddItem(TXT_EDIT);
                     mod.menu->AddItem(TXT_DELETE, 0, 0, 0, BUT_DELETE);
                     mod.menu->AddItem(TXT_RENAME);
                     mod.menu->AddSeparator();
                     mod.menu->AddItem((rules[mod.selection].flags&S_rule::FLG_ACTIVE) ? TXT_DISABLE : TXT_ENABLE);
                     PrepareTouchMenu(mod.menu, ui);
                  }
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
                     if(mod.touch_down_selection==line){
                        if(ui.mouse.x < mod.max_text_x){
                           ui.key = K_ENTER;
                        }else{
                                    //toggle checkbox
                           Rules_ToggleSelected(mod);
                           redraw = true;
                        }
                     }
                     mod.touch_down_selection = -1;
                  }
               }
            }
         }
      }
   }
#endif

   int num_r = rules.Size();

   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      if(mod.te){
                              //cancel creating/renaming entry
         mod.te = NULL;
         if(mod.adding_new)
            Rules_DeleteSelected(mod);
         redraw = true;
      }else{
         CloseMode(mod);
         return;
      }
      break;

   case K_ENTER:
   case K_LEFT_SOFT:
   case K_MENU:
      if(!mod.te){
         if(ui.key==K_ENTER){
                              //edit rule
            if(num_r){
               SetModeRuleEditor(rules[mod.selection]);
               return;
            }
            break;
         }else{
            mod.menu = mod.CreateMenu();
            if(num_r){
               mod.menu->AddItem(TXT_EDIT, 0, ok_key_name);
               mod.menu->AddItem((rules[mod.selection].flags&S_rule::FLG_ACTIVE) ? TXT_DISABLE : TXT_ENABLE, 0, "[3]", "[E]");
               mod.menu->AddSeparator();
               mod.menu->AddItem(TXT_MOVE_UP, mod.selection ? 0 : C_menu::DISABLED);
               mod.menu->AddItem(TXT_MOVE_DOWN, mod.selection<int(rules.Size()-1) ? 0 : C_menu::DISABLED);
               mod.menu->AddSeparator();
            }
            mod.menu->AddItem(TXT_NEW);
            mod.menu->AddItem(TXT_DELETE, (!num_r ? C_menu::DISABLED : 0), delete_key_name);
            mod.menu->AddItem(TXT_RENAME, (!num_r ? C_menu::DISABLED : 0));
            mod.menu->AddSeparator();
            mod.menu->AddItem(TXT_BACK);
            PrepareMenu(mod.menu);
            return;
         }
      }else{
                              //don't allow empty name
         if(!mod.te->GetTextLength())
            break;
         S_rule &rul = rules[mod.selection];
         rul.name = mod.te->GetText();
         SaveRules();
         mod.te = NULL;
                              //done renaming / adding new
         if(mod.adding_new){
            SetModeRuleEditor(rules[mod.selection]);
            return;
         }
         redraw = true;
      }
      break;

   case '3':
   case 'e':
      if(rules.Size()){
         Rules_ToggleSelected(mod);
         redraw = true;
      }
      break;

   case K_DEL:
#ifdef _WIN32_WCE
   case 'd':
#endif
      if(!mod.te && num_r)
         CreateQuestion(*this, TXT_Q_DELETE_RULE, rules[mod.selection].name, new(true) C_question_del_rule(*this, mod), true);
      break;

   case K_CURSORUP:
      if(!mod.te && num_r){
         if(ui.key_bits&GKEY_SHIFT)
            Rules_MoveSelected(mod, true);
         else{
            if(!mod.selection)
               mod.selection = num_r;
            --mod.selection;
            mod.EnsureVisible();
         }
         redraw = true;
      }
      break;

   case K_CURSORDOWN:
      if(!mod.te && num_r){
         if(ui.key_bits&GKEY_SHIFT)
            Rules_MoveSelected(mod, false);
         else{
            if(++mod.selection == num_r)
               mod.selection = 0;
            mod.EnsureVisible();
         }
         redraw = true;
      }
      break;
   }
}

//----------------------------

void C_mail_client::DrawRulesBrowser(const C_mode_rules_browser &mod){

   dword col_text = GetColor(COL_TEXT);
   ClearWorkArea(mod.rc);

   DrawTitleBar(GetText(TXT_RULES), mod.rc.y-2);
   ClearSoftButtonsArea(mod.rc.Bottom() + 2);

   DrawEtchedFrame(mod.rc);
   int num_r = rules.Size();
   if(num_r){
                           //draw entries
      int max_x = mod.sb.visible ? mod.sb.rc.x-1 : mod.rc.Right();
      int x = mod.rc.x;
      int y = mod.rc.y;
      int max_width = max_x-mod.rc.x - fdb.letter_size_x;
      int sep_width = max_width;

      for(int li=0; li<mod.sb.visible_space; li++){
         int ai = mod.top_line + li;
         if(ai >= num_r)
            break;
         const S_rule &rul = rules[ai];

         dword color = col_text;
         if(ai==mod.selection){
            S_rect rc(x, y, max_x-mod.rc.x, fdb.line_spacing*2);
            DrawSelection(rc);
            color = GetColor(COL_TEXT_HIGHLIGHTED);
         }
                              //draw separator
         if(li && (ai<mod.selection || ai>mod.selection+1))
            DrawSeparator(x+fdb.letter_size_x*1, sep_width-fdb.letter_size_x*2, y);

                              //draw name
         if(!(rul.flags&rul.FLG_ACTIVE))
            color = MulAlpha(color, 0x4000);
         //DrawString(GetText(TXT_NAME), x + fdb.letter_size_x, y + 1, UI_FONT_BIG, 0, color);
         const int frame_size = fdb.cell_size_y;
         int xx = x+fdb.letter_size_x*2;
         int name_w = mod.max_text_x - xx;
         DrawString(rul.name, xx, y + 1, UI_FONT_BIG, FF_BOLD, color, -name_w);
         if(ai==mod.selection){
            if(mod.te)
               DrawEditedText(*mod.te);
         }else{
            //DrawString(con.e_mail, x + fdb.letter_size_x, y + fdb.line_spacing, UI_FONT_BIG, 0, 0xff000000, -max_width);
         }
         {
                              //draw 'active' checkbox
            DrawCheckbox(xx+name_w+2, y+fdb.line_spacing-frame_size/2, frame_size, (rul.flags&S_rule::FLG_ACTIVE));
            /*
            xx = xx + name_w + frame_size/2 + 2;
            int yy = y + fdb.line_spacing;
            if(rul.flags&S_rule::FLG_ACTIVE)
               DrawCheckbox(xx, yy, frame_size);
               */
         }
         y += fdb.line_spacing * 2;
      }
      DrawScrollbar(mod.sb);
   }
   {
      E_TEXT_ID lsk, rsk;
      if(mod.te){
         lsk = TXT_OK, rsk = TXT_CANCEL;
      }else{
         lsk = TXT_MENU, rsk = TXT_BACK;
      }
      DrawSoftButtonsBar(mod, lsk, rsk, mod.te);
   }
   SetScreenDirty();
}

//----------------------------
//----------------------------

void C_mail_client::SetModeRuleEditor(S_rule &rul){

   C_mode_rule_editor &mod = *new(true) C_mode_rule_editor(*this, rul);

                              //conds + <add cond> + action
   mod.sb.total_space = rul.NumConds() + 2;

   InitLayoutRuleEditor(mod);
   ActivateMode(mod);
}

//----------------------------

void C_mail_client::InitLayoutRuleEditor(C_mode_rule_editor &mod){

   const int border = 2;
   const int top = GetTitleBarHeight();
   mod.rc = S_rect(border, top, ScrnSX()-border*2, ScrnSY()-top);
   mod.rc.sy -= GetSoftButtonBarHeight()+border;
                              //compute # of visible lines, and resize rectangle to whole lines
   C_scrollbar &sb = mod.sb;
   const int entry_size = fdb.line_spacing*2;
   sb.visible_space = mod.rc.sy / entry_size;
   mod.rc.y += (mod.rc.sy - sb.visible_space*entry_size)/2;
   mod.rc.sy = sb.visible_space * entry_size;

                              //init scrollbar
   const int width = GetScrollbarWidth();
   sb.rc = S_rect(mod.rc.Right()-width-3, mod.rc.y+3, width, mod.rc.sy-6);
   sb.SetVisibleFlag();

   mod.and_or_if_width = fdb.letter_size_x +
      Max(GetTextWidth(GetText(TXT_RULE_IF), UI_FONT_BIG), Max(GetTextWidth(GetText(TXT_RULE_AND), UI_FONT_BIG), GetTextWidth(GetText(TXT_RULE_OR), UI_FONT_BIG)));

   mod.EnsureVisible();
   if(mod.te){
      S_rect trc = mod.te->GetRect();
      trc.y = mod.rc.y + fdb.line_spacing + entry_size*(mod.selection-mod.top_line);
      mod.te->SetRect(trc);
   }
}

//----------------------------

void C_mail_client::Rule_AddCond(C_mode_rule_editor &mod){

   mod.selection = mod.rul.NumConds();
   ++mod.rul.flags;
   mod.need_save = true;
   ++mod.sb.total_space;
   mod.sb.SetVisibleFlag();
   mod.EnsureVisible();
}

//----------------------------

void C_mail_client::Rule_DeleteSelectedCond(C_mode_rule_editor &mod){

   assert(mod.rul.NumConds()>1);
   for(dword i=mod.selection; i<mod.rul.NumConds()-1; i++)
      mod.rul.conds[i] = mod.rul.conds[i+1];
   --mod.rul.flags;
   --mod.sb.total_space;
   mod.sb.SetVisibleFlag();
   mod.need_save = true;
}

//----------------------------

/*
bool C_mail_client::Rule_MoveSelectedCond(C_mode_rule_editor &mod, bool up){

   if(up){
      if(!mod.selection)
         return false;
      --mod.selection;
      Swap(mod.rul.conds[mod.selection], mod.rul.conds[mod.selection+1]);
   }else{
      if(mod.selection>=mod.rul.NumConds()-1)
         return false;
      Swap(mod.rul.conds[mod.selection], mod.rul.conds[mod.selection+1]);
      ++mod.selection;
   }
   mod.need_save = true;
   mod.EnsureVisible();
   return true;
}
*/

//----------------------------

void C_mail_client::RuleSoundEntered(const Cstr_w &fn){

   C_mode_rule_editor &mod_rule = (C_mode_rule_editor&)*mode;
   S_rule &rul = mod_rule.rul;

   simple_snd_plr = NULL;
   simple_snd_plr = C_simple_sound_player::Create(fn,
      Max(1ul, Min(10ul, rul.action_param_i))
      );
   if(simple_snd_plr){
      simple_snd_plr->Release();
      rul.action_param = fn;
      RedrawScreen();
      mod_rule.need_save = true;
      ManageTimer();
   }
}

//----------------------------

bool C_mail_client::RuleSoundSelectCallback(const Cstr_w *file, C_vector<Cstr_w> *files){

   if(!file)
      return false;
   C_client_file_mgr::C_mode_file_browser &mod = (C_client_file_mgr::C_mode_file_browser&)*mode;
   Cstr_w fn = GetAudioAlertRealFileName(mod, *file);
   C_client_file_mgr::FileBrowser_Close(this, mod);

   RuleSoundEntered(fn);
   return true;
}

//----------------------------

void C_mail_client::Rule_BeginEdit(C_mode_rule_editor &mod){

   mod.EnsureVisible();
   mod.te = NULL;
   const dword sx = ScrnSX();

   S_rule &rul = mod.rul;
   int num_c = rul.NumConds();

   int te_x, te_sx;
   if(mod.selection<num_c){
      const S_rule::S_condition &cond = mod.rul.conds[mod.selection];
      switch(cond.cond){
      case S_rule::S_condition::SIZE_LESS:
      case S_rule::S_condition::SIZE_MORE:
      case S_rule::S_condition::AGE_LESS:
      case S_rule::S_condition::AGE_MORE:
      case S_rule::S_condition::SPAM_SCORE_LESS:
      case S_rule::S_condition::SPAM_SCORE_MORE:
         {
            Cstr_w str; str<<cond.size;
            mod.te = CreateTextEditor(TXTED_NUMERIC, UI_FONT_BIG, 0, str, 8); mod.te->Release();
            te_sx = fdb.letter_size_x*8;
            te_x = sx/2 - te_sx;
         }
         break;
      case S_rule::S_condition::SENDER_IN_CONTACTS:
      case S_rule::S_condition::SENDER_NOT_IN_CONTACTS:
         return;
      default:
         mod.te = CreateTextEditor(0, UI_FONT_BIG, 0, cond.param, 100); mod.te->Release();
         mod.te->SetCase(C_text_editor::CASE_UPPER|C_text_editor::CASE_LOWER, C_text_editor::CASE_LOWER);
         te_x = fdb.letter_size_x*2;
         te_sx = mod.sb.rc.x - te_x - fdb.letter_size_x;
      }
   }else{
                              //action
      switch(rul.action){
      case S_rule::ACT_MOVE_TO_FOLDER:
         mod.te = CreateTextEditor(0, UI_FONT_BIG, 0, rul.action_param, 100); mod.te->Release();
         mod.te->SetCase(C_text_editor::CASE_ALL, C_text_editor::CASE_LOWER);
         te_x = fdb.letter_size_x*2;
         te_sx = mod.sb.rc.x - te_x - fdb.letter_size_x;
         break;
      case S_rule::ACT_DOWNLOAD_PARTIAL_BODY:
         {
            Cstr_w s;
            if(rul.action_param_i)
               s<<rul.action_param_i;
            mod.te = CreateTextEditor(TXTED_NUMERIC, UI_FONT_BIG, 0, s, 4); mod.te->Release();
            te_x = fdb.letter_size_x*2;
            te_sx = mod.sb.rc.x - te_x - fdb.letter_size_x;
         }
         break;
      case S_rule::ACT_PLAY_SOUND:
         SelectAudioAlert((C_client_file_mgr::C_mode_file_browser::t_OpenCallback)&C_mail_client::RuleSoundSelectCallback, rul.action_param);
         return;
      default:
         return;
      }
   }
   const int entry_size = fdb.line_spacing * 2;
   mod.te->SetRect(S_rect(te_x, mod.rc.y + fdb.line_spacing + entry_size*(mod.selection-mod.top_line), te_sx, fdb.cell_size_y+1));
   MakeSureCursorIsVisible(*mod.te);
}

//----------------------------

void C_mail_client::Rule_EndEdit(C_mode_rule_editor &mod){

   S_rule &rul = mod.rul;
   int num_c = rul.NumConds();
   const wchar *wp = mod.te->GetText();

   if(mod.selection<num_c){
      S_rule::S_condition &cond = mod.rul.conds[mod.selection];
      switch(cond.cond){
      case S_rule::S_condition::SIZE_LESS:
      case S_rule::S_condition::SIZE_MORE:
      case S_rule::S_condition::AGE_LESS:
      case S_rule::S_condition::AGE_MORE:
      case S_rule::S_condition::SPAM_SCORE_LESS:
      case S_rule::S_condition::SPAM_SCORE_MORE:
         {
            char buf[12], *cp = buf;
            while(*wp)
               *cp++ = char(*wp++);
            *cp = 0;
            cp = buf;
            int n;
            if(text_utils::ScanDecimalNumber((const char*&)cp, n)){
               cond.size = Min(n, (int)0xffff);
               //cond.flags &= ~cond.FLG_SIZE_MASK;
               //cond.flags |= n;
            }
         }
         break;
      default:
         cond.param = wp;
      }
   }else{
      switch(rul.action){
      case S_rule::ACT_MOVE_TO_FOLDER:
         rul.action_param = wp;
         break;
      case S_rule::ACT_DOWNLOAD_PARTIAL_BODY:
         {
            Cstr_c s; s.Copy(wp);
            const char *cp = s;
            int i;
            if(!text_utils::ScanDecimalNumber(cp, i))
               i = 0;
            rul.action_param_i = i;
         }
         break;
      }
   }
   mod.te = NULL;
   mod.need_save = true;
}

//----------------------------

void C_mail_client::Rule_CloseEditor(C_mode_rule_editor &mod){

   bool can_close = true;
                              //check if rule is valid
   switch(mod.rul.action){
   case S_rule::ACT_MOVE_TO_FOLDER:
      if(!mod.rul.action_param.Length())
         can_close = false;
      break;
   case S_rule::ACT_DOWNLOAD_PARTIAL_BODY:
      if(!mod.rul.action_param_i)
         can_close = false;
      break;
   }
   if(can_close){
      if(mod.need_save){
         mod.save_to = mod.rul;
         SaveRules();
      }
      CloseMode(mod);
   }else{
      mod.selection = mod.rul.NumConds()+1;
      mod.EnsureVisible();
      Rule_BeginEdit(mod);
      RedrawScreen();
   }
}

//----------------------------

void C_mail_client::RuleEditProcessMenu(C_mode_rule_editor &mod, int itm, dword menu_id){

   switch(itm){
   case TXT_EDIT:
      Rule_BeginEdit(mod);
      break;

   case TXT_ADD_COND:
      Rule_AddCond(mod);
      break;

   case TXT_DELETE_COND:
      CreateQuestion(*this, TXT_Q_DELETE_CONDITION, L"", new(true) C_question_del_cond(*this, mod), true);
      break;

      /*
   case TXT_MOVE_UP:
      Rule_MoveSelectedCond(mod, true);
      break;

   case TXT_MOVE_DOWN:
      Rule_MoveSelectedCond(mod, false);
      break;
      */

   case TXT_RULE_OPERATOR:
      {
         bool is_or = (mod.rul.flags&mod.rul.FLG_OP_OR);
         mod.menu = mod.CreateMenu();
         mod.menu->AddItem(TXT_RULE_AND, !is_or ? C_menu::MARKED : 0);
         mod.menu->AddItem(TXT_RULE_OR, is_or ? C_menu::MARKED : 0);
         PrepareMenu(mod.menu);
      }
      break;

   case TXT_RULE_AND:
   case TXT_RULE_OR:
      {
         mod.rul.flags &= ~mod.rul.FLG_OP_OR;
         if(itm==TXT_RULE_OR)
            mod.rul.flags |= mod.rul.FLG_OP_OR;
         mod.need_save = true;
      }
      break;

   case TXT_CANCEL:
      CloseMode(mod);
      break;

   case TXT_CFG_ALERT_VOLUME:
      {
         mod.menu = mod.CreateMenu();
         dword vol = Max(1ul, Min(mod.rul.action_param_i, 10ul));
         for(int i=1; i<=10; i++){
            Cstr_w s;
            s<<i;
            mod.menu->AddItem(s, vol==i ? C_menu::MARKED : 0);
         }
         PrepareMenu(mod.menu);
      }
      break;

   case TXT_DONE:
      Rule_CloseEditor(mod);
      break;

   default:
      if(itm>=0x10000){
         mod.rul.action_param_i = itm-0x10000+1;
         simple_snd_plr = NULL;
         simple_snd_plr = C_simple_sound_player::Create(mod.rul.action_param, mod.rul.action_param_i);
         simple_snd_plr->Release();
         ManageTimer();

         mod.need_save = true;
      }
   }
}

//----------------------------

void C_mail_client::RuleEditProcessInput(C_mode_rule_editor &mod, S_user_input &ui, bool &redraw){

   S_rule &rul = mod.rul;
   const int num_c = rul.NumConds();
#ifdef USE_MOUSE
   if(!ProcessMouseInSoftButtons(ui, redraw)){
      if(mod.te){
         if(ProcessMouseInTextEditor(*mod.te, ui))
            redraw = true;
      }else{
         C_scrollbar::E_PROCESS_MOUSE pm = ProcessScrollbarMouse(mod.sb, ui);
         switch(pm){
         case C_scrollbar::PM_PROCESSED: redraw = true; break;
         case C_scrollbar::PM_CHANGED:
            //mod.top_line = mod.sb.pos;
            redraw = true;
            break;
         default:
            if(ui.CheckMouseInRect(mod.rc)){
               const int entry_size = fdb.line_spacing*2;
               int line = (ui.mouse.y - mod.rc.y) / entry_size;
               line += mod.top_line;
               if(line < num_c+2){
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
                     if(mod.selection != line){
                        mod.selection = line;
                        redraw = true;
                     }else
                        mod.touch_down_selection = line;
                     if(line<num_c){
                        mod.menu = CreateTouchMenu();
                        mod.menu->AddItem(TXT_EDIT);
                        mod.menu->AddItem(TXT_DELETE_COND, num_c>1 ? 0 : C_menu::DISABLED, 0, 0, BUT_DELETE);
                        PrepareTouchMenu(mod.menu, ui);
                     }
                  }
                  if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
                     if(mod.touch_down_selection==line){
                              //simulate left/right key
                        if(ui.mouse.x<mod.rc.sx/3){
                           ui.key = K_CURSORLEFT;
                        }else
                        if(ui.mouse.x>=mod.rc.sx*2/3){
                           ui.key = K_CURSORRIGHT;
                        }else
                           ui.key = K_ENTER;
                     }
                     mod.touch_down_selection = -1;
                  }
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
      if(mod.te){
                              //cancel editing condition
         mod.te = NULL;
         redraw = true;
      }else{
         Rule_CloseEditor(mod);
         return;
      }
      break;

   case K_ENTER:
      if(mod.te){
                              //confirm editing
         Rule_EndEdit(mod);
         redraw = true;
         break;
      }
      if(mod.selection==num_c){
                           //add new rule
         if(num_c<rul.MAX_CONDS){
            Rule_AddCond(mod);
            redraw = true;
         }
      }else{
                           //edit condition
         Rule_BeginEdit(mod);
         if(mode->Id()!=mod.Id())
            return;

         if(mod.selection > num_c){
            switch(rul.action){
            case S_rule::ACT_SET_COLOR:
               ++rul.action_param_i %= NUM_RULE_COLORS;
               mod.need_save = true;
               break;
            }
         }
         redraw = true;
      }
      break;

   case K_LEFT_SOFT:
   case K_MENU:
      if(!mod.te){
         mod.menu = mod.CreateMenu();

         if(mod.selection<num_c || (mod.selection>num_c && rul.action==rul.ACT_MOVE_TO_FOLDER)){
            mod.menu->AddItem(TXT_EDIT, 0, ok_key_name);
            /*
            mod.menu->AddItem(TXT_MOVE_UP, mod.selection ? 0 : C_menu::DISABLED);
            mod.menu->AddItem(TXT_MOVE_DOWN, mod.selection<num_c-1 ? 0 : C_menu::DISABLED);
            mod.menu->AddSeparator();
            */
            mod.menu->AddItem(TXT_ADD_COND, (num_c==rul.MAX_CONDS ? C_menu::DISABLED : 0));
            mod.menu->AddItem(TXT_DELETE_COND, (num_c<=1 ? C_menu::DISABLED : 0), delete_key_name);
         }
         if(mod.selection>num_c){
            switch(rul.action){
            case S_rule::ACT_PLAY_SOUND:
               mod.menu->AddItem(TXT_EDIT, 0, ok_key_name);
               mod.menu->AddItem(TXT_CFG_ALERT_VOLUME, C_menu::HAS_SUBMENU);
               break;
            }
         }
         mod.menu->AddItem(TXT_RULE_OPERATOR, (num_c<2 ? C_menu::DISABLED : 0) | C_menu::HAS_SUBMENU);
         mod.menu->AddSeparator();
         mod.menu->AddItem(TXT_CANCEL);
         mod.menu->AddItem(TXT_DONE);
         PrepareMenu(mod.menu);
         return;
      }else{
         Rule_EndEdit(mod);
         redraw = true;
      }
      break;

   case K_VOLUME_DOWN:
   case K_VOLUME_UP:
      if(rul.action==rul.ACT_PLAY_SOUND){
         dword &vol = rul.action_param_i;
         if(ui.key==K_VOLUME_DOWN){
            if(vol>1)
               --vol;
         }else{
            if(vol<10)
               ++vol;
         }
         simple_snd_plr = NULL;
         simple_snd_plr = C_simple_sound_player::Create(rul.action_param, vol);
         simple_snd_plr->Release();
         ManageTimer();
         mod.need_save = true;
         redraw = true;
      }
      break;

   case K_DEL:
#ifdef _WIN32_WCE
   case 'd':
#endif
      if(mod.selection < num_c && num_c>1){
         CreateQuestion(*this, TXT_Q_DELETE_CONDITION, L"", new(true) C_question_del_cond(*this, mod), true);
         return;
      }
      break;

   case K_CURSORUP:
      if(!mod.te){
         /*
         if(ui.key_bits&GKEY_SHIFT)
            Rule_MoveSelectedCond(mod, true);
         else{
         */
            if(!mod.selection)
               mod.selection = mod.sb.total_space;
            --mod.selection;
            mod.EnsureVisible();
         //}
         redraw = true;
      }
      break;

   case K_CURSORDOWN:
      if(!mod.te){
         /*
         if(ui.key_bits&GKEY_SHIFT)
            Rule_MoveSelectedCond(mod, false);
         else{
         */
            if(++mod.selection == mod.sb.total_space)
               mod.selection = 0;
            mod.EnsureVisible();
         //}
         redraw = true;
      }
      break;

   case K_CURSORLEFT:
   case K_CURSORRIGHT:
      if(!mod.te){
         if(mod.selection<num_c){
                              //change condition
            S_rule::S_condition &cond = rul.conds[mod.selection];
            if(ui.key==K_CURSORLEFT){
               if(cond.cond){
                  cond.cond = (S_rule::S_condition::E_CONDITION)(cond.cond - 1);
                  redraw = true;
               }
            }else{
               if(cond.cond<cond.LAST-1){
                  cond.cond = (S_rule::S_condition::E_CONDITION)(cond.cond + 1);
                  redraw = true;
               }
            }
         }else
         if(mod.selection==num_c+1){
                              //change action
            if(ui.key==K_CURSORLEFT){
               if(rul.action){
                  rul.action = (S_rule::E_ACTION)(rul.action - 1);
                  rul.SetActionDefaults();
                  redraw = true;
               }
            }else{
               if(rul.action<rul.ACT_LAST-1){
                  rul.action = (S_rule::E_ACTION)(rul.action + 1);
                  rul.SetActionDefaults();
                  redraw = true;
               }
            }
         }
         if(redraw)
            mod.need_save = true;
      }
      break;
   }
}

//----------------------------

void C_mail_client::DrawRuleEditor(const C_mode_rule_editor &mod){

   dword col_text = GetColor(COL_TEXT);
   const dword sx = ScrnSX();
   ClearWorkArea(mod.rc);

   const S_rule &rul = mod.rul;
   {
      Cstr_w title;
      title<<GetText(TXT_RULE) <<L" - " <<rul.name;
      DrawTitleBar(title, mod.rc.y);
   }
   ClearSoftButtonsArea(mod.rc.Bottom() + 2);

   DrawEtchedFrame(mod.rc);
   int num_c = rul.NumConds();

   {
                           //draw rule entries
      int max_x = mod.GetMaxX();
      int x = mod.rc.x;
      int y = mod.rc.y;
      int max_width = max_x-mod.rc.x - fdb.letter_size_x*2;
      int sep_width = max_width;

      for(int li=0; li<mod.sb.visible_space; li++){
         int ai = mod.top_line + li;
         if(ai >= mod.sb.total_space)
            break;
         dword color = col_text;
         if(ai==mod.selection){
            S_rect rc(x, y, max_x-mod.rc.x, fdb.line_spacing*2);
            DrawSelection(rc);
            color = GetColor(COL_TEXT_HIGHLIGHTED);
         }
                              //draw separator
         if(li && (ai<mod.selection || ai>mod.selection+1))
            DrawSeparator(x+fdb.letter_size_x*1, sep_width-fdb.letter_size_x*2, y);

         bool draw_arrow_l = false, draw_arrow_r = false;
         int xx = x+fdb.letter_size_x;
         int yy = y + 2;
         int yy_line2 = yy+fdb.line_spacing-2;

         int cx = (mod.sb.rc.x - x) / 2;
         if(ai<num_c){
                              //draw condition
            const S_rule::S_condition &cond = rul.conds[ai];
            DrawString(GetText(!ai ? TXT_RULE_IF : (rul.flags&rul.FLG_OP_OR) ? TXT_RULE_OR : TXT_RULE_AND), xx, yy, UI_FONT_SMALL, 0, color);
            xx += mod.and_or_if_width;
            cx += mod.and_or_if_width/2;
            E_TEXT_ID title;
            switch(cond.cond){
            case S_rule::S_condition::SENDER_HEADER_CONTAINS: title = TXT_RULE_COND_HEADER_FIELD; break;
            default:
               title = E_TEXT_ID(TXT_RULE_COND_SUBJ_BEGINS+cond.cond);
            }
            DrawString(GetText(title), cx, yy, UI_FONT_BIG, FF_CENTER, color);

            draw_arrow_l = (cond.cond!=0);
            draw_arrow_r = (cond.cond<cond.LAST-1);
                              //draw condition param
            switch(cond.cond){
            case S_rule::S_condition::SIZE_LESS:
            case S_rule::S_condition::SIZE_MORE:
            case S_rule::S_condition::AGE_LESS:
            case S_rule::S_condition::AGE_MORE:
            case S_rule::S_condition::SPAM_SCORE_LESS:
            case S_rule::S_condition::SPAM_SCORE_MORE:
               if(ai==mod.selection && mod.te)
                  break;
               {
                  cx = sx/2;
                  const wchar *unit = NULL;
                  if(cond.cond==cond.SIZE_LESS || cond.cond==cond.SIZE_MORE)
                     unit = L"KB";
                  else
                  if(cond.cond==cond.AGE_LESS || cond.cond==cond.AGE_MORE)
                     unit = GetText(TXT_RULE_DAYS);
                  if(unit)
                     DrawString(unit, cx+fdb.letter_size_x, yy_line2, UI_FONT_BIG, 0, color);
                  Cstr_w s; s<<cond.size;
                  DrawString(s, cx, yy_line2, UI_FONT_BIG, FF_RIGHT, color);
               }
               break;
            case S_rule::S_condition::SENDER_IN_CONTACTS:
            case S_rule::S_condition::SENDER_NOT_IN_CONTACTS:
               break;
               /*
            case S_rule::S_condition::SENDER_HEADER_CONTAINS:
               {
                              //split to 2 parts, header field and value
                  //int w = mod.GetMaxX() - mod.rc.x;
                  //int x1 = x + max_width/2, x2 = x + max_width*3/2;
                  //DrawString(L"!", x1, yy_line2, UI_FONT_BIG, FF_RIGHT, color);
               }
               //break;
               */
            default:
               if(ai==mod.selection && mod.te)
                  break;
               {
                  int xx1 = fdb.letter_size_x*2;
                  wchar buf[120];
                  buf[0] = '\"';
                  int n = StrCpy(buf+1, cond.param);
                  buf[n+1] = '\"';
                  buf[n+2] = 0;
                  DrawString(buf, cx, yy_line2, UI_FONT_BIG, FF_CENTER, color, -(mod.sb.rc.x-xx1-fdb.letter_size_x));
               }
            }
         }else
         if(ai==num_c){
                              //draw <add new cond> or separator if already have max conds
            dword col = MulAlpha(color, 0x8000);
            if(num_c==rul.MAX_CONDS)
               DrawThickSeparator(fdb.letter_size_x*10, sx-fdb.letter_size_x*20, y+fdb.line_spacing);
            else
               DrawString(GetText(TXT_ADD_CONDITION), cx, y + fdb.line_spacing/2, UI_FONT_BIG, FF_CENTER, col);
         }else{
            S_rect rc(x+3, y+3, max_x-mod.rc.x-8, fdb.line_spacing*2 - 6);
            DrawOutline(rc, 0xff000000);
                              //draw action param
            switch(rul.action){
            case S_rule::ACT_MOVE_TO_FOLDER:
               if(!(ai==mod.selection && mod.te)){
                  int xx1 = fdb.letter_size_x*2;
                  wchar buf[120];
                  buf[0] = '\"';
                  int n = StrCpy(buf+1, rul.action_param);
                  buf[n+1] = '\"';
                  buf[n+2] = 0;
                  DrawString(buf, cx, yy_line2, UI_FONT_BIG, FF_CENTER, color, -(mod.sb.rc.x-xx1-fdb.letter_size_x));
               }
               break;
            case S_rule::ACT_DOWNLOAD_PARTIAL_BODY:
               if(!(ai==mod.selection && mod.te)){
                  int xx1 = fdb.letter_size_x*2;
                  Cstr_w str;
                  if(rul.action_param_i)
                     str.Format(L"% KB") <<rul.action_param_i;
                  else
                     str = L". . .";
                  DrawString(str, cx, yy_line2, UI_FONT_BIG, FF_CENTER, color, -(mod.sb.rc.x-xx1-fdb.letter_size_x));
               }
               break;
            case S_rule::ACT_PLAY_SOUND:
               {
                  Cstr_w str = file_utils::GetFileNameNoPath(rul.action_param);
                  if(!str.Length())
                     str = L". . .";
                  else{       //append volume
                     dword vol = Max(1ul, Min(rul.action_param_i, 10ul));
                     str.AppendFormat(L" (%)") <<vol;
                  }
                  DrawString(str, cx, yy_line2, UI_FONT_BIG, FF_CENTER, color, -(mod.sb.rc.x-xx-fdb.letter_size_x));
               }
               break;
            case S_rule::ACT_SET_COLOR:
               {
                  //DrawString(L"!", cx, yy_line2, UI_FONT_BIG, FF_CENTER, color, -(mod.sb.rc.x-xx-fdb.letter_size_x));
                  const int SX = fdb.cell_size_x*6;
                  S_rect rc_x(cx-SX/2, yy+fdb.line_spacing, SX, fdb.cell_size_y-3);
                  DrawOutline(rc_x, col_text);
                  FillRect(rc_x, 0xff000000 | rule_colors[rul.action_param_i]);
               }
               break;
            default:
               yy += fdb.line_spacing/2;
            }
                              //draw action
            DrawString(GetText((E_TEXT_ID)(TXT_RULE_ACT_DELETE_HDR+rul.action)), cx, yy, UI_FONT_BIG, FF_CENTER|FF_BOLD, color);
            draw_arrow_l = (rul.action!=0);
            draw_arrow_r = (rul.action<rul.ACT_LAST-1);
         }
         const int arrow_size = (fdb.line_spacing/2) | 1;
         if(draw_arrow_l)
            DrawArrowHorizontal(xx, yy+1, arrow_size, color, false);
         if(draw_arrow_r)
            DrawArrowHorizontal(x+max_width-2, yy+1, arrow_size, color, true);

         if(ai==mod.selection && mod.te)
            DrawEditedText(*mod.te);

         y += fdb.line_spacing * 2;
      }
      DrawScrollbar(mod.sb);
   }

   {
      E_TEXT_ID lsk, rsk;
      if(mod.te){
         lsk = TXT_OK, rsk = TXT_CANCEL;
      }else
         lsk = TXT_MENU, rsk = TXT_DONE;
      DrawSoftButtonsBar(mod, lsk, rsk, mod.te);
   }
   SetScreenDirty();
}

//----------------------------
