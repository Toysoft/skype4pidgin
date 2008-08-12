#include <ft.h>

void skype_auth_allow(gpointer sender);
void skype_auth_deny(gpointer sender);
static gboolean skype_handle_received_message(char *message);
gint skype_find_filetransfer(PurpleXfer *transfer, char *skypeid);
void skype_accept_transfer(PurpleXfer *transfer);
void skype_decline_transfer(PurpleXfer *transfer);
gint skype_find_chat(PurpleConversation *conv, char *chat_id);
static void purple_xfer_set_status(PurpleXfer *xfer, PurpleXferStatusType status);
void skype_call_accept_cb(gchar *call);
void skype_call_reject_cb(gchar *call);
gboolean skype_sync_skype_close(PurpleConnection *gc);
void handle_complete_message(int messagenumber);

gboolean skype_update_buddy_status(PurpleBuddy *buddy);
void skype_update_buddy_alias(PurpleBuddy *buddy);
void skype_update_buddy_icon(PurpleBuddy *buddy);
static PurpleAccount *skype_get_account(PurpleAccount *account);
const char *skype_get_account_username(PurpleAccount *acct);
gchar *skype_get_user_info(const gchar *username, const gchar *property);
gchar *skype_strdup_withhtml(const gchar *src);
void skype_put_buddies_in_groups();
void skype_get_chatmessage_info(int message);
void set_skype_buddy_attribute(SkypeBuddy *sbuddy, const gchar *skype_buddy_property, const gchar *value);
SkypeBuddy *skype_buddy_new(PurpleBuddy *buddy);

char *skype_send_message(char *message, ...);
//dont use this unless you know what you're doing:
void skype_send_message_nowait(char *message, ...);

static time_t last_pong = 0;
static GHashTable *messages_table = NULL;
static GHashTable *groups_table = NULL;

typedef enum _SkypeMessageType {
	SKYPE_MESSAGE_OTHER = 0,
	SKYPE_MESSAGE_TEXT,
	SKYPE_MESSAGE_EMOTE,
	SKYPE_MESSAGE_ADD,
	SKYPE_MESSAGE_LEFT,
	SKYPE_MESSAGE_KICKED,
	SKYPE_MESSAGE_TOPIC
} SkypeMessageType;

typedef struct _SkypeMessage {
	//Required properties
	SkypeMessageType type;
	PurpleMessageFlags flags;  //send or recv
	gchar *chatname;
	
	//Optional properties
	gchar *body;		//topic, text, emote
	gchar *from_handle;	//topic, text, emote, left
	gint  timestamp;		//text, emote
	gchar **users;		//add, kicked
	gchar *leavereason;	//left
} SkypeMessage;

/*
	This function must only be called from the main loop, using purple_timeout_add
*/
static gboolean
skype_handle_received_message(char *message)
{
	char command[255];
	char **string_parts = NULL;
	PurpleAccount *this_account;
	PurpleConnection *gc;
	const char *my_username;
	PurpleBuddy *buddy;
	SkypeBuddy *sbuddy;
	char *body;
	char *body_html;
	char *msg_num;
	char *sender;
	char *type;
	int mtime;
	char *chatname;
	char *temp;
	//char *chat_type;
	char **chatusers = NULL;
	PurpleXfer *transfer = NULL;
	PurpleConversation *conv = NULL;
	GList *glist_temp = NULL;
	int i;
	static int chat_count = 0;
	PurpleGroup *temp_group;
	PurpleStatusPrimitive primitive;
	SkypeMessage *skypemessage;
	
	sscanf(message, "%s ", command);
	this_account = skype_get_account(NULL);
	if (this_account == NULL)
		return FALSE;
	gc = purple_account_get_connection(this_account);
	my_username = skype_get_account_username(this_account);
	string_parts = g_strsplit(message, " ", 4);
	
	if (g_str_equal(command, "PONG"))
	{
		last_pong = time(NULL);
	} else if (g_str_equal(command, "USERSTATUS"))
	{

	} else if (g_str_equal(command, "CONNSTATUS"))
	{
		if (g_str_equal(string_parts[1], "LOGGEDOUT"))
		{
			//need to make this synchronous :(
			if (gc != NULL)
				purple_connection_error(gc, _("\nSkype program closed"));
			//purple_timeout_add(0, (GSourceFunc)skype_sync_skype_close, gc);
		}
	} else if (g_str_equal(command, "USER"))
	{
		buddy = purple_find_buddy(this_account, string_parts[1]);
		if (buddy != NULL)
		{
			sbuddy = buddy->proto_data;
			if (g_str_equal(string_parts[2], "ONLINESTATUS"))
			{
				if (g_str_equal(string_parts[3], "OFFLINE"))
				{
					primitive = PURPLE_STATUS_OFFLINE;
				} else if (g_str_equal(string_parts[3], "ONLINE") ||
						g_str_equal(string_parts[3], "SKYPEME"))
				{
					primitive = PURPLE_STATUS_AVAILABLE;
				} else if (g_str_equal(string_parts[3], "AWAY"))
				{
					primitive = PURPLE_STATUS_AWAY;
				} else if (g_str_equal(string_parts[3], "NA"))
				{
					primitive = PURPLE_STATUS_EXTENDED_AWAY;
				} else if (g_str_equal(string_parts[3], "DND"))
				{
					primitive = PURPLE_STATUS_UNAVAILABLE;
				} else if (g_str_equal(string_parts[3], "SKYPEOUT"))
				{
					if (purple_account_get_bool(buddy->account, "skypeout_online", TRUE))
					{
						primitive = PURPLE_STATUS_AVAILABLE;
					} else {
						primitive = PURPLE_STATUS_OFFLINE;
					}
					set_skype_buddy_attribute(sbuddy, "MOOD_TEXT", _("SkypeOut"));
				} else if (g_str_equal(string_parts[3], "UNKNOWN"))
				{
					//user doesn't exist
					purple_blist_remove_buddy(buddy);
					purple_notify_error(gc, "Error", "User does not exist", "The user does not exist in Skype");
					buddy = NULL;
				} else {
					primitive = PURPLE_STATUS_UNSET;
				}
				
				if (buddy)
				{
					//Dont say we got their status unless its changed
					if (!g_str_equal(purple_status_get_id(purple_presence_get_active_status(purple_buddy_get_presence(buddy))), purple_primitive_get_id_from_type(primitive)))
						purple_prpl_got_user_status(this_account, buddy->name, purple_primitive_get_id_from_type(primitive), NULL);

					//Grab the buddy's mood and avatar
					skype_send_message_nowait("GET USER %s MOOD_TEXT", string_parts[1]);
					if (primitive != PURPLE_STATUS_OFFLINE && 
						primitive != PURPLE_STATUS_UNSET);
					skype_update_buddy_icon(buddy);
				}
			} else if (g_str_equal(string_parts[2], "DISPLAYNAME"))
			{
				if (strlen(g_strstrip(string_parts[3])))
					purple_blist_server_alias_buddy(buddy, string_parts[3]);
			} else if (g_str_equal(string_parts[2], "FULLNAME"))
			{
				if (strlen(g_strstrip(string_parts[3])) && (!purple_buddy_get_server_alias(buddy) || !strlen(purple_buddy_get_server_alias(buddy))))
					purple_blist_server_alias_buddy(buddy, string_parts[3]);
				set_skype_buddy_attribute(sbuddy, "FULLNAME", string_parts[3]);
			} else if ((g_str_equal(string_parts[2], "BUDDYSTATUS")) &&
					(g_str_equal(string_parts[3], "1")))
			{
				purple_blist_remove_buddy(buddy);
			} else {
				set_skype_buddy_attribute(sbuddy, string_parts[2], string_parts[3]);
			}
		} else if (g_str_equal(string_parts[2], "BUDDYSTATUS"))
		{
			if (g_str_equal(string_parts[3], "3"))
			{
				skype_debug_info("skype", "Buddy %s just got added\n", string_parts[1]);
				//buddy just got added.. handle it
				if (purple_find_buddy(this_account, string_parts[1]) == NULL)
				{
					skype_debug_info("skype", "Buddy not in list\n");
					buddy = purple_buddy_new(this_account, g_strdup(string_parts[1]), NULL);
					skype_buddy_new(buddy);
					if (string_parts[1][0] == '+')
					{
						temp_group = purple_find_group("SkypeOut");
						if (temp_group == NULL)
						{
							temp_group = purple_group_new("SkypeOut");
							purple_blist_add_group(temp_group, NULL);
						}
					} else {
						temp_group = purple_find_group("Skype");
						if (temp_group == NULL)
						{
							temp_group = purple_group_new("Skype");
							purple_blist_add_group(temp_group, NULL);
						}
					}
					purple_blist_add_buddy(buddy, NULL, temp_group, NULL);
					skype_update_buddy_status(buddy);
					skype_update_buddy_alias(buddy);
					purple_prpl_got_user_idle(this_account, buddy->name, FALSE, 0);
					skype_update_buddy_icon(buddy);
					skype_put_buddies_in_groups();
				}
			}
		} else if (g_str_equal(string_parts[2], "RECEIVEDAUTHREQUEST"))
		{
			//this event can be fired directly after authorising someone
			temp = skype_get_user_info(string_parts[1], "ISAUTHORIZED");
			if (!g_str_equal(temp, "TRUE"))
			{
				skype_debug_info("skype", "User %s requested authorisation\n", string_parts[1]);
				purple_account_request_authorization(this_account, string_parts[1], NULL, skype_get_user_info(string_parts[1], "FULLNAME"),
													string_parts[3], (purple_find_buddy(this_account, string_parts[1]) != NULL),
													skype_auth_allow, skype_auth_deny, (gpointer)g_strdup(string_parts[1]));
			}
			g_free(temp);
		}
	} else if (g_str_equal(command, "MESSAGE"))
	{
		if (g_str_equal(string_parts[3], "RECEIVED"))
		{
			msg_num = string_parts[1];
			temp = skype_send_message("GET MESSAGE %s TYPE", msg_num);
			type = g_strdup(&temp[14+strlen(msg_num)]);
			g_free(temp);
			if (g_str_equal(type, "TEXT") ||
				g_str_equal(type, "AUTHREQUEST"))
			{
				temp = skype_send_message("GET MESSAGE %s PARTNER_HANDLE", msg_num);
				sender = g_strdup(&temp[24+strlen(msg_num)]);
				g_free(temp);
				temp = skype_send_message("GET MESSAGE %s BODY", msg_num);
				body = g_strdup(&temp[14+strlen(msg_num)]);
				g_free(temp);
				temp = skype_send_message("GET MESSAGE %s TIMESTAMP", msg_num);
				mtime = atoi(&temp[19+strlen(msg_num)]);
				g_free(temp);
				
				/* Escape the body to HTML */
				body_html = skype_strdup_withhtml(body);
				g_free(body);

				if (g_str_equal(type, "TEXT"))
				{
					if (g_str_equal(sender, my_username))
					{
						temp = skype_send_message("GET CHATMESSAGE %s CHATNAME", msg_num);
						chatname = g_strdup(&temp[18+strlen(msg_num)]);
						g_free(temp);
						//skype_debug_info("skype", "Chatname: '%s'\n", chatname);
						chatusers = g_strsplit_set(chatname, "/;", 3);
						if (g_str_equal(&chatusers[0][1], my_username))
							sender = &chatusers[1][1];
						else
							sender = &chatusers[0][1];
						serv_got_im(gc, sender, body_html, PURPLE_MESSAGE_SEND, mtime);
						g_strfreev(chatusers);
					} else {
						serv_got_im(gc, sender, body_html, PURPLE_MESSAGE_RECV, mtime);
					}
				}/* else if (g_str_equal(type, "AUTHREQUEST") && !g_str_equal(sender, my_username))
				{
					skype_debug_info("User %s requested alternate authorisation\n", sender);
					purple_account_request_authorization(this_account, sender, NULL, skype_get_user_info(sender, "FULLNAME"),
												body, (purple_find_buddy(this_account, sender) != NULL),
												skype_auth_allow, skype_auth_deny, (gpointer)g_strdup(sender));
				}*/

				skype_send_message("SET MESSAGE %s SEEN", msg_num);
			}
		} else if (g_str_equal(string_parts[3], "SENT"))
		{
			/* mark it as seen, to remove notification from skype ui */
			
			/* dont async this -> infinite loop */
			skype_send_message("SET MESSAGE %s SEEN", string_parts[1]);
		}
	} else if (g_str_equal(command, "CHATMESSAGE"))
	{
		if ((g_str_equal(string_parts[3], "RECEIVED"))
			|| (g_str_equal(string_parts[3], "SENT"))
			)
		{
			if (messages_table == NULL)
			{
				messages_table = g_hash_table_new(NULL, NULL);
			}
			skypemessage = g_new0(SkypeMessage, 1);
			if (g_str_equal(string_parts[3], "RECEIVED"))
				skypemessage->flags = PURPLE_MESSAGE_RECV;
			else if (g_str_equal(string_parts[3], "SENT"))
				skypemessage->flags = PURPLE_MESSAGE_SEND;
			g_hash_table_insert(messages_table, GINT_TO_POINTER(atoi(string_parts[1])), skypemessage);
//			printf("Message %s has int %d (%d)\n", string_parts[1], atoi(string_parts[1]), GINT_TO_POINTER(atoi(string_parts[1])));
			skype_get_chatmessage_info(atoi(string_parts[1]));
		} else if (g_str_equal(string_parts[2], "TYPE"))
		{
			skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (skypemessage != NULL)
			{
				//try to keep these in order of most likely to least likely
				if (g_str_equal(string_parts[3], "SAID") ||
							g_str_equal(string_parts[3], "TEXT"))
				{
					skypemessage->type = SKYPE_MESSAGE_TEXT;
				} else if (g_str_equal(string_parts[3], "EMOTED"))
				{
					skypemessage->type = SKYPE_MESSAGE_EMOTE;
				} else if (g_str_equal(string_parts[3], "ADDEDMEMBERS"))
				{
					skypemessage->type = SKYPE_MESSAGE_ADD;
				} else if (g_str_equal(string_parts[3], "LEFT"))
				{
					skypemessage->type = SKYPE_MESSAGE_LEFT;
				} else if (g_str_equal(string_parts[3], "KICKED") ||
							g_str_equal(string_parts[3], "KICKBANNED"))
				{
					skypemessage->type = SKYPE_MESSAGE_KICKED;
				} else if (g_str_equal(string_parts[3], "SETTOPIC"))
				{
					skypemessage->type = SKYPE_MESSAGE_TOPIC;
				}
			}
		} else if (g_str_equal(string_parts[2], "CHATNAME"))
		{
			skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (skypemessage != NULL)
			{
				skypemessage->chatname = g_strdup(string_parts[3]);
			}
		} else if (g_str_equal(string_parts[2], "BODY"))
		{
			skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (skypemessage != NULL)
			{
				skypemessage->body = g_strdup(string_parts[3]);
			}
		} else if (g_str_equal(string_parts[2], "FROM_HANDLE"))
		{
			skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (skypemessage != NULL)
			{
				skypemessage->from_handle = g_strdup(string_parts[3]);
			}
		} else if (g_str_equal(string_parts[2], "USERS"))
		{
			skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (skypemessage != NULL)
			{
				skypemessage->users = g_strsplit(string_parts[3], " ", -1);
			}
		} else if (g_str_equal(string_parts[2], "LEAVEREASON"))
		{
			skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (skypemessage != NULL)
			{
				skypemessage->leavereason = g_strdup(string_parts[3]);
			}
		} else if (g_str_equal(string_parts[2], "TIMESTAMP"))
		{
			skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (skypemessage != NULL)
			{
				skypemessage->timestamp = atoi(string_parts[3]);
			}
		}
		
		handle_complete_message(atoi(string_parts[1]));
	} else if (g_str_equal(command, "CHAT"))
	{
		//printf("Looking for conv %s\n", string_parts[1]);
		//find the matching chat to update
		glist_temp = g_list_find_custom(purple_get_conversations(), string_parts[1], (GCompareFunc)skype_find_chat);
		if (glist_temp == NULL || glist_temp->data == NULL)
		{
			//printf("No conversation\n");
			if (g_str_equal(string_parts[2], "DIALOG_PARTNER"))
			{
				//most likely an IM
				//if they have an IM window open, assign it the chatname
				conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, string_parts[3], this_account);
				if (conv == NULL)
				{
					conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, this_account, string_parts[3]);
					//serv_got_im(gc, string_parts[3], ".", PURPLE_MESSAGE_RECV, time(NULL));
					//conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, string_parts[3], this_account);
				}
				purple_conversation_set_data(conv, "chat_id", g_strdup(string_parts[1]));
			} else if (g_str_equal(string_parts[2], "TYPE") && !g_str_equal(string_parts[3], "DIALOG") && !g_str_equal(string_parts[3], "LEGACY_DIALOG"))
			{
				//most likely a chat
				conv = serv_got_joined_chat(gc, chat_count++, string_parts[1]);
				purple_conversation_set_data(conv, "chat_id", g_strdup(string_parts[1]));
			}
			//printf("Conv %d\n", (int)conv);
		} else {
			conv = glist_temp->data;
		}
		if (conv && conv->type == PURPLE_CONV_TYPE_CHAT)
		{
			if (g_str_equal(string_parts[2], "MEMBERS"))
			{
				if (conv->type == PURPLE_CONV_TYPE_CHAT)
				{
					purple_conv_chat_clear_users(PURPLE_CONV_CHAT(conv));
					chatusers = g_strsplit(string_parts[3], " ", 0);
					for (i=0; chatusers[i]; i++)
					{
						purple_conv_chat_add_user(PURPLE_CONV_CHAT(conv), chatusers[i], NULL, PURPLE_CBFLAGS_NONE, FALSE);
					}
					g_strfreev(chatusers);
				}
			} else if (g_str_equal(string_parts[2], "FRIENDLYNAME"))
			{
				if (conv->type == PURPLE_CONV_TYPE_CHAT)
				{
					purple_conversation_set_title(conv, g_strdup(string_parts[3]));
					purple_conversation_update(conv, PURPLE_CONV_UPDATE_TITLE);
				}
			} else if (g_str_equal(string_parts[2], "TOPIC"))
			{
				if (conv->type == PURPLE_CONV_TYPE_CHAT)
				{
					purple_conv_chat_set_topic(PURPLE_CONV_CHAT(conv), my_username, g_strdup(string_parts[3]));
				}
			}
		}
	} else if (g_str_equal(command, "FILETRANSFER"))
	{
		//lookup current file transfers to see if there's already one there
		glist_temp = g_list_find_custom(purple_xfers_get_all(),
										string_parts[1],
										(GCompareFunc)skype_find_filetransfer);
		if (glist_temp == NULL && g_str_equal(string_parts[2], "TYPE"))
		{
			temp = skype_send_message("GET FILETRANSFER %s PARTNER_HANDLE", string_parts[1]);
			sender = g_strdup(&temp[29+strlen(string_parts[1])]);
			g_free(temp);
			if (g_str_equal(string_parts[3], "INCOMING"))
			{
				transfer = purple_xfer_new(this_account, PURPLE_XFER_RECEIVE, sender);
			} else {
				transfer = purple_xfer_new(this_account, PURPLE_XFER_SEND, sender);
			}
			transfer->data = g_strdup(string_parts[1]);
			purple_xfer_set_init_fnc(transfer, skype_accept_transfer);
			purple_xfer_set_request_denied_fnc(transfer, skype_decline_transfer);
			temp = skype_send_message("GET FILETRANSFER %s FILENAME", string_parts[1]);
			skype_debug_info("skype", "Filename: '%s'\n", &temp[23+strlen(string_parts[1])]);
			purple_xfer_set_filename(transfer, g_strdup(&temp[23+strlen(string_parts[1])]));
			g_free(temp);
			temp = skype_send_message("GET FILETRANSFER %s FILEPATH", string_parts[1]);
			if (strlen(&temp[23+strlen(string_parts[1])]))
				purple_xfer_set_local_filename(transfer, g_strdup(&temp[23+strlen(string_parts[1])]));
			else
				purple_xfer_set_local_filename(transfer, purple_xfer_get_filename(transfer));
			g_free(temp);
			temp = skype_send_message("GET FILETRANSFER %s FILESIZE", string_parts[1]);
			purple_xfer_set_size(transfer, atol(&temp[23+strlen(string_parts[1])]));
			g_free(temp);
			purple_xfer_add(transfer);
		} else if (glist_temp != NULL) {
			transfer = glist_temp->data;
		}
		if (transfer != NULL)
		{
			/*if (g_str_equal(string_parts[2], "TYPE"))
			{
				if (g_str_equal(string_parts[3], "INCOMING"))
				{
					transfer->type = PURPLE_XFER_RECEIVE;
				} else {
					transfer->type = PURPLE_XFER_SEND;
				}
			} else if (g_str_equal(string_parts[2], "PARTNER_HANDLE"))
			{
				transfer->who = g_strdup(string_parts[3]);
			} else*/ if (g_str_equal(string_parts[2], "FILENAME"))
			{
				purple_xfer_set_filename(transfer, string_parts[3]);
			} else if (g_str_equal(string_parts[2], "FILEPATH"))
			{
				if (strlen(string_parts[3]))
					purple_xfer_set_local_filename(transfer, string_parts[3]);
			} else if (g_str_equal(string_parts[2], "STATUS"))
			{
				if (g_str_equal(string_parts[3], "NEW") ||
					g_str_equal(string_parts[3], "WAITING_FOR_ACCEPT"))
				{
					//Skype API doesn't let us accept transfers
					//purple_xfer_request(transfer);
					if (purple_xfer_get_type(transfer) == PURPLE_XFER_RECEIVE)
					{
#						ifndef __APPLE__
							skype_send_message("OPEN FILETRANSFER");
#						else
							purple_notify_info(this_account, _("File Transfers"), g_strdup_printf(_("%s wants to send you a file"), purple_xfer_get_remote_user(transfer)), NULL);
#						endif
						purple_xfer_conversation_write(transfer, g_strdup_printf(_("%s wants to send you a file"), purple_xfer_get_remote_user(transfer)), FALSE);
					}
					purple_xfer_set_status(transfer, PURPLE_XFER_STATUS_NOT_STARTED);
				} else if (g_str_equal(string_parts[3], "COMPLETED"))
				{
					purple_xfer_set_completed(transfer, TRUE);
				} else if (g_str_equal(string_parts[3], "CONNECTING") ||
							g_str_equal(string_parts[3], "TRANSFERRING") ||
							g_str_equal(string_parts[3], "TRANSFERRING_OVER_RELAY"))
				{
					purple_xfer_set_status(transfer, PURPLE_XFER_STATUS_STARTED);
					transfer->start_time = time(NULL);
				} else if (g_str_equal(string_parts[3], "CANCELLED"))
				{
					//transfer->end_time = time(NULL);
					//transfer->bytes_remaining = 0;
					//purple_xfer_set_status(transfer, PURPLE_XFER_STATUS_CANCEL_LOCAL);
					purple_xfer_cancel_local(transfer);
				}/* else if (g_str_equal(string_parts[3], "FAILED"))
				{
					//transfer->end_time = time(NULL);
					//purple_xfer_set_status(transfer, PURPLE_XFER_STATUS_CANCEL_REMOTE);
					purple_xfer_cancel_remote(transfer);
				}*/
				purple_xfer_update_progress(transfer);
			} else if (g_str_equal(string_parts[2], "STARTTIME"))
			{
				transfer->start_time = atol(string_parts[3]);
				purple_xfer_update_progress(transfer);
			/*} else if (g_str_equal(string_parts[2], "FINISHTIME"))
			{
				if (!g_str_equal(string_parts[3], "0"))
					transfer->end_time = atol(string_parts[3]);
				purple_xfer_update_progress(transfer);*/
			} else if (g_str_equal(string_parts[2], "BYTESTRANSFERRED"))
			{
				purple_xfer_set_bytes_sent(transfer, atol(string_parts[3]));
				purple_xfer_update_progress(transfer);
			} else if (g_str_equal(string_parts[2], "FILESIZE"))
			{
				purple_xfer_set_size(transfer, atol(string_parts[3]));
			} else if (g_str_equal(string_parts[2], "FAILUREREASON") &&
						!g_str_equal(string_parts[3], "UNKNOWN"))
			{
				temp = NULL;
				if (g_str_equal(string_parts[3], "SENDER_NOT_AUTHORIZED"))
				{
					temp = g_strdup(_("Not Authorized"));
				} else if (g_str_equal(string_parts[3], "REMOTELY_CANCELLED"))
				{
					purple_xfer_cancel_remote(transfer);
					purple_xfer_update_progress(transfer);
				} else if (g_str_equal(string_parts[3], "FAILED_READ"))
				{
					temp = g_strdup(_("Read error"));
				} else if (g_str_equal(string_parts[3], "FAILED_REMOTE_READ"))
				{
					temp = g_strdup(_("Read error"));
				} else if (g_str_equal(string_parts[3], "FAILED_WRITE"))
				{
					temp = g_strdup(_("Write error"));
				} else if (g_str_equal(string_parts[3], "FAILED_REMOTE_WRITE"))
				{
					temp = g_strdup(_("Write error"));
				} else if (g_str_equal(string_parts[3], "REMOTE_DOES_NOT_SUPPORT_FT"))
				{
					temp = g_strdup_printf(_("Unable to send file to %s, user does not support file transfers"), transfer->who);
				} else if (g_str_equal(string_parts[3], "REMOTE_OFFLINE_FOR_TOO_LONG"))
				{
					temp = g_strdup(_("Recipient Unavailable"));
				}
				if (temp && strlen(temp))
				{
					purple_xfer_error(transfer->type, this_account, transfer->who, temp);
					g_free(temp);
				}
			}
		}
	} else if (g_str_equal(command, "WINDOWSTATE"))
	{
		if (g_str_equal(string_parts[1], "HIDDEN"))
		{
			skype_send_message("SET SILENT_MODE ON");
		}
	} else if (g_str_equal(command, "GROUPS"))
	{
		if (groups_table == NULL)
			groups_table = g_hash_table_new(NULL, NULL);
		chatusers = g_strsplit(strchr(message, ' ')+1, ", ", 0);
		for(i = 0; chatusers[i]; i++)
		{
			skype_send_message_nowait("GET GROUP %s DISPLAYNAME", chatusers[i]);
			skype_send_message_nowait("GET GROUP %s USERS", chatusers[i]);
			//temp = skype_send_message("GET GROUP %s DISPLAYNAME", chatusers[i]);
			//body = g_strdup(&temp[19+strlen(chatusers[i])]);
			//g_free(temp);
			//if (!purple_find_group(body))
			//	purple_blist_add_group(purple_group_new(body), NULL);
			//
			//temp = skype_send_message("GET GROUP %s USERS", chatusers[i]);
			//g_strfreev(string_parts);
			//string_parts = g_strsplit(&temp[13+strlen(chatusers[i])], ", ", -1);
			//for (j = 0; string_parts[j]; j++)
			//	purple_blist_add_buddy(purple_find_buddy(this_account, string_parts[j]), NULL, purple_find_group(body), NULL);
		}
		g_strfreev(chatusers);
	} else if (g_str_equal(command, "GROUP"))
	{
		if (groups_table == NULL)
			groups_table = g_hash_table_new(NULL, NULL);
		//TODO Handle Group stuff:
		//  Messages from skype to move users in to/out of a group
		if (g_str_equal(string_parts[2], "DISPLAYNAME"))
		{
			temp_group = g_hash_table_lookup(groups_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (!temp_group)
			{
				temp_group = purple_find_group(string_parts[3]);
				if (!temp_group)
				{
					temp_group = purple_group_new(string_parts[3]);
					purple_blist_add_group(temp_group, NULL);
				}
				purple_blist_node_set_int(&temp_group->node, "skype_group_number", atoi(string_parts[1]));
				g_hash_table_insert(groups_table, GINT_TO_POINTER(string_parts[1]), temp_group);
			}
		} else if (g_str_equal(string_parts[2], "USERS"))
		{
			temp_group = g_hash_table_lookup(groups_table, GINT_TO_POINTER(atoi(string_parts[1])));
			if (temp_group)
			{
				chatusers = g_strsplit(string_parts[3], ", ", -1);
				for (i = 0; chatusers[i]; i++)
					purple_blist_add_buddy(purple_find_buddy(this_account, chatusers[i]), NULL, temp_group, NULL);
				g_strfreev(chatusers);	
			}		
		}
	} else if (g_str_equal(command, "APPLICATION") && 
				g_str_equal(string_parts[1], "libpurple_typing"))
	{
		if (g_str_equal(string_parts[2], "DATAGRAM"))
		{
			chatusers = g_strsplit_set(string_parts[3], ": ", 3);
			sender = chatusers[0];
			temp = chatusers[2];
			if (sender != NULL && temp != NULL)
			{
				if (g_str_equal(temp, "PURPLE_NOT_TYPING"))
					serv_got_typing(gc, sender, 10, PURPLE_NOT_TYPING);
				else if (g_str_equal(temp, "PURPLE_TYPING"))
					serv_got_typing(gc, sender, 10, PURPLE_TYPING);
				else if (g_str_equal(temp, "PURPLE_TYPED"))
					serv_got_typing(gc, sender, 10, PURPLE_TYPED);
			}
			g_strfreev(chatusers);
		} else if (g_str_equal(string_parts[2], "STREAMS"))
		{
			chatusers = g_strsplit_set(string_parts[3], ": ", -1);
			for(i=0; chatusers[i] && chatusers[i+1]; i+=2)
			{
				temp = g_strconcat("stream-", chatusers[i], NULL);
				purple_account_set_string(this_account, temp, chatusers[i+1]);
				g_free(temp);
			}
			g_strfreev(chatusers);
		}
#ifdef USE_FARSIGHT
	} else if (g_str_equal(command, "CALL"))
	{
		if (g_str_equal(string_parts[2], "STATUS"))
		{ 
			if (g_str_equal(string_parts[3], "RINGING"))
			{
				skype_handle_incoming_call(gc, string_parts[1]);
			} else if (g_str_equal(string_parts[3], "FINISHED") ||
						g_str_equal(string_parts[3], "CANCELLED") ||
						g_str_equal(string_parts[3], "FAILED"))
			{
				skype_handle_call_got_ended(string_parts[1]);
			}
		}
#else
	} else if (g_str_equal(command, "CALL"))
	{
		if (g_str_equal(string_parts[2], "STATUS") &&
			g_str_equal(string_parts[3], "RINGING"))
		{
			temp = skype_send_message("GET CALL %s TYPE", string_parts[1]);
			type = g_new0(gchar, 9);
			sscanf(temp, "CALL %*s TYPE %[^_]", type);
			g_free(temp);
			temp = skype_send_message("GET CALL %s PARTNER_HANDLE", string_parts[1]);
			sender = g_strdup(&temp[21+strlen(string_parts[1])]);
			g_free(temp);
			if (g_str_equal(type, "INCOMING"))
			{
				purple_request_action(gc, _("Incoming Call"), g_strdup_printf(_("%s is calling you."), sender), _("Do you want to accept their call?"),
								0, this_account, sender, NULL, g_strdup(string_parts[1]), 2, _("_Accept"), 
								G_CALLBACK(skype_call_accept_cb), _("_Reject"), G_CALLBACK(skype_call_reject_cb));
			}
			g_free(sender);
			g_free(type);
		}
#endif	
	}
	if (string_parts)
	{
		g_strfreev(string_parts);
	}
	if (message)
	{
		g_free(message);
	}
	return FALSE;
}

void
skype_call_accept_cb(gchar *call)
{
	skype_send_message("ALTER CALL %s ANSWER", call);
	g_free(call);
}

void
skype_call_reject_cb(gchar *call)
{
	skype_send_message("ALTER CALL %s HANGUP", call);
	g_free(call);
}

void
skype_auth_allow(gpointer sender)
{
	skype_send_message("SET USER %s ISAUTHORIZED TRUE", sender);
}

void
skype_auth_deny(gpointer sender)
{
	skype_send_message("SET USER %s ISAUTHORIZED FALSE", sender);
}

gint
skype_find_filetransfer(PurpleXfer *transfer, char *skypeid)
{
	if (transfer == NULL || transfer->data == NULL || skypeid == NULL)
		return -1;
	return strcmp(transfer->data, skypeid);
}

void
skype_accept_transfer(PurpleXfer *transfer)
{
	//can't accept transfers
}

void
skype_decline_transfer(PurpleXfer *transfer)
{
	//can't reject transfers
}

gint
skype_find_chat(PurpleConversation *conv, char *chat_id)
{
	char *lookup;
	if (chat_id == NULL || conv == NULL || conv->data == NULL)
		return -1;
	//lookup = g_hash_table_lookup(conv->data, "chat_id");
	lookup = purple_conversation_get_data(conv, "chat_id");
	if (lookup == NULL)
		return -1;
	return strcmp(lookup, chat_id);
}

/* Since this function isn't public, and we need it to be, redefine it here */
static void
purple_xfer_set_status(PurpleXfer *xfer, PurpleXferStatusType status)
{
	g_return_if_fail(xfer != NULL);

	if(xfer->type == PURPLE_XFER_SEND) {
		switch(status) {
			case PURPLE_XFER_STATUS_ACCEPTED:
				purple_signal_emit(purple_xfers_get_handle(), "file-send-accept", xfer);
				break;
			case PURPLE_XFER_STATUS_STARTED:
				purple_signal_emit(purple_xfers_get_handle(), "file-send-start", xfer);
				break;
			case PURPLE_XFER_STATUS_DONE:
				purple_signal_emit(purple_xfers_get_handle(), "file-send-complete", xfer);
				break;
			case PURPLE_XFER_STATUS_CANCEL_LOCAL:
			case PURPLE_XFER_STATUS_CANCEL_REMOTE:
				purple_signal_emit(purple_xfers_get_handle(), "file-send-cancel", xfer);
				break;
			default:
				break;
		}
	} else if(xfer->type == PURPLE_XFER_RECEIVE) {
		switch(status) {
			case PURPLE_XFER_STATUS_ACCEPTED:
				purple_signal_emit(purple_xfers_get_handle(), "file-recv-accept", xfer);
				break;
			case PURPLE_XFER_STATUS_STARTED:
				purple_signal_emit(purple_xfers_get_handle(), "file-recv-start", xfer);
				break;
			case PURPLE_XFER_STATUS_DONE:
				purple_signal_emit(purple_xfers_get_handle(), "file-recv-complete", xfer);
				break;
			case PURPLE_XFER_STATUS_CANCEL_LOCAL:
			case PURPLE_XFER_STATUS_CANCEL_REMOTE:
				purple_signal_emit(purple_xfers_get_handle(), "file-recv-cancel", xfer);
				break;
			default:
				break;
		}
	}

	xfer->status = status;
}
gboolean
skype_sync_skype_close(PurpleConnection *gc)
{
	if (gc != NULL)
		purple_connection_error(gc, _("\nSkype program closed"));
	return FALSE;
}




void
handle_complete_message(int messagenumber)
{
	SkypeMessage *skypemessage = NULL;
	GList *glist_temp = NULL;
	gchar *body_html = NULL;
	PurpleConversation *conv = NULL;
	int i;

	if (messages_table == NULL)
		return;
	
	skypemessage = g_hash_table_lookup(messages_table, GINT_TO_POINTER(messagenumber));
	if (skypemessage == NULL)
		return;
	
	if (!skypemessage->chatname || !skypemessage->type)
		return;
		
	glist_temp = g_list_find_custom(purple_get_conversations(), skypemessage->chatname, (GCompareFunc)skype_find_chat);
	if (glist_temp && glist_temp->data)
	{
		conv = glist_temp->data;
	}
	
	switch(skypemessage->type)
	{
		case SKYPE_MESSAGE_OTHER:
			return;
		case SKYPE_MESSAGE_EMOTE:
			if (!skypemessage->body)
				return;
			body_html = g_strdup_printf("/me %s", skypemessage->body);
			g_free(skypemessage->body);
			skypemessage->body = body_html;
			skypemessage->type = SKYPE_MESSAGE_TEXT;
			//fallthrough
		case SKYPE_MESSAGE_TEXT:
			if (!skypemessage->body || !skypemessage->from_handle || !skypemessage->timestamp)
				return;
			body_html = skype_strdup_withhtml(skypemessage->body);
			if (conv && conv->type == PURPLE_CONV_TYPE_CHAT)
			{
				serv_got_chat_in(conv->account->gc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(conv)), skypemessage->from_handle, skypemessage->flags, body_html, skypemessage->timestamp);
			} else
			{
				if (skypemessage->flags != PURPLE_MESSAGE_SEND)
				{
					PurpleAccount *acct;
					if (!conv)
						acct = skype_get_account(NULL);
					else
						acct = conv->account;
					if (!g_str_equal(skypemessage->from_handle, skype_get_account_username(acct)))
					{
						serv_got_im(acct->gc, skypemessage->from_handle, body_html, skypemessage->flags, skypemessage->timestamp);
					} else {
						//if we're here, then we're receiving a message that we sent from a different computer
						//use the chat name to work out who it came from
						//in format #username1/$username2;junktext for IM's
						char *start, *end;
						start = strchr(skypemessage->chatname, '#');
						if (start)
						{
							start += 1;
							end = strchr(start, '/');
							if (end)
							{
								start = g_strndup(start, end-start);
								if (!g_str_equal(skype_get_account_username(acct), start))
								{
									serv_got_im(acct->gc, start, body_html, PURPLE_MESSAGE_SEND, skypemessage->timestamp);
									g_free(start);
									start = (char *) 1;
								}
								else
								{
									g_free(start);
									start = NULL;
								}
							} else {
								start = NULL;
							}
						}
						if (!start)
						{
							start = strchr(skypemessage->chatname, '$');
							if (start)
							{
								start += 1;
								end = strchr(start, ';');
								if (end)
								{
									start = g_strndup(start, end-start);
									if (!g_str_equal(skype_get_account_username(acct), start))
									{
										serv_got_im(acct->gc, start, body_html, PURPLE_MESSAGE_SEND, skypemessage->timestamp);
										g_free(start);
									}
								}
							}
						}
					}
				}
			}
			break;
		case SKYPE_MESSAGE_LEFT:
			if (!skypemessage->from_handle || !skypemessage->leavereason)
				return;
			if (conv && conv->type == PURPLE_CONV_TYPE_CHAT)
			{
				if (g_str_equal(skypemessage->from_handle, conv->account->username))
					purple_conv_chat_left(PURPLE_CONV_CHAT(conv));
				purple_conv_chat_remove_user(PURPLE_CONV_CHAT(conv), skypemessage->from_handle, skypemessage->leavereason);
			}
			break;
		case SKYPE_MESSAGE_ADD:
			if (!skypemessage->users)
				return;
			if (conv && conv->type == PURPLE_CONV_TYPE_CHAT)
			{
				for (i=0; skypemessage->users[i]; i++)
					if (!purple_conv_chat_find_user(PURPLE_CONV_CHAT(conv), skypemessage->users[i]))
						purple_conv_chat_add_user(PURPLE_CONV_CHAT(conv), skypemessage->users[i], NULL, PURPLE_CBFLAGS_NONE, FALSE);	
			}
			break;
		case SKYPE_MESSAGE_KICKED:
			if (!skypemessage->users)
				return;
			if (conv && conv->type == PURPLE_CONV_TYPE_CHAT)
			{
				for (i=0; skypemessage->users[i]; i++)
					purple_conv_chat_remove_user(PURPLE_CONV_CHAT(conv), skypemessage->users[i], g_strdup("Kicked"));
			}
			break;
		case SKYPE_MESSAGE_TOPIC:
			if (!skypemessage->body || !skypemessage->from_handle)
				return;
			if (conv && conv->type == PURPLE_CONV_TYPE_CHAT)
			{
				purple_conv_chat_set_topic(PURPLE_CONV_CHAT(conv), NULL, skypemessage->body);
				serv_got_chat_in(conv->account->gc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(conv)), skypemessage->from_handle, PURPLE_MESSAGE_SYSTEM, skype_strdup_withhtml(g_strdup_printf(_("%s has changed the topic to: %s"), skypemessage->from_handle, skypemessage->body)), time(NULL));
			} 
			break;
	}
	
	if (skypemessage->flags == PURPLE_MESSAGE_RECV)
		skype_send_message_nowait("SET CHATMESSAGE %d SEEN", messagenumber);
	if (g_hash_table_remove(messages_table, GINT_TO_POINTER(messagenumber)))
	{
		//free the message here
		skypemessage->type = 0;
		skypemessage->timestamp = 0;
		if (skypemessage->chatname)
		{
			g_free(skypemessage->chatname);
			skypemessage->chatname = NULL;
		}
		if (skypemessage->body)
		{
			g_free(skypemessage->body);
			skypemessage->body = NULL;
		}
		if (skypemessage->from_handle)
		{
			g_free(skypemessage->from_handle);
			skypemessage->from_handle = NULL;
		}
		if (skypemessage->users)
		{
			g_strfreev(skypemessage->users);
			skypemessage->users = NULL;
		}
		if (skypemessage->leavereason)
		{
			g_free(skypemessage->leavereason);
			skypemessage->leavereason = NULL;
		}
		
		g_free(skypemessage);
	}
}

