#include "..\Main.h"
#include "Main_Email.h"

//----------------------------

class C_mode_data_counters: public C_mode_app<C_mail_client>{
   typedef C_mode_app<C_mail_client> super;
public:
   S_rect rc;
   C_mode_data_counters(C_mail_client &_app):
      super(_app)
   {}
   virtual void InitLayout(){

      const int border = 2;
      const int top = app.GetTitleBarHeight();
      rc = S_rect(border, top, app.ScrnSX()-border*2, app.ScrnSY()-top-app.GetSoftButtonBarHeight()-border);
   }
//----------------------------
   virtual void ProcessInput(S_user_input &ui, bool &redraw){
      app.ProcessMouseInSoftButtons(ui, redraw);

      switch(ui.key){
      case K_RIGHT_SOFT:
      case K_BACK:
      case K_ESC:
         Close();
         return;
      case K_LEFT_SOFT:
      case K_MENU:
         menu = CreateMenu();
         menu->AddItem(TXT_RESET_COUNTERS);
         menu->AddSeparator();
         menu->AddItem(TXT_BACK);
         app.PrepareMenu(menu);
         return;
      }
   }
//----------------------------
   virtual void ProcessMenu(int itm, dword menu_id){
      switch(itm){
      case TXT_RESET_COUNTERS:
         class C_question: public C_question_callback{
            C_mail_client &app;
            virtual void QuestionConfirm(){
               app.DataCounters_Reset();
            }
         public:
            C_question(C_mail_client &a): app(a){}
         };
         CreateQuestion(app, TXT_RESET_COUNTERS, app.GetText(TXT_Q_ARE_YOU_SURE), new(true) C_question(app), true);
         return;
      case TXT_BACK:
         Close();
         return;
      }
      if(!menu)
         Draw();
   }
//----------------------------
   virtual void Draw() const;
};

//----------------------------

void C_mode_data_counters::Draw() const{

   dword col_text = app.GetColor(app.COL_TEXT);
   const dword sx = app.ScrnSX();
   app.ClearWorkArea(rc);
   app.DrawTitleBar(app.GetText(TXT_DATA_COUNTERS));
   app.ClearSoftButtonsArea(rc.Bottom() + 2);

   app.DrawEtchedFrame(rc);
   int y = rc.y;
   y += app.fdb.line_spacing;

   for(int i=0; i<2; i++){
      app.DrawString(app.GetText(!i ? TXT_CFG_SHOW_CNT_CURRENT : TXT_CFG_SHOW_CNT_TOTAL), sx/2, y, app.UI_FONT_BIG, FF_CENTER | FF_BOLD, col_text, 0);
      y += app.fdb.line_spacing;
      app.DrawThickSeparator(sx/4, sx/2, y);
      y += app.fdb.line_spacing;

      for(int j=0; j<2; j++){
         int sz = 0;
         if(app.connection)
            sz = !j ? app.connection->GetDataSent() : app.connection->GetDataReceived();
         if(i)
            sz += !j ? app.config.total_data_sent : app.config.total_data_received;
         Cstr_w s, ssz = text_utils::MakeFileSizeText(sz, false, false);
         s<<app.GetText(!j ? TXT_STAT_SENT : TXT_STAT_RECEIVED) <<L' ' <<ssz;
         app.DrawString(s, app.fdb.letter_size_x*4, y, app.UI_FONT_BIG, 0, col_text);
         y += app.fdb.line_spacing;
      }
      if(!i){
         y += app.fdb.line_spacing;
         app.DrawThickSeparator(sx/16, sx-sx/8, y);
         y += app.fdb.line_spacing;
      }
   }
   app.DrawSoftButtonsBar(*this, TXT_MENU, TXT_BACK);
   app.SetScreenDirty();
}

//----------------------------

void C_mail_client::DataCounters_Reset(){

   //LOG_RUN("Reset"); return;
   config.total_data_sent = 0;
   config.total_data_received = 0;
   SaveConfig();
   if(connection)
      connection->ResetDataCounters();
   RedrawScreen();
}

//----------------------------

void C_mail_client::SetModeDataCounters(){

   C_mode_data_counters &mod = *new(true) C_mode_data_counters(*this);
   mod.InitLayout();
   mod.Activate();
}

//----------------------------
