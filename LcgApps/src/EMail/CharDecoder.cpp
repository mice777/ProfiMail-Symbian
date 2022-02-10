#ifdef __SYMBIAN32__
#include <e32std.h>
#endif

#include "..\Main.h"
#include "Main_Email.h"

#ifdef __SYMBIAN32__
#include <charconv.h>
#include <coemain.h>
#endif

#if defined _WINDOWS || defined WINDOWS_MOBILE
#include <String.h>
namespace win{
#include <Windows.h>
#undef GetDriveType
#undef DRIVE_REMOVABLE
}
#endif

//----------------------------

void C_mail_client::CloseCharConv(){
#ifdef __SYMBIAN32__
   delete (CCnvCharacterSetConverter*)char_conv;
   char_conv = NULL;
#endif
}

void C_mail_client::ConvertMultiByteStringToUnicode(const char *src, E_TEXT_CODING coding, Cstr_w &dst) const{

   switch(coding){
   case COD_BIG5:
   case COD_GB2312:
   case COD_GBK:
   case COD_SHIFT_JIS:
   case COD_JIS:
   case COD_EUC_KR:
   case COD_HEBREW:
      {
         dword src_len = StrLen(src);
         C_buffer<wchar> tmp;
         const int dst_len = src_len;
         tmp.Resize(dst_len);
         wchar *wt = tmp.Begin();
#ifdef __SYMBIAN32__
         if(!char_conv)
            char_conv = CCnvCharacterSetConverter::NewL();
         CCnvCharacterSetConverter *cnv = (CCnvCharacterSetConverter*)char_conv;
         dword cp;
         switch(coding){
         case COD_BIG5: cp = KCharacterSetIdentifierBig5; break;
         case COD_GB2312: cp = KCharacterSetIdentifierGb2312; break;
         case COD_GBK: cp = KCharacterSetIdentifierGbk; break;
         case COD_SHIFT_JIS: cp = KCharacterSetIdentifierShiftJis; break;
         case COD_JIS: cp = KCharacterSetIdentifierIso2022Jp; break;
         case COD_HEBREW: cp = KCharacterSetIdentifierIso88598; break;
         case COD_EUC_KR: cp = 0x2000e526; break; //KCharacterSetIdentifierEUCKR;
         default:
            dst.Copy(src);
            return;
         }
         CCnvCharacterSetConverter::TAvailability av = cnv->PrepareToConvertToOrFromL(cp, CCoeEnv::Static()->FsSession());
         if(av!=CCnvCharacterSetConverter::EAvailable){
            dst.Copy(src);
            return;
         }
         TInt state = CCnvCharacterSetConverter::KStateDefault;
         TPtr des((word*)wt, dst_len);
         int n = cnv->ConvertToUnicode(des, TPtrC8((byte*)src, src_len), state);
         if(n<0){
            assert(0);
            dst.Copy(src);
            return;
         }
         assert(!n);
         n = des.Length();
#else
                              //codepages: msdn2.microsoft.com/en-us/library/ms776446.aspx
         int cp;
         switch(coding){
         default:
         case COD_BIG5: cp = 950; break;
         case COD_GB2312: cp = 936; break;
         case COD_GBK: cp = 936; break;
         case COD_JIS: cp = 50220; break;
         case COD_SHIFT_JIS: cp = 932; break;
         case COD_EUC_KR: cp = 949; break;
         case COD_HEBREW: cp = 1255; break;
         }
         dword n;
         if(!src_len)
            n = 0;
         else{
            n = win::MultiByteToWideChar(cp, 0, src, src_len, wt, dst_len);
            if(!n){
#ifdef _DEBUG
               int err = win::GetLastError();
               err = err;
#endif
               dst.Copy(src);
               return;
            }
         }
#endif
         dst.Allocate(wt, n);
      }
      break;
   default:
      C_client::ConvertMultiByteStringToUnicode(src, coding, dst);
   }
}

//----------------------------
