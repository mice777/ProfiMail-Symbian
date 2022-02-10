#ifndef _HW_WIDGET_H_
#define _HW_WIDGET_H_

#include <Rules.h>
#include <SmartPtr.h>
//----------------------------

class C_hs_widget: public C_unknown{
public:
   enum E_TEMPLATE{
      TEMPL_WIDEIMAGE,
      TEMPL_ONEROW,
      TEMPL_TWOROWS,
      TEMPL_THREEROWS,
   };
   enum E_ITEM{
      ITEM_IMAGE,
      ITEM_TEXT_1,
      ITEM_TEXT_2,
      ITEM_TEXT_3,
      ITEM_LAST
   };
   enum E_EVENT{
      EVENT_ACTIVATE,         //Activation event: Means that widget has been added to HS as content
      EVENT_DEACTIVATE,       //Deactivation event: Means that widget has been removed from
      EVENT_SUSPEND,          //Suspension event: Means that HS reading widget data is suspended
      EVENT_RESUME,           //Resume event. Means that HS reading widget data is resumed
   };

   class C_obeserver{
   public:
      virtual void Event(E_EVENT ev) = 0;
      virtual void ItemSelected(E_ITEM item) = 0;
   };
   virtual void SetItem(E_ITEM itm, const char *text) = 0;
   virtual void SetImage(const char *fn) = 0;
   virtual void SetMifImage(const char *fn) = 0;
   virtual void Publish() = 0;
   virtual void Remove() = 0;
   virtual void DrawBitmap(const class CFbsBitmap &bmp) = 0;
};

//----------------------------

typedef C_hs_widget *(*t_CreateHsWidget)(const char *widget_name, const char *uid, C_hs_widget::C_obeserver &obs, C_hs_widget::E_TEMPLATE templ);

//----------------------------

#endif

