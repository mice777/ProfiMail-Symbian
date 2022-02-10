#include "..\FileBrowser.h"
#include "..\SimpleSoundPlayer.h"
#include "..\NotifyWindow.h"
#include <Ui\MultiQuestion.h>
#include <Xml.h>
#include <Phone.h>
#include <Zlib.h>
#include <Ui\TextEntry.h>

//----------------------------

#if defined __SYMBIAN_3RD__ && !defined _DEBUG
#define DATA_PATH_PREFIX L"\\System\\Data\\ProfiMail\\"
#else
#define DATA_PATH_PREFIX L"Data\\"
#endif

extern const wchar MAIL_PATH[];     //must be in lower-case

#if defined S60 && defined __SYMBIAN_3RD__
#define USE_NOKIA_N97_WIDGET
#define USE_SYSTEM_PROFILES
#endif

#if defined __SYMBIAN_3RD__
#define USE_SYSTEM_VIBRATE
#endif

#ifdef USE_ALT_IAP
#define AUTO_CONNECTION_BACK_SWITCH
#endif

//#define THREAD_SORT_DESCENDING_ALTERNATIVE

extern const wchar NETWORK_LOG_FILE[];

#ifdef _DEBUG
//#define UPDATE_IN_BACKGROUND
#endif

//----------------------------

enum E_CONTENT_ENCODING{
   ENCODING_7BIT,
   ENCODING_8BIT,
   ENCODING_BINARY,
   ENCODING_QUOTED_PRINTABLE,
   ENCODING_BASE64,
};

//----------------------------

enum E_CONTENT_DISPOSITION{
   DISPOSITION_INLINE,
   DISPOSITION_ATTACHMENT,
   DISPOSITION_UNKNOWN
};

//----------------------------

struct S_date_time_x: public S_date_time{
//----------------------------
// Get English names of day and month, in 3-letter format (Mon..., Jan...), computed from date stored in this class.
   const char *GetDayName() const;
   const char *GetMonthName() const;
};

//----------------------------

struct S_attachment{
   Cstr_c filename;           //if null, then attachments was not retrieved
   Cstr_w suggested_filename;
   Cstr_c content_id;         //mainly used for attachments in multipart/related parts - images embedded in and referenced by html text body

                              //v2.34:
   dword file_size;           //size of file (reported by server, not actual file on disk)
   E_CONTENT_ENCODING content_encoding;
   word part_index;

   S_attachment():
      content_encoding(ENCODING_7BIT),
      file_size(0),
      part_index(0xffff)
   {}
   inline bool IsDownloaded() const{ return (filename.Length()!=0); }

   void Save(C_file &ck) const;
   bool Load(C_file &ck, dword save_version);

};

//----------------------------
#include "..\Viewer.h"

//----------------------------
                              //header base, used for sync tasks
struct S_message_header_base{
                              //unique message identifier
   Cstr_c pop3_uid;
   dword imap_uid;
   union{
      int pop3_server_msg_index;    //index of pop3 message (hint, must be verified)
      int imap_text_part_size;
   };
   dword size;
   dword flags;               //combination of MSG_* flags
   enum{
      MSG_READ = 1,           //msg was already read
      MSG_DELETED = 2,        //msg is scheduled for deletion (at next connection)
      MSG_HTML = 4,           //html text
      MSG_PRIORITY_LOW = 8,

      MSG_PRIORITY_HIGH = 0x10,
      MSG_SERVER_SYNC = 0x20, //message synced with remote server
      MSG_DRAFT = 0x40,       //created message, saved as draft
      MSG_TO_SEND = 0x80,     //message ready to be send

      MSG_SENT = 0x100,       //message was sent
      MSG_REPLIED = 0x200,    //message was replied to
      MSG_FORWARDED = 0x400,  //message was forwarded
      MSG_HIDDEN = 0x800,     //message hidden from user's view

                              //IMAP sync flags - need to upload to server
      MSG_IMAP_REPLIED_DIRTY = 0x1000,
      MSG_IMAP_READ_DIRTY = 0x2000,
      MSG_HAS_ATTACHMENTS = 0x4000, //mark that message has attachment(s) (valid even if only header is downloaded; works only for IMAP)
      MSG_PARTIAL_DOWNLOAD = 0x8000,   //message downloaded partially (truncated)

      MSG_FLAGGED = 0x10000,
      MSG_IMAP_FLAGGED_DIRTY = 0x20000,
      MSG_DELETED_DIRTY = 0x40000,
      MSG_NEED_UPLOAD = 0x80000,    //drafts

      MSG_RECENT = 0x100000,
      MSG_COLOR_SHIFT = 21,   //4 bits
      MSG_TEXT_PART_TYPE_SHIFT = 25,   //2 bits; 0=unknown, 1=part 1, 2=part 1.1, 3=part 1.2 (2,3 = subparts of multipart/alternative); set only for multipart/mixed msgs
      MSG_IMAP_FORWARDED_DIRTY = 0x8000000,
   };

   S_message_header_base():
      size(0),
      flags(0),
      imap_uid(0),
      pop3_server_msg_index(-1)
   {}

   bool MatchUID(const S_message_header_base &hdr, bool is_imap) const{
      if(is_imap)
         return (imap_uid==hdr.imap_uid);
      return (pop3_uid==hdr.pop3_uid);
   }

   inline bool IsDeleted() const{ return (flags&MSG_DELETED); }
   inline bool IsRead() const{ return (flags&MSG_READ); }
   inline bool IsHidden() const{ return (flags&MSG_HIDDEN); }
   inline bool IsDraft() const{ return (flags&MSG_DRAFT); }
   inline bool IsFlagged() const{ return (flags&MSG_FLAGGED); }
   inline bool IsServerSynced() const{ return (flags&MSG_SERVER_SYNC); }
   inline bool IsRecent() const{ return (flags&MSG_RECENT); }
};

//----------------------------

struct S_identity{
   Cstr_c display_name;       //in utf8
   Cstr_c email;
   Cstr_c reply_to_email;
   bool operator==(const S_identity &i) const{
      return (display_name==i.display_name && email==i.email && reply_to_email==i.reply_to_email);
   }
};

//----------------------------
                              //message header (also saved on disk)
struct S_message_header: public S_message_header_base{
   Cstr_c subject;            //in utf8
   S_identity sender;
   Cstr_c to_emails;          //"To:" (email addresses)
   Cstr_c to_names;           //"To:" (names)
   Cstr_c cc_emails;          //"Cc:"
   Cstr_c bcc_emails;         //"Bcc:"
   Cstr_c reply_to_email;     //"Reply-to:"
   Cstr_c message_id;         //"Message-ID:" (without < and >)
   Cstr_c our_message_id;     //"Message-ID:"
   Cstr_c references;         //"References:" (cleaned to not contain last message_id in case of draft)
   dword date;                //seconds

   bool marked;               //editor only, not saved

   S_message_header():
      date(0),
      marked(false)
   {
   }
   bool operator ==(const S_message_header &hdr) const;
};

//----------------------------
                              //complete header, used only when retrieving
struct S_complete_header: public S_message_header{
   S_content_type content;
   E_CONTENT_ENCODING content_encoding;
   E_CONTENT_DISPOSITION content_disposition;
   E_TEXT_CODING text_coding;           //used with text/* MIME type
   Cstr_c multipart_boundary;
   Cstr_w suggested_filename;
   Cstr_c content_id;
   Cstr_c cc_names;           //Cc:"
   Cstr_c complete_headers;
   int x_spam_score;          //multiplied by 100
   bool format_flowed, format_delsp;

   S_complete_header():
      content_encoding(ENCODING_7BIT),
      text_coding(COD_DEFAULT),
      content_disposition(DISPOSITION_UNKNOWN),
      format_flowed(false),
      format_delsp(false),
      x_spam_score(0)
   {}
   void Reset(){
      *this = S_complete_header();
   }
};

//----------------------------
class C_message_container;

struct S_message: public S_message_header{
                              //filename of mail body
   Cstr_c body_filename;
   E_TEXT_CODING body_coding;
   byte thread_level;         //in threaded sorting, this is set to level of threaded message
                              //real attachments
   C_buffer<S_attachment> attachments;
                              //inline attachments (usually html-related images or other content)
   C_buffer<S_attachment> inline_attachments;

   S_message():
      body_coding(COD_DEFAULT),
      thread_level(0)
   {}
   S_message(const S_message_header &hdr):
      S_message_header(hdr)
   {}

   inline bool HasBody() const{ return (body_filename.Length()!=0); }

   void Save(class C_file &ck, bool is_imap) const;
   bool Load(C_file &ck, dword save_version, bool is_imap);

//----------------------------
// Move all message's files from one folder to another.
   void MoveMessageFiles(const Cstr_w &mail_data_path, C_message_container &from, C_message_container &to);

//----------------------------
// Update non-dirty server flags.
   bool UpdateFlags(dword flags);

   bool HasMultipleRecipients(const struct S_account_settings *acc) const;
};

//----------------------------

class C_message_container: public C_unknown{
   bool need_save;
public:
   bool is_imap;
   bool loaded;
   mutable bool stats_dirty;

   C_vector<S_message> messages;
   dword msg_folder_id;
   dword last_msg_cleanup_day;

   Cstr_c folder_name;       //in utf-8

   enum{
      FLG_HIDDEN = 1,
      FLG_TEMP = 2,           //created for internal purposes (might not be on server)
      FLG_NEED_SORT = 4,      //sort messages next time when they're loaded
      FLG_NOSELECT = 8,       //folder is not selectable in IMAP account
      FLG_EXPANDED = 0x10,    //subfolders are expanded
      FLG_NOINFERIORS = 0x20, //folder can't have IMAP subfolders
   };
   dword flags;

   enum E_STATISTICS{
      STAT_READ,
      STAT_UNREAD,
      STAT_DRAFTS,
      STAT_TO_SEND,
      STAT_SENT,
      STAT_RECENT,
      STAT_LAST
   };
   dword stats[STAT_LAST];
   dword imap_uid_validity;

   C_buffer<C_smart_ptr<C_message_container> > subfolders;
   C_message_container *parent_folder;

   C_message_container();

   void MakeDirty();
public:
   bool LoadMessages(const Cstr_w &mail_data_path);
   void SaveMessages(const Cstr_w &mail_data_path, bool force = false);
   void SaveAndUnloadMessages(const Cstr_w &mail_data_path);

//----------------------------
// Cleanup contents of container's folder.
   void CleanupMailFiles(const Cstr_w &mail_data_path);

//----------------------------
// Delete all external files associated with message.
// Returns true if 'msg' was modified.
   bool DeleteMessageFiles(const Cstr_w &mail_data_path, S_message &msg) const;
   bool DeleteMessageBody(const Cstr_w &mail_data_path, S_message &msg) const;
   bool DeleteMessageAttachments(S_message &msg, bool files_only = false) const;

//----------------------------
// Get complete mail path to this container's folder (including last '\').
   Cstr_w GetMailPath(const Cstr_w &mail_data_path) const;

//----------------------------

   void ResetStats();

//----------------------------
// Get statistics about messages.
   void BuildMessagesStatistics();
   const dword *GetMessagesStatistics() const;

   void CollectHierarchyStatistics(dword stats[], bool show_hidden) const;

//----------------------------
   void MoveMessageTo(const Cstr_w &mail_data_path, S_message &src, const C_message_container &dst_cnt, S_message &dst) const;

//----------------------------
// Delete all container's messages, associated files, and entire folder.
   void DeleteContainerFiles(const Cstr_w &mail_data_path);

//----------------------------
   dword GetMaxUid() const;

   bool IsEmpty(const Cstr_w &mail_data_path) const;
   inline bool IsTemp() const{ return (flags&FLG_TEMP); }
   inline bool IsHidden() const{ return (flags&FLG_HIDDEN); }
   inline bool IsExpanded() const{ return (flags&FLG_EXPANDED); }

//----------------------------
// Check if folder name is "inbox" (case non-sensitive).
   bool IsInbox() const;

   dword GetTotalMessageSize(const S_message &msg, const Cstr_w &mail_data_path) const;

//----------------------------
   void ClearAllMessageMarks();
};

typedef C_buffer<C_smart_ptr<C_message_container> > t_folders;

//----------------------------

class C_folders_iterator{
   t_folders &folders;
   C_vector<int> levels;
   bool show_hidden, show_collapsed;
   void PrepareNext();
public:
   C_folders_iterator(t_folders &f, bool _show_hidden = true, bool _show_collapsed = true):
      folders(f),
      show_hidden(_show_hidden),
      show_collapsed(_show_collapsed)
   {
      levels.push_back(-1);
      PrepareNext();
   }
   inline bool IsEnd() const{ return (levels.size()==0); }
   C_message_container *PeekNext();
   C_message_container *Next();
};

//----------------------------

struct S_account_settings{
   Cstr_w name;
   S_identity primary_identity;
   Cstr_c mail_server;        //in utf-8
   Cstr_c smtp_server;        // ''
   Cstr_c username, password;
   Cstr_c smtp_username, smtp_password;
   word port_in, port_out;
   Cstr_w imap_draft_folder, imap_sent_folder, imap_trash_folder, imap_root_path;
   Cstr_c send_msg_copy_to;   //email address where message copy will be sent (as Bcc)
   Cstr_c signature_name;     //auto-inserted signature, in utf-8
   dword max_kb_to_retrieve;  //max kb of message to get; 0 = no limit
   word imap_last_x_days_limit;  //last days to see in mailbox; 0 = all messages
   static const word IMAP_IDLE_DEFAULT_PING_TIME = 25;
   word imap_idle_ping_time;  //minutes how often idle is pinged
   char imap_folder_delimiter;
   enum{
      ACC_INCLUDE_IN_UPDATE_ALL = 1,
      ACC_UPDATE_GET_ENTIRE_MSG = 2,//when updating mailbox, get entire msg (as opposed to only headers)
      ACC_USE_IMAP4 = 4,            //use IMAP4 mail protocol (otherwise use POP3)
      ACC_USE_SMTP_AUTH = 8,        //use authentication for smtp server
      _ACC_USE_SSL_IN = 0x10,        //use SSL for incoming server
      _ACC_USE_SSL_OUT = 0x20,       //use SSL for outgoing server
      ACC_USE_APOP = 0x40,          //use APOP login for POP3 server
      ACC_IMAP_UPDATE_INBOX_ONLY = 0x80, //update only INBOX folder
      ACC_IMAP_UPLOAD_SENT = 0x100, //upload sent messages onto server
      ACC_IMAP_DOWNLOAD_ATTACHMENTS = 0x200, //download IMAP attachments immediately
      _ACC_SMTP_USE_STARTTLS = 0x400, //use STARTTLS command for SSL SMTP
      ACC_NEED_FOLDER_REFRESH = 0x800, //set after creation to refresh IMAP folders
      ACC_USE_IMAP_IDLE = 0x1000,
   };
   dword flags;
   bool save_sent_messages, move_to_trash;
   enum E_SECURE_CONN{
      SECURE_NO,
      SECURE_SSL,
      SECURE_STARTTLS,
      SECURE_LAST
   } secure_in, secure_out;

   static const wchar inbox_folder_name[], default_sent_folder_name[], default_trash_folder_name[], default_outbox_folder_name[], default_draft_folder_name[];

   S_account_settings();

   inline bool IsImap() const{ return (flags&ACC_USE_IMAP4); }
};

//----------------------------

struct S_contact{
   Cstr_w first_name, last_name, company;
   Cstr_c email[3];
   Cstr_c telephone, mobile;

#ifdef __SYMBIAN32__
   int email_id[3];
#endif
   int phone_id;              //id of contact in phone book (for sync)
   S_contact();

   bool operator ==(const S_contact &c) const;
   bool IsEmpty() const;

//----------------------------
// Assign name - parse first and last part;
   void AssignName(const wchar *n);

   dword NumEmails() const;
   const Cstr_c &GetEmail(int i) const;

   bool ContainEmail(const Cstr_c &s) const;
// Return bits for which emails the string matches beginning.
   dword BeginsWithEmail(const Cstr_c &s) const;
};

//----------------------------

struct S_contact_match: public S_contact{
   dword email_match_mask;
};


class C_address_book: public C_unknown{
public:
   C_buffer<S_contact> items;
};
//----------------------------

struct S_signature{
   Cstr_w name;
   Cstr_w body;
};

//----------------------------
const int ATTACH_BROWSER_NUM_ICONS = 3;

class C_attach_browser{
   class C_mail_client &app;
   S_rect rc_arrow[2];        //arrows indicating that scrolling is possible
   S_rect rc_icons;           //rectangle for all icons; sx must be sy*ATTACH_BROWSER_NUM_ICONS
   int scroll_offset;         //horizontal scroll offset in pixels
   int touch_down_selection;
   int beg_drag_offset;       //-1 = not dragging
   mutable C_vector<C_smart_ptr<C_image> > img_thumbnails;
public:
   S_rect rc;                 //entire rectangle of browser
   int selection;             //currently selected item

   C_attach_browser(C_mail_client &_app):
      app(_app),
      selection(0),
      scroll_offset(0),
      beg_drag_offset(-1),
      touch_down_selection(-1)
   {}

//----------------------------
// Initialize browser - setup rectangles.
   void Init();

//----------------------------
// Returns true if icon has been clicked (by mouse click).
   bool Tick(const S_user_input &ui, int num_atts, bool &redraw, bool &sel_changed, bool &popup_touch_menu);

   void ResetTouchInput(){
      touch_down_selection = -1;
      beg_drag_offset = -1;
   }

   void Draw(const C_buffer<S_attachment> &attachments, C_fixed cursor_alpha = C_fixed::One()) const;

   void MakeSelectionVisible();
   void ResetAfterScreenResize(){ img_thumbnails.clear(); }
};

//----------------------------
#ifdef USE_NOKIA_N97_WIDGET
#include "..\Symbian\Mail\HsWidget\Main.h"
#endif

//----------------------------

class C_mail_client: public C_client{
   typedef C_client super;

   C_timer *timer;            //global timer
   dword curr_timer_freq;
   bool tick_was_working_time;
   C_timer *work_time_alarm;  //for starting work times
   int auto_update_counter;
   bool exiting;
   
   struct S_theme{
      dword bgnd, selection, title, scrollbar, text_color;
   };
   static const S_theme color_themes[];
   static const int NUM_COLOR_THEMES;

   S_theme system_theme;

   mutable void *char_conv;
   void CloseCharConv();

   virtual void ConvertMultiByteStringToUnicode(const char *src, E_TEXT_CODING coding, Cstr_w &dst) const;

public:
   virtual dword GetColor(E_COLOR col) const;

   enum E_BOTTOM_BUTTON_MAIL{
      BUT_REPLY = BUT_LAST_BASE,
      BUT_FORWARD,
      BUT_UPDATE_MAILBOX,
      BUT_FILE_EXPLORER,
      BUT_ADDRESS_BOOK,
      BUT_NEW,
      BUT_SEND,
      BUT_ADD_ATTACHMENT,
      BUT_ADD_SIGNATURE,
      BUT_MARK_AS_READ,
      BUT_DISCONNECT_ALL,
      BUT_CONNECT_ALL,
   };

   dword GetNextEnabledWorkTime() const;

   void ManageTimer();

protected:
   friend class C_client;
   friend class C_client_file_mgr;
   friend class C_client_viewer;

   virtual const wchar *GetDataFileName() const{
#ifdef ANDROID_
      return NULL;
#else
      return L"Email\\PM.dta";
#endif
   }

//----------------------------
   class C_mode_connection_imap;
public:
   static const dword MAX_ACCOUNTS = 30;

   struct S_account: public S_account_settings{
   private:
      Cstr_w GetFolderNameWithRoot(const Cstr_w &n) const;
   public:
      t_folders _folders;
      C_buffer<S_identity> identities;

      C_smart_ptr<C_socket> socket;
      dword imap_capability;
      C_smart_ptr<C_message_container> selected_folder;

      enum E_UPDATE_STATE{
         UPDATE_DISCONNECTED,
         UPDATE_INIT,
         UPDATE_WORKING,
         UPDATE_IDLING,
         UPDATE_ERROR,
         UPDATE_FATAL_ERROR,
      };

      class C_background_processor{
      public:
         C_smart_ptr<C_mode> auth_check;
         C_smart_ptr<C_mode> mode;
         E_UPDATE_STATE state;
         union{
            dword error_time;    //time when error happened, for reconnecting
            dword idle_begin_time;  //time of beginning of idling, for pinging after some minutes
         };
         Cstr_w status_text;
         int progress_pos, progress_total;

         void Close(){
            auth_check = NULL;
            mode = NULL;
            state = UPDATE_DISCONNECTED;
            error_time = 0;
            status_text.Clear();
            progress_total = 0;
         }
         inline C_mode_connection_imap *GetMode(){ return (C_mode_connection_imap*)(C_mode*)mode; }
         inline const C_mode_connection_imap *GetMode() const{ return (const C_mode_connection_imap*)(const C_mode*)mode; }

         C_background_processor():
            state(UPDATE_DISCONNECTED),
            progress_total(0)
         {}

         inline bool IsIdling() const{ return (state==S_account::UPDATE_IDLING); }
      } background_processor;

      bool use_imap_idle;  //enabled/disabled idle mode

      S_account():
         use_imap_idle(false)
      {}

      void CloseConnection(){
         socket = NULL;
         imap_capability = 0;
         selected_folder = NULL;
         background_processor.Close();
      }

      void CloseIdleConnection(){
         CloseConnection();
         use_imap_idle = false;
      }

   //----------------------------
   // Delete all account's folders.
      void DeleteAllFolders(const Cstr_w &mail_data_path);

   //----------------------------
   // Delete folder from account. This also deletes all messages stored in the folder, and associated files, and all subfolders.
      void DeleteFolder(const Cstr_w &mail_data_path, C_message_container *cnt, bool delete_subfolders = true);

      void GetMessagesStatistics(dword stats[C_message_container::STAT_LAST]) const;

      Cstr_w GetDraftFolderName() const;
      Cstr_w GetSentFolderName() const;
      inline Cstr_w GetTrashFolderName() const{ return GetFolderNameWithRoot(imap_trash_folder); }

      Cstr_w GetFullFolderName(const C_message_container &cnt) const;
      dword NumFolders() const;
   //----------------------------
   // Get folder name encoded for IMAP protocol.
      Cstr_c GetImapEncodedName(const C_message_container &cnt) const;
   };
   C_buffer<S_account> accounts;
   inline dword NumAccounts() const{ return accounts.Size(); }
protected:
//----------------------------

   virtual void TimerTick(C_timer *t, void *context, dword ms);
   virtual void AlarmNotify(C_timer *t, void *context);
   virtual void ClientTick(C_mode &mod, dword time);
   virtual void ProcessInput(S_user_input &ui);
   virtual void InitAfterScreenResize();

   virtual void SocketEvent(E_SOCKET_EVENT event, C_socket *socket, void *context);

   void ProcessImapIdle(S_account &acc, E_SOCKET_EVENT ev);
   void PerformAutoUpdate();

//----------------------------
   int pass_enter_attempts_left;
                              //C_application_base methods:
   virtual void FocusChange(bool foreground);
#ifdef _DEBUG
   virtual void OpenDocument(const wchar *fname);
#endif
   void MailBaseConstruct();
   virtual void Construct();

   void FinishConstructWithPass(const Cstr_w &password, bool after_import = false);
   virtual void FinishConstruct();

public:
//----------------------------
// Ask password for loading account.
   void AskInitPassword();
   void AskNewPassword();
   void AskPasswordWhenFocusGained(bool blank_screen_bgnd);
protected:
//----------------------------
   struct S_data_location{
      Cstr_w path, name;
   };
//----------------------------
// Get all locations where mail data could be stored (device memory, cards), this is drive letter: on all systems except of WinCE where it is full path.
// Return index of currently active location.
   int GetPossibleDataLocations(C_vector<S_data_location> &paths);

//----------------------------

   void LoadGraphics();

//----------------------------
   enum E_CONFIG_ITEM_TYPE{
      //CFG_ITEM_COUNTER_MODE
      CFG_ITEM_TIME_OUT = CFG_ITEM_LAST_CLIENT,
      CFG_ITEM_ALERT_SOUND,
      CFG_ITEM_ALERT_VOLUME,
      CFG_ITEM_DATE_FORMAT,
      //CFG_ITEM_IMAGE_SCALE,
      CFG_ITEM_MAIL_SERVER_TYPE,
      CFG_ITEM_ADVANCED,
      CFG_ITEM_IMAP_CONNECT_MODE,
      CFG_ITEM_WORK_HOURS_SUM,
      CFG_ITEM_WORK_HOUR,
      CFG_ITEM_TWEAKS,
      CFG_ITEM_TWEAKS_RESET,
      CFG_ITEM_DATA_LOCATION,
      CFG_ITEM_SECURITY,
      CFG_ITEM_ENUM,
      CFG_ITEM_IDENTITIES,
      CFG_ITEM_LAST
   };
   static const S_config_item config_options[], config_work_hours[], config_language, config_tweaks[];

//----------------------------
   class C_configuration_editing_email: public C_configuration_editing_client{
      typedef C_configuration_editing_client super;
      C_mail_client &App(){ return (C_mail_client&)app; }
   public:
      int init_loc_index, curr_loc_index;
      C_vector<S_data_location> data_locations;

      C_configuration_editing_email(C_mail_client &_a):
         super(_a),
         init_loc_index(0), curr_loc_index(0)
      { }
      virtual void OnConfigItemChanged(const S_config_item &ec);
      virtual void OnClose();
      virtual void OnClick(const S_config_item &ec);
   };
//----------------------------
   class C_mode_config_mail;
   friend class C_mode_config_mail;

   class C_mode_config_mail: public C_mode_config_client{
      typedef C_mode_config_client super;
   protected:
      inline C_mail_client &App(){ return (C_mail_client&)app; }
      inline const C_mail_client &App() const{ return (C_mail_client&)app; }

      bool IsDefaultAlert() const;
   public:
      C_mode_config_mail(C_mail_client &_app, const S_config_item *opts, dword num_opts, C_configuration_editing_email *ce);

      virtual bool GetConfigOptionText(const S_config_item &ec, Cstr_w &str, bool &draw_left_arrow, bool &draw_right_arrow) const;
      virtual bool ChangeConfigOption(const S_config_item &ec, dword key, dword key_bits);
      virtual dword GetLeftSoftKey(const S_config_item &ec) const;
   };

//----------------------------
   class C_configuration_editing_work_times: public C_configuration_editing_email{
      typedef C_configuration_editing_email super;
   public:
      C_configuration_editing_work_times(C_mail_client &_a):
         super(_a)
      { }
      //virtual void OnConfigItemChanged(const S_config_item &ec);
      //virtual void OnClose();
      //virtual void OnClick(const S_config_item &ec);
   };

   class C_mode_config_work_times;
   friend class C_mode_config_work_times;

   class C_mode_config_work_times: public C_mode_config_mail{
   public:
      C_mode_config_work_times(C_mail_client &_app, const S_config_item *opts, dword num_opts, C_configuration_editing_email *ce):
         C_mode_config_mail(_app, opts, num_opts, ce)
      {}
      ~C_mode_config_work_times(){
         if(modified){
            delete App().work_time_alarm;
            App().work_time_alarm = NULL;
         }
      }
   };

//----------------------------
public:
   void CloseConfigNotify(const C_vector<S_data_location> &data_locations, int curr_loc_index, int init_loc_index);
   //void CloseLangSelectNotify(C_mode_configuration &mod);
   //E_TEXT_ID LangSelectGetLeftSoftKey(const C_mode_configuration &mod, const S_config_item &ec){ return TXT_OK; }
   //void LangSelectProcessInput(C_mode_configuration &mod, S_user_input &ui, bool &redraw);
   void ConfigMailLocationChanged(dword index);

   void SetConfig();
   void SetConfigTweaks();

   dword last_focus_loss_time;

   static const dword MODE_ID_PASS_ENTER = FOUR_CC('A','P','W','D');

   Cstr_w GetAudioAlertRealFileName(C_client_file_mgr::C_mode_file_browser &mod, const wchar *fn);
   void SelectAudioAlert(C_client_file_mgr::C_mode_file_browser::t_OpenCallback, const Cstr_w &fn_base);
   bool AlertSoundSelectCallback(const Cstr_w *file, C_vector<Cstr_w> *files);

//----------------------------

   C_vibration vibration;
   bool MakeVibration(){
      if(!config_mail.tweaks.vibration_length)
         return true;
      return vibration.Vibrate(config_mail.tweaks.vibration_length);
   }

//----------------------------
                              //base path to data, on Symbian it is drive letter (e.g. "C:"), on WM it is full path ("\\Storage Card\\Program Files\\ProfiMail\\")
                              //stored separately from Config, because config is also stored there
   Cstr_w mail_data_path;

//----------------------------

   void LoadDataPath();
   void SaveDataPath() const;

//----------------------------

   void ConvertFormattedTextToPlainText(const S_text_display_info &td, C_vector<wchar> &body);

   const Cstr_w &GetMailDataPath() const{ return mail_data_path; }

   struct S_config_mail: public S_config{
      enum{
#ifndef USE_SYSTEM_VIBRATE
         CONF_VIBRATE_ALERT = 0x10000,
#endif
         CONF_SHOW_PREVIEW = 0x40000,     //show preview in mailbox
         CONF_SEND_MSG_IMMEDIATELY = 0x80000,   //send msg immediately after finishing writing

         _CONF_SORT_DESCENDING = 0x100000,       //sort direction
         CONF_AUTO_START = 0x200000,            //start application automatically after phone boot
         CONF_DOWNLOAD_HTML_IMAGES = 0x400000,  //download images in html mail
         // = 0x800000,

         //CONF_SORT_BY_DATE = 0x0000000,
         //CONF_SORT_BY_SUBJECT = 0x1000000,
         //CONF_SORT_BY_SENDER = 0x2000000,
         _CONF_SORT_MASK = 0x3000000,
      };
      dword last_online_check_day;     //last day when we performed online check
      enum E_DATE_FORMAT{
         DATE_MM_SLASH_DD,
         DATE_MM_DASH_DD,
         DATE_DD_SLASH_MM,
         DATE_DD_DASH_MM,
         DATE_DD_DOT_MM_DOT,
         DATE_DD_DOT_MM_DOT_YY,
         DATE_DD_SLASH_MM_SLASH_YY,
         DATE_MM_SLASH_DD_SLASH_YY,
         DATE_MM_DASH_DD_DASH_YY,
         DATE_DD_DASH_MM_DASH_YY,
         DATE_YY_DASH_MM_DASH_DD,
         DATE_LAST
      } date_format;
      enum E_SORT_MODE{
         SORT_BY_DATE,
         SORT_BY_SUBJECT,
         SORT_BY_SENDER,
         SORT_BY_RECEIVE_ORDER,
      } sort_mode;
      bool sort_descending;
      static Cstr_w GetDateFormatString(E_DATE_FORMAT tf);

      int last_msg_cleanup_day;  //last day when message cleanup was made
      dword auto_check_time;  //in minutes
      word alert_volume;      //0 - 10
      Cstr_w alert_sound;  //filename of audio sound played for new msg
      static Cstr_w GetDefaultAlertSound();
      void SetDefaultAlertSound();

      Cstr_w _app_password;   //old, saved in config, will be converted to non-saved app_password
      Cstr_w app_password;
      byte audio_volume;      //volume of audio preview player
      bool imap_auto_expunge;
      bool sort_by_threads;
      bool sort_contacts_by_last;
      enum E_IDLE_CONNECT_MODE{
         IDLE_CONNECT_MANUAL,
         IDLE_CONNECT_ASK,
         IDLE_CONNECT_AUTOMATIC,
      } imap_idle_startup_connect;

                              //unhash password
      Cstr_w _GetPassword() const;

      word work_time_beg, work_time_end;  //in minutes, starting from 0:00
      byte work_days;         //day bits
      bool work_times_set;

   //----------------------------
   // Check if current time is work hour. If work hours are set, this is inside of defined period, otherwise it is always.
      bool IsWorkingTime() const;

      struct S_tweaks{
         word preview_area_percent;
         word vibration_length;
         bool imap_go_to_inbox;
         bool show_new_mail_notify;
         bool focus_go_to_main_screen;
         bool prefer_plain_text_body;
         bool reply_below_quote;
         bool quote_when_reply;
         bool open_links_by_system;
         bool no_led_flash;
         bool show_minimum_counters;
         bool scroll_preview_marks_read;
         bool check_mail_on_startup;
         bool detect_phone_numbers;
         bool show_recent_flags;
         Cstr_c date_fmt;
         word pass_ask_timeout;
         bool show_only_unread_msgs;
         bool always_keep_messages;
         bool exit_in_menus;
         bool ask_to_exit;
#if defined __SYMBIAN32__ && defined __SYMBIAN_3RD__
         bool red_key_close_app;
#endif
         void SetDefaults();
      } tweaks;

      S_config_mail();
   };
   S_config_mail config_mail;
protected:

#if defined __SYMBIAN32__ && defined __SYMBIAN_3RD__
   virtual bool RedKeyWantClose() const;
#endif

//----------------------------

   C_smart_ptr<C_unknown> phone_call;
   void CallNumberConfirm(const Cstr_w &txt);
public:
   class C_text_entry_call;
   friend class C_text_entry_call;
   class C_text_entry_call: public C_text_entry_callback{
      C_mail_client &app;
      virtual void TextEntered(const Cstr_w &txt){
         app.CallNumberConfirm(txt);
      }
   public:
      C_text_entry_call(C_mail_client &a): app(a){}
   };
protected:
#ifdef __SYMBIAN32__
   void SymbianMakePhoneCall(const char *num);
#endif
//----------------------------

   static const S_config_store_type save_config_values[];
   virtual const S_config_store_type *GetConfigStoreTypes() const{ return save_config_values; }
   virtual Cstr_w GetConfigFilename() const;

public:
//----------------------------
// Draw settings base, except title and softkeys.
   void DrawSettings(const C_mode_settings &mod, const S_config_item *ctrls, const void *cfg_base, bool draw_help = true);

//----------------------------
   C_smart_ptr<C_simple_sound_player> simple_snd_plr;

//----------------------------
// Create simple sound player for playing scheduled alert sounds.
   void PlayNextAlertSound();
   void StartAlertSoundsPlayback();

//----------------------------
// Add default mail sound to alert scheduler, and start its playback now.
   void PlayNewMailSound();

   mutable C_smart_ptr<C_image> spec_icons;
   mutable C_smart_ptr<C_image> folder_icons;
   enum E_SPECIAL_ICON{
      SPEC_DISCONNECTED,
      SPEC_CONNECTING,
      SPEC_WORKING,
      SPEC_IDLE,
      SPEC_ERROR,
      SPEC_SCHEDULED,

      SPEC_LAST
   };

   void LoadSpecialIcons();

//----------------------------
// Draw spec. icon, return width of drawn icon.
   int DrawSpecialIcon(int x, int y, E_SPECIAL_ICON index, bool center_y = true);

   int DrawConnectIconType(int x, int y, S_account::E_UPDATE_STATE state, bool center_y = true);
   int DrawImapFolderIcon(int x, int y, int index);
public:
   C_smart_ptr<C_image> icons_file;

   enum E_MESSAGE_ICON{
      MESSAGE_ICON_OPENED,
      MESSAGE_ICON_NEW,
      MESSAGE_ICON_SENT,
      MESSAGE_ICON_DRAFT,
      MESSAGE_ICON_TO_SEND,
      MESSAGE_ICON_OPENED_PARTIAL,
      MESSAGE_ICON_NEW_PARTIAL,
      MESSAGE_ICON_DRAFT_PARTIAL,
      MESSAGE_ICON_DELETED,

      MESSAGE_ICON_SCISSORS,
      MESSAGE_ICON_PRIORITY_HIGH,
      MESSAGE_ICON_PRIORITY_LOW,
      MESSAGE_ICON_FLAG,
      MESSAGE_ICON_REPLIED,
      MESSAGE_ICON_FORWARDED,
      MESSAGE_ICON_ATTACH_CLIP,
      MESSAGE_ICON_RECENT,
      MESSAGE_ICON_PIN,

      MESSAGE_ICON_LAST,
      MESSAGE_ICON_SMALL_LAST = 5
   };
   C_smart_ptr<C_image> small_msg_icons[MESSAGE_ICON_SMALL_LAST],
      msg_icons[MESSAGE_ICON_LAST];
protected:
   friend class C_attach_browser;

//----------------------------

   bool ExportSettings(const wchar *filename);
   bool ImportSettings(const wchar *filename);

//----------------------------
public:
   bool LoadMessages(C_message_container &cnt) const;

   void CloseAccountsConnections(bool disable_idle = false);

   void CloseConnection(){
      CloseAccountsConnections();
      C_client::CloseConnection();
#ifdef AUTO_CONNECTION_BACK_SWITCH
      alt_test_connection = NULL;
      alt_test_socket = NULL;
#endif
   }
//----------------------------
//----------------------------
                              //mode account browser
   class C_mode_accounts;
   friend class C_mode_accounts;

   class C_mode_accounts: public C_mode_list<C_mail_client>{
      typedef C_mode_list<C_mail_client> super;
      virtual bool IsPixelMode() const{ return true; }
      virtual int GetNumEntries() const{ return app.NumAccounts(); }
   public:
      static const dword ID = FOUR_CC('A','C','N','T');

      C_mode_accounts(C_mail_client &_app):
         super(_app)
      {
         mode_id = ID;
      }
      virtual void InitLayout(){ InitLayoutAccounts1(-1); }
      void InitLayoutAccounts1(int sel);
      virtual void ProcessInput(S_user_input &ui, bool &redraw);
      virtual void ProcessMenu(int itm, dword menu_id);
      virtual void DrawContents() const;
      virtual void Draw() const;

      void DrawAccount(int acc_i) const;
   };

   void SetModeAccounts(int sel = 0);
   void DeleteAccount(C_mode_accounts &mod);

   void CreateNewAccountFromEmail(const Cstr_w &email_address);
   void CreateNewAccount(const wchar *init_text);

   void OpenMailbox(C_mode_accounts &mod, bool force_folder_mode);

   void DrawAccountNameAndIcons(const Cstr_w &name, int x, int y, const S_rect &rc, int max_width, dword fnt_flags, dword txt_color, const dword stats[C_message_container::STAT_LAST], const wchar *status_text = NULL, const int progress_pos_total[2] = NULL);

//----------------------------
//----------------------------

   bool ConnectAccountInBackground(S_account &acc, bool init_other_accounts = false, bool allow_imap_idle = true);
   bool ConnectEnabledAccountsInBackground(bool ask = true, bool manual = false, bool force_all = false, bool allow_imap_idle = true);

//----------------------------
// Check if any account is IMAP IDLE enabled.
   bool IsImapIdleConnected() const;
// Check if any account is connected in background mode.
   bool IsBackgroundConnected() const;

   Cstr_w GetAccountsFilename() const;

//----------------------------
// Load accounts settings; return false if load failed due to invalid password.
   bool LoadAccounts(const Cstr_w &password);
   void LoadAccountsXmlContent(S_account &acc, const C_xml &xml, const C_xml::C_element *el, t_folders &folders, bool is_imap, C_message_container *parent, int save_version);

   inline bool IsMailClientInitialized() const{ return (NumAccounts()!=0); }

//----------------------------
// Collect all assigned container folder id's. The list is sorted.
   void CollectContainerFolderIds(C_vector<dword> &ids) const;

//----------------------------
// Get next unused folder id for storing messages on disk.
   dword GetMsgContainerFolderId() const;

public:
//----------------------------
   void SaveAccounts() const;
public:
//----------------------------
//----------------------------
   mutable C_smart_ptr<C_address_book> address_book;

//----------------------------
// Open address book fully, load all contacts to memory.
   void OpenAddressBook();

//----------------------------
// Modify contact in address book (apply changes to system address book).
   void ModifyContact(int ci);

//----------------------------
// Remove contact from both local address_book and system AB).
   void RemoveContact(dword ci);

public:
   bool FindContactByEmail(const Cstr_c &email, S_contact &con) const;
   void CollectMatchingContacts(const Cstr_w &name, const Cstr_c &email, C_vector<S_contact_match> &matches);
protected:
//----------------------------
//----------------------------

   class C_alert_manager{
   public:
      struct S_alert{
         Cstr_w alert_name;   //filename or ringtone name (Android)
         int volume;
      };
      C_vector<S_alert> alerts_to_play;
      bool vibrate;

      void AddAlert(const Cstr_w &alert_name, int volume = -1);
      void Clear(){
         alerts_to_play.clear();
         vibrate = false;
      }
      C_alert_manager():
         vibrate(false)
      {}
   } alert_manager;

//----------------------------
//----------------------------

                                 //mode withiout graphical UI, just updating active mailobxes
   class C_mode_update_mailboxes;
   friend class C_mode_update_mailboxes;
   void InitLayoutUpdateMailboxes(C_mode_update_mailboxes &mod){}
   void TickUpdateMailboxes(C_mode_update_mailboxes &mod, dword time, bool&);
   void UpdMailboxesProcessInput(C_mode_update_mailboxes &mod, S_user_input &ui, bool &redraw){}
   void DrawUpdateMailboxes(const C_mode_update_mailboxes &mod){}

   class C_mode_update_mailboxes: public C_mode{
   public:
      int curr_mailbox;
      bool auto_update;
      dword alive_progress_pos;

      C_mode_update_mailboxes(C_application_ui &_app, C_mode *sm, bool au):
         C_mode(_app, sm),
         auto_update(au),
         curr_mailbox(-1),
         alive_progress_pos(0)
      {}
      virtual bool IsUsingConnection() const{ return true; }

      virtual void InitLayout(){ static_cast<C_mail_client&>(app).InitLayoutUpdateMailboxes(*this); }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ static_cast<C_mail_client&>(app).UpdMailboxesProcessInput(*this, ui, redraw); }
      virtual void Tick(dword time, bool &redraw){ static_cast<C_mail_client&>(app).TickUpdateMailboxes(*this, time, redraw); }
      virtual void Draw() const{ static_cast<C_mail_client&>(app).DrawUpdateMailboxes(*this); }
   };
   void SetModeUpdateMailboxes(bool auto_update = false);

//----------------------------
//----------------------------

   //void MsgDetailsProcessInput(C_mode &mod, S_user_input &ui, bool &redraw);
   void MsgDetailsHeaders();

//----------------------------
//----------------------------
public:
                              //mode for browsing messages in a mailbox (account)
   class C_mode_mailbox;
   friend class C_mode_mailbox;
   void InitLayoutMailbox(C_mode_mailbox &mod);
   void MailboxProcessMenu(C_mode_mailbox &mod, int itm, dword menu_id);
   void TickMailbox(C_mode_mailbox &mod, dword time, bool &redraw);
   void DrawMailbox(const C_mode_mailbox &mod);

   class C_mode_mailbox: public C_mode_list<C_mail_client>, public C_multi_selection_callback{
      typedef C_mode_list<C_mail_client> super;
      virtual bool WantInactiveTimer() const{ return false; }

      virtual void Select(int option){
         app.MailboxSearchMessagesMode(option);
      }
   public:
      static const dword ID = FOUR_CC('M','B','O','X');
      C_smart_ptr<C_mode_update_mailboxes> mod_upd_mboxes;
                                 //account we operate on
      S_account &acc;
      C_smart_ptr<C_message_container> folder;

      inline bool IsImap() const{ return acc.IsImap(); }

      inline C_message_container &GetContainer(){ return *folder; }
      inline const C_message_container &GetContainer() const{ return *folder; }
      inline C_vector<S_message> &GetMessages(){ return GetContainer().messages; }
      inline const C_vector<S_message> &GetMessages() const{ return const_cast<C_mode_mailbox*>(this)->GetMessages(); }

      inline dword GetRealMessageIndex(dword sel) const{
         if(find_messages.size())
            sel = find_messages[sel];
         return sel;
      }

      inline S_message &GetMessage(dword sel){
         return GetMessages()[GetRealMessageIndex(sel)];
      }
      inline const S_message &GetMessage(dword i) const{ return const_cast<C_mode_mailbox*>(this)->GetMessage(i); }

      virtual int GetNumEntries() const{
         return num_vis_msgs;
      }
      void InitWidthLength();

      S_rect rc_preview;       //valid if preview mode enabled

                              //preview text
      S_text_display_info text_info;

      C_scrollbar sb_preview;

      int shift_mark;         //-1=no shift, 0=unmark, 1=mark
      int mouse_mark;

                              //subject scrolling
      int subj_scroll_len;    //lenght of scrolling width
      enum{
         SUBSCRL_NO,
         SUBSCRL_WAIT_1,
         SUBSCRL_GO_LEFT,
         SUBSCRL_WAIT_2,
         SUBSCRL_GO_RIGHT,
      } subj_scroll_phase;
      int subj_scroll_count;
      int subj_draw_shift,    //pixels from item x, where subject gets drawn (for thread level 0)
         subj_draw_max_x, subj_draw_max_x_today, subj_draw_max_x_last_year;

                              //time begin - today, this year
      dword today_begin, this_year_begin;

      int auto_scroll_time;   //24.8 fixed-point (whole numbers are pixels)

      bool preview_drag;
      C_kinetic_movement preview_kinetic_move;
      int drag_mouse_y;

                                 //number of visible messages (may be lower then # of messages, due to hidden messages)
      dword num_vis_msgs;
      bool show_hidden;
#ifdef USE_MOUSE
      dword enabled_buttons;     //bits specify which buttons are enabled
#endif
      enum E_SEARCH_MODE{
         SEARCH_SUBJECT,
         SEARCH_BODY,
         SEARCH_SENDER,
         SEARCH_RECIPIENT,
         SEARCH_LAST
      };
      C_vector<dword> find_messages;

      C_mode_mailbox(C_mail_client &_app, S_account &acc1, C_message_container *fld):
         super(_app),
         acc(acc1),
         folder(fld),
         preview_drag(false),
         auto_scroll_time(0),
         shift_mark(-1),
         mouse_mark(-1),
         num_vis_msgs(0),
         show_hidden(false),
         subj_scroll_phase(SUBSCRL_NO)
      {
         mode_id = ID;
      }

      virtual bool IsPixelMode() const{ return true; }
      bool IsAnyMarked() const;

      virtual void ResetTouchInput(){
         super::ResetTouchInput();
         preview_drag = false;
      }

      virtual void InitLayout(){ app.InitLayoutMailbox(*this); }
      virtual void ProcessMenu(int itm, dword menu_id){ app.MailboxProcessMenu(*this, itm, menu_id); }
      virtual void Tick(dword time, bool &redraw){ app.TickMailbox(*this, time, redraw); }
      virtual void DrawContents() const;
      virtual void DrawPreview(bool draw_window) const;
      virtual void Draw() const{ app.DrawMailbox(*this); }

      inline bool IsOfflineFolder() const{ return (!acc.IsImap() && !GetContainer().IsInbox()); }

   //----------------------------
   // Get pixel offset for single thread level.
      int GetThreadLevelOffset() const;
   //----------------------------
   // For given message, get # of pixels how it is drawn more right to indicate threaded message level.
      int GetMsgDrawLevelOffset(byte thread_level) const;

      void MarkMessages(bool set_flags, dword flag, bool update_push_mail = true);
      void CursorMove(bool down, bool shift, bool &redraw, bool wrap_list = true);
   };
   void Mailbox_ShowDetails(C_mode_mailbox &mod, const char *full_headers = NULL){
      ShowMessageDetails(mod.GetMessage(mod.selection), full_headers);
   }

   C_mode_mailbox &SetModeMailbox(S_account &acc, C_message_container *folder, int top_line = 0);

   void DrawMailboxMessage(const C_mode_mailbox &mod, int msg_i);
   bool MailboxScrollEnd(C_mode_mailbox &mod, bool up);
#ifdef USE_MOUSE
   void Mailbox_SetButtonsState(C_mode_mailbox &mod) const;
#endif
   static void SkipReFwd(const char *&cp);

   void MailboxSearchMessages(C_mode_mailbox &mod, C_mode_mailbox::E_SEARCH_MODE mode, const wchar *what);
   void MailboxSearchMessagesMode(int option);
   void MailboxResetSearch(C_mode_mailbox &mod);

   void MailboxBack(C_mode_mailbox &mod);
   void MailboxUpdateImapIdleFolder(C_mode_mailbox &mod, bool also_delete);
   void MailboxPreviewScrolled(C_mode_mailbox &mod, bool &redraw);

public:
   void MailboxMoveMessagesFolderSelected(C_message_container *fld);

//----------------------------
   void Mailbox_RecalculateDisplayArea(C_mode_mailbox &mod);
   void GetVisibleMessagesCount(C_mode_mailbox &mod) const;

//----------------------------
// Get list of messages, which may be moved to different folder. Multiple messages are put to list from lowest to highest index.
   void GetMovableMessages(C_mode_mailbox &mod, C_vector<S_message*> &uids) const;

//----------------------------
// Set active selection on mailbox.
   void SetMailboxSelection(C_mode_mailbox &mod, int sel);

//----------------------------

   void OpenMessage(C_mode_mailbox &mod, bool force_preview = false);

//----------------------------
// Delete all marked messages, or selected one.
   void DeleteMarkedOrSelectedMessages(C_mode_mailbox &mod, bool prompt, bool &redraw, bool move_cursor_down = true);
   void DeleteMarkedOrSelectedMessagesQ(C_mode_mailbox &mod);

//----------------------------
//----------------------------
public:

   enum E_ADDRESS_BOOK_MODE{
      AB_MODE_EXPLORE,
      AB_MODE_SELECT,
      AB_MODE_UPDATE_CONTACT,
   };
   static int CompareContacts(const void *a1, const void *a2, void *context);
   void SortAddressBook(int &indx);

public:
   void SetModeAddressBook(E_ADDRESS_BOOK_MODE m = AB_MODE_EXPLORE);
   void SetModeAddressBook_NewContact(const S_contact &c, bool update_existing);
   Cstr_w AddressBook_GetName(const S_contact &con) const;
//----------------------------
//----------------------------
// Open all images in HTML text - try to search in cache first (cache is represented by attachments),
//    if not found, setup http loader.
   void OpenHtmlImages(const C_buffer<S_attachment> &attachments, const C_buffer<S_attachment> *attachments1, bool allow_download,
      S_text_display_info &text_info, C_multi_item_loader &loader, bool allow_progress = true);

protected:
//----------------------------
// Open html images, try to reuse images from 'mod_reuse' (may be NULL), reset image loader on 'mod_reuse').
   void OpenHtmlImages1(C_client_viewer::C_mode_this &mod, C_client_viewer::C_mode_this *mod_reuse);
public:
//----------------------------
// Returns true when image was successfully downloaded.
   bool TickViewerImageLoader(C_mode &mod, C_socket_notify::E_SOCKET_EVENT ev, C_text_viewer &viewer, C_message_container *cnt, C_buffer<S_attachment> &atts, bool &redraw);

//----------------------------
                              //mode for reading mail
   class C_mode_read_mail_base: public C_mode_app<C_mail_client>, public C_viewer_previous_next{
   public:
      static const dword ID = FOUR_CC('R','D','M','L');

      C_mode_read_mail_base(C_mail_client &_app):
         C_mode_app<C_mail_client>(_app)
      {}
      virtual C_mode_mailbox &GetMailbox() = 0;
      virtual void StartCopyText() = 0;
   };
//----------------------------

   bool ReadMail_SaveAttachments(const Cstr_w &file, bool all);
   bool ReadMail_SaveAttachmentOne(const Cstr_w *file, const C_vector<Cstr_w> *files){ return ReadMail_SaveAttachments(*file, false); }
   bool ReadMail_SaveAttachmentAll(const Cstr_w *file, const C_vector<Cstr_w> *files){ return ReadMail_SaveAttachments(*file, true); }

   void SetModeReadMail(C_mode_mailbox &mod);
   bool SetModeReadMail(const wchar *fn);

protected:
//----------------------------
//----------------------------

   void CreateDemoMail(C_message_container &cnt, const C_zip_package *dta, const char *subject, dword size, dword date, const wchar *fnames) const;
   void CreateDemoMail(C_message_container &cnt);
public:

//----------------------------
// Delete message - erase from account, including all its files.
   void DeleteMessage(C_message_container &cnt, int i, bool also_files = true);

//----------------------------
   static int CompareMessages(const void *m1, const void *m2, void *context);
   void SortMessages(C_vector<S_message> &messages, bool is_imap, int *selection = NULL) const;
   void SortByThread(S_message *messages, dword count, bool is_imap, int *selection) const;

//----------------------------
// Save targeted link to... (ask for dest using file browser).
   void Viewer_SaveTargetAs(C_text_viewer &vw);
   bool Viewer_SaveTargetAsCallback(const Cstr_w *file, const C_vector<Cstr_w> *files);

//----------------------------
// Sort all accounts. Current 'selection' is adjusted for account specified by 'curr_account'.
   void SortAllAccounts(const C_message_container *curr_cnt, int *selection);
   void MarkToSortAllAccounts(){
      SortAllAccounts(NULL, NULL);
   }

//----------------------------
//----------------------------
//----------------------------
public:
   void SetModeDataCounters();
   void DataCounters_Reset();

   //typedef void (C_mail_client::*t_fnc)();
protected:
//----------------------------
//----------------------------

   struct S_rule{
      Cstr_w name;
      enum{
         FLG_NUM_CONDS_MASK = 0xff,
         FLG_ACTIVE = 0x100,
         FLG_OP_OR = 0x10000, //use logical OR (when not set, use AND)
      };
      dword flags;

      inline dword NumConds() const{ return flags&FLG_NUM_CONDS_MASK; }
                              //conditions
      enum{ MAX_CONDS = 32 };
      struct S_condition{
         enum E_CONDITION{
            SUBJECT_BEGINS,
            SUBJECT_CONTAINS,
            SENDER_BEGINS,
            SENDER_CONTAINS,
            TO_CONTAINS,
            SIZE_LESS,
            SIZE_MORE,
            AGE_LESS,
            AGE_MORE,
            SPAM_SCORE_LESS,
            SPAM_SCORE_MORE,
            SENDER_IN_CONTACTS,
            SENDER_NOT_IN_CONTACTS,
            SENDER_HEADER_CONTAINS,    //param is in form <header> '=' <match>
            LAST
         } cond;
         /*
         enum{
            FLG_SIZE_MASK = 0xffff,
         };
         dword flags;
         */
         int size;
         Cstr_w param;

         S_condition():
            cond(SUBJECT_BEGINS),
            //flags(5)
            size(5)
         {}

         void Save(C_file &ck) const;
         bool Load(C_file &ck);
         //inline dword GetSize() const{ return flags&FLG_SIZE_MASK; }
         static bool CheckStringMatch(const Cstr_w &what, const Cstr_w &where, bool beg_only);
         bool CheckStringMatch(const Cstr_w &where, bool beg_only) const;
         bool CheckStringMatch(const Cstr_c &where, bool beg_only) const;
      } conds[MAX_CONDS];

      enum E_ACTION{
         ACT_MARK_FOR_DELETE,
         ACT_DELETE_IMMEDIATELY,
         ACT_DOWNLOAD_HEADER,
         ACT_DOWNLOAD_BODY,
         ACT_PRIORITY_LOW,
         ACT_PRIORITY_HIGH,
         ACT_SET_HIDDEN,
         ACT_MOVE_TO_FOLDER,
         ACT_DOWNLOAD_PARTIAL_BODY,
         ACT_PLAY_SOUND,
         ACT_SET_COLOR,
         ACT_LAST,
      } action;
      Cstr_w action_param;
      dword action_param_i;

      S_rule():
         flags(FLG_ACTIVE | FLG_OP_OR | 1),
         action(ACT_MARK_FOR_DELETE),
         action_param_i(5)
      {}

      void SetActionDefaults();
   };
   C_buffer<S_rule> rules;

   static const int NUM_RULE_COLORS = 6;
   static const dword rule_colors[NUM_RULE_COLORS];   //colors without alpha channel

   Cstr_w GetRulesFilename() const;
   void LoadRules();
   void SaveRules() const;

//----------------------------
//----------------------------

   class C_mode_rules_browser;
   friend class C_mode_rules_browser;
   void InitLayoutRulesBrowser(C_mode_rules_browser &mod);
   void RulesBrowserProcessInput(C_mode_rules_browser &mod, S_user_input &ui, bool &redraw);
   void RulesProcessMenu(C_mode_rules_browser &mod, int itm, dword menu_id);
   void DrawRulesBrowser(const C_mode_rules_browser &mod);

   class C_mode_rules_browser: public C_mode_list<C_mail_client>{
      typedef C_mode_list<C_mail_client> super;
   public:
      static const dword ID = FOUR_CC('R','L','B','R');
      C_smart_ptr<C_text_editor> te;   //text editor for rename
      int max_text_x;
      bool adding_new;

      C_mode_rules_browser(C_mail_client &_app):
         super(_app)
      {
         mode_id = ID;
      }
      virtual void InitLayout(){ app.InitLayoutRulesBrowser(*this); }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ app.RulesBrowserProcessInput(*this, ui, redraw); }
      virtual void ProcessMenu(int itm, dword menu_id){ app.RulesProcessMenu(*this, itm, menu_id); }
      virtual void Draw() const{ app.DrawRulesBrowser(*this); }
   };
   void SetModeRulesBrowser();

   void Rules_DeleteSelected(C_mode_rules_browser &mod);
   void Rules_ToggleSelected(C_mode_rules_browser &mod);
   bool Rules_MoveSelected(C_mode_rules_browser &mod, bool up);

   class C_question_del_rule;
   friend class C_question_del_rule;
   class C_question_del_rule: public C_question_callback{
      C_mail_client &app;
      C_mode_rules_browser &mod;
      virtual void QuestionConfirm(){
         app.Rules_DeleteSelected(mod);
      }
   public:
      C_question_del_rule(C_mail_client &a, C_mode_rules_browser &m): app(a), mod(m){}
   };

//----------------------------
//----------------------------

   class C_mode_rule_editor;
   friend class C_mode_rule_editor;
   void InitLayoutRuleEditor(C_mode_rule_editor &mod);
   void RuleEditProcessInput(C_mode_rule_editor &mod, S_user_input &ui, bool &redraw);
   void RuleEditProcessMenu(C_mode_rule_editor &mod, int itm, dword menu_id);
   void DrawRuleEditor(const C_mode_rule_editor &mod);

   class C_mode_rule_editor: public C_mode_list<C_mail_client>{
      typedef C_mode_list<C_mail_client> super;
      void operator=(const C_mode_rule_editor&);
   public:
      static const dword ID = FOUR_CC('R','L','E','D');
      S_rule rul;
      S_rule &save_to;
      C_smart_ptr<C_text_editor> te;
      int and_or_if_width;
      bool need_save;

      C_mode_rule_editor(C_mail_client &_app, S_rule &r):
         super(_app),
         rul(r),
         save_to(r),
         need_save(false)

      {
         mode_id = ID;
      }
      virtual void InitLayout(){ app.InitLayoutRuleEditor(*this); }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ app.RuleEditProcessInput(*this, ui, redraw); }
      virtual void ProcessMenu(int itm, dword menu_id){ app.RuleEditProcessMenu(*this, itm, menu_id); }
      virtual void Draw() const{ app.DrawRuleEditor(*this); }
   };
   void SetModeRuleEditor(S_rule &r);

   void Rule_BeginEdit(C_mode_rule_editor &mod);
   void Rule_EndEdit(C_mode_rule_editor &mod);
   void Rule_DeleteSelectedCond(C_mode_rule_editor &mod);
   void Rule_AddCond(C_mode_rule_editor &mod);
   void Rule_CloseEditor(C_mode_rule_editor &mod);

   class C_question_del_cond;
   friend class C_question_del_cond;
   class C_question_del_cond: public C_question_callback{
      C_mail_client &app;
      C_mode_rule_editor &mod;
      virtual void QuestionConfirm(){
         app.Rule_DeleteSelectedCond(mod);
      }
   public:
      C_question_del_cond(C_mail_client &a, C_mode_rule_editor &m): app(a), mod(m){}
   };

   void RuleSoundEntered(const Cstr_w &fn);
   bool RuleSoundSelectCallback(const Cstr_w *file, C_vector<Cstr_w> *files);

//----------------------------
//----------------------------
                              //account editor
   class C_mode_edit_account;
   friend class C_mode_edit_account;
   void EditAccountProcessMenu(C_mode_edit_account &mod, int itm, dword menu_id);

   class C_mode_edit_account: public C_mode_app<C_mail_client>, public C_mode_settings{
      virtual C_application_ui &AppForListMode() const{ return app; }
      virtual bool IsPixelMode() const{ return true; }
      virtual int GetNumEntries() const;
   public:
      dword acc_indx;
      bool advanced;
      bool new_account;

      S_account_settings init_settings;

      C_mode_edit_account(C_mail_client &_app, dword ai, bool adv, bool nw):
         C_mode_app<C_mail_client>(_app),
         acc_indx(ai),
         advanced(adv),
         new_account(nw)
      {
      }
      virtual void InitLayout();
      virtual void ProcessInput(S_user_input &ui, bool &redraw);
      virtual void ProcessMenu(int itm, dword menu_id){ static_cast<C_mail_client&>(app).EditAccountProcessMenu(*this, itm, menu_id); }
      virtual void TextEditNotify(bool cursor_moved, bool text_changed, bool &redraw);
      virtual void Draw() const;
      virtual void DrawContents() const{
         Draw();
      }
      virtual void SelectionChanged(int old_sel){
         app.SetEditAccountSelection(*this, selection);
      }
      virtual void ScrollChanged(){ if(text_editor) PositionTextEditor(); }
      void PositionTextEditor();
   };

   static const S_config_item ctrls_edit_account[],
      ctrls_edit_account_pop[],
      ctrls_edit_account_imap[];
   static const dword num_edit_acc_ctrls[];

   void SetModeEditAccount(dword acc_i, bool advanced, bool new_account);

//----------------------------
// Set selection. If 'sel' is negative, EnsureVisible is not called.
   void SetEditAccountSelection(C_mode_edit_account &mod, int sel);

   void CloseEditAccounts(C_mode_edit_account &mod, bool validate);

   void StoreEditorText(C_text_editor &te, Cstr_w &str);

//----------------------------

   struct S_bodystructure{

      struct S_part{
         E_CONTENT_ENCODING content_encoding;
         S_content_type content_type;
         E_TEXT_CODING charset;
         Cstr_w name;
         dword size;

         C_vector<S_part> nested_parts;
         Cstr_c nested_multipart_boundary;
         S_part();

         bool ParsePart(const char *&cp, const C_mail_client &app);
         bool ParseParts(const char *&cp, const C_mail_client &app);
      };
      S_part root_part;
      bool has_text_part;     //true if text part is the first part (either text/ or multipart/)

      C_vector<char> src_data;   //source string (may be sent in multiple batches)
      int src_open_braces_count;
   private:

      static void SkipWord(const char *&cp);

      static bool ReadQuotedString(const char *&cp, Cstr_c &str);
   public:
      static bool ReadWord(const char *&cp, Cstr_c &str);
      static void SkipImapParam(const char *&cp);

   //----------------------------
   // Get all text in braces '(' ... ')', including the braces. Allow nested braces. Text in quotes is included as-is.
   // Return current open braces count.
      static int GetTextInBraces(const char *&cp, C_vector<char> &buf, int brace_count);
      static void GetTextInBraces(const char *&cp, Cstr_c &str);

      void AppendSourceData(const char *cp);
      void Clear();

   //----------------------------
   // Get number of attachments. Attachments are stored under root's nested list from position #1 (#0 position is always text).
      int GetNumAttachments() const;

      const S_bodystructure::S_part *GetFirstAttachment() const;

   //----------------------------
   // Compute size of text data (without attachments).
      dword GetSizeOfTextPart() const;

      S_bodystructure():
         src_open_braces_count(0),
         has_text_part(false)
      { }
      bool Parse(const C_mail_client &app);
   };

//----------------------------

                              //mode managing internet connection (updating mailboxes, getting, sending messages)
                              // it displays progress in small window
public:
   class C_mode_connection;
   class C_mode_connection_base;

   friend class C_mode_connection;
   friend class C_mode_connection_base;
   void InitLayoutConnection(C_mode_connection_base &mod);
   void TickConnection(C_mode_connection_base &mod, dword time, bool &redraw);
   void DrawConnection(const C_mode_connection_base &mod);

   struct S_connection_params{
      bool auto_update;
      bool imap_update_hidden;   //for ACT_UPDATE_IMAP_FOLDERS
      bool imap_finish_init_idle_accounts;
      bool schedule_update_after_auth; //after authentication finishes, call PerformAutoUpdate
      dword message_index;    //for single-message actions
      dword attachment_index; //for downloading single attachment
      dword alive_progress_pos;
      Cstr_w text;

      C_message_container *imap_folder_move_dest; //used when moving messages accross IMAP folders
      S_connection_params():
         auto_update(false),
         imap_finish_init_idle_accounts(false),
         schedule_update_after_auth(false),
         imap_update_hidden(false),
         imap_folder_move_dest(NULL),
         alive_progress_pos(0)
      {}
   };

//----------------------------

   class C_mode_connection_base: public C_mode{
   public:
      S_rect rc;
      C_progress_indicator progress;
      Cstr_w dlg_title, dlg_folder;
      mutable C_smart_ptr<C_image> data_cnt_bgnd;   //image holding background of data counters
      E_TEXT_ID rsk;

      enum{
         LINE_LIGHTS,
         LINE_FOLDER_NAME,
         LINE_ACTION,
         LINE_PROGRESS,
         LINE_LAST
      };

      S_connection_params params;
      dword last_anim_time;
      dword last_anim_draw_time;
      //dword last_data_count_draw_time;
      C_socket_notify::E_SOCKET_EVENT last_drawn_socket_event;

      S_rect rc_data_in, rc_data_out;
      struct S_alive_progress{
         S_rect rc;
         dword last_anim_time;
         dword pos;
         C_smart_ptr<C_image> img;

         S_alive_progress():
            last_anim_time(0),
            pos(0)
         {}
      } alive_progress;
      dword data_draw_last_time;

      S_account &acc;         //account on which connection operates
      enum E_ACTION{          //high-level action
         ACT_UPDATE_MAILBOX,  //update (sync) single mailbox
         ACT_UPDATE_ACCOUNTS,//update (send & receive) all chosen mailboxes
         ACT_UPDATE_IMAP_FOLDERS,   //update all active imap folders

         ACT_SEND_MAILS,      //send mails from current account
         ACT_GET_BODY,        //get single message body (current mode must be C_mode_mailbox)
         ACT_GET_MARKED_BODIES,  //get all marked msg bodies   ( '' )
         ACT_GET_MSG_HEADERS, //get full headers of single message

         ACT_REFRESH_IMAP_FOLDERS,
         ACT_DELETE_IMAP_FOLDER,
         ACT_RENAME_IMAP_FOLDER,
         ACT_CREATE_IMAP_FOLDER,
         ACT_IMAP_MOVE_MESSAGES,
         ACT_IMAP_PURGE,

         ACT_UPLOAD_SENT,     //upload sent messages, then continue in mailbox update (valid only for C_mode_connection_imap_upload)
         ACT_UPLOAD_DRAFTS,
         ACT_DOWNLOAD_IMAP_ATTACHMENT,
         ACT_DOWNLOAD_IMAP_ATTACHMENTS_ALL,
         ACT_DOWNLOAD_IMAP_ATTACHMENT_AND_OPEN,
         ACT_IMAP_IDLE,
         ACT_IMAP_IDLE_UPDATE_FLAGS,
         ACT_IMAP_IDLE_UPDATE_FLAGS_AND_DELETE,
      } action;
      C_smart_ptr<C_message_container> folder;   //folder with which we work (download/rename/delete)

      C_mode_connection_base(C_application_ui &_app, C_mode *sm, S_account &ac, C_message_container *cnt, E_ACTION a, const S_connection_params *_params):
         C_mode(_app, sm),
         action(a),
         acc(ac),
         folder(cnt),
         last_anim_time(0),
         last_anim_draw_time(0),
         //last_data_count_draw_time(0),
         data_draw_last_time(0),
         last_drawn_socket_event(E_SOCKET_EVENT(-1)),
         rsk(TXT_CANCEL)
      {
         if(_params)
            params = *_params;
      }
      virtual void InitLayout(){ static_cast<C_mail_client&>(app).InitLayoutConnection(*this); }
      virtual void Tick(dword time, bool &redraw){ static_cast<C_mail_client&>(app).TickConnection(*this, time, redraw); }
      virtual void Draw() const{ static_cast<C_mail_client&>(app).DrawConnection(*this); }
   };

//----------------------------

   class C_mode_connection: public C_mode_connection_base{
      void operator=(const C_mode_connection&);
   public:
      static const dword ID = FOUR_CC('C','O','N','N');

      bool cancel_request;    //connection canceled by user
      bool container_invalid; //after deleting folder (temp or on server)

      virtual C_message_container &GetContainer(){ return *folder; }
      inline const C_message_container &GetContainer() const{ return const_cast<C_mode_connection*>(this)->GetContainer(); }
      C_vector<S_message> &GetMessages(){ return GetContainer().messages; }

      S_message *FindImapMessage(dword imap_uid);

      C_mode_connection(C_application_ui &_app, C_mode *sm, S_account &ac, C_message_container *cnt, E_ACTION _action, const S_connection_params *_params):
         C_mode_connection_base(_app, sm, ac, cnt, _action, _params),
         cancel_request(false),
         container_invalid(false)
      {
         mode_id = ID;
      }
      virtual bool IsUsingConnection() const{ return true; }
   };
protected:
   void TickAndDrawAliveProgress(C_mode_connection_base &mod, dword time);
   void DrawAliveProgress(const C_mode_connection_base &mod);
   void ConnectionDrawProgress(const C_mode_connection &mod);
   void ConnectionClearProgress(const C_mode_connection &mod);

//----------------------------

   class C_connection_send{
   public:
      C_message_container *cnt_out;   //folder from which we send (smtp)

                              //for displaying progress:
      dword send_message_index, num_send_messages;

      C_vector<Cstr_c> send_recipients;
      Cstr_c send_body;       //in utf-8
      union{
         int char_index;      //index of currently sent character
         int attach_indx;     //    ''                  attachment
      };
      int msg_send_progress_size; //size of message's text file, used for progress indicator
      int txt_flowed_curr_quote_count; //quote count for current line if flowed format is used; -1 if prev line was hard break
      Cstr_c send_multipart_boundary;
      C_file fl_attachment_send;
      enum E_SEND_PHASE{
         SEND_TEXT_PLAIN,
         SEND_TEXT_QUOTED_PRINTABLE,
         SEND_ATTACHMENT_PREPARE,
         SEND_ATTACHMENT_SEND,
         SEND_ATTACHMENT_DONE,
      } send_phase;

      C_connection_send():
         send_message_index(0),
         txt_flowed_curr_quote_count(-1),
         cnt_out(NULL)
      {}
   };

//----------------------------
   class C_mode_connection_smtp;
   friend class C_mode_connection_smtp;
   void SmtpSocketEvent(C_mode_connection_smtp &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw);
   void SmtpProcessInput(C_mode_connection_smtp &mod, S_user_input &ui, bool &redraw);

   class C_mode_connection_smtp: public C_mode_connection, public C_connection_send{
   public:
      C_smart_ptr<C_socket> socket;
      enum{
         SMTP_AUTH_PLAIN = 1,
         SMTP_AUTH_LOGIN = 2,
         SMTP_AUTH_CRAM_MD5 = 4,
         SMTP_AUTH_DIGEST_MD5 = 8,
         SMTP_ALLOW_STARTTLS = 0x10,
         SMTP_IN_TLS = 0x20,
      };
      dword smtp_caps;
      dword rcpt_index;      //index into recipients, as we send them using RCPT

      dword message_index;

      enum{                   //low-level operational state
         ST_CONNECTING,
         ST_WAIT_CONNECT_OK,

         ST_SMTP_EHLO_TEST,   //check if EHLO is supported
         ST_SMTP_HELO_TEST,   //    ''   HELO
         ST_SMTP_STARTTLS,
         ST_SMTP_AUTH_PLAIN,
         ST_SMTP_AUTH_LOGIN,
         ST_SMTP_AUTH_LOGIN_PHASE2,
         ST_SMTP_AUTH_CRAM_MD5,
         ST_SMTP_AUTH,
         ST_SMTP_MAIL,
         ST_SMTP_RCPT,
         ST_SMTP_DATA_BEGIN,  //DATA transaction beginning
         ST_SMTP_DATA_SEND,   //DATA sending
         ST_SMTP_DATA_END,    //after last .<LFCR>, waiting for response
         ST_USER_CANCELED
      } state;

      virtual C_message_container &GetContainer(){ return *cnt_out; }

      C_mode_connection_smtp(C_application_ui &_app, C_mode *sm, S_account &_acc, C_message_container *fld, E_ACTION _action, const S_connection_params *_params):
         C_mode_connection(_app, sm, _acc, fld, _action, _params),
         state(ST_CONNECTING),
         smtp_caps(0),
         message_index(dword(-1))
      {
      }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ static_cast<C_mail_client&>(app).SmtpProcessInput(*this, ui, redraw); }
      virtual void SocketEvent(C_socket_notify::E_SOCKET_EVENT ev, C_socket *_socket, bool &redraw){ static_cast<C_mail_client&>(app).SmtpSocketEvent(*this, ev, _socket, redraw); }

      Cstr_c GetSmtpUsername() const;
   };

   void SetModeConnectionSmtp(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params);
   void AfterSmtpConnected(C_mode_connection_smtp &mod);
   bool SmtpAfterMailSent(C_mode_connection_smtp &mod);  //return true if sending next message
   void SmtpError(C_mode_connection_smtp &mod, const wchar *err);
   bool ProcessLineSmtp(C_mode_connection_smtp &mod, const C_socket::t_buffer &line);
   bool BeginSMTPAuthentication(C_mode_connection_smtp &mod);
   bool StartSendingNextMessage(C_mode_connection_smtp &mod); // Find next message to send, Return false if not found.
   void SmtpCloseMode(C_mode_connection_smtp &mod);
   void SmtpPrepareAndSendMessageHeaders(C_mode_connection_smtp &mod);
   Cstr_c SmtpGetAuthLoginBase64Param(C_mode_connection_smtp &mod);

//----------------------------
public:
   class C_mode_connection_in;
   friend class C_mode_connection_in;

   void ConnectionSocketEvent(C_mode_connection_in &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw);

   void ConnectionDataReceived(C_mode_connection_in &mod, C_socket *socket, Cstr_w &err, bool check_mode);

                              //structure containing all helper data used for downloading single message
   struct S_download_body_data{
      C_vector<char> curr_hdr;  //data of currently read header
      C_data_saving body_saving;
      S_complete_header retrieved_header; //parsed header of currently retrieved message
      bool got_header;        //set after header was fully received
      bool partial_body;

      struct S_flowed_text{
         bool enable_decoding;
         int prev_soft_line_quote_count;  //number of quotes on previous line, if it was soft-break; -1 otherwise

         void Reset(){
            enable_decoding = false;
            prev_soft_line_quote_count = -1;
         }
         S_flowed_text(){
            Reset();
         }
      } flowed_text;

      struct S_multipart_info: public C_unknown{
         S_complete_header hdr;
         Cstr_c boundary;     //boundary of parent's level
         enum{
            WAIT_PART_HEADER,
            GET_PART_HEADER,
            GET_PART_BODY,
            GET_PART_ATTACHMENT,
            GET_PART_UNKNOWN,
            IGNORE_PART,
            FINISHED,
         } phase;
         C_smart_ptr<S_multipart_info> upper_part;

         S_multipart_info(S_multipart_info *p):
            phase(WAIT_PART_HEADER),
            upper_part(p)
         {}
      };
      C_smart_ptr<S_multipart_info> multi_part;
      void CreateMultiPartInfo(){
         S_multipart_info *p = new(true) S_multipart_info(multi_part);
         multi_part = p;
         p->Release();
      }
      C_data_saving att_saving;  //attachment storing

      S_download_body_data(){
         Reset();
      }

      void Reset(){
         curr_hdr.clear();
         retrieved_header.Reset();
         body_saving.CancelOutstanding();
         att_saving.CancelOutstanding();
         multi_part = NULL;
         got_header = false;
         partial_body = false;
      }
   };

//----------------------------

   class C_mode_connection_in: public C_mode_connection{
   public:
      bool using_cached_socket;
      bool progress_drawn;   //for optimized progress drawing when downloading in loop
      dword last_progress_draw_time;
      bool need_expunge;
      bool headers_added;     //headers were added, need to call AfterHeadersAdded

                              //counters for downloading headers (counting down to zero)
      dword num_hdrs_to_ask;  //used as index to 'headers'
      dword num_hdrs_to_get;
      dword num_sync_hdrs;    //total new headers being downloaded

                              //indexes for body download:
      dword num_get_bodies;   //number of marked bodies to get (just for displaying)
      dword get_bodies_index; //current marked body index (for displaying)

      struct S_message_header_download: public S_message_header_base{
         dword partial_download_kb; //when nonzero, partial body is downloaded

         S_message_header_download(): partial_download_kb(0){}
         S_message_header_download(const S_message_header_base &h):
            S_message_header_base(h),
            partial_download_kb(0)
         {}
      };
      struct S_message_header_imap_move: public S_message_header_base{
         Cstr_w move_folder_name;
         S_message_header_imap_move(){}
         S_message_header_imap_move(const S_message_header_base &h):
            S_message_header_base(h)
         {}
      };

      C_vector<S_message_header_base> headers;             //retrieved headers
      C_vector<S_message_header_imap_move> headers_to_move;
      C_vector<S_message_header_download> headers_to_download;
      C_vector<dword> msgs_to_delete;      //messages to delete from server, filled by folder sync, or rule, or msg moving; (POP3=server index, IMAP=UID)
      bool force_no_partial_download;

                              //message retrieved in mode 'get entire message'
      inline bool IsImap() const{ return acc.IsImap(); }

      void SortHeaders();
      int FindHeader(const S_message_header &match) const;

      void AddDeletedToDeleteList();

      C_mode_connection_in(C_application_ui &_app, C_mode *sm, S_account &_acc, C_message_container *fld, E_ACTION _action, const S_connection_params *_params):
         C_mode_connection(_app, sm, _acc, fld, _action, _params),
         using_cached_socket(false),
         progress_drawn(false),
         need_expunge(false),
         last_progress_draw_time(0),
         force_no_partial_download(false),
         num_sync_hdrs(0),
         num_hdrs_to_ask(0),
         num_get_bodies(0),
         headers_added(0)
      {
      }

      virtual void ProcessInput(S_user_input &ui, bool &redraw){ assert(0); }
      virtual void SocketEvent(C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){ static_cast<C_mail_client&>(app).ConnectionSocketEvent(*this, ev, socket, redraw); }

      void SocketSendString(const Cstr_c &s, int override_timeout = 0, bool no_log = false){
         acc.socket->SendString(s, override_timeout, no_log);
      }
      void SocketSendCString(const char *str, int override_timeout = 0, bool no_log = false){
         acc.socket->SendCString(str, override_timeout, no_log);
      }
   };
protected:
//----------------------------

   //class C_mode_connection_imap;
   friend class C_mode_connection_imap;
   void ConnectionProcessInputImap(C_mode_connection_imap &mod, S_user_input &ui, bool &redraw);

   class C_mode_connection_imap: public C_mode_connection_in{
   public:
      int curr_tag_id;        //tag id of last sent command
      enum{
         CAPS_AUTH_LOGIN = 1,
         CAPS_AUTH_PLAIN = 2,
         CAPS_AUTH_MD5 = 4,
         CAPS_IDLE = 8,
         CAPS_UIDPLUS = 0x10,
         CAPS_NO_SEARCH = 0x20,
         CAPS_ID = 0x40,
         CAPS_STARTTLS = 0x80,
         CAPS_COMPRESS = 0x100,

         CAPS_IN_COMPRESSION = 0x40000000,
         CAPS_IN_TLS = 0x80000000,
      };
      dword capability;

      enum E_STATE{
         ST_WAIT_CONNECT_OK,

         ST_IMAP_CAPABILITY,  //CAPABILITY command - checking caps of server
         ST_IMAP_ID,          //ID command - identify client/server
         ST_IMAP_LOGIN,       //LOGIN command sent
         ST_IMAP_SELECT,      //SELECT - opening IMAP folder
         ST_IMAP_CLOSE,       //close folder
         ST_IMAP_EXPUNGE,
                              //folder list:
         ST_IMAP_LIST,
         ST_IMAP_CREATE_FOLDER,
         ST_IMAP_RENAME_FOLDER,
         ST_IMAP_DELETE_FOLDER,

         ST_IMAP_SEARCH_SINCE,//search all messages not older than date
         ST_IMAP_GET_UIDS,    //fetch list of messages (fetch UID) and flags from server
         ST_IMAP_GET_HDR,     //fetch body header, rfc822 size and bodystructure; param = msg_index

                              //special operations:
         ST_IMAP_MOVE_MESSAGES_COPY,   //move marked messages to different imap folder
         ST_IMAP_MOVE_TO_FOLDER_BY_RULE,
         ST_IMAP_SET_FLAGS,
         ST_IMAP_MOVE_MESSAGES_SET_FLAGS, //update message's flags before copying messages

         ST_IMAP_GET_BODY,

         ST_IMAP_GET_ATTACHMENT_BODY,  //fetch message's attachment part (fetch body[n])
         ST_IMAP_GET_MSG_HEADERS,
         ST_IMAP_MOVE_MESSAGES_TO_THRASH,
         ST_IMAP_DELETE_MESSAGES,

         ST_IMAP_UPLOAD_MESSAGE,
         ST_IMAP_UPLOAD_MESSAGE_SEND,
         ST_IMAP_UPLOAD_MESSAGE_DONE,
         ST_IMAP_UPLOAD_MESSAGE_CREATE_FOLDER,
         ST_IMAP_FETCH_SYNC,
         ST_IMAP_IDLE,
         ST_IMAP_STARTTLS,
         ST_IMAP_COMPRESS,
         ST_NULL
      } state;

      z_stream compress_out, compress_in;
      C_vector<byte> decompress_cache;    //not yet decompressed data (waiting for new data)
      Cstr_c decompress_buf;  //decompressed data (waiting for more data + eol)
   //----------------------------
   // Check if operation is part if IMAP IDLE processing.
      inline bool IsImapIdle() const{ return (action==ACT_IMAP_IDLE); }

   //----------------------------

      class C_command: public C_unknown{
      public:
         E_STATE state;
         int tag_id;

         C_command(E_STATE st):
            state(st)
         {}
         virtual S_bodystructure *GetBodyStruct(){ return NULL; }
      };

   //----------------------------

      class C_command_select: public C_command{
      public:
         dword num_msgs;

         C_command_select():
            C_command(ST_IMAP_SELECT),
            num_msgs(0)
         {}
      };

   //----------------------------

      class C_command_list_base: public C_command{
      public:
         enum{
            FLAG_NOSELECT = 1,
            FLAG_NOINFERIORS = 2,
         };
         dword flags;

         C_command_list_base(E_STATE st):
            C_command(st),
            flags(0)
         {}
         bool ParseFlags(const char *&cp);
      };

   //----------------------------

      class C_command_create: public C_command_list_base{
      public:
         bool in_list;

         C_command_create():
            C_command_list_base(ST_IMAP_CREATE_FOLDER),
            in_list(false)
         {}
      };

   //----------------------------

      class C_command_list: public C_command_list_base{
      public:
         struct S_folder{
            Cstr_c name;
            C_vector<S_folder> subfolders;
            dword flags;
            S_folder():
               flags(FLAG_NOSELECT)
            {}
         };
         C_vector<S_folder> folders;

         char delimiter;
         enum{
            ROOT,
            INBOX,
         } phase;

         C_command_list():
            C_command_list_base(ST_IMAP_LIST),
            delimiter(0),
            phase(ROOT)
         {}

         void AddFolder(const char *name, bool need_decode);
      };

   //----------------------------

      class C_command_get_uids: public C_command{
      public:
         dword seq_num;

         C_command_get_uids():
            C_command(ST_IMAP_GET_UIDS),
            seq_num(0)
         {}
      };

   //----------------------------

      class C_command_search_since_date: public C_command{
      public:
         C_vector<dword> seqs;
         bool idle_refresh;

         C_command_search_since_date(bool _idle_refresh):
            C_command(ST_IMAP_SEARCH_SINCE),
            idle_refresh(_idle_refresh)
         {}
      };

   //----------------------------

      class C_command_uid: public C_command{
      public:
         dword uid;
         C_command_uid(E_STATE st):
            C_command(st),
            uid(0)
         {}
      };

   //----------------------------

      class C_command_get_hdr: public C_command_uid{
      public:
         C_vector<char> hdr_data;
         S_bodystructure bodystructure;
         dword hdr_size;
         dword seq_num;
         dword flags;
         bool sync_with_headers;
         bool got_uid;
         bool optimized_uid_range;

         C_command_get_hdr():
            C_command_uid(ST_IMAP_GET_HDR),
            hdr_size(0),
            sync_with_headers(false),
            optimized_uid_range(false),
            got_uid(false)
         {}

         virtual S_bodystructure *GetBodyStruct(){ return &bodystructure; }
         void Reset(){
            hdr_data.clear();
            bodystructure.Clear();
            hdr_size = 0;
            uid = 0;
            seq_num = 0;
            flags = 0;
            got_uid = false;
         }
      };

   //----------------------------

      class C_command_get_body: public C_command_uid, public S_download_body_data{
      public:
         bool got_fetch;
         dword start_progress_pos;  //before body download, to fix progress bar
         S_message temp_msg;

         enum{
            PHASE_GET_HDRS,         //get all retrieved message headers, so that next phase can decode data
            PHASE_GET_BODY_TEXT,    //get message text, done after previous phase
            PHASE_GET_BODY_FULL,    //get entire message body (incl. headers)
         } phase;
         C_vector<dword>
            uids_full,        //msgs retrieved as full text
            uids_text[3],     //msgs retrieved from sub-part; [0]=part "1", [1] = part "1.1", [2] = part "1.2"
            curr_uid_range;
         dword curr_text_subpart;

                              //msg header data, filled in PHASE_GET_HDRS, used in PHASE_GET_BODY_TEXT
         struct S_mime_header{
            dword msg_sequence_number;
            S_content_type content;
            E_CONTENT_ENCODING content_encoding;
            E_TEXT_CODING coding;
            Cstr_c multipart_boundary;
            bool format_flowed, format_delsp;
         };
         C_vector<S_mime_header> mime_headers;
         dword seq_num;
         bool got_flags;

         C_command_get_body():
            C_command_uid(ST_IMAP_GET_BODY),
            curr_text_subpart(0),
            got_fetch(false)
         {
         }
         void Reset(){
            got_fetch = true;
            got_flags = false;
            uid = 0;
            temp_msg = S_message();
            S_download_body_data::Reset();
         }
      };

   //----------------------------

      class C_command_get_attachment_body: public C_command_uid, public S_download_body_data{
      public:
         dword message_index;
         dword attachment_index;
         bool got_fetch;
                              //unexpected fetch:
         dword seq_num;
         dword untagged_flags;
         bool got_flags;

         C_command_get_attachment_body(dword msg_index, dword att_index):
            C_command_uid(ST_IMAP_GET_ATTACHMENT_BODY),
            message_index(msg_index),
            attachment_index(att_index),
            got_fetch(false),
            untagged_flags(0),
            got_flags(false)
         {}
      };

   //----------------------------

      class C_command_get_msg_headers: public C_command{
      public:
         C_vector<char> full_hdr;
         dword message_index;
         bool got_fetch;
                              //unexpected fetch:
         dword seq_num;
         dword untagged_flags;
         bool got_flags;

         C_command_get_msg_headers(dword msg_index):
            C_command(ST_IMAP_GET_MSG_HEADERS),
            message_index(msg_index),
            got_fetch(false),
            untagged_flags(0),
            got_flags(false)
         {}
      };

   //----------------------------

      class C_command_move_to_folder: public C_command{
      public:
         C_vector<dword> uids;
         Cstr_w folder_name;
         Cstr_c enc_folder_name;
         Cstr_c imap_cmd;     //sent command, saved for case of folder creation and resending
         enum{
            PHASE_MOVE,
            PHASE_CREATE,
         } phase;

         C_command_move_to_folder(E_STATE st):
            C_command(st),
            phase(PHASE_MOVE)
         {}
      };

   //----------------------------

      class C_command_fetch_sync: public C_command{
      public:
         S_message *curr_msg;
         bool need_update;

         C_command_fetch_sync():
            C_command(ST_IMAP_FETCH_SYNC),
            curr_msg(NULL),
            need_update(false)
         {}
      };

   //----------------------------

      class C_command_idle: public C_command{
      public:
         bool done_sent;

         C_command_idle():
            C_command(ST_IMAP_IDLE),
            done_sent(false)
         {}
      };

   //----------------------------

      struct S_msg_seq_map{
         struct S_value{
            dword seq, uid;
         };
         C_vector<S_value> map;
         bool is_synced;
         S_msg_seq_map():
            is_synced(false)
         {}

         void Clear(){
            map.clear();
            is_synced = false;
         }
         void Assign(dword seq, dword uid);
         dword FindUid(dword seq) const;  //Find UID of given message, return 0xffffffff if not found.
         dword FindAndRemoveUid(dword seq);
         dword GetMaxSeqNum() const;
      } msg_seq_map;

   //----------------------------

      C_vector<C_smart_ptr<C_command> > commands;

                              //downloaded multiline command state
      struct S_multiline{
         int string_size;     //size of fetched IMAP string; 0 = no multiline, -1 = waiting for last line
         C_smart_ptr<C_command> cmd;       //command which is being processed
      } multiline;

      C_mode_connection_imap(C_application_ui &_app, C_mode *sm, S_account &_acc, C_message_container *fld, E_ACTION _action, const S_connection_params *_params):
         C_mode_connection_in(_app, sm, _acc, fld, _action, _params),
         state(ST_WAIT_CONNECT_OK),
         capability(0),
         curr_tag_id(0)
      {
         multiline.string_size = 0;
      }
      ~C_mode_connection_imap();

      virtual void ProcessInput(S_user_input &ui, bool &redraw){ static_cast<C_mail_client&>(app).ConnectionProcessInputImap(*this, ui, redraw); }
      void AdoptOpenedConnection();
   };

//----------------------------

   class C_mode_connection_pop3;
   friend class C_mode_connection_pop3;
   void ConnectionProcessInputPop3(C_mode_connection_pop3 &mod, S_user_input &ui, bool &redraw);

   class C_mode_connection_pop3: public C_mode_connection_in{
   public:
      enum{
         CAPS_PIPELINING = 1,
         CAPS_STARTTLS = 2,
         CAPS_IN_TLS = 0x80000000,
      };
      dword capability;

      dword num_messages;     //in mailbox, determined from STAT
      bool is_gmail;
      bool uidls_fixed;
      struct S_download_body_data_pop3: public S_download_body_data{
         dword message_index;
         dword start_progress_pos;  //before body download, to fix progress bar
      } body_data;

                              //message indexes for which we retrieve bodies
      C_vector<dword> retr_msg_indexes;
      dword get_body_ask_index;

      dword delete_message_index;
      Cstr_c apop_timestamp;

      enum{                   //low-level operational state
         ST_WAIT_CONNECT_OK,
         ST_POP3_APOP,
         ST_POP3_USER,
         ST_POP3_PASS,
         ST_POP3_CAPA_CHECK,
         ST_POP3_CAPA,           //use CAPA for checking capabilities
         ST_POP3_STAT,           //use STAT for checking # of messages
         ST_POP3_LIST_BEGIN,     //use LIST to determine sizes of messages
         ST_POP3_LIST,           //use LIST to determine sizes of messages
         ST_POP3_UIDL_CHECK,     //check if UIDL is supported
         ST_POP3_UIDL,           //use UIDL to get messages' UIDs
         ST_POP3_UIDL_SINGLE,    //UIDL <n>
         ST_POP3_GET_TOP_CHECK,  //check if TOP is supported
         ST_POP3_GET_HDR_TOP,    //get header using TOP
         ST_POP3_RETR_CHECK,     //begin RETR
         ST_POP3_RETR,           //get message using RETR
         ST_POP3_DELE,           //delete messages using DELE
         ST_POP3_QUIT,
         ST_POP3_STARTTLS,       //switch to TLS connection
      } state;

      C_mode_connection_pop3(C_application_ui &_app, C_mode *sm, S_account &_acc, C_message_container *fld, E_ACTION _action, const S_connection_params *_params):
         C_mode_connection_in(_app, sm, _acc, fld, _action, _params),
         state(ST_WAIT_CONNECT_OK),
         capability(0),
         num_messages(0),
         uidls_fixed(false),
         is_gmail(false)
      {
      }

   //----------------------------
   // From downloaded UIDs, fix pop3 indexes in all messages. Set index -1 for messages not existing on server.
      void FixPop3Indexes();

      virtual void ProcessInput(S_user_input &ui, bool &redraw){ static_cast<C_mail_client&>(app).ConnectionProcessInputPop3(*this, ui, redraw); }
   };

//----------------------------

   class C_mode_connection_imap_upload;
   friend class C_mode_connection_imap_upload;

   void ImapUploadSocketEvent(C_mode_connection_imap_upload &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw);

   class C_mode_connection_imap_upload: public C_mode_connection_imap, public C_connection_send{
   public:
      C_mode_connection::E_ACTION next_action;

      dword message_index;
      bool user_canceled;
      C_vector<char> curr_header;

      C_mode_connection_imap_upload(C_application_ui &_app, C_mode *sm, S_account &_acc, C_message_container *fld, E_ACTION _action, const S_connection_params *_params, C_mode_connection::E_ACTION na):
         C_mode_connection_imap(_app, sm, _acc, fld, _action, _params),
         next_action(na),
         user_canceled(false),
         message_index(0)
      {
      }
      virtual C_message_container &GetContainer(){
         if(cnt_out)
            return *cnt_out;
         return C_mode_connection_imap::GetContainer();
      }
      virtual void SocketEvent(C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){ static_cast<C_mail_client&>(app).ImapUploadSocketEvent(*this, ev, socket, redraw); }
   };
   void SetModeConnectionImapUpload(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params);
// Start uploading next sent message to IMAP server. If there's no message, continue in connection.
   void StartUploadingMessageToImap(C_mode_connection_imap_upload &mod);
   void ImapUploadCloseMode(C_mode_connection_imap_upload &mod);

//----------------------------

   class C_mode_connection_auth;
   friend class C_mode_connection_auth;
   void AuthCheckSocketEvent(C_mode_connection_auth &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw);
   void AuthCheckProcessInput(C_mode_connection_auth &mod, S_user_input &ui, bool &redraw);

   class C_mode_connection_auth: public C_mode_connection_base{
   public:
      static const dword ID = FOUR_CC('A','U','T','H');
      C_application_http::C_http_data_loader ldr;

      C_mode_connection_auth(C_application_ui &_app, C_mode *sm, S_account &ac, C_message_container *fld, E_ACTION _action, const S_connection_params *_params):
         C_mode_connection_base(_app, sm, ac, fld, _action, _params)
      {
         mode_id = ID;
         ldr.mod_socket_notify = this;
      }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ static_cast<C_mail_client&>(app).AuthCheckProcessInput(*this, ui, redraw); }
      virtual void SocketEvent(C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){ static_cast<C_mail_client&>(app).AuthCheckSocketEvent(*this, ev, socket, redraw); }
   };
public:
//----------------------------
// Setup connection mode.
// Return true if successful, false if nothing to do for given action (e.g. no messages for send).
   void SetModeConnection(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params = NULL);

   void ConnectionInit(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, const S_connection_params *params);
   void ConnectionFinishInit(C_mode_connection_base &mod);
   void ConnectionError(C_mode_connection_in &mod, const wchar *err);
   void ConnectionErrorImap(C_mode_connection_imap &mod, const wchar *err);
   void ConnectionErrorPop3(C_mode_connection_pop3 &mod, const wchar *err);

   void ConnectionCancelAllImapCommands(C_mode_connection_imap &mod);
protected:
   void ConnectionDrawText(const C_mode_connection_base &mod, const Cstr_w &t, dword line);
   void ConnectionDrawTitle(C_mode_connection_base &mod, const Cstr_w &t);
   void ConnectionDrawAction(C_mode_connection_base &mod, const Cstr_w &s);
   void ConnectionDrawFolderName(C_mode_connection_base &mod, const Cstr_w &s);

   void ConnectionInitSocket(C_mode_connection_in &mod);

   void ConnectionDrawSocketEvent(C_mode_connection_base &mod, C_socket_notify::E_SOCKET_EVENT ev);

//----------------------------

   void ConnectionCleanupAndDisconnect(C_mode_connection_in &mod, bool allow_imap_delete_thrash_move = true);

//----------------------------
// Disconnect: play new mail alert, expunge, close mode.
   void ConnectionDisconnect(C_mode_connection_in &mod);

   void ConnectionExpungeIMAP(C_mode_connection_imap &mod);
   void ConnectionExpungePOP3(C_mode_connection_pop3 &mod);

//----------------------------
// After sending, start uploading sent messages. Return false if not possible/successful.
   bool ConnectionImapStartUploadingSent(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION act, const S_connection_params *params);


//----------------------------
// When updating multiple IMAP folders.
   bool ConnectionImapFolderClose(C_mode_connection_imap &mod);

//----------------------------

   void SendImapCommand(C_mode_connection_imap &mod, const char *str, C_mode_connection_imap::C_command *cmd = NULL, int override_timeout = 0);

   void SendCompressedData(C_socket *socket, const void *data, dword len, z_stream &zs, int override_timeout = 0);

//----------------------------

   bool ParseImapFetch(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd, const char *cp);
   void ImapFetchDone(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd);

   void ImapAddNewHeader(C_mode_connection_imap &mod, S_message_header_base &hdr, C_mode_connection_imap::C_command_get_hdr *cmd);

   bool BeginImapIdle(C_mode_connection_imap &mod);
   void ImapIdleUpdateAfterOperation(C_mode_connection_imap &mod, bool num_msgs_changed);
   void ImapIdleAfterGotHeaders(C_mode_connection_imap &mod);
//----------------------------
   void ConnectionRedrawImapIdleFolder(const S_account &acc);
//----------------------------
// Update state, if it change, redraw appropriate part of screen.
   void ConnectionUpdateState(S_account &acc, S_account::E_UPDATE_STATE state, const wchar *status_text = NULL);

//----------------------------

   void Connection_AfterMailboxSelect(C_mode_connection_in &mod, int num_msgs);
   void Connection_MoveImapMessages(C_mode_connection_imap &mod);

   void GetMessageListIMAP(C_mode_connection_imap &mod, int num_msgs);
   void GetMessageListPOP3(C_mode_connection_pop3 &mod, int num_msgs);

   void SearchLastMessagesIMAP(C_mode_connection_imap &mod);

   bool Connection_AfterUidList(C_mode_connection_in &mod, bool use_delete_list);

//----------------------------
// After getting new header, parse it, determine by rules next action (add, delete, or other).
// Returns ref to newly added message.
   S_message &Connection_AfterGotHeader(C_mode_connection_in &mod, S_message_header_base &hdr, const char *hdr_data, dword hdr_size, int override_msg_size = -1);

//----------------------------
// Action to be performed after all new headers were downloaded.
   void Connection_AfterAllHeaders(C_mode_connection_in &mod);

//----------------------------
// Check if message matches to some defined rule.
   const S_rule *DetectRule(const S_account &acc, const S_complete_header &hdr, const C_message_container *fld) const;

//----------------------------
// Begin retrieval of next marked header.
   void BeginGetHeaderPOP3(C_mode_connection_pop3 &mod);

//----------------------------
// Begin downloading message body.
   void BeginRetrieveMessageImap(C_mode_connection_imap &mod, dword msg_index);
   void BeginRetrieveMessagePop3(C_mode_connection_pop3 &mod, dword msg_index);
   bool RetrieveMessagePop3(C_mode_connection_pop3 &mod, dword msg_index, dword force_partial_kb = 0);

//----------------------------

   void BeginRetrieveMsgHeadersImap(C_mode_connection_imap &mod);

//----------------------------
// Sync account's messages - check UIDs, remove old ones, add deleted to delete list.
// Also remove headers which are present in local messages, and need not to be retrieved.
   void SyncMessages(C_mode_connection_in &mod, bool use_delete_list = true);

//----------------------------
// Send command for retrieving body of selected message.
   void StartRetrievingMessageMarkedBodiesPOP3(C_mode_connection_pop3 &mod, const C_vector<dword> *msg_indexes);
   void StartRetrievingMessageMarkedBodiesIMAP(C_mode_connection_imap &mod, const C_vector<dword> &msg_indexes);

   void StartRetrievingBodiesTextIMAP(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_get_body *cmd);
   void StartRetrievingBodiesFullIMAP(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_get_body *cmd);

//----------------------------
// After getting headers, determine if there's messages to move to different folder by Rule, and initiate commands.
   void StartMoveMessagesByRuleIMAP(C_mode_connection_imap &mod);

//----------------------------
// Send command for retrieving attachment of selected message.
   bool StartRetrievingMessageAttachment(C_mode_connection_imap &mod);
   bool RetrieveNextMessageAttachment(C_mode_connection_imap &mod);

//----------------------------
// Start deleting of messages marked as deleted from server.
// Returns false if no message is to be deleted.
   bool StartDeletingMessages(C_mode_connection_in &mod, bool allow_imap_thrash_move = true);
   void StartDeletingMessagesImap(C_mode_connection_imap &mod, bool allow_imap_thrash_move);
   void StartDeletingMessagesPop3(C_mode_connection_pop3 &mod);

//----------------------------
// Srart updating message flags on server.
// Returns false if no update was needed.
   bool StartUpdatingServerFlags(C_mode_connection_imap &mod, C_mode_connection_imap::E_STATE set_flags_state = C_mode_connection_imap::ST_IMAP_SET_FLAGS);

//----------------------------
// Issue command for deleting next message
   void Pop3StartDeleteNextMessage(C_mode_connection_pop3 &mod);

//----------------------------
// Remove messages deleted from server.
   void RemoveDeletedMessages(C_mode_connection_in &mod);

//----------------------------
// Issue command for retrieving new headers (or disconnect if no headers to retrieve).
   void StartGettingNewHeadersIMAP(C_mode_connection_imap &mod, bool force_optimized_uid_range = false);
   void StartGettingNewHeadersPOP3(C_mode_connection_pop3 &mod);

//----------------------------
// This function sorts message container after some new headers were added at end of list.
   void AfterHeadersAdded(C_mode_connection_in &mod);

   //void ConnectionDrawFloatDataCounters(C_mode_connection_base &mod);

//----------------------------
// Add line to currently retrieved message.
// 'line' contains terminating '0' character.
// Returns false when end of message was detected.
   bool AddRetrievedMessageLine(C_mode_connection_in &mod, S_download_body_data &body_data, S_message &msg, const C_buffer<char> &line, Cstr_w &err);
   bool AddRetrievedMessageLine1(C_mode_connection_in &mod, S_download_body_data &body_data, S_message &msg, const C_buffer<char> &line, Cstr_w &err);

   void AddRetrievedAttachmentData(const C_mode_connection_in &mod, S_download_body_data &body_data, const char *cp, dword sz, E_CONTENT_ENCODING content_encoding, Cstr_w &err) const;

//----------------------------

   void FinishBodyRetrieval(C_mode_connection_in &mod, S_download_body_data &body_data, S_message &msg);

//----------------------------
   bool BeginAttachmentRetrieval(const C_mode_connection_in &mod, S_download_body_data &body_data, const char *content_subtype, const wchar *suggested_filename) const;
   static void FinishAttachmentRetrieval(C_mode_connection_in &mod, S_download_body_data &body_data, const S_complete_header &hdr, S_message &msg);

//----------------------------

   bool BeginMessageBodyRetrieval(const C_message_container &cnt, S_download_body_data &body_data, const char *ext, const wchar *suggested_filename) const;
   bool FinishMessageBodyRetrieval(C_message_container &cnt, S_download_body_data &body_data, const S_complete_header &hdr, S_message &msg) const;

//----------------------------
// Process line received from socket, according to current connection state.
// If 'err' is set, the connection is closed and error is displayed.
// If returned value is false, the mode changed.
   bool ProcessReceivedLine(C_mode_connection_in &mod, const C_socket::t_buffer &line, Cstr_w &err);

   bool ProcessLinePop3(C_mode_connection_pop3 &mod, const C_socket::t_buffer &line, Cstr_w &err);
   bool ProcessLineImap(C_mode_connection_imap &mod, const C_socket::t_buffer &line, Cstr_w &err);

   void Pop3Login(C_mode_connection_pop3 &mod, Cstr_w &err);
   void Pop3CopyMessagesToFolders(C_mode_connection_in &mod);

//----------------------------

   void ImapProcessCapability(C_mode_connection_imap &mod, const char *cp);
   bool ImapProcessList(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_list *cmd, const char *cp);
   bool ImapProcessMultiLine(C_mode_connection_imap &mod, const C_socket::t_buffer &line, Cstr_w &err);
   bool ImapProcessOkUntagged(C_mode_connection_imap &mod, const char *cp, C_mode_connection_imap::E_STATE state, Cstr_w &err);
   bool ImapProcessOkTagged(C_mode_connection_imap &mod, const char *cp, C_mode_connection_imap::C_command *cmd, Cstr_w &err);
   bool ImapProcessNoTagged(C_mode_connection_imap &mod, const char *cp, C_mode_connection_imap::C_command *cmd, Cstr_w &err);
   void ImapProcessExpunge(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd0, dword msg_seq_no);
   void ImapProcessExists(C_mode_connection_imap &mod, C_mode_connection_imap::C_command *cmd0, dword num_msgs);
   void ImapIdleUpdateFlags(C_mode_connection_imap &mod, bool also_delete);
   bool ImapAfterGotCapability(C_mode_connection_imap &mod, Cstr_w &err);

   void ImapIdleSendDone(C_mode_connection_imap &mod);

//----------------------------
// Move messages from one folder to another, as result of IMAP operation with UIDPLUS support. Also mark moved messages as deleted in original folder and put them to delete list.
   bool ImapMoveMessagesToFolder(C_mode_connection_imap &mod, const C_vector<dword> &uids, const char *uid_map, C_message_container &fld_dst, bool clear_delete_flag = false);
//----------------------------
// Same as above, but in case of failure, just perform deletion part.
   void ImapMoveMessagesToFolderAndDelete(C_mode_connection_imap &mod, const C_vector<dword> &uids, const char *uid_map, C_message_container &fld_dst);

//----------------------------
// Login to IMAP server.
   void ImapLogin(C_mode_connection_imap &mod);

//----------------------------

   void AfterPop3Login(C_mode_connection_pop3 &mod);

   void ConnectionImapSelectFolder(C_mode_connection_imap &mod);
   void AfterImapLogin(C_mode_connection_imap &mod);
public:
//----------------------------
// Check # of messages for sending, and determine total send size (for progress indicator).
   dword CountMessagesForSending(S_account &acc, C_message_container *cnt, C_mode_connection::E_ACTION action, C_message_container **outbox, int *total_size = NULL) const;

//----------------------------
// Get # of messages for server upload. The count is positive for sent messages, and negative for drafts.
   int CountMessagesForUpload(S_account &acc, C_message_container **outbox, int *total_size = NULL) const;

//----------------------------
// Prepare and send mail headers (Subject, Date, etc) for currently sent message.
   void PrepareMailHeadersForSending(const C_mode_connection &mod, C_connection_send &con_send, const S_message &msg, C_vector<char> &buf);

//----------------------------
// Prepare next part of message (text/attachments), return true if nothing to send.
   bool PrepareNextMessageData(C_mode_connection &mod, C_connection_send &con_send, const S_message &msg, C_vector<char> &buf, bool count_progress);

//----------------------------
// Prepare and send next part of message. Return true if sending finished.
   bool SendNextMessageData(C_mode_connection &mod, C_connection_send &con_send, C_socket *socket, const S_message &msg, z_stream *compress_out = NULL);

//----------------------------
// Cancel retrieving current message (delete any message's files, clear retrieve flag, and save messages).
   void CancelMessageRetrieval(C_mode_connection &mod, dword msg_index);

//----------------------------
// After IMAP LIST command finishes, update account's folders with retrieved folders.
   void CleanImapFolders(C_mode_connection_imap &mod, C_mode_connection_imap::C_command_list &cmd);
   void CleanFoldersHierarchy(C_mode_connection_imap &mod, t_folders &flds, const C_vector<C_mode_connection_imap::C_command_list::S_folder> &flst, bool &changed, C_vector<dword> &folder_ids, C_message_container *parent);
//----------------------------
//----------------------------
protected:
                              //mode displaying list of accounts in small window, and letting user choose one of them
   class C_mode_account_selector;
   friend class C_mode_account_selector;

   class C_mode_account_selector: public C_mode_list<C_mail_client>{
      typedef C_mode_list<C_mail_client> super;
   public:
      Cstr_c force_recipient;
      mutable bool draw_bgnd;
      S_rect rc1;

      S_message tmp_msg;      //when doing File->send by email, this is temp message holding attachment filename

      C_mode_account_selector(C_mail_client &_app, const char *rcpt):
         super(_app),
         force_recipient(rcpt),
         draw_bgnd(true)
      {}
      virtual void InitLayout();
      virtual void ProcessInput(S_user_input &ui, bool &redraw);
      virtual void Draw() const;
   };
public:
//----------------------------
// Set account selector for sending message.
   void SetModeAccountSelector(const char *rcpt = NULL);

//----------------------------
// Set account selector for sending attachment.
   void SetModeAccountSelector_SendFiles(const C_client_file_mgr::C_mode_file_browser &mod);

//----------------------------
//----------------------------
   dword CountUnreadVisibleMessages();

   C_smart_ptr<C_unknown> led_flash_notify;

//----------------------------
#ifdef USE_NOKIA_N97_WIDGET
   class C_hs_widget_profimail: public C_hs_widget, public C_hs_widget::C_obeserver{
      class RLibrary *lib;
      C_smart_ptr<C_hs_widget> imp;
      bool activated, suspended;

      virtual void Event(C_hs_widget::E_EVENT ev);
      virtual void ItemSelected(C_hs_widget::E_ITEM item);
   public:
      C_mail_client &app;

      C_hs_widget_profimail(C_mail_client &_app):
         app(_app),
         lib(NULL),
         activated(false),
         suspended(false)
      {}
      ~C_hs_widget_profimail();
      virtual void SetItem(E_ITEM itm, const char *text){ imp->SetItem(itm, text); }
      virtual void SetImage(const char *fn){ imp->SetImage(fn); }
      virtual void SetMifImage(const char *fn){ imp->SetMifImage(fn); }
      virtual void Publish(){ imp->Publish(); }
      virtual void Remove(){ if(imp) imp->Remove(); }
      virtual void DrawBitmap(const class CFbsBitmap &bmp){ imp->DrawBitmap(bmp); }

      bool Init();
      void Close();
      inline bool IsInited() const{ return (imp!=NULL); }
      inline bool IsActivated() const{ return (IsInited() && activated); }
      inline bool IsSuspended() const{ return suspended; }

   } hs_widget;
   /*
   virtual void Exit(){
      if(!hs_widget.IsActivated())
         C_client::Exit();
   }
   */
#endif
   class C_home_screen_notify;
   friend class C_home_screen_notify;

   class C_home_screen_notify{
      C_mail_client &app;

      bool active;            //same as app.IsFocused()
      C_smart_ptr<C_notify_window> mail_notify;

      struct S_msg{
         S_message_header hdr;
         S_account *acc;
         C_message_container *cnt;
         /*
         bool deleted;
         S_msg():
            deleted(false)
         {}
         */
      };
      C_vector<S_msg> new_hdrs;
      int display_hdr_index;

   //----------------------------
   // Find real message from our header.
      S_message *FindMessage(const S_msg &msg) const;

   public:
      bool debug_show_all;    //add all unread msgs to list when activating, not only recent coming later

      C_home_screen_notify(C_mail_client &_app):
         app(_app),
         active(false),
         display_hdr_index(0),
         debug_show_all(false)
      {}
      void Activate();
      inline bool IsActive() const{ return active; }
      void Close();           //remove any notifications when focus gained
      void AddNewMailNotify(S_account &acc, C_message_container &cnt, const S_message_header &msg, bool allow_simple_notify);
      void RemoveMailNotify(S_account &acc, C_message_container &cnt, const S_message_header &msg);
      void InitAfterScreenResize();
#ifdef USE_NOKIA_N97_WIDGET
      void DrawHsWidgetHeader();
      void HsWidgetClickField(int txt_id);
      void HsWidgetResume();
#endif
   } home_screen_notify;

   void DrawUnreadMailNotify(C_notify_window *mail_notify);
   void FlashNewMailLed();

//----------------------------
// Update counter of unread messages (so that they can be displayed in idle screen).
   void UpdateUnreadMessageNotify();

//----------------------------
// Check mail files - delete unused ones.
   void CleanupMailFiles();

public:
//----------------------------
   bool OpenMessageBody(const C_message_container &cnt, const S_message &msg, S_text_display_info &text_info, bool preview_mode) const;

//----------------------------
   bool SaveMessageBody(C_message_container &cnt, S_message &msg, const char *body, dword len, const char *ext = NULL) const;

// Save msg to EML file.
// Not implemented.
   void SaveMessageToFile(const S_message &msg, const wchar *fn);

   void ShowMessageDetails(const S_message &msg, const char *full_headers = NULL);
protected:
//----------------------------
// Decode message text line, depending on given encoding.
// Returns false if line can't be stored (disk full).
   static bool DecodeMessageTextLine(const char *cp, dword sz, E_CONTENT_ENCODING content_encoding, C_file &fl, S_download_body_data *body_data = NULL);

   static bool DecodeMessageAttachmentLine(const char *cp, dword sz, E_CONTENT_ENCODING content_encoding, C_file &fl);

//----------------------------
// Parse header and init fields of 'hdr'. Return true if successful.
   bool ParseMailHeader(const char *cp, int hdr_size, S_complete_header &hdr) const;

//----------------------------
//----------------------------
public:
   class C_mode_folders_list;
   friend class C_mode_folders_list;
   void InitLayoutFoldersList(C_mode_folders_list &mod);
   void FoldersListProcessInput(C_mode_folders_list &mod, S_user_input &ui, bool &redraw);
   void FoldersListProcessMenu(C_mode_folders_list &mod, int itm, dword menu_id);
   void DrawFoldersList(const C_mode_folders_list &mod);
   void DrawFoldersListContents(const C_mode_folders_list &mod);

   class C_mode_folders_list: public C_mode_list<C_mail_client>{
      typedef C_mode_list<C_mail_client> super;
   public:
      static const dword ID = FOUR_CC('F','L','S','T');
      S_account &acc;
      bool show_hidden;
      bool creating_new;      //used when te_rename is not NULL for difference between rename and remove

      C_smart_ptr<C_text_editor> te_rename;
      int max_name_width;
      int num_entries;

      C_smart_ptr<C_mode_update_mailboxes> mod_upd_mboxes;
      C_message_container *folder_move_target;

      C_mode_folders_list(C_mail_client &_app, S_account &acc1):
         super(_app),
         acc(acc1),
         folder_move_target(NULL),
         show_hidden(false),
         creating_new(false)
      {
         mode_id = ID;
      }
      ~C_mode_folders_list();

      virtual bool IsPixelMode() const{ return true; }
      virtual int GetNumEntries() const{ return num_entries; }

      virtual void InitLayout(){ app.InitLayoutFoldersList(*this); }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ app.FoldersListProcessInput(*this, ui, redraw); }
      virtual void ProcessMenu(int itm, dword menu_id){ app.FoldersListProcessMenu(*this, itm, menu_id); }
      virtual void Draw() const{ app.DrawFoldersList(*this); }
      virtual void DrawContents() const{ app.DrawFoldersListContents(*this); }

      C_message_container *GetSelectedFolder();
   };
   C_mode_folders_list &SetModeFoldersList(S_account &acc, bool allow_folders_refresh = true);
   void DrawFolder(const C_mode_folders_list &mod, int fi);

   void FoldersList_StartRename(C_mode_folders_list &mod);
   void FoldersList_FinishRename(C_mode_folders_list &mod);
   void FoldersList_FinishDelete(C_mode_folders_list &mod);

   void FoldersList_StartCreate(C_mode_folders_list &mod, bool ask_subfolder, bool in_root = true);
   void ImapFolders_FinishCreate(C_mode_folders_list &mod);

   void FoldersList_InitView(C_mode_folders_list &mod, const C_message_container *sel_fld = NULL);
   void FoldersList_DeleteFolder(C_mode_folders_list &mod);
   void FoldersListClose(C_mode_folders_list &mod);

   void ImapFolders_UpdateFolders(C_mode_folders_list &mod, bool auto_update = false);

   void OpenImapFolder(C_mode_folders_list &mod, bool &redraw);
   void FoldersList_MoveToFolder(C_mode_folders_list &mod, bool ask, C_message_container *target = NULL);
   void FoldersList_MoveToFolder1(C_message_container *fld){
      FoldersList_MoveToFolder((C_mode_folders_list&)*mode, false, fld);
   }
protected:
   static void SortFolders(S_account &acc, C_mode_folders_list *mod, const C_message_container *sel_fld = NULL);
   static void SortFoldersHierarchy(t_folders &flds);

//----------------------------
//----------------------------
public:
                              //mode displaying list of accounts in small window, and letting user choose one of them
   class C_mode_folder_selector;
   friend class C_mode_folder_selector;
   void InitLayoutImapFolderSelector(C_mode_folder_selector &mod);
   void ImapFolderSelectorProcessInput(C_mode_folder_selector &mod, S_user_input &ui, bool &redraw);
   void DrawImapFolderSelector(const C_mode_folder_selector &mod);
   void DrawImapFolderSelectorContets(const C_mode_folder_selector &mod);

   class C_mode_folder_selector: public C_mode_list<C_mail_client>{
      typedef C_mode_list<C_mail_client> super;
   public:
      typedef void (C_mail_client::*t_FolderSelected)(C_message_container *fld);
      t_FolderSelected FolderSelected;
      S_account &acc;
      const C_message_container *ignore_folder;
      bool allow_nonselectable, allow_noninferiors;
      bool show_root;
      C_vector<bool> save_expand_flag;

      mutable bool redraw_bgnd;
      S_rect rc_outline;
      int num_entries;

      C_mode_folder_selector(C_mail_client &_app, S_account &_acc, t_FolderSelected fsel, const C_message_container *ignore, bool allow_nonsel, bool allow_noninf, bool _show_root):
         super(_app),
         FolderSelected(fsel),
         acc(_acc),
         ignore_folder(ignore),
         allow_nonselectable(allow_nonsel),
         allow_noninferiors(allow_noninf),
         show_root(_show_root),
         redraw_bgnd(true)
      {}
      C_message_container *GetSelectedFolder();
      bool IsValidTraget(const C_message_container *fld) const;

      virtual bool IsPixelMode() const{ return true; }
      virtual int GetNumEntries() const{ return num_entries; }

      virtual void InitLayout(){ app.InitLayoutImapFolderSelector(*this); }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ app.ImapFolderSelectorProcessInput(*this, ui, redraw); }
      virtual void Draw() const{ app.DrawImapFolderSelector(*this); }
      virtual void DrawContents() const{ app.DrawImapFolderSelectorContets(*this); }
   };
   void SetModeFolderSelector(S_account &acc, C_mode_folder_selector::t_FolderSelected fsel, const C_message_container *ignore_folder,
      bool allow_nonselectable = false, bool allow_noninferiors = true, bool show_root = false);
protected:
   void CloseModeFolderSelector(C_mode_folder_selector &mod, bool redraw = true);
   void FolderSelector_InitView(C_mode_folder_selector &mod, const C_message_container *sel_fld = NULL);

//----------------------------
//----------------------------

   void DeleteMessagesFromPhone(C_mode_mailbox &mod);

//----------------------------
//----------------------------
public:
                              //signatures
   C_buffer<S_signature> signatures;

   Cstr_w GetSignaturesFilename() const;
   void LoadSignatures();
   void SaveSignatures() const;

//----------------------------
//----------------------------
                              //mode for writing mail
   class C_mode_write_mail_base: public C_mode_app<C_mail_client>{
   public:
      static const dword ID = FOUR_CC('W','R','T','M');

      C_mode_write_mail_base(C_mail_client &_app):
         C_mode_app<C_mail_client>(_app)
      {}
      virtual bool SaveMessage(bool draft, bool upload_draft) = 0;
      virtual void AddAttachment(const Cstr_w &filename, bool set_edited_flag = true) = 0;
      virtual void AddRecipient(const Cstr_c &e_mail) = 0;
   };
   void SetModeWriteMail(C_message_container *cnt, S_message *msg, bool reply, bool forward, bool reply_all, const char *rcpt = NULL);
protected:
   C_mode_write_mail_base &SetModeWriteMail(const char *to, const char *cc, const char *bcc, const wchar *subject, const wchar *body);
public:
   C_message_container *FindFolder(S_account &acc, const wchar *name) const;
   C_message_container *FindOrCreateImapFolder(S_account &acc, const wchar *name, bool &created) const;
   C_message_container *FindInbox(S_account &acc) const;

   void SetModeEditSignatures();

   virtual void GetDateString(const S_date_time &dt, Cstr_w &str, bool force_short = false, bool force_year = false) const;
// Delete empty temps, return true if something was deleted.
   bool CleanEmptyTempImapFolders(S_account &acc, int *selection = NULL);
   C_message_container *FindImapFolderForUpdate(S_account &acc, const C_message_container *curr_fld, bool allow_hidden);

protected:
//----------------------------
//----------------------------

   void SetModeEditIdentities(S_account &acc);

//----------------------------
//----------------------------
                              //mode for downloading single file from net
                              // progress is displayed in small window
                              // after finishing, file is opened
   class C_mode_download;
   friend class C_mode_download;
   void InitLayoutDownload(C_mode_download &mod);
   void DownloadProcessInput(C_mode_download &mod, S_user_input &ui, bool &redraw);
   void DownloadSocketEvent(C_mode_download &mod, C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw);
   void DrawDownload(const C_mode_download &mod);

   class C_mode_download: public C_mode{
   public:
      Cstr_c url;
      Cstr_w filename;
      C_file fl;
      S_rect rc;

      mutable C_application_http::C_http_data_loader ldr;
      Cstr_w custom_msg;   //drawn instead of downloaded filename

                                    //image holding background of data counters
      mutable C_smart_ptr<C_image> data_cnt_bgnd;

      mutable bool draw_bgnd;

      C_mode_download(C_application_ui &_app, C_mode *sm, const wchar *_cm):
         C_mode(_app, sm),
         custom_msg(_cm),
         draw_bgnd(true)
      {
         ldr.mod_socket_notify = this;
      }
      virtual bool IsUsingConnection() const{ return true; }

      virtual void InitLayout(){ static_cast<C_mail_client&>(app).InitLayoutDownload(*this); }
      virtual void ProcessInput(S_user_input &ui, bool &redraw){ static_cast<C_mail_client&>(app).DownloadProcessInput(*this, ui, redraw); }
      virtual void SocketEvent(C_socket_notify::E_SOCKET_EVENT ev, C_socket *socket, bool &redraw){ static_cast<C_mail_client&>(app).DownloadSocketEvent(*this, ev, socket, redraw); }
      virtual void Draw() const{ static_cast<C_mail_client&>(app).DrawDownload(*this); }
   };

//----------------------------
// Set download mode. If dest name is given, file is saved there, and opened in file explorer, otherwise
//    it's loaded into temp dir, and default viewer is invodeked on it.
   void SetModeDownload(const char *url, const Cstr_w *dst_filename = NULL, const wchar *custom_msg = NULL);

//----------------------------
// Finish downloading of html image - put it into html text, adjust scrollbar, etc.
   void FinishImageDownload(C_text_viewer &vw, C_message_container *cnt, C_buffer<S_attachment> &atts);

//----------------------------

   bool SettingsExportCallback(const Cstr_w *file, const C_vector<Cstr_w> *files);
   bool SettingsImportCallback(const Cstr_w *file, const C_vector<Cstr_w> *files);
   void SettingsImportConfirm();

//----------------------------
// content = "Content-Type" ":" type "/" subtype *(";" parameter)
   void ReadContent(const char *&cp, S_complete_header &hdr) const;

//----------------------------
   void ReadContentDisposition(const char *&cp, S_complete_header &hdr) const;

//----------------------------
// text = <any CHAR, including bare CR & bare LF, but NOT including CRLF>
   void ReadText(const char *&cp, Cstr_w &str) const;

//----------------------------

   virtual bool ProcessKeyCode(dword code);

//----------------------------

#ifdef AUTO_CONNECTION_BACK_SWITCH
   C_smart_ptr<C_internet_connection> alt_test_connection;  //test of primary connection in case that alternative is being used (to switch back)
   C_smart_ptr<C_socket> alt_test_socket;
   dword alt_test_connection_counter;
#endif

public:
#ifdef _DEBUG_
   virtual dword GetSoftButtonBarHeight() const{
      return 0;
   }
#endif

   C_mail_client();
   ~C_mail_client();

//----------------------------
// address =  mailbox (one addressee) | group (named list)
   bool ReadAddress(const char *&cp, Cstr_w &name, Cstr_c &email) const;

//----------------------------
// Returns true if link was successfully opened.
   bool Viewer_OpenLink(C_text_viewer &vw, class C_mode_mailbox *parent_mode, bool open_in_system_browser, bool open_image = false);

//----------------------------
// Decode header text encoded with =?<charset>?<encoding>?<text>?= mechanism.
   void DecodeEncodedText(const Cstr_c &src, Cstr_w &dst) const;

//----------------------------
// Return to Accounts mode, if it is safe. It's prohibited from message composition, and other modes which would cause loss of data.
   bool SafeReturnToAccountsMode();

//----------------------------
// Reply to message with question if reply all or sender only.
   void ReplyWithQuestion(C_message_container &cnt, S_message &msg);

//----------------------------
   class C_compose_mail_data: public C_unknown{
   public:
      Cstr_w subj;
      Cstr_w body;
      Cstr_c rcpt[3];
      C_vector<Cstr_w> atts;

      bool IsEmpty() const{
         return (!subj.Length() &&
            !body.Length() &&
            !rcpt[0].Length() &&
            !rcpt[1].Length() &&
            !rcpt[2].Length() &&
            !atts.size());
      }
   };

   void ComposeEmail(const C_compose_mail_data &cdata);

   C_smart_ptr<C_unknown> after_init_compose_mail_data;  //conpose data waiting to be used after init password is entered; it is C_compose_mail_data*

};

//----------------------------

void GetMessageRecipients(const S_message &msg, const Cstr_c &ignore, C_vector<Cstr_c> &addresses);

//----------------------------

Cstr_w DecodeImapFolderName(const char *cp);
Cstr_c EncodeImapFolderName(const wchar *wp);

//----------------------------
// Check recipients string in 'str' - parse all recipients and add them to 'addresses'.
// Recipients are separated by spaces or comma.
// If any error is encountered, the func returns false, and err_l & err_r contain range of 1st error.
// 'addresses' is not cleared!
bool ParseRecipients(const char *str, C_vector<Cstr_c> &addresses, int *err_l = NULL, int *err_r = NULL);

//----------------------------
// addr-spec = local-part "@" domain (global address).
bool ReadAddressSpec(const char *&cp, Cstr_c &str);

//----------------------------
template<class T>
static bool CompareStringsNoCasePartial(const T *s, const T *match){

   while(*match){
      T cs = (T)text_utils::LowerCase(wchar(*s++));
      T cm = *match++;
      if(cs!=cm)
         return false;
   }
   return true;
}
