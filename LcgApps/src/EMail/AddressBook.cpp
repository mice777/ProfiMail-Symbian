#ifdef __SYMBIAN32__
#define __E32MATH_H__   //don't compile e32math.h
#include <cntdb.h>
#include <cntitem.h>
#include <cntfldst.h>
#include "..\Main.h"
#include "Main_email.h"
//----------------------------

class C_address_book_imp: public C_address_book{
public:
   CContactDatabase *cdb;
   //CContactItemViewDef *vd;

   C_address_book_imp():
      //vd(NULL),
      cdb(NULL)
   {
      struct S_lf: public C_leave_func{
         CContactDatabase *cdb;
         virtual int Run(){
            cdb = CContactDatabase::OpenL();
            return 0;
         }
      } lf;
      lf.cdb = NULL;
      lf.Execute();
      cdb = lf.cdb;

      /*
      vd = CContactItemViewDef::NewL(CContactItemViewDef::EIncludeFields, CContactItemViewDef::EMaskHiddenFields);
      vd->AddL(KUidContactFieldGivenName);
      vd->AddL(KUidContactFieldFamilyName);
      vd->AddL(KUidContactFieldCompanyName);
      vd->AddL(KUidContactFieldEMail);
      */
   }
   ~C_address_book_imp(){
      //delete vd;
      delete cdb;
   }

   bool ReadContact(int id, S_contact &con){

      CContactItem *ci = NULL;
      ci = cdb->ReadMinimalContactL(id);
      //ci = cdb->ReadContactL(id, *vd);  //slow!
      if(!ci)
         return false;
      bool ret = false;
      TUid type = ci->Type();
      if(type==KUidContactCard || type==KUidContactOwnCard){
         const CContactItemFieldSet &cfs = ci->CardFields();
         for(int fi=0; fi<cfs.Count(); fi++){
            const CContactItemField &fld = cfs[fi];
            const CContentType &ct = fld.ContentType();
            int num_types = ct.FieldTypeCount();
            if(num_types){
               if(fld.StorageType()==KStorageTypeText){
                  TPtrC txt = fld.TextStorage()->Text();
                  TFieldType ft = ct.FieldType(0);
                  //TUid map = ct.Mapping();
                  switch(ft.iUid){
                  case KUidContactFieldGivenNameValue: con.first_name.Allocate((wchar*)txt.Ptr(), txt.Length()); break;
                  case KUidContactFieldFamilyNameValue: con.last_name.Allocate((wchar*)txt.Ptr(), txt.Length()); break;
                  case KUidContactFieldCompanyNameValue: con.company.Allocate((wchar*)txt.Ptr(), txt.Length()); break;
#if 1
                  case KUidContactFieldPhoneNumberValue:
                     if(num_types>1){
                        ft = ct.FieldType(1);
                        Cstr_w tmp;
                        tmp.Allocate((wchar*)txt.Ptr(), txt.Length());
                        switch(ft.iUid){
                        case KIntContactFieldVCardMapCELL: con.mobile.Copy(tmp); break;
                        case KIntContactFieldVCardMapVOICE: con.telephone.Copy(tmp); break;
                        }
                     }
                     break;
#endif
                  case KUidContactFieldEMailValue:
                     {
                        int di = con.NumEmails();
                        if(di<3){
                           Cstr_w tmp;
                           tmp.Allocate((wchar*)txt.Ptr(), txt.Length());
                           con.email[di].Copy(tmp);
                           con.email_id[di] = fi;
                        }
                     }
                     break;
                  }
               }
            }
         }
         if(!con.IsEmpty()){
            con.phone_id = id;
            ret = true;
         }
      }
      delete ci;
      return ret;
   }
};

//----------------------------

bool C_mail_client::FindContactByEmail(const Cstr_c &email, S_contact &con) const{

   if(!address_book){
      address_book = new(true) C_address_book_imp;
      address_book->Release();
   }
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;
   if(ab.cdb){
      struct S_lf: public C_leave_func{
         CContactDatabase *cdb;
         Cstr_w e;
         CContactItemFieldDef *def;
         CContactIdArray *ia;
         S_lf():
            ia(NULL)
         {
            def = new(true) CContactItemFieldDef;
         }
         ~S_lf(){
            delete ia;
            delete def;
         }

         virtual int Run(){
            def->AppendL(KUidContactFieldEMail);
            CContactIdArray *il = cdb->FindLC(TPtrC((word*)(const wchar*)e, e.Length()), def);
            //Info("!", il->Count());
            if(il && il->Count()){
               struct S_lf1: public C_leave_func{
               public:
                  CContactIdArray *ia;
                  CContactIdArray *il;
                  S_lf1(): ia(NULL){}
                  virtual int Run(){
                     ia = CContactIdArray::NewL(il);
                     return 0;
                  }
               } lf1;
               lf1.il = il;
               if(lf1.Execute()==0)
                  ia = lf1.ia;
            }
            CleanupStack::PopAndDestroy();
            return 0;
         }
      } lf;
      lf.cdb = ab.cdb;
      lf.e.Copy(email);
      if(lf.Execute()==KErrNone && lf.ia){
         int n = lf.ia->Count();
         for(int i=0; i<n; i++){
            S_contact c;
            if(ab.ReadContact((*lf.ia)[i], c)){
               if(c.ContainEmail(email)){
                  con = c;
                  return true;
               }
            }
         }
      }
   }
   return false;
}

//----------------------------

void C_mail_client::OpenAddressBook(){

   if(!address_book){
      address_book = new(true) C_address_book_imp;
      address_book->Release();
   }
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;
   if(ab.items.Size())
      return;

   if(!ab.cdb)
      return;

   const CContactIdArray *il = ab.cdb->SortedItemsL();
   C_vector<S_contact> buf;
   int cnt = il->Count();
   buf.reserve(cnt);
   for(int i=0; i<cnt; i++){
      S_contact con;
      if(ab.ReadContact((*il)[i], con))
         buf.push_back(con);
   }
   ab.items.Assign(buf.begin(), buf.end());

   int tmp = 0;
   SortAddressBook(tmp);
}

//----------------------------

void C_mail_client::ModifyContact(int ci){

   if(!address_book)
      return;
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;

   if(!ab.cdb)
      ab.cdb = CContactDatabase::CreateL();
   const S_contact &con = ab.items[ci];
   int id = con.phone_id;
   if(id==-1){
      CContactCard *itm = CContactCard::NewL();
      id = ab.cdb->AddNewContactL(*itm);
      delete itm;
   }
   {
      CContactItem *ci = ab.cdb->OpenContactL(id);
      assert(ci->Type()==KUidContactCard || ci->Type()==KUidContactOwnCard);
      CContactItemFieldSet &cfs = ci->CardFields();
                              //update all fields
      for(int i=0; i<8; i++){
         struct S_uid: public TUid{
            S_uid(){}
            S_uid(int i){ iUid = i; }
         };
         Cstr_w str;
         int uid, uid1 = 0;
         int vcard = 0;
         int fi = -1;
         switch(i){
         case 0:
         case 1: 
         case 2:
            {
               uid = KUidContactFieldEMailValue;
               str.Copy(con.email[i]);
               vcard = KIntContactFieldVCardMapEMAILINTERNET;
               switch(i){
               case 1: uid1 = KIntContactFieldVCardMapHOME; break;
               case 2: uid1 = KIntContactFieldVCardMapWORK; break;
               }
               int fii = con.email_id[i];
               if(fii!=-1 && fii<cfs.Count()){
                  const CContentType &ct = cfs[fii].ContentType();
                  int fc = ct.FieldTypeCount();
                  if(fc){
                     TFieldType ft = ct.FieldType(0);
                     if(ft==S_uid(KUidContactFieldEMailValue))
                        fi = fii;
                  }
               }
            }
            break;
         case 3: str = con.first_name; uid = KUidContactFieldGivenNameValue; vcard = KIntContactFieldVCardMapUnusedN; break;
         case 4: str = con.last_name; uid = KUidContactFieldFamilyNameValue; vcard = KIntContactFieldVCardMapUnusedN; break;
         case 5: str = con.company; uid = KUidContactFieldCompanyNameValue; vcard = KIntContactFieldVCardMapORG; break;
         case 6: str.Copy(con.mobile); uid = KUidContactFieldPhoneNumberValue; vcard = KIntContactFieldVCardMapTEL; uid1 = KIntContactFieldVCardMapCELL; break;
         case 7: str.Copy(con.telephone); uid = KUidContactFieldPhoneNumberValue; vcard = KIntContactFieldVCardMapTEL; uid1 = KIntContactFieldVCardMapVOICE; break;
         default:
            continue;
         }
         if(fi==-1){
            fi = cfs.Find(S_uid(uid));
            if(uid1){
               while(fi!=-1){
                  const CContentType &ct = cfs[fi].ContentType();
                  if(ct.FieldTypeCount()>=2 && ct.FieldType(1)==S_uid(uid1))
                     break;
                  fi = cfs.FindNext(S_uid(uid), fi+1);
               }
            }
         }
         if(fi!=KErrNotFound){
            if(str.Length()){
                              //update existing
               CContactItemField &fld = cfs[fi];
               CContactTextField *txt = fld.TextStorage();
               txt->SetTextL(TPtrC((word*)(const wchar*)str, str.Length()));
            }else{
                              //delete field
               cfs.Remove(fi);
            }
         }else{
            if(str.Length()){
                              //create new
               CContentType *ct = CContentType::NewL(S_uid(uid), S_uid(vcard));
               if(uid1)
                  ct->AddFieldTypeL(S_uid(uid1));
               CContactItemField *fld = CContactItemField::NewL(KStorageTypeText, *ct);
               CContactTextField *txt = fld->TextStorage();
               txt->SetTextL(TPtrC((word*)(const wchar*)str, str.Length()));
               cfs.AddL(*fld);
               delete ct;
            }
         }
      }
      ab.cdb->CommitContactL(*ci);
      delete ci;
   }
}

//----------------------------

void C_mail_client::RemoveContact(dword ci){

   if(!address_book)
      return;
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;

   const S_contact &con = ab.items[ci];
                              //remove from system
   assert(con.phone_id!=-1);
   if(ab.cdb){
      struct S_lf: public C_leave_func{
         CContactDatabase *cdb;
         dword id;
         virtual int Run(){
            cdb->DeleteContactL(id);
            return 0;
         }
      } lf;
      lf.cdb = ab.cdb;
      lf.id = con.phone_id;
      lf.Execute();
   }
   while(++ci < ab.items.Size())
      ab.items[ci-1] = ab.items[ci];
   ab.items.Resize(ab.items.Size()-1);
}

//----------------------------
#elif defined _WIN32_WCE //|| defined _WINDOWS
//----------------------------

namespace win{
#ifdef _WIN32_WCE
#define INITGUID
#include <CeMapi.h>
#include <pimstore.h> 
#else
#include <Windows.h>
#undef SOCKET_ERROR
#undef FILE_TYPE_UNKNOWN
#include <WabApi.h>
#endif
}
#include "..\Main.h"
#include "Main_email.h"

//----------------------------

class C_address_book_imp: public C_address_book{
public:
   win::IPOutlookApp *pol;
   win::IFolder *fld;
   win::IPOutlookItemCollection *itm_col;
   int num;

   C_address_book_imp():
      pol(NULL),
      fld(NULL),
      itm_col(NULL),
      num(0)
   {
      win::HRESULT hr;
      hr = win::CoInitializeEx(NULL, 0);
      if(hr>=0){
         hr = win::CoCreateInstance(win::CLSID_Application, NULL, win::CLSCTX_INPROC_SERVER, win::IID_IPOutlookApp, (void**)&pol);
         if(hr>=0){
            hr = pol->Logon(NULL);
            if(hr>=0){
               hr = pol->GetDefaultFolder(win::olFolderContacts, &fld);
               if(hr>=0){
                  hr = fld->get_Items(&itm_col);
                  if(hr>=0){
                     itm_col->get_Count(&num);
                  }
               }
            }
         }
      }
   }
   ~C_address_book_imp(){
      if(itm_col)
         itm_col->Release();
      if(fld)
         fld->Release();
      if(pol){
         pol->Logoff();
         pol->Release();
      }
   }
};

struct S_con_conv{
   C_buffer<wchar> email[3], name1, name2, company, mobile, telephone;
   wchar *em[3], *n1, *n2, *cy, *mo, *tl;

   S_con_conv(){
      email[0].Resize(1024);
      email[1].Resize(1024);
      email[2].Resize(1024);
      name1.Resize(1024);
      name2.Resize(1024);
      company.Resize(1024);
      mobile.Resize(1024);
      telephone.Resize(1024);

      em[0] = email[0].Begin();
      em[1] = email[1].Begin();
      em[2] = email[2].Begin();
      n1 = name1.Begin();
      n2 = name2.Begin();
      cy = company.Begin();
      mo = mobile.Begin();
      tl = telephone.Begin();
   }

   bool Get(win::IContact *icnt, S_contact &con){
      *em[0] = 0; *em[1] = 0; *em[2] = 0;
      *n1 = 0, *n2 = 0, *cy = 0, *mo = 0, *tl = 0;

      icnt->get_Email1Address(&em[0]);
      icnt->get_Email2Address(&em[1]);
      icnt->get_Email3Address(&em[2]);
      icnt->get_FirstName(&n1);
      icnt->get_LastName(&n2);
      icnt->get_CompanyName(&cy);
      icnt->get_MobileTelephoneNumber(&mo);
      icnt->get_BusinessTelephoneNumber(&tl);

      con.first_name = n1;
      con.last_name = n2;
      con.company = cy;
      con.email[0].Copy(em[0]);
      con.email[1].Copy(em[1]);
      con.email[2].Copy(em[2]);
      con.mobile.Copy(mo);
      con.telephone.Copy(tl);

      if(con.IsEmpty())
         return false;
      long id;
      icnt->get_Oid(&id);
      con.phone_id = id;
      return true;
   }
};

//----------------------------

bool C_mail_client::FindContactByEmail(const Cstr_c &email, S_contact &con) const{

   /*
   const_cast<C_mail_client*>(this)->OpenAddressBook();
   for(dword i=0; i<address_book->items.Size(); i++){
      const S_contact &c = address_book->items[i];
      if(c.ContainEmail(email))
         cons.push_back(c);
      }
   }
   */
   if(!address_book){
      address_book = new(true) C_address_book_imp;
      address_book->Release();
   }
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;
   if(ab.itm_col){
      win::IContact *icnt = NULL;
      Cstr_w s, e;
      e.Copy(email);
      for(int i=0; i<3; i++){
         if(i)
            s<<L" OR ";
         s.AppendFormat(L"[Email%Address] = \"%\"") <<(i+1) <<e;
      }
      win::HRESULT hr = ab.itm_col->Find((wchar*)(const wchar*)s, (win::IDispatch**)&icnt);
      while(hr>=0 && icnt){
         S_con_conv cc;
         cc.Get(icnt, con);
         icnt->Release();
         return true;
         //hr = ab.itm_col->FindNext((win::IDispatch**)&icnt);
      }
   }
   return false;
}

//----------------------------

void C_mail_client::OpenAddressBook(){

   if(!address_book){
      address_book = new(true) C_address_book_imp;
      address_book->Release();
   }
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;
   if(ab.items.Size())
      return;

   C_vector<S_contact> buf;
   buf.reserve(ab.num);

   S_con_conv cc;

   for(int i=ab.num; i--; ){
      win::HRESULT hr;
      win::IContact *icnt;
      hr = ab.itm_col->Item(i+1, (win::IDispatch**)&icnt);
      if(hr>=0){
         S_contact con;
         if(cc.Get(icnt, con))
            buf.push_back(con);
         icnt->Release();
      }
   }
   ab.items.Assign(buf.begin(), buf.end());

   int tmp = 0;
   SortAddressBook(tmp);
}

//----------------------------

void C_mail_client::ModifyContact(int ci){

   if(!address_book)
      return;
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;

   const S_contact &con = ab.items[ci];

   win::HRESULT hr;
   win::IContact *icnt = NULL;

   if(con.phone_id==-1){
      if(!ab.itm_col)
         return;
      hr = ab.itm_col->Add((win::IDispatch**)&icnt);
      //hr = ab.pol->CreateItem(olContactItem, (IDispatch**)&cc);
      if(hr<0)
         return;
   }else{
      for(int i=ab.num; i--; ){
         win::HRESULT hr = ab.itm_col->Item(i+1, (win::IDispatch**)&icnt);
         if(hr>=0){
            long id;
            icnt->get_Oid(&id);
            if(id==con.phone_id)
               break;
            icnt->Release();
         }
      }
   }
   if(icnt){
                              //setup contact
      Cstr_w tmp;
      tmp.Copy(con.email[0]); icnt->put_Email1Address((wchar*)(const wchar*)tmp);
      tmp.Copy(con.email[1]); icnt->put_Email2Address((wchar*)(const wchar*)tmp);
      tmp.Copy(con.email[2]); icnt->put_Email3Address((wchar*)(const wchar*)tmp);
      icnt->put_FirstName((wchar*)(const wchar*)con.first_name);
      icnt->put_LastName((wchar*)(const wchar*)con.last_name);
      icnt->put_CompanyName((wchar*)(const wchar*)con.company);
      tmp.Copy(con.mobile); icnt->put_MobileTelephoneNumber((wchar*)(const wchar*)tmp);
      tmp.Copy(con.telephone); icnt->put_BusinessTelephoneNumber((wchar*)(const wchar*)tmp);

      Cstr_w fa = AddressBook_GetName(con);
      icnt->put_FileAs((wchar*)(const wchar*)fa);
      icnt->Save();
      icnt->Release();
   }
}

//----------------------------

void C_mail_client::RemoveContact(dword ci){

   if(!address_book)
      return;
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;

   const S_contact &con = ab.items[ci];
   for(int i=ab.num; i--; ){
      win::IContact *cc;
      win::HRESULT hr = ab.itm_col->Item(i+1, (win::IDispatch**)&cc);
      if(hr>=0){
         long id;
         cc->get_Oid(&id);
         cc->Release();
         if(id==con.phone_id){
            ab.itm_col->Remove(i+1);
            break;
         }
      }
   }
   while(++ci < ab.items.Size())
      ab.items[ci-1] = ab.items[ci];
   ab.items.Resize(ab.items.Size()-1);
}

//----------------------------
#else//_WIN32_WCE
//----------------------------
#include "..\Main.h"
#include "Main_email.h"

static const wchar abook_filename[] = DATA_PATH_PREFIX L"abook.bin";
const int ADDRESS_BOOK_SAVE_VERSION = 6;

class C_address_book_imp: public C_address_book{
public:
};

//----------------------------

bool C_mail_client::FindContactByEmail(const Cstr_c &email, S_contact &con) const{

   const_cast<C_mail_client*>(this)->OpenAddressBook();
   for(dword i=0; i<address_book->items.Size(); i++){
      const S_contact &c = address_book->items[i];
      if(c.ContainEmail(email)){
         con = c;
         return true;
      }
   }
   return false;
}

//----------------------------

void C_mail_client::OpenAddressBook(){

   if(!address_book){
      address_book = new(true) C_address_book_imp;
      address_book->Release();
   }
   C_address_book_imp &ab = (C_address_book_imp&)*address_book;
   if(ab.items.Size())
      return;

   C_file ck;
   do{
      if(!ck.Open(abook_filename))
         break;
      dword ver;
      if(!ck.ReadDword(ver) || ver>ADDRESS_BOOK_SAVE_VERSION)
         break;
      dword num;
      if(!ck.ReadDword(num))
         break;
      ab.items.Resize(num);
      dword i;
      for(i=0; i<num; i++){
         S_contact &c = ab.items[i];
         if(!file_utils::ReadString(ck, c.first_name))
            break;
         if(!file_utils::ReadString(ck, c.last_name))   
            break;
         if(!file_utils::ReadString(ck, c.email[0]) ||
            !file_utils::ReadString(ck, c.email[1]) ||
            !file_utils::ReadString(ck, c.email[2]) ||
            !file_utils::ReadString(ck, c.company) ||
            !file_utils::ReadString(ck, c.telephone) ||
            !file_utils::ReadString(ck, c.mobile))
            break;
         //if(!ck.ReadDword((dword&)c.phone_id))
            //break;
#ifdef __SYMBIAN32__
         for(int mi=0; mi<3; mi++)
            ck.ReadDword((dword&)c.email_id[mi]);
#endif
      }
      ab.items.Resize(i);
   }while(false);
}

//----------------------------

static void SaveAddressBook(const C_address_book &ab){

   C_file ck;
   if(ck.Open(abook_filename, C_file::FILE_WRITE|C_file::FILE_WRITE_CREATE_PATH)){
      ck.WriteDword(ADDRESS_BOOK_SAVE_VERSION);
      int num = ab.items.Size();
      ck.WriteDword(num);
      for(int i=0; i<num; i++){
         const S_contact &c = ab.items[i];
         file_utils::WriteString(ck, c.first_name);
         file_utils::WriteString(ck, c.last_name);
         file_utils::WriteString(ck, c.email[0]);
         file_utils::WriteString(ck, c.email[1]);
         file_utils::WriteString(ck, c.email[2]);
         file_utils::WriteString(ck, c.company);
         file_utils::WriteString(ck, c.telephone);
         file_utils::WriteString(ck, c.mobile);
      }
   }
}

//----------------------------

void C_mail_client::ModifyContact(int ci){

   if(address_book)
      SaveAddressBook(*address_book);
}

//----------------------------

void C_mail_client::RemoveContact(dword ci){

   if(!address_book)
      return;
   C_address_book &ab = *address_book;
   while(++ci < ab.items.Size())
      ab.items[ci-1] = ab.items[ci];
   ab.items.Resize(ab.items.Size()-1);
   SaveAddressBook(ab);
}

//----------------------------
#endif//!__SYMBIAN32__

#include <UI\TextEntry.h>
//----------------------------

S_contact::S_contact():
   phone_id(-1)
{
#ifdef __SYMBIAN32__
   MemSet(email_id, 0xff, sizeof(email_id));
#endif
}

//----------------------------

bool S_contact::operator ==(const S_contact &c) const{
   return (
      first_name==c.first_name &&
      last_name==c.last_name &&
      email[0]==c.email[0] &&
      email[1]==c.email[1] &&
      email[2]==c.email[2] &&
      company==c.company &&
      telephone==c.telephone &&
      mobile==c.mobile
      );
}

//----------------------------

bool S_contact::IsEmpty() const{
   return (
      !first_name.Length() &&
      !last_name.Length() &&
      !email[0].Length() &&
      !email[1].Length() &&
      !email[2].Length() &&
      !company.Length() &&
      !telephone.Length() &&
      !mobile.Length()
      );
}

//----------------------------

void S_contact::AssignName(const wchar *wp){

   while(*wp==' ') ++wp;
   int i;
   for(i=0; wp[i] && wp[i]!=' '; i++);
   first_name.Allocate(wp, i);
   wp += i;
   while(*wp==' ') ++wp;
   last_name.Clear();
   if(*wp)
      last_name = wp;
}

//----------------------------

dword S_contact::NumEmails() const{
   return
      (email[0].Length() ? 1 : 0) +
      (email[1].Length() ? 1 : 0) +
      (email[2].Length() ? 1 : 0)
      ;
}

//----------------------------

const Cstr_c &S_contact::GetEmail(int i) const{

   int ii = -1;
   do{
      while(ii<2 && !email[++ii].Length());
   }while(i--);
   return email[ii];
}

//----------------------------

bool S_contact::ContainEmail(const Cstr_c &s) const{
   for(int i=0; i<3; i++){
      if(!text_utils::CompareStringsNoCase(email[i], s))
         return true;
   }
   return false;
}

//----------------------------

dword S_contact::BeginsWithEmail(const Cstr_c &s) const{

   dword ret = 0;
   for(int i=0; i<3; i++){
      const Cstr_c &e = email[i];
      if(e.Length()>s.Length() && CompareStringsNoCasePartial((const char*)e, (const char*)s))
         ret |= (1<<i);
   }
   return ret;
}

//----------------------------

Cstr_w C_mail_client::AddressBook_GetName(const S_contact &con) const{

   const Cstr_w *n1 = &con.first_name, *n2 = &con.last_name;
   if(config_mail.sort_contacts_by_last)
      Swap(n1, n2);
   Cstr_w ret = *n1;
   if(n2->Length()){
      if(ret.Length())
         ret<<' ';
      ret<<*n2;
   }
   if(!ret.Length())
      ret = con.company;
   if(!ret.Length())
      ret.Copy(con.GetEmail(0));
   return ret;
}

//----------------------------

void C_mail_client::CollectMatchingContacts(const Cstr_w &name, const Cstr_c &email, C_vector<S_contact_match> &matches){

   matches.reserve(20);
   OpenAddressBook();
   for(dword i=0; i<address_book->items.Size(); i++){
      const S_contact &con = address_book->items[i];
      dword match = con.BeginsWithEmail(email);
      if(match){
         S_contact_match &cm = matches.push_back(S_contact_match());
         (S_contact&)cm = con;
         cm.email_match_mask = match;
      }
      Cstr_w full_name = AddressBook_GetName(con);
      if((con.first_name.Length()>=name.Length() && CompareStringsNoCasePartial((const wchar*)con.first_name, (const wchar*) name)) ||
         (con.last_name.Length()>=name.Length() && CompareStringsNoCasePartial((const wchar*)con.last_name, (const wchar*)name)) ||
         (full_name.Length()>=name.Length() && CompareStringsNoCasePartial((const wchar*)full_name, (const wchar*)name))){

         S_contact_match &cm = matches.push_back(S_contact_match());
         (S_contact&)cm = con;
         cm.email_match_mask = 0;
      }
   }
}

//----------------------------
struct S_sort_ab_help{
   C_mail_client *_this;
   const S_contact *contacts;
   int *track_indx;
};

int C_mail_client::CompareContacts(const void *a1, const void *a2, void *context){

   const S_sort_ab_help &ah = *(S_sort_ab_help*)context;
   const S_contact &c1 = *(S_contact*)a1;
   const S_contact &c2 = *(S_contact*)a2;
   return text_utils::CompareStringsNoCase(ah._this->AddressBook_GetName(c1), ah._this->AddressBook_GetName(c2));
}

//----------------------------

static void SwapContacts(void *a1, void *a2, dword w, void *context){

   if(a1!=a2){
      S_contact &c1 = *(S_contact*)a1;
      S_contact &c2 = *(S_contact*)a2;
      Swap(c1, c2);
                              //adjust account index
      S_sort_ab_help &ah = *(S_sort_ab_help*)context;
      int i1 = &c1 - ah.contacts;
      int i2 = &c2 - ah.contacts;
      if(i1==*ah.track_indx) *ah.track_indx = i2;
      else
      if(i2==*ah.track_indx) *ah.track_indx = i1;
   }
}

//----------------------------

void C_mail_client::SortAddressBook(int &indx){

   if(!address_book)
      return;
                              //sort all except system accounts (which are at back)
   S_sort_ab_help ah = { this, address_book->items.Begin(), &indx };
   QuickSort(address_book->items.Begin(), address_book->items.Size(), sizeof(S_contact), &CompareContacts, &ah, &SwapContacts);
}

//----------------------------

                           //mode for address book viewer + editor
class C_mode_address_book: public C_mode_list<C_mail_client>, public C_question_callback{
   virtual void QuestionConfirm(){
      DeleteSelected();
   }
   virtual void InitMenu(){
      int num_c = curr_list.size();
      if(num_c){
         const S_contact &con = curr_list[selection];
         int num_e = con.NumEmails();
         switch(browse_mode){
         case C_mail_client::AB_MODE_SELECT:
            menu->AddItem(TXT_SELECT, (!num_e ? C_menu::DISABLED : 0) | (num_e>1 ? C_menu::HAS_SUBMENU : 0), num_e<=1 ? app.ok_key_name : NULL);
            menu->AddSeparator();
            break;
         case C_mail_client::AB_MODE_UPDATE_CONTACT:
            menu->AddItem(TXT_SELECT, !num_e ? C_menu::DISABLED : 0, num_e<=1 ? app.ok_key_name : NULL);
            break;
         case C_mail_client::AB_MODE_EXPLORE:
            menu->AddItem(TXT_WRITE_EMAIL, (!num_e ? C_menu::DISABLED : 0) | (num_e>1 ? C_menu::HAS_SUBMENU : 0));
            menu->AddItem(TXT_CALL_NUMBER, (con.mobile.Length() || con.telephone.Length()) ? 0 : C_menu::DISABLED);
            menu->AddSeparator();
            break;
         }
      }
      if(browse_mode!=C_mail_client::AB_MODE_UPDATE_CONTACT){
         menu->AddItem(TXT_NEW);
         menu->AddItem(TXT_DELETE, (!num_c ? C_menu::DISABLED : 0));
         menu->AddItem(TXT_EDIT, (!num_c ? C_menu::DISABLED : 0));
      }
      menu->AddItem(TXT_SORT_BY, C_menu::HAS_SUBMENU);
      menu->AddSeparator();
      menu->AddItem(TXT_BACK);
   }
public:
   static const dword ID = FOUR_CC('A','B','O','K');
   C_vector<S_contact> curr_list;   //current view list (matching search criteria)

   C_smart_ptr<C_image> img_mglass;
   C_ctrl_text_entry_line *ctrl_input; //search text entry

   S_contact edit_contact;

   C_mail_client::E_ADDRESS_BOOK_MODE browse_mode;
   int curr_email_index;   //index of selected contact's email
   virtual int GetNumEntries() const{ return curr_list.size(); }
   virtual bool IsPixelMode() const{ return true; }

   C_mode_address_book(C_mail_client &_app, C_mail_client::E_ADDRESS_BOOK_MODE m):
      C_mode_list<C_mail_client>(_app),
      browse_mode(m),
      ctrl_input(NULL),
      curr_email_index(0)
   {
      mode_id = ID;
      SetTitle(app.GetText(TXT_ADDRESS_BOOK));

      ctrl_input = new(true) C_ctrl_text_entry_line(this, 100);
      AddControl(ctrl_input);
      SetFocus(ctrl_input);

      InitLayout();
      InitList();
      Activate();
   }

   virtual void InitLayout();
   virtual void ProcessMenu(int itm, dword menu_id);
   virtual void ProcessInput(S_user_input &ui, bool &redraw);
   virtual void DrawContents() const;
//----------------------------
   virtual void Draw() const{
      C_mode::Draw();
      app.DrawEtchedFrame(rc);
      DrawContents();
      img_mglass->Draw(2, 2);
   }
//----------------------------
   virtual void TextEditNotify(bool cursor_moved, bool text_changed, bool &redraw){
      C_mode::TextEditNotify(cursor_moved, text_changed, redraw);
      if(text_changed){
         InitList();
         EnsureVisible();
         redraw = true;
      }
   }

//----------------------------
// Init visible list based on search string.
   void InitList();
//----------------------------
   void EditSelected(){
      const S_contact &con = curr_list[selection];
      for(int ci=app.address_book->items.Size(); ci--; ){
         if(con==app.address_book->items[ci]){
            SetModeABEditor(ci,
               browse_mode==C_mail_client::AB_MODE_UPDATE_CONTACT ? &edit_contact : NULL);
            break;
         }
      }
   }
//----------------------------
   void DeleteSelected(){
      const S_contact &c = curr_list[selection];
      int i;
      for(i=app.address_book->items.Size(); i--; ){
         if(c==app.address_book->items[i])
            break;
      }
      assert(i!=-1);
      app.RemoveContact(i);

      InitList();
      EnsureVisible();
   }
//----------------------------
   void SetModeABEditor(int con_index, const S_contact *con_new);
   void CallContact();
};

//----------------------------

void C_mode_address_book::InitLayout(){

   C_mode::InitLayout();
   entry_height = app.fdb.line_spacing * 2;
   const int border = 2;
   const int top = app.GetTitleBarHeight() + app.fdb.line_spacing+2;
   rc = S_rect(border, top, app.ScrnSX()-border*2, app.ScrnSY()-top);
   rc.sy -= app.GetSoftButtonBarHeight()+border;
                           //compute # of visible lines, and resize rectangle to whole lines
   sb.visible_space = rc.sy;//vis_lines*entry_height;
   img_mglass = C_image::Create(app);
   img_mglass->Release();
   img_mglass->Open(L"mglass.png", 0, app.fdb.line_spacing*2, CreateDtaFile());

   {
      ctrl_input->SetText(NULL);
      ctrl_input->SetCase(C_text_editor::CASE_CAPITAL);
      int x = img_mglass->SizeX();
      ctrl_input->SetRect(S_rect(x, rc.y-app.fdb.line_spacing-2, rc.sx-x, app.fdb.cell_size_y+1));
   }
                           //init scrollbar
   const int width = app.GetScrollbarWidth();
   sb.rc = S_rect(rc.Right()-width-3, rc.y+3, width, rc.sy-6);

   sb.SetVisibleFlag();
   EnsureVisible();
}

//----------------------------

void C_mode_address_book::InitList(){

   curr_list.clear();
   int sz = app.address_book ? app.address_book->items.Size() : 0;
   curr_list.reserve(sz);
   Cstr_w s;
   s = ctrl_input->GetText();
   s.ToLower();
   for(int i=0; i<sz; i++){
      const S_contact &con = app.address_book->items[i];
      if(browse_mode==C_mail_client::AB_MODE_SELECT){
                              //don't list contacts without email address 
         if(!con.email[0].Length() &&
            !con.email[1].Length() &&
            !con.email[2].Length())
            continue;
      }
      if(s.Length()){
         bool match = false;
         for(int di=0; di<6 && !match; di++){
            Cstr_w n;
            switch(di){
            case 0: n = con.last_name; break;
            case 1: n = con.first_name; break;
            case 2: n = con.company; break;
            case 3: n.Copy(con.email[0]); break;
            case 4: n.Copy(con.email[1]); break;
            case 5: n.Copy(con.email[2]); break;
            default: assert(0);
            }
            if(n.Length() >= s.Length()){
               n.ToLower();
               match = (!MemCmp(s, n, s.Length()*sizeof(wchar)));
            }
         }
         if(!match)
            continue;
      }
      curr_list.push_back(con);
   }
   sb.total_space = curr_list.size();
   if(IsPixelMode())
      sb.total_space *= entry_height;
   sb.SetVisibleFlag();
   selection = 0;
   curr_email_index = 0;
}

//----------------------------

void C_mode_address_book::CallContact(){

   S_contact con = curr_list[selection];
   if(con.mobile.Length() || con.telephone.Length()){
      const char *tel = con.mobile.Length() ? con.mobile : con.telephone;
#ifdef WINDOWS_MOBILE
                        //run standard browser for making phonecall
      Cstr_c s; s<<"tel:" <<tel;
      app.StartBrowser(s);
#else
      Cstr_w tmp;
      tmp.Copy(tel);
      CreateTextEntryMode(app, TXT_CALL_NUMBER, new(true) C_mail_client::C_text_entry_call(app), true, 80, tmp);
#endif
   }
}

//----------------------------

void C_mode_address_book::ProcessMenu(int itm, dword menu_id){

   switch(itm){
   case TXT_NEW:
      {
         S_contact c;
         SetModeABEditor(-1, &c);
      }
      break;

   case TXT_EDIT:
      EditSelected();
      return;

   case TXT_DELETE:
      if(curr_list.size()){
         CreateQuestion(app, TXT_Q_DELETE_CONTACT, app.AddressBook_GetName(curr_list[selection]), this);
      }
      break;

   case TXT_SELECT:
   case TXT_WRITE_EMAIL:
      {
         S_contact con = curr_list[selection];
         if(browse_mode==C_mail_client::AB_MODE_UPDATE_CONTACT){
            EditSelected();
            break;
         }
         int num_e = con.NumEmails();
         if(num_e>1){
            menu = CreateMenu();
            for(int i=0; i<num_e; i++){
               Cstr_w s; s.Copy(con.GetEmail(i));
               menu->AddItem(s);
            }
            app.PrepareMenu(menu);
         }else{
            if(browse_mode==C_mail_client::AB_MODE_SELECT){
               Close(false);
               assert(app.mode->Id()==C_mail_client::C_mode_write_mail_base::ID);
               ((C_mail_client::C_mode_write_mail_base&)*app.mode).AddRecipient(con.GetEmail(0));
            }else
            if(browse_mode==C_mail_client::AB_MODE_EXPLORE){
               Close(false);
               assert(app.mode->Id()==C_mail_client::C_mode_accounts::ID);
               app.SetModeAccountSelector(con.GetEmail(0));
            }else
               assert(0);
         }
      }
      return;

   case TXT_CALL_NUMBER:
      CallContact();
      break;

   case TXT_SORT_BY:
      menu = CreateMenu();
      menu->AddItem(TXT_CON_FIRST_NAME, !app.config_mail.sort_contacts_by_last ? C_menu::MARKED : 0);
      menu->AddItem(TXT_CON_LAST_NAME, app.config_mail.sort_contacts_by_last ? C_menu::MARKED : 0);
      app.PrepareMenu(menu);
      break;

   case TXT_CON_FIRST_NAME:
   case TXT_CON_LAST_NAME:
      app.config_mail.sort_contacts_by_last = (itm==TXT_CON_LAST_NAME);
      app.SaveConfig();
      app.SortAddressBook(selection);
      InitList();
      break;

   case TXT_BACK:
      Close();
      break;

   default:
      if(itm >= 0x10000){
                           //email selected
         itm -= 0x10000;
         S_contact con = curr_list[selection];
         assert(dword(itm) < con.NumEmails());
         if(browse_mode==C_mail_client::AB_MODE_SELECT){
            Close(false);
            assert(app.mode->Id()==C_mail_client::C_mode_write_mail_base::ID);
            ((C_mail_client::C_mode_write_mail_base&)*app.mode).AddRecipient(con.GetEmail(itm));
         }else
         if(browse_mode==C_mail_client::AB_MODE_EXPLORE){
            Close(false);
            assert(app.mode->Id()==C_mail_client::C_mode_accounts::ID);
            app.SetModeAccountSelector(con.GetEmail(itm));
         }else
            assert(0);
      }
   }
}

//----------------------------

void C_mode_address_book::ProcessInput(S_user_input &ui, bool &redraw){

   C_mode::ProcessInput(ui, redraw);
   if(ProcessInputInList(ui, redraw)){
      curr_email_index = 0;
   }

   int num_c = curr_list.size();
#ifdef USE_MOUSE
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){
      if(GetNumEntries()){
         const S_contact &con = curr_list[selection];
         if(ui.CheckMouseInRect(rc)){
            if(ui.mouse_buttons&MOUSE_BUTTON_1_DOWN){
               //te->Activate(false);
               menu = app.CreateTouchMenu();
               menu->AddItem(TXT_EDIT);
               menu->AddItem(TXT_DELETE, 0, 0, 0, C_mail_client::BUT_DELETE);
               menu->AddSeparator();
               if(con.mobile.Length() || con.telephone.Length())
                  menu->AddItem(TXT_CALL_NUMBER);
               else
                  menu->AddSeparator();
               menu->AddItem(browse_mode==C_mail_client::AB_MODE_EXPLORE ? TXT_WRITE_EMAIL : TXT_SELECT);
               app.PrepareTouchMenu(menu, ui);
            }
            if(ui.mouse_buttons&MOUSE_BUTTON_1_UP){
               if(ui.key==K_ENTER){
                  int num_e = con.NumEmails();
                  if(ui.mouse.x<rc.sx/5){
                     if(curr_email_index){
                        --curr_email_index;
                        redraw = true;
                     }
                  }else
                  if(ui.mouse.x>=rc.sx*4/5){
                     if(curr_email_index<num_e-1){
                        ++curr_email_index;
                        redraw = true;
                     }
                  }
               }
            }
         }
      }
   }
#endif
   switch(ui.key){
   case K_SEND:
      if(num_c)
         CallContact();
      break;

   case K_ENTER:
      switch(browse_mode){
      case C_mail_client::AB_MODE_SELECT:
         {
            Cstr_c e_mail;
            if(num_c){
               const S_contact &con = curr_list[selection];
               int num_e = con.NumEmails();
               if(num_e>1){
                  menu = CreateMenu();
                  for(int i=0; i<num_e; i++){
                     Cstr_w s; s.Copy(con.GetEmail(i));
                     menu->AddItem(s);
                  }
                  app.PrepareMenu(menu);
                  return;
               }
                                 //contact selected, return
               e_mail = curr_list[selection].GetEmail(0);
            }else{
                                 //get contents of edit field
               e_mail.Copy(ctrl_input->GetText());
               if(!e_mail.Length())
                  break;
               e_mail.ToLower();
            }
            Close(false);
            assert(app.mode->Id()==C_mail_client::C_mode_write_mail_base::ID);
            ((C_mail_client::C_mode_write_mail_base&)*app.mode).AddRecipient(e_mail);
            app.RedrawScreen();
         }
         return;
      case C_mail_client::AB_MODE_UPDATE_CONTACT:
         EditSelected();
         break;
      }
      break;
   }
}

//----------------------------

void C_mode_address_book::DrawContents() const{

   app.ClearWorkArea(rc);
   int num_c = curr_list.size();
   if(num_c){
      dword col_text = app.GetColor(app.COL_TEXT);
                           //draw entries
      int max_x = GetMaxX();
      int x = rc.x;
      int max_width = max_x-rc.x - app.fdb.letter_size_x;
      int sep_width = max_width;
      app.DrawScrollbar(sb);

      S_rect rc_item;
      int item_index = -1;
      while(BeginDrawNextItem(rc_item, item_index)){
         const S_contact &con = curr_list[item_index];

         dword color = col_text;
         if(item_index==selection){
            //S_rect rc(x, y, max_x-rc.x-2, app.fdb.line_spacing*2);
            app.DrawSelection(rc_item);
            color = app.GetColor(app.COL_TEXT_HIGHLIGHTED);
         }
                              //draw separator
         if(item_index && (item_index<selection || item_index>selection+1))
            app.DrawSeparator(x+app.fdb.letter_size_x*1, sep_width-app.fdb.letter_size_x*2, rc_item.y);

                              //draw name & email
         app.DrawString(app.AddressBook_GetName(con), x + app.fdb.letter_size_x, rc_item.y + 1, app.UI_FONT_BIG, FF_BOLD, color, -max_width);
         int yy = rc_item.y + app.fdb.line_spacing;
         int num_e = con.NumEmails();
         int mi = 0;
         if(item_index==selection)
            mi = curr_email_index;
         Cstr_c s = con.GetEmail(mi);
         if(con.mobile.Length() || con.telephone.Length()){
            if(s.Length())
               s<<"     ";
            const char *tel = con.mobile.Length() ? con.mobile : con.telephone;
            s<<tel;
         }
         if(s.Length())
            app.DrawStringSingle(s, x + app.fdb.letter_size_x*2, yy, app.UI_FONT_BIG, 0, color, -(max_width-app.fdb.letter_size_x*3));
         const int arrow_size = (app.fdb.line_spacing/2) | 1;
         if(mi)
            app.DrawArrowHorizontal(x + app.fdb.letter_size_x/2, yy+2, arrow_size, color, false);
         if(mi<num_e-1)
            app.DrawArrowHorizontal(x+max_width-4, yy+2, arrow_size, color, true);
      }
      EndDrawItems();
   }
}

//----------------------------
//----------------------------

const S_config_item ctrls_address_book_edit[] = {
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_FIRST_NAME, 80, OffsetOf(S_contact, first_name), C_text_editor::CASE_CAPITAL, true },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_LAST_NAME, 80, OffsetOf(S_contact, last_name), C_text_editor::CASE_CAPITAL, true },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_EMAIL, 80, OffsetOf(S_contact, email[0]), C_text_editor::CASE_LOWER, false },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_MOBILE, 80, OffsetOf(S_contact, mobile), C_text_editor::CASE_LOWER, false },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_TELEPHONE, 80, OffsetOf(S_contact, telephone), C_text_editor::CASE_LOWER, false },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_EMAIL_1, 80, OffsetOf(S_contact, email[1]), C_text_editor::CASE_LOWER, false },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_EMAIL_2, 80, OffsetOf(S_contact, email[2]), C_text_editor::CASE_LOWER, false },
   { CFG_ITEM_TEXTBOX_CSTR, TXT_CON_COMPANY, 80, OffsetOf(S_contact, company), C_text_editor::CASE_CAPITAL, true },
};

#define NUM_AB_CONTROLS (sizeof(ctrls_address_book_edit)/sizeof(S_config_item))

//----------------------------

class C_mode_address_book_editor: public C_mode_app<C_mail_client>, public C_mode_settings{
   virtual C_application_ui &AppForListMode() const{ return app; }
   virtual bool IsPixelMode() const{ return true; }
   virtual int GetNumEntries() const{
      return NUM_AB_CONTROLS;
   }

   virtual void DrawContents() const{
      Draw();
   }
   virtual void SelectionChanged(int old_sel){
      SetSelection(selection);
   }
   virtual void ScrollChanged(){ if(text_editor) PositionTextEditor(); }
public:
   S_contact con;
   int con_index;          //-1 = creating new

   C_mode_address_book_editor(C_mail_client &_app, int ci):
      C_mode_app<C_mail_client>(_app),
      con_index(ci)
   {
      mode_id = C_mode_address_book::ID;
      selection = -1;
   }
   virtual void InitLayout();
   virtual void ProcessInput(S_user_input &ui, bool &redraw);
//----------------------------
   virtual void ProcessMenu(int itm, dword menu_id){
      if(!menu && text_editor)
         text_editor->Activate(true);
      switch(itm){
      case TXT_EDIT:
         menu = app.CreateEditCCPSubmenu(text_editor, menu);
         app.PrepareMenu(menu);
         break;
      case C_application_ui::SPECIAL_TEXT_CUT:
      case C_application_ui::SPECIAL_TEXT_COPY:
      case C_application_ui::SPECIAL_TEXT_PASTE:
         app.ProcessCCPMenuOption(itm, text_editor);
         break;

      case TXT_DONE:
      case TXT_CANCEL:
         CloseEdit((itm==TXT_DONE));
         return;
      }
   }
//----------------------------
   virtual void Draw() const{
      app.DrawTitleBar(app.GetText(TXT_CONTACT_EDITOR));
      app.DrawSettings(*this, ctrls_address_book_edit, &con, false);

      app.DrawSoftButtonsBar(*this, TXT_MENU, TXT_DONE, text_editor);
   }
//----------------------------
   virtual void TextEditNotify(bool cursor_moved, bool text_changed, bool &redraw){
      if(text_changed){
         if(EnsureVisible())
            PositionTextEditor();
         app.ModifySettingsTextEditor(*this, ctrls_address_book_edit[selection], (byte*)&con);
      }
      redraw = true;
   }
//----------------------------
   void PositionTextEditor(){
      assert(text_editor);

      C_application_ui &app = AppForListMode();
      C_text_editor &te = *text_editor;
      te.SetRect(S_rect(rc.x+app.fdb.letter_size_x, rc.y + 1 + app.fdb.line_spacing + selection*entry_height-sb.pos, max_textbox_width, app.fdb.cell_size_y+1));
   }
//----------------------------
   void SetSelection(int sel);
   void CloseEdit(bool save_changes);
};

//----------------------------

void C_mode_address_book_editor::InitLayout(){

   menu = NULL;
   const int border = 2;
   const int top = app.GetTitleBarHeight();
   rc = S_rect(border, top, app.ScrnSX()-border*2, app.ScrnSY()-top);
   rc.sy -= app.GetSoftButtonBarHeight()+border;
                           //compute # of visible lines, and resize rectangle to whole lines
   entry_height = app.fdb.line_spacing*2 + 2;
   sb.visible_space = rc.sy;// / entry_height;
   //sb.visible_space *= entry_height;
   rc.sy = sb.visible_space;

                           //init scrollbar
   const int width = app.GetScrollbarWidth();
   sb.rc = S_rect(rc.Right()-width-3, rc.y+3, width, rc.sy-6);
   sb.total_space = NUM_AB_CONTROLS * entry_height;
   sb.SetVisibleFlag();

   int max_x = sb.visible ? sb.rc.x-1 : rc.Right();
   max_textbox_width = max_x - rc.x - app.fdb.letter_size_x*3;
   EnsureVisible();
   
   if(text_editor)
      PositionTextEditor();
}

//----------------------------

void C_mode_address_book_editor::SetSelection(int sel){

   selection = Abs(sel);
   text_editor = NULL;

                              //determine field type
   const S_config_item &ec = ctrls_address_book_edit[selection];
   switch(ec.ctype){
   case CFG_ITEM_TEXTBOX_CSTR:
   //case CFG_ITEM_TEXT_NUMBER:
      {
         text_editor = app.CreateTextEditor(
            (!ec.is_wide ? TXTED_ANSI_ONLY : 0),// | (ec.ctype==CFG_ITEM_TEXT_NUMBER ? TXTED_NUMERIC : 0),
            app.UI_FONT_BIG, 0, NULL, ec.param);
         text_editor->Release();
         C_text_editor &te = *text_editor;
         if(!ec.is_wide){
            Cstr_c &str = *(Cstr_c*)((byte*)&con + ec.elem_offset);
            Cstr_w sw; sw.Copy(str);
            te.SetInitText(sw);
         }else{
            Cstr_w &str = *(Cstr_w*)((byte*)&con + ec.elem_offset);
            te.SetInitText(str);
         }
         te.SetCase(C_text_editor::CASE_ALL, ec.param2);
      }
      break;
   case CFG_ITEM_NUMBER:
      {
         text_editor = app.CreateTextEditor(TXTED_NUMERIC, app.UI_FONT_BIG, 0, NULL, ec.param); text_editor->Release();
         C_text_editor &te = *text_editor;
         dword n = *(word*)((byte*)&con + ec.elem_offset);
         Cstr_w s;
         if(n)
            s<<n;
         te.SetInitText(s);
      }
      break;
   }
   if(sel>=0)
      EnsureVisible();
   if(text_editor){
      C_text_editor &te = *text_editor;
      PositionTextEditor();
      app.MakeSureCursorIsVisible(te);
   }
}

//----------------------------

void C_mode_address_book_editor::CloseEdit(bool save_changes){

   int sel = -1;
   if(save_changes){
      if(con_index==-1){
                              //check if not empty
         if(!con.IsEmpty()){
                     //definitely add new entry
            sel = app.address_book->items.Size();
            app.address_book->items.Resize(sel+1);
            app.address_book->items.Back() = con;
         }else
            save_changes = false;
      }else{
         sel = con_index;
         app.address_book->items[sel] = con;
      }
      if(save_changes){
         app.SortAddressBook(sel);
         app.ModifyContact(sel);
      }
   }
   text_editor = NULL;
   Close(false);
   C_mode_address_book &mod_ab = (C_mode_address_book&)*app.mode;
   mod_ab.ctrl_input->SetText(NULL);
   //mod_ab.te->Activate(true);

   mod_ab.InitList();
   if(save_changes)
      mod_ab.selection = sel;

   C_scrollbar &sb = mod_ab.sb;
   sb.SetVisibleFlag();

   mod_ab.EnsureVisible();
   app.RedrawScreen();
}

//----------------------------

void C_mode_address_book_editor::ProcessInput(S_user_input &ui, bool &redraw){

   ProcessInputInList(ui, redraw);
#ifdef USE_MOUSE
   if(app.HasMouse())
   if(!app.ProcessMouseInSoftButtons(ui, redraw)){
      if(text_editor && app.ProcessMouseInTextEditor(*text_editor, ui))
         redraw = true;
   }
#endif

   switch(ui.key){
   case K_RIGHT_SOFT:
   case K_BACK:
   case K_ESC:
      CloseEdit(true);
      return;
   case K_LEFT_SOFT:
   case K_MENU:
      menu = CreateMenu();
      menu->AddItem(TXT_DONE);
      if(text_editor){
         text_editor->Activate(false);
         app.AddEditSubmenu(menu);
      }
      menu->AddSeparator();
      menu->AddItem(TXT_CANCEL);
      app.PrepareMenu(menu);
      return;
   }
}

//----------------------------

void C_mode_address_book::SetModeABEditor(int con_index, const S_contact *con_new){

   C_mode_address_book_editor &mod = *new(true) C_mode_address_book_editor(app, con_index);
   //te->Activate(false);

   if(con_index!=-1){
                              //edit contact
      mod.con = curr_list[selection];
      if(con_new){
                              //update existing
         if(!mod.con.ContainEmail(con_new->email[0])){
            int ei;
            for(ei=0; ei<2; ei++){
               if(!mod.con.email[ei].Length())
                  break;
            }
            mod.con.email[ei] = con_new->email[0];
         }
      }
   }else
      mod.con = *con_new;     //create new

   mod.InitLayout();
   mod.Activate(false);
   mod.SetSelection(0);
   mod.Draw();
}

//----------------------------

void C_mail_client::SetModeAddressBook_NewContact(const S_contact &c, bool update_existing){

   SetModeAddressBook(update_existing ? AB_MODE_UPDATE_CONTACT : AB_MODE_EXPLORE);
   C_mode_address_book &mod = (C_mode_address_book&)*mode;
   if(update_existing){
      mod.edit_contact = c;
                              //preselect most likely contact
      Cstr_w name = AddressBook_GetName(c);
      for(int i=mod.curr_list.size(); i--; ){
         S_contact &cc = mod.curr_list[i];
         if(AddressBook_GetName(cc)==name){
            mod.selection = i;
            mod.curr_email_index = 0;
            mod.EnsureVisible();
            RedrawScreen();
            break;
         }
      }
   }else{
      mod.SetModeABEditor(-1, &c);
   }
}

//----------------------------

void C_mail_client::SetModeAddressBook(E_ADDRESS_BOOK_MODE am){

   OpenAddressBook();
   new(true) C_mode_address_book(*this, am);
}

//----------------------------
