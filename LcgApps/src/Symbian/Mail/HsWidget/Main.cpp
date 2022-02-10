#include <E32Std.h>
/*
#include <E32Base.h>
*/
#include <Fbs.h>
#include <BitDev.h>

#ifndef _DEBUG
#define _WCHAR_T_DECLARED
//#define USE_LOAD_LIB
#endif
namespace std{
struct nothrow_t;
}
#include <HsWidget.h>
#include <HsWidgetPublisher.h>
#include <HsDataObserver.h>
#include <HsException.h>

extern"C"
__declspec(dllexport) TInt _E32Dll(){
   return KErrNone;
}

#include "Main.h"

#if defined _MSC_VER || defined __SYMBIAN_3RD__
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

//----------------------------
//*

static const char *const item_names[] = {
   "image1", "text1", "text2", "text3"
};
static const char *const templ_names[] = {
   "wideimage", "onerow", "tworows", "threerows"
};

//----------------------------

class C_hs_widget_imp: public C_hs_widget, public Hs::IHsDataObserver{
   C_obeserver &obs;
#ifdef USE_LOAD_LIB
   RLibrary lib;
#endif
   Hs::HsWidgetPublisher *hspub;
   Hs::HsWidget *hsw;
   CFbsBitmap *bitmap;
   CFbsBitmapDevice *bbuf_dev;
   CFbsBitGc *bbuf_gc;
   bool activated;
//----------------------------
   virtual void handleEvent(std::string widget_name, Hs::IHsDataObserver::EEvent _event){
      //User::Panic(_L("handleEvent"), _event);
      //*
      switch(_event){
      case Hs::IHsDataObserver::EActivate:
         obs.Event(EVENT_ACTIVATE);
         activated = true;
         break;
      case Hs::IHsDataObserver::EDeactivate:
         activated = false;
         obs.Event(EVENT_DEACTIVATE);
         break;
      case Hs::IHsDataObserver::ESuspend: obs.Event(EVENT_SUSPEND); break;
      case Hs::IHsDataObserver::EResume: obs.Event(EVENT_RESUME); break;
      }
      /**/
   }
//----------------------------
   virtual void handleItemEvent(std::string widget_name, std::string widget_item_name, Hs::IHsDataObserver::EItemEvent _event){
      //User::Panic(_L("handleItemEvent"), 0);
      //*
      if(_event==Hs::IHsDataObserver::ESelect){
         for(int i=0; i<ITEM_LAST; i++){
            if(!widget_item_name.compare(item_names[i])){
               obs.ItemSelected(E_ITEM(i));
               break;
            }
         }
      }
      /**/
   }
//----------------------------
   std::string widget_name, uid;
   const E_TEMPLATE template_id;
public:
   C_hs_widget_imp(C_obeserver &_obs, E_TEMPLATE tmpl):
      obs(_obs),
      hspub(NULL),
      hsw(NULL),
      bitmap(NULL),
      bbuf_dev(NULL),
      bbuf_gc(NULL),
      activated(false),
      template_id(tmpl)
   {
   }
//----------------------------
   ~C_hs_widget_imp(){
      //Publish();
      delete bbuf_dev;
      delete bbuf_gc;
      delete bitmap;
#ifndef USE_LOAD_LIB
      delete hspub;
#else
      if(hspub){
         typedef void (*t_Dtr)(Hs::HsWidgetPublisher*);
         t_Dtr fp = (t_Dtr)lib.Lookup(13);
         if(fp)
            fp(hspub);
         delete[] (byte*)hspub;
      }
      lib.Close();
#endif
   }
//----------------------------
   bool Init(const char *wn, const char *_uid){
#ifdef USE_LOAD_LIB
      int err = lib.Load(_L("z:hswidgetpublisher"));
      if(err)
         return false;
#endif
      //User::Panic(_L("Init!!"), err);
      //try{
         widget_name = wn;
         uid = _uid;
#ifndef USE_LOAD_LIB
         hspub = new Hs::HsWidgetPublisher(this);
         hsw = &hspub->createHsWidget(templ_names[template_id], widget_name, uid);
#else
         typedef void (*t_Ctr)(Hs::HsWidgetPublisher*, Hs::IHsDataObserver*);
         t_Ctr fp = (t_Ctr)lib.Lookup(11);
         if(!fp)
            return false;
         hspub = (Hs::HsWidgetPublisher*)new byte[sizeof(Hs::HsWidgetPublisher)];
         fp(hspub, this);
         {
            typedef Hs::HsWidget &(*t_createHsWidget)(Hs::HsWidgetPublisher*, std::string, std::string, std::string);
            t_createHsWidget fp = (t_createHsWidget)lib.Lookup(8);
            if(!fp)
               return false;
            hsw = &fp(hspub, templ_names[template_id], widget_name, uid);
         //User::Panic(_L("ok!!"), err);
         }
#endif
         /*
      }catch(Hs::HsException &exc){
         return false;
      }
      */
      return true;
   }
//----------------------------
   virtual void SetItem(E_ITEM itm, const char *text){
      if(hsw){
#ifndef USE_LOAD_LIB
         hsw->setItem(item_names[itm], text);
#else
         typedef void (*t_setItem)(Hs::HsWidget*, std::string, std::string);
         t_setItem fp = (t_setItem)lib.Lookup(16);
         if(fp)
            fp(hsw, item_names[itm], text);
#endif
      }
   }
//----------------------------
   virtual void SetImage(const char *fn){
      SetItem(ITEM_IMAGE, fn);
   }
//----------------------------
   virtual void SetMifImage(const char *fn){
      std::string s;
      s += "mif(";
      s += fn;
      s += " 16384 16385)";
      SetItem(ITEM_IMAGE, s.c_str());
   }
//----------------------------
   virtual void Publish(){
      if(hsw && activated){
#ifndef USE_LOAD_LIB
         hspub->publishHsWidget(*hsw);
#else
         typedef void (*t_publishHsWidget)(Hs::HsWidgetPublisher*, Hs::HsWidget&);
         t_publishHsWidget fp = (t_publishHsWidget)lib.Lookup(10);
         if(fp){
            fp(hspub, *hsw);
         }
#endif
      }
   }
//----------------------------
   virtual void Remove(){
      if(hspub){
#ifndef USE_LOAD_LIB
         hspub->removeHsWidget(templ_names[template_id], widget_name, uid);
#else
         typedef void (*t_removeHsWidget)(Hs::HsWidgetPublisher*, std::string, std::string, std::string);
         t_removeHsWidget fp = (t_removeHsWidget)lib.Lookup(9);
         if(fp)
            fp(hspub, templ_names[template_id], widget_name, uid);
#endif
      }
   }
//----------------------------
   virtual void DrawBitmap(const CFbsBitmap &bmp){
      if(!hsw)
         return;
      if(!bitmap){
         bitmap = new(ELeave) CFbsBitmap;
         bitmap->Create(TSize(312, 82), EColor16MA);
         bbuf_dev = CFbsBitmapDevice::NewL(bitmap);
         bbuf_dev->CreateContext(bbuf_gc);
         bbuf_gc->SetBrushColor(KRgbRed);
         bbuf_gc->DrawRect(TRect(TPoint(10,10), TPoint(100,20)));

#ifndef USE_LOAD_LIB
         hsw->setItem(item_names[ITEM_IMAGE], bitmap->Handle());
#else
         typedef void (*t_setItem)(Hs::HsWidget*, std::string, int);
         t_setItem fp = (t_setItem)lib.Lookup(17);
         if(fp)
            fp(hsw, item_names[ITEM_IMAGE], bitmap->Handle());
#endif
      }
      bbuf_gc->BitBlt(TPoint(0, 0), &bmp);
   }
};
/**/

//----------------------------

#if defined __SYMBIAN32__ || defined _MSC_VER
extern"C"
#endif
C_hs_widget EXPORT
*CreateHsWidget(const char *widget_name, const char *uid, C_hs_widget::C_obeserver &obs, C_hs_widget::E_TEMPLATE templ){
   C_hs_widget_imp *p = new C_hs_widget_imp(obs, templ);
   if(!p)
      return NULL;
   if(!p->Init(widget_name, uid)){
      p->Release();
      p = NULL;
   }
   return p;

}

//----------------------------

#ifdef __WINS__
#pragma data_seg(".SYMBIAN")
extern TEmulatorImageHeader uid;
//                        UIDS:  DLL     0    UID     ProcessPriority  SecureId, VendorId ProtServ  ModuleVersion Flags
TEmulatorImageHeader uid = { {0x10000079, 0, 0xa000b86b}, EPriorityForeground, {0xa000b86b, 0, {0x0, 0}}, 0x00010000, 0};
#pragma data_seg()
#endif

//----------------------------
