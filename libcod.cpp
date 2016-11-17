#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h> // dir stuff

#include <sys/mman.h> // mprotect
#include <execinfo.h> // stacktrace

#include "cracking.hpp"
#include "gsc.hpp"

cvar_t *sv_maxclients;
cvar_t *sv_allowDownload;
cvar_t *sv_pure;
cvar_t *developer;
cvar_t *rcon_password;

#if COD_VERSION == COD2_1_2 || COD_VERSION == COD2_1_3
cvar_t *sv_wwwDownload;
#endif

void hook_sv_init(char *format, ...)
{
	char s[COD2_MAX_STRINGLENGTH];
	va_list va;

	va_start(va, format);
	vsnprintf(s, sizeof(s), format, va);
	va_end(va);

	Com_Printf("%s", s);

	/* Do stuff after sv has been initialized here */

	sv_maxclients = Cvar_FindVar("sv_maxclients");
	sv_allowDownload = Cvar_FindVar("sv_allowDownload");
	sv_pure = Cvar_FindVar("sv_pure");
	developer = Cvar_FindVar("developer");
	rcon_password = Cvar_FindVar("rcon_password");

#if COD_VERSION == COD2_1_2 || COD_VERSION == COD2_1_3
	sv_wwwDownload = Cvar_FindVar("sv_wwwDownload");
#endif

}

int codecallback_playercommand = 0;
int codecallback_userinfochanged = 0;
int codecallback_fire_grenade = 0;
int codecallback_vid_restart = 0;

cHook *hook_gametype_scripts;
int hook_codscript_gametype_scripts()
{
	hook_gametype_scripts->unhook();

	codecallback_playercommand = codscript_load_function((char *)"maps/mp/gametypes/_callbacksetup", (char *)"CodeCallback_PlayerCommand", 0);
	codecallback_userinfochanged = codscript_load_function((char *)"maps/mp/gametypes/_callbacksetup", (char *)"CodeCallback_UserInfoChanged", 0);
	codecallback_fire_grenade = codscript_load_function((char *)"maps/mp/gametypes/_callbacksetup", (char *)"CodeCallback_FireGrenade", 0);
	codecallback_vid_restart = codscript_load_function((char *)"maps/mp/gametypes/_callbacksetup", (char *)"CodeCallback_VidRestart", 0);

	int (*sig)();
	*(int *)&sig = hook_gametype_scripts->from;
	int ret = sig();
	hook_gametype_scripts->hook();

	return ret;
}

cHook *hook_player_collision;
cvar_t *g_playerCollision;
int player_collision(int a1)
{
	hook_player_collision->unhook();

	int (*sig)(int a1);
	*(int *)&sig = hook_player_collision->from;

	int ret;

	if (g_playerCollision->boolean)
		ret = sig(a1);
	else
		ret = 0;

	hook_player_collision->hook();

	return ret;
}

cHook *hook_player_eject;
cvar_t *g_playerEject;
int player_eject(int a1)
{
	hook_player_eject->unhook();

	int (*sig)(int a1);
	*(int *)&sig = hook_player_eject->from;

	int ret;

	if (g_playerEject->boolean)
		ret = sig(a1);
	else
		ret = 0;

	hook_player_eject->hook();

	return ret;
}

cHook *hook_fire_grenade;
int fire_grenade(int player, int a2, int a3, int weapon, int a5)
{
	hook_fire_grenade->unhook();
	int (*sig)(int player, int a2, int a3, int a4, int a5);
	*(int *)&sig = hook_fire_grenade->from;
	int grenade = sig(player, a2, a3, weapon, a5);
	hook_fire_grenade->hook();
	int (*sig2)(int weapon);
#if COD_VERSION == COD2_1_0
	*(int *)&sig2 = 0x80E9270;
#elif COD_VERSION == COD2_1_2
	*(int *)&sig2 = 0x80EB860;
#elif COD_VERSION == COD2_1_3
	*(int *)&sig2 = 0x80EB9A4;
#endif
	int weaponname = sig2(weapon);
	char *wname2 = *(char**)weaponname;
	stackPushString(wname2);
	stackPushEntity(grenade);
	short ret = codscript_call_callback_entity(player, codecallback_fire_grenade, 2);
	codscript_callback_finish(ret);
	return grenade;
}

void hook_vid_restart(char *format, ...)
{
	char s[COD2_MAX_STRINGLENGTH];
	va_list va;

	va_start(va, format);
	vsnprintf(s, sizeof(s), format, va);
	va_end(va);

	Com_DPrintf("%s", s);

	if (strncmp(&s[strlen(s) - 4], "vdr", 3) == 0)
	{
		char *name = &s[24];
		name[strlen(name) - 6] = '\0';

		for (int i = 0; i < sv_maxclients->integer; i++)
		{
			if (CLIENTSTATE(i) == CS_ACTIVE)
			{
				if (strcmp(name, (char*)(PLAYERBASE(i) + 134216)) == 0)
				{
					stackPushInt(i);
					short ret = codscript_call_callback_entity(G_ENTITY(i), codecallback_vid_restart, 1);
					codscript_callback_finish(ret);
				}
			}
		}
	}
}

int hook_ClientCommand(int clientNum)
{
	if ( ! codecallback_playercommand)
	{
		return ClientCommand(clientNum);
	}

	stackPushArray();
	int args = trap_Argc();
	for (int i = 0; i < args; i++)
	{
		char tmp[COD2_MAX_STRINGLENGTH];
		trap_Argv(i, tmp, sizeof(tmp));
		if(i == 1 && tmp[0] >= 20 && tmp[0] <= 22)
		{
			char *part = strtok(tmp + 1, " ");
			while(part != NULL)
			{
				stackPushString(part);
				stackPushArrayLast();
				part = strtok(NULL, " ");
			}
		}
		else
		{
			stackPushString(tmp);
			stackPushArrayLast();
		}
	}

	short ret = codscript_call_callback_entity(G_ENTITY(clientNum), codecallback_playercommand, 1);

	codscript_callback_finish(ret);

	return 0;
}

cvar_t *sv_cracked;
char* hook_AuthorizeState( int arg )
{
	char *s = Cmd_Argv(arg);

	if (sv_cracked->boolean && strcmp(s, "deny") == 0)
		return "accept";

	return s;
}

void hook_SV_BeginDownload_f( int a1 )
{
	char *file = Cmd_Argv(1);
	int len = strlen(file);

	if (len > 3 && strcmp(&file[len - 4], ".iwd") == 0)
		SV_BeginDownload_f(a1);
	else
		Com_DPrintf("Invalid download attempt: %s\n", file);
}

int hook_ClientUserinfoChanged(int clientNum)
{
	if ( ! codecallback_userinfochanged)
	{
		return changeClientUserinfo(clientNum);
	}

	stackPushInt(clientNum); // one parameter is required

	short ret = codscript_call_callback_entity(G_ENTITY(clientNum), codecallback_userinfochanged, 1);

	codscript_callback_finish(ret);

	return 0;
}

cvar_t *sv_downloadMessage;
int custom_SV_WriteDownloadToClient(int cl, int msg) // As in ioquake3, always use 1 block per snapshot
{
	char errorMessage[COD2_MAX_STRINGLENGTH];
	int iwdFile;
	int curindex;

	int MAX_DOWNLOAD_BLKSIZE = 1024; // default -> 2048
	int MAX_DOWNLOAD_WINDOW = 8;

	int (*Z_Malloc)(size_t size);
#if COD_VERSION == COD2_1_0
	*(int *)&Z_Malloc = 0x80A92FA;
#elif COD_VERSION == COD2_1_2
	*(int *)&Z_Malloc = 0x80AB51A;
#elif COD_VERSION == COD2_1_3
	*(int *)&Z_Malloc = 0x80AB65E;
#endif

	int (*FS_Read)(void *a1, size_t a2, signed int a3);
#if COD_VERSION == COD2_1_0
	*(int *)&FS_Read = 0x809E328;
#elif COD_VERSION == COD2_1_2
	*(int *)&FS_Read = 0x80A03E6;
#elif COD_VERSION == COD2_1_3
	*(int *)&FS_Read = 0x80A052A;
#endif

	int (*MSG_WriteByte)(int a1, char a2);
#if COD_VERSION == COD2_1_0
	*(int *)&MSG_WriteByte = 0x8067B4C;
#elif COD_VERSION == COD2_1_2
	*(int *)&MSG_WriteByte = 0x8068014;
#elif COD_VERSION == COD2_1_3
	*(int *)&MSG_WriteByte = 0x806800C;
#endif

	int (*MSG_WriteShort)(int a1, int16_t a2);
#if COD_VERSION == COD2_1_0
	*(int *)&MSG_WriteShort = 0x8067BDA;
#elif COD_VERSION == COD2_1_2
	*(int *)&MSG_WriteShort = 0x80680A2;
#elif COD_VERSION == COD2_1_3
	*(int *)&MSG_WriteShort = 0x806809A;
#endif

	int (*MSG_WriteLong)(int a1, int a2);
	*(int *)&MSG_WriteLong = 0x8067C2A;
#if COD_VERSION == COD2_1_0
	*(int *)&MSG_WriteLong = 0x8067C2A;
#elif COD_VERSION == COD2_1_2
	*(int *)&MSG_WriteLong = 0x80680F2;
#elif COD_VERSION == COD2_1_3
	*(int *)&MSG_WriteLong = 0x80680EA;
#endif

	int (*MSG_WriteString)(int a1, char *s);
#if COD_VERSION == COD2_1_0
	*(int *)&MSG_WriteString = 0x8067CE4;
#elif COD_VERSION == COD2_1_2
	*(int *)&MSG_WriteString = 0x80681AC;
#elif COD_VERSION == COD2_1_3
	*(int *)&MSG_WriteString = 0x80681A4;
#endif

	int (*MSG_WriteData)(int a1, void *src, size_t n);
#if COD_VERSION == COD2_1_0
	*(int *)&MSG_WriteData = 0x8067B84;
#elif COD_VERSION == COD2_1_2
	*(int *)&MSG_WriteData = 0x806804C;
#elif COD_VERSION == COD2_1_3
	*(int *)&MSG_WriteData = 0x8068044;
#endif

	int (*FS_SV_FOpenFileRead)(char *src, int a2);
#if COD_VERSION == COD2_1_0
	*(int *)&FS_SV_FOpenFileRead = 0x8064100;
#elif COD_VERSION == COD2_1_2
	*(int *)&FS_SV_FOpenFileRead = 0x8064560;
#elif COD_VERSION == COD2_1_3
	*(int *)&FS_SV_FOpenFileRead = 0x8064558;
#endif

	int (*FS_iwdFile)(char *haystack, int a2);
#if COD_VERSION == COD2_1_0
	*(int *)&FS_iwdFile = 0x8064ECC;
#elif COD_VERSION == COD2_1_2
	*(int *)&FS_iwdFile = 0x806532C;
#elif COD_VERSION == COD2_1_3
	*(int *)&FS_iwdFile = 0x8065324;
#endif

#if COD_VERSION == COD2_1_2 || COD_VERSION == COD2_1_3

	signed int (*SV_WWWRedirectClient)(int a1, int a2);
#if COD_VERSION == COD2_1_2
	*(int *)&SV_WWWRedirectClient = 0x808FB78;
#elif COD_VERSION == COD2_1_3
	*(int *)&SV_WWWRedirectClient = 0x808FC0C;
#endif

#endif

#if COD_VERSION == COD2_1_0
	int rate_offset = 452008;
#elif COD_VERSION == COD2_1_2
	int rate_offset = 452280;
#elif COD_VERSION == COD2_1_3
	int rate_offset = 452280;
#endif

#if COD_VERSION == COD2_1_0
	int snaps_offset = 452012;
#elif COD_VERSION == COD2_1_2
	int snaps_offset = 452284;
#elif COD_VERSION == COD2_1_3
	int snaps_offset = 452284;
#endif

	int filename_offset = 134248;
	int file_offset = 134312;
	int cl_iwd_offset = 134316;
	int block_offset = 134332;
	int section_offset = 134324;
	int nextblock_offset = 134328;
	int eof_offset = 134320;
	int addeof_offset = 134400;
	int unknown_offset1 = 134336;
	int unknown_offset2 = 134368;
	int time_offset = 134404;

#if COD_VERSION == COD2_1_2 || COD_VERSION == COD2_1_3
	int wwwdld_offset1 = 134664;
	int wwwdld_offset2 = 134676;
#endif

	char *file = (char *)(cl + filename_offset);
	int len = strlen(file);

	if (!*(int *)(cl + filename_offset))
		return 0;	// Nothing being downloaded

	if (len < 4 || strcmp(&file[len - 4], ".iwd") != 0)
		return 0;	// Not a valid iwd file

	if (strlen(sv_downloadMessage->string))
	{
		Com_sprintf(errorMessage, sizeof(errorMessage), sv_downloadMessage->string);
		MSG_WriteByte(msg, 5);
		MSG_WriteShort(msg, 0);
		MSG_WriteLong(msg, -1);
		MSG_WriteString(msg, errorMessage);
		*(int *)(cl + filename_offset) = 0;
		return 0;
	}

	*(int *)cl = CS_CONNECTED;  	       // Set client state - connected. Now players that are downloading will show as 'CNCT' in rcon, etc.
	*(int *)(cl + rate_offset)  = 25000;   // Hardcode client rate so even users with lower rate values will have fullspeed download. Setting it to above 25000 doesn't do anything
	*(int *)(cl + snaps_offset) = 50;      // Hadrcode client snaps. They will be equal to sv_fps anyway. Edit: Actually its snapshotMsec, 50 ~ /snaps "20", is the best value.

#if COD_VERSION == COD2_1_2 || COD_VERSION == COD2_1_3
	if ( sv_wwwDownload->boolean && *(int *)(cl + wwwdld_offset1) )
	{
		if ( *(int *)(cl + wwwdld_offset2) )
			*(int *)(cl + wwwdld_offset2) = 0;
		else if ( SV_WWWRedirectClient(cl, msg) )
			return 0;
	}
#endif

	if (!*(int *)(cl + file_offset))
	{
		// We open the file here

		Com_Printf("clientDownload: %d : begining \"%s\"\n", PLAYERBASE_ID(cl), cl + filename_offset);

		iwdFile = FS_iwdFile((char *)(cl + filename_offset), (int)"main");

		if ( !sv_allowDownload->boolean || iwdFile || ( *(int *)(cl + cl_iwd_offset) = FS_SV_FOpenFileRead((char *)(cl + filename_offset), cl + file_offset) ) <= 0 )
		{
			// cannot auto-download file
			if (iwdFile)
			{
				Com_Printf("clientDownload: %d : \"%s\" cannot download iwd files\n", PLAYERBASE_ID(cl), cl + filename_offset);
				Com_sprintf(errorMessage, sizeof(errorMessage), "EXE_CANTAUTODLGAMEIWD\x15%s", cl + filename_offset);
			}
			else if ( !sv_allowDownload->boolean )
			{
				Com_Printf("clientDownload: %d : \"%s\" download disabled\n", PLAYERBASE_ID(cl), cl + filename_offset);
				if (sv_pure->boolean)
					Com_sprintf(errorMessage, sizeof(errorMessage), "EXE_AUTODL_SERVERDISABLED_PURE\x15%s", cl + filename_offset);
				else
					Com_sprintf(errorMessage, sizeof(errorMessage), "EXE_AUTODL_SERVERDISABLED\x15%s", cl + filename_offset);
			}
			else
			{
				Com_Printf("clientDownload: %d : \"%s\" file not found on server\n", PLAYERBASE_ID(cl), cl + filename_offset);
				Com_sprintf(errorMessage, sizeof(errorMessage), "EXE_AUTODL_FILENOTONSERVER\x15%s", cl + filename_offset);
			}
			MSG_WriteByte(msg, 5);
			MSG_WriteShort(msg, 0);
			MSG_WriteLong(msg, -1);
			MSG_WriteString(msg, errorMessage);

			*(int *)(cl + filename_offset) = 0;
			return 0;
		}

		// Init
		*(int *)(cl + block_offset) = 0;
		*(int *)(cl + section_offset) = 0;
		*(int *)(cl + nextblock_offset) = 0;
		*(int *)(cl + eof_offset) = 0;
		*(int *)(cl + addeof_offset) = 0;
	}

	// Perform any reads that we need to
	while ( *(int *)(cl + nextblock_offset) - *(int *)(cl + section_offset) < MAX_DOWNLOAD_WINDOW && *(int *)(cl + cl_iwd_offset) != *(int *)(cl + eof_offset) )
	{
		curindex = (*(int *)(cl + nextblock_offset) % MAX_DOWNLOAD_WINDOW);

		if (!*(int *)(cl + 4 * curindex + unknown_offset1))
			*(int *)(cl + 4 * curindex + unknown_offset1) = Z_Malloc(MAX_DOWNLOAD_BLKSIZE);

		*(int *)(cl + 4 * curindex + unknown_offset2) = FS_Read(*(void **)(cl + 4 * curindex + unknown_offset1), MAX_DOWNLOAD_BLKSIZE, *(int *)(cl + file_offset));

		if ( *(int *)(cl + 4 * curindex + unknown_offset2) < 0 )
		{
			// EOF right now
			*(int *)(cl + eof_offset) = *(int *)(cl + cl_iwd_offset);
			break;
		}

		*(int *)(cl + eof_offset) += *(int *)(cl + 4 * curindex + unknown_offset2);

		// Load in next block
		( *(int *)(cl + nextblock_offset) )++;
	}

	// Check to see if we have eof condition and add the EOF block
	if ( *(int *)(cl + eof_offset) == *(int *)(cl + cl_iwd_offset) && !*(int *)(cl + addeof_offset) && *(int *)(cl + nextblock_offset) - *(int *)(cl + section_offset) < MAX_DOWNLOAD_WINDOW)
	{
		*(int *)(cl + 4 * (*(int *)(cl + nextblock_offset) % MAX_DOWNLOAD_WINDOW) + unknown_offset2) = 0;
		( *(int *)(cl + nextblock_offset) )++;

		*(int *)(cl + addeof_offset) = 1;  // We have added the EOF block
	}

	// Write out the next section of the file, if we have already reached our window,
	// automatically start retransmitting
	if ( *(int *)(cl + section_offset) == *(int *)(cl + nextblock_offset) )
		return 0; // Nothing to transmit

	if ( *(int *)(cl + block_offset) == *(int *)(cl + nextblock_offset) )
	{
		// We have transmitted the complete window, should we start resending?
		if (getSVSTime() - *(int *)(cl + time_offset) > 1000)
			*(int *)(cl + block_offset) = *(int *)(cl + section_offset);
		else
			return 0;
	}

	// Send current block
	curindex = *(int *)(cl + block_offset) % MAX_DOWNLOAD_WINDOW;

	MSG_WriteByte(msg, 5);
	MSG_WriteShort(msg, *(int *)(cl + block_offset));

	// block zero is special, contains file size
	if ( *(int *)(cl + block_offset) == 0 )
		MSG_WriteLong(msg, *(int *)(cl + cl_iwd_offset));

	MSG_WriteShort(msg, *(int *)(cl + 4 * curindex + unknown_offset2));

	// Write the block
	if ( *(int *)(cl + 4 * curindex + unknown_offset2) )
		MSG_WriteData(msg, *(void **)(cl + 4 * curindex + unknown_offset1), *(int *)(cl + 4 * curindex + unknown_offset2));

	Com_DPrintf("clientDownload: %d : writing block %d\n", PLAYERBASE_ID(cl), *(int *)(cl + block_offset));

	// Move on to the next block
	// It will get sent with next snap shot.  The rate will keep us in line.
	( *(int *)(cl + block_offset) )++;
	*(int *)(cl + time_offset) = getSVSTime();

	return 1;
}

// Segfault fix
int hook_BG_IsWeaponValid(int a1, int a2)
{

#if COD_VERSION == COD2_1_0
	int sub_80E9758_offset = 0x80E9758;
	int sub_80D9E84_offset = 0x80D9E84;
	int getWeaponStruct_offset = 0x80E9270;
#elif COD_VERSION == COD2_1_2
	int sub_80E9758_offset = 0x80EBD48;
	int sub_80D9E84_offset = 0x80DC464;
	int getWeaponStruct_offset = 0x80EB860;
#elif COD_VERSION == COD2_1_3
	int sub_80E9758_offset = 0x80EBE8C;
	int sub_80D9E84_offset = 0x80DC5A8;
	int getWeaponStruct_offset = 0x80EB9A4;
#endif

	int weapon;

	signed int (*sub_80E9758)(int a1);
	*(int *)&sub_80E9758 = sub_80E9758_offset;

	int (*sub_80D9E84)(int a1, signed int a2);
	*(int *)&sub_80D9E84 = sub_80D9E84_offset;

	int (*getWeaponStruct)(int a1);
	*(int *)&getWeaponStruct = getWeaponStruct_offset;

	if ( !(unsigned char)sub_80E9758(a2) )
		return 0;

	if ( !(unsigned char)sub_80D9E84(a1 + 1348, a2) )
		return 0;

	weapon = getWeaponStruct(a2);

	if ( !weapon )
		return 0;

	if ( !*(long *)(weapon + 132) && *(char *)(a1 + 1365) != a2 && *(char *)(a1 + 1366) != a2 && *(long *)(weapon + 876) != a2 )
		return 0;

	return 1;
}

cvar_t *nodeveloper_errors;
void hook_scriptError(int a1, int a2, int a3, void *a4)
{

#if COD_VERSION == COD2_1_0
	int scriptError_offset = 0x8078282;
	int runtimeError_offset = 0x807818C;
#elif COD_VERSION == COD2_1_2
	int scriptError_offset = 0x8078806;
	int runtimeError_offset = 0x8078710;
#elif COD_VERSION == COD2_1_3
	int scriptError_offset = 0x80788D2;
	int runtimeError_offset = 0x80787DC;
#endif

	int (*scriptError)(int a1, int a2, int a3, void *a4);
	*(int *)&scriptError = scriptError_offset;

	int (*runtimeError)(int a1, int a2, int a3, int a4);
	*(int *)&runtimeError = runtimeError_offset;

	scriptError(a1, a2, a3, a4);

	if (!developer->integer && nodeveloper_errors->boolean)
		runtimeError(0, a1, a2, a3);
}

int gamestate_size[64] = {0};
void hook_gamestate_info(char *format, ...)
{
	char s[COD2_MAX_STRINGLENGTH];
	va_list va;

	va_start(va, format);
	vsnprintf(s, sizeof(s), format, va);
	va_end(va);

	Com_DPrintf("%s", s);

	char *tok;
	int gamestate = 0;
	int clientnum = 0;
	tok = strtok(s, " ");

	for (int i = 0; tok != NULL; i++)
	{
		if (i == 1)
			gamestate = atoi(tok);
		if (i == 7)
			clientnum = atoi(tok);
		tok = strtok(NULL, " ");
	}

	gamestate_size[clientnum] = gamestate;
}

int custom_animation[64] = {0};
cHook *hook_set_anim;
int set_anim(int a1, int a2, signed int a3, int a4, int a5, int a6, int a7)
{
	int clientnum = PLAYERSTATE_ID(a1);

	if (CLIENTSTATE(clientnum) == CS_ACTIVE && custom_animation[clientnum])
	{
		a2 = custom_animation[clientnum];
		a4 = 0;
		a5 = 1;
		a6 = 0;
		a7 = 1;
	}

	hook_set_anim->unhook();
	int (*sig)(int a1, int a2, signed int a3, int a4, int a5, int a6, int a7);
	*(int *)&sig = hook_set_anim->from;
	int ret = sig(a1, a2, a3, a4, a5, a6, a7);
	hook_set_anim->hook();

	return ret;
}

#if COMPILE_BOTS == 1
cHook *hook_set_bot_variables;
int set_bot_variables()
{
	hook_set_bot_variables->unhook();
	int (*sig)();
	*(int *)&sig = hook_set_bot_variables->from;
	int ret = sig();
	hook_set_bot_variables->hook();

#if COD_VERSION == COD2_1_0
	int ping_offset = 113001;
	int lastmsg_offset = 134416;
#elif COD_VERSION == COD2_1_2
	int ping_offset = 113069;
	int lastmsg_offset = 134688;
#elif COD_VERSION == COD2_1_3
	int ping_offset = 113069;
	int lastmsg_offset = 134688;
#endif

	for (int i = 0; i < sv_maxclients->integer; i++)
	{
		if (CLIENTSTATE(i) == CS_ACTIVE && ADDRESSTYPE(i) == NA_BOT)
		{
			*(int *)(PLAYERBASE(i) + (ping_offset * 4)) = 0;
			*(int *)(PLAYERBASE(i) + lastmsg_offset) = getSVSTime() + 50;
		}
	}

	return ret;
}

cHook *hook_fire_antilag;
int fire_antilag(int a1, int a2)
{
	hook_fire_antilag->unhook();

#if COD_VERSION == COD2_1_0
	int offset = 0x0859B5EC;
#elif COD_VERSION == COD2_1_2
	int offset = 0x085AF4EC;
#elif COD_VERSION == COD2_1_3
	int offset = 0x0864C56C;
#endif

	int clientnum = G_ENTITY_ID(a1);

	if (ADDRESSTYPE(clientnum) == NA_BOT)
		a2 = *(int *)offset;

	int (*sig)(int a1, int a2);
	*(int *)&sig = hook_fire_antilag->from;
	int ret = sig(a1, a2);

	hook_fire_antilag->hook();

	return ret;
}

cHook *hook_free_slot;
int free_slot(int a1, char* message)
{
	hook_free_slot->unhook();
	int (*sig)(int a1, char* message);
	*(int *)&sig = hook_free_slot->from;
	int ret = sig(a1, message);
	hook_free_slot->hook();

	int clientnum = PLAYERBASE_ID(a1);

	if (ADDRESSTYPE(clientnum) == NA_BOT)
	{
		Com_DPrintf("Going from CS_ZOMBIE to CS_FREE for %s\n", (char *)a1 + 134216);
		*(int *)a1 = CS_FREE;
	}

	return ret;
}

int bot_movement[64] = {0};
int bot_state[64] = {0};
int bot_grenade[64] = {0};
int bot_stance[64] = {0};
int bot_shoot[64] = {0};
int bot_melee[64] = {0};
int bot_ads[64] = {0};
int bot_lean[64] = {0};
int bot_reload[64] = {0};
int bot_weapon[64] = {0};
#endif

int clfps[64][20] = {{0}};
int clfpstemp[64] = {0};
int clfpsindex = 0;

cHook *hook_play_movement;
int play_movement(int a1, int a2)
{
	int clientnum = PLAYERBASE_ID(a1);

	clfpstemp[clientnum]++; // FPS

#if COMPILE_BOTS == 1
	if (CLIENTSTATE(clientnum) == CS_ACTIVE && ADDRESSTYPE(clientnum) == NA_BOT)
	{
		bot_state[clientnum] = (bot_stance[clientnum] + bot_melee[clientnum] + bot_grenade[clientnum] + bot_shoot[clientnum] + bot_ads[clientnum] + bot_lean[clientnum] + bot_reload[clientnum]);

		*(int *)(a2 + 4) = bot_state[clientnum];

		if (bot_weapon[clientnum])
			*(int *)(a2 + 8) = bot_weapon[clientnum];

		*(int *)(a2 + 24) = bot_movement[clientnum];
	}
#endif

	hook_play_movement->unhook();
	int (*sig)(int a1, int a2);
	*(int *)&sig = hook_play_movement->from;
	int ret = sig(a1, a2);
	hook_play_movement->hook();

	return ret;
}

#if COMPILE_RATELIMITER == 1
// ioquake3 rate limit connectionless requests
// https://github.com/ioquake/ioq3/blob/master/code/server/sv_main.c

// This is deliberately quite large to make it more of an effort to DoS
#define MAX_BUCKETS	16384
#define MAX_HASHES 1024

static leakyBucket_t buckets[ MAX_BUCKETS ];
static leakyBucket_t* bucketHashes[ MAX_HASHES ];
leakyBucket_t outboundLeakyBucket;

static long SVC_HashForAddress( netadr_t address )
{
	unsigned char *ip = address.ip;
	int	i;
	long hash = 0;

	for ( i = 0; i < 4; i++ )
	{
		hash += (long)( ip[ i ] ) * ( i + 119 );
	}

	hash = ( hash ^ ( hash >> 10 ) ^ ( hash >> 20 ) );
	hash &= ( MAX_HASHES - 1 );

	return hash;
}

static leakyBucket_t *SVC_BucketForAddress( netadr_t address, int burst, int period )
{
	leakyBucket_t *bucket = NULL;
	int	i;
	long hash = SVC_HashForAddress( address );
	int	now = Sys_MilliSeconds();

	for ( bucket = bucketHashes[ hash ]; bucket; bucket = bucket->next )
	{
		if ( memcmp( bucket->adr, address.ip, 4 ) == 0 )
		{
			return bucket;
		}
	}

	for ( i = 0; i < MAX_BUCKETS; i++ )
	{
		int interval;

		bucket = &buckets[ i ];
		interval = now - bucket->lastTime;

		// Reclaim expired buckets
		if ( bucket->lastTime > 0 && ( interval > ( burst * period ) ||
		                               interval < 0 ) )
		{
			if ( bucket->prev != NULL )
			{
				bucket->prev->next = bucket->next;
			}
			else
			{
				bucketHashes[ bucket->hash ] = bucket->next;
			}

			if ( bucket->next != NULL )
			{
				bucket->next->prev = bucket->prev;
			}

			memset( bucket, 0, sizeof( leakyBucket_t ) );
		}

		if ( bucket->type == 0 )
		{
			bucket->type = address.type;
			memcpy( bucket->adr, address.ip, 4 );

			bucket->lastTime = now;
			bucket->burst = 0;
			bucket->hash = hash;

			// Add to the head of the relevant hash chain
			bucket->next = bucketHashes[ hash ];
			if ( bucketHashes[ hash ] != NULL )
			{
				bucketHashes[ hash ]->prev = bucket;
			}

			bucket->prev = NULL;
			bucketHashes[ hash ] = bucket;

			return bucket;
		}
	}

	// Couldn't allocate a bucket for this address
	return NULL;
}

bool SVC_RateLimit( leakyBucket_t *bucket, int burst, int period )
{
	if ( bucket != NULL )
	{
		int now = Sys_MilliSeconds();
		int interval = now - bucket->lastTime;
		int expired = interval / period;
		int expiredRemainder = interval % period;

		if ( expired > bucket->burst || interval < 0 )
		{
			bucket->burst = 0;
			bucket->lastTime = now;
		}
		else
		{
			bucket->burst -= expired;
			bucket->lastTime = now - expiredRemainder;
		}

		if ( bucket->burst < burst )
		{
			bucket->burst++;

			return false;
		}
	}

	return true;
}

bool SVC_RateLimitAddress( netadr_t from, int burst, int period )
{
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	return SVC_RateLimit( bucket, burst, period );
}

int hook_SVC_RemoteCommand(netadr_t from)
{
	// Prevent using rcon as an amplifier and make dictionary attacks impractical
	if ( SVC_RateLimitAddress( from, 10, 1000 ) )
	{
		Com_DPrintf( "SVC_RemoteCommand: rate limit from %s exceeded, dropping request\n", NET_AdrToString( from ) );
		return 0;
	}

	if ( !strlen( rcon_password->string ) || strcmp(Cmd_Argv(1), rcon_password->string) != 0 )
	{
		static leakyBucket_t bucket;

		// Make DoS via rcon impractical
		if ( SVC_RateLimit( &bucket, 10, 1000 ) )
		{
			Com_DPrintf( "SVC_RemoteCommand: rate limit exceeded, dropping request\n" );
			return 0;
		}
	}

	return SVC_RemoteCommand(from);
}

int hook_SV_GetChallenge(netadr_t from)
{
	// Prevent using getchallenge as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) )
	{
		Com_DPrintf( "SV_GetChallenge: rate limit from %s exceeded, dropping request\n", NET_AdrToString( from ) );
		return 0;
	}

	// Allow getchallenge to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) )
	{
		Com_DPrintf( "SV_GetChallenge: rate limit exceeded, dropping request\n" );
		return 0;
	}

	return SV_GetChallenge(from);
}

int hook_SVC_Info(netadr_t from)
{
	// Prevent using getinfo as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) )
	{
		Com_DPrintf( "SVC_Info: rate limit from %s exceeded, dropping request\n", NET_AdrToString( from ) );
		return 0;
	}

	// Allow getinfo to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) )
	{
		Com_DPrintf( "SVC_Info: rate limit exceeded, dropping request\n" );
		return 0;
	}

	return SVC_Info(from);
}

int hook_SVC_Status(netadr_t from)
{
	// Prevent using getstatus as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) )
	{
		Com_DPrintf( "SVC_Status: rate limit from %s exceeded, dropping request\n", NET_AdrToString( from ) );
		return 0;
	}

	// Allow getstatus to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) )
	{
		Com_DPrintf( "SVC_Status: rate limit exceeded, dropping request\n" );
		return 0;
	}

	return SVC_Status(from);
}
#endif

cvar_t *fs_library;
void manymaps_prepare(char *mapname, int read)
{
	char library_path[512];

	cvar_t *fs_homepath = Cvar_FindVar("fs_homepath");
	cvar_t *fs_game = Cvar_FindVar("fs_game");
	cvar_t *map = Cvar_FindVar("mapname");

	if (strlen(fs_library->string))
		strncpy(library_path, fs_library->string, sizeof(library_path));
	else
		snprintf(library_path, sizeof(library_path), "%s/%s/Library", fs_homepath->string, fs_game->string);

	if (strcmp(map->string, mapname) == 0) // Same map is about to load, no need to trigger manymap (equals map_restart)
		return;

	char map_check[512];
	snprintf(map_check, sizeof(map_check), "%s/%s.iwd", library_path, mapname);
	int map_exists = access(map_check, F_OK) != -1;

#if COD_VERSION == COD2_1_0
	char *stock_maps[] = { "mp_breakout", "mp_brecourt", "mp_burgundy", "mp_carentan", "mp_dawnville", "mp_decoy", "mp_downtown", "mp_farmhouse", "mp_leningrad", "mp_matmata", "mp_railyard", "mp_toujane", "mp_trainstation" };
#else
	char *stock_maps[] = { "mp_breakout", "mp_brecourt", "mp_burgundy", "mp_carentan", "mp_dawnville", "mp_decoy", "mp_downtown", "mp_farmhouse", "mp_leningrad", "mp_matmata", "mp_railyard", "mp_toujane", "mp_trainstation", "mp_rhine", "mp_harbor" };
#endif

	bool map_found = false;
	bool from_stock_map = false;

	for (int i = 0; i < int( sizeof(stock_maps) / sizeof(stock_maps[0]) ); i++)
	{
		if (strcmp(map->string, stock_maps[i]) == 0)
		{
			from_stock_map = true;
			break;
		}
	}

	for (int i = 0; i < int( sizeof(stock_maps) / sizeof(stock_maps[0]) ); i++)
	{
		if (strcmp(mapname, stock_maps[i]) == 0)
		{
			map_found = true;
			if (from_stock_map) // When changing from stock map to stock map do not trigger manymap
				return;
			else
				break;
		}
	}

	if (!map_exists && !map_found)
		return;

	DIR *dir;
	struct dirent *dir_ent;
	dir = opendir(library_path);

	if (!dir)
		return;

	while ( (dir_ent = readdir(dir)) != NULL)
	{
		if (strcmp(dir_ent->d_name, ".") == 0 || strcmp(dir_ent->d_name, "..") == 0)
			continue;

		char fileDelete[512];
		snprintf(fileDelete, sizeof(fileDelete), "%s/%s/%s", fs_homepath->string, fs_game->string, dir_ent->d_name);
		int exists = access(fileDelete, F_OK) != -1;
		if (exists)
			printf("manymaps> REMOVED MANYMAP: %s result of unlink: %d\n", fileDelete, unlink(fileDelete));
	}

	closedir(dir);

	if (map_exists)
	{
		char src[512], dst[512];
		snprintf(src, sizeof(src), "%s/%s.iwd", library_path, mapname);
		snprintf(dst, sizeof(dst), "%s/%s/%s.iwd", fs_homepath->string, fs_game->string, mapname);
		printf("manymaps> LINK src=%s dst=%s\n", src, dst);
		if (access(src, F_OK) != -1)
		{
			char cmd[COD2_MAX_STRINGLENGTH];
			setenv("LD_PRELOAD", "", 1); // dont inherit lib of parent
			snprintf(cmd, sizeof(cmd), "ln -sfn %s %s", src, dst);
			int link_success = system(cmd) == 0;
			printf("manymaps> LINK: %s\n", link_success?"success":"failed (probably already exists)");
			if (read == -1) // FS_LoadDir is needed when empty.iwd is missing (then .d3dbsp isn't referenced anywhere)
				FS_LoadDir(fs_homepath->string, fs_game->string);
		}
	}
}

int hook_findMap(const char *qpath, void **buffer)
{
	int read = FS_ReadFile(qpath, buffer);
	manymaps_prepare(Cmd_Argv(1), read);

	if (read != -1)
		return read;
	else
		return FS_ReadFile(qpath, buffer);
}

class cCallOfDuty2Pro
{
public:
	cCallOfDuty2Pro()
	{

#if COD_VERSION == COD2_1_0
		printf("> [LIBCOD] Compiled for: CoD2 1.0\n");
#elif COD_VERSION == COD2_1_2
		printf("> [LIBCOD] Compiled for: CoD2 1.2\n");
#elif COD_VERSION == COD2_1_3
		printf("> [LIBCOD] Compiled for: CoD2 1.3\n");
#endif

		printf("> [LIBCOD] Compiled %s %s using GCC %s\n", __DATE__, __TIME__, __VERSION__);

		// allow to write in executable memory
		mprotect((void *)0x08048000, 0x135000, PROT_READ | PROT_WRITE | PROT_EXEC);

#if COD_VERSION == COD2_1_0
		int *addressToPickUpItemPointer = (int *)0x08167B34;
#elif COD_VERSION == COD2_1_2
		int *addressToPickUpItemPointer = (int *)0x08186F94;
#elif COD_VERSION == COD2_1_3
		int *addressToPickUpItemPointer = (int *)0x08187FB4;
#endif

		*addressToPickUpItemPointer = (int)hook_pickup_item;

#if COD_VERSION == COD2_1_0
		int *addressToDownloadPointer = (int *)0x0815D584;
#elif COD_VERSION == COD2_1_2
		int *addressToDownloadPointer = (int *)0x0817C9E4;
#elif COD_VERSION == COD2_1_3
		int *addressToDownloadPointer = (int *)0x0817DA04;
#endif

		*addressToDownloadPointer = (int)hook_SV_BeginDownload_f;

#if COD_VERSION == COD2_1_0
		cracking_hook_call(0x08061FE7, (int)hook_sv_init);
		cracking_hook_call(0x0808F281, (int)hook_ClientCommand);
		cracking_hook_call(0x0808C8C0, (int)hook_AuthorizeState);
		cracking_hook_call(0x0808AD00, (int)hook_findMap);
		cracking_hook_call(0x0808F134, (int)hook_ClientUserinfoChanged);
		cracking_hook_call(0x0807059F, (int)Scr_GetCustomFunction);
		cracking_hook_call(0x080707C3, (int)Scr_GetCustomMethod);
		cracking_hook_call(0x0808E18F, (int)hook_gamestate_info);
		cracking_hook_call(0x0808F412, (int)hook_vid_restart);
		cracking_hook_call(0x080DFF66, (int)hook_player_setmovespeed);
		cracking_hook_call(0x080F50AB, (int)hook_player_g_speed);
		cracking_hook_call(0x080E9524, (int)hook_findWeaponIndex);
		cracking_hook_call(0x08081CFE, (int)hook_scriptError);
		hook_gametype_scripts = new cHook(0x0810DDEE, (int)hook_codscript_gametype_scripts);
		hook_gametype_scripts->hook();
		hook_set_anim = new cHook(0x080D69B2, (int)set_anim);
		hook_set_anim->hook();
		hook_player_collision = new cHook(0x080F2F2E, (int)player_collision);
		hook_player_collision->hook();
		hook_player_eject = new cHook(0x080F474A, (int)player_eject);
		hook_player_eject->hook();

#if COMPILE_BOTS == 1
		hook_set_bot_variables = new cHook(0x0809443E, (int)set_bot_variables);
		hook_set_bot_variables->hook();
		hook_fire_antilag = new cHook(0x0811E3E0, (int)fire_antilag);
		hook_fire_antilag->hook();
		hook_free_slot = new cHook(0x0808DC8C, (int)free_slot);
		hook_free_slot->hook();
#endif

		hook_play_movement = new cHook(0x0808F488, (int)play_movement);
		hook_play_movement->hook();
		hook_fire_grenade = new cHook(0x0810C1F6, (int)fire_grenade);
		hook_fire_grenade->hook();
		cracking_hook_function(0x080E97F0, (int)hook_BG_IsWeaponValid);
		cracking_hook_function(0x0808E544, (int)custom_SV_WriteDownloadToClient);

#if COMPILE_RATELIMITER == 1
		cracking_hook_call(0x08094081, (int)hook_SVC_Info);
		cracking_hook_call(0x0809403E, (int)hook_SVC_Status);
		cracking_hook_call(0x080940C4, (int)hook_SV_GetChallenge);
		cracking_hook_call(0x08094191, (int)hook_SVC_RemoteCommand);
		cracking_write_hex(0x080951BE, (char *)"9090909090909090"); // time = Com_Milliseconds();
		cracking_write_hex(0x080951E0, (char *)"EB"); // skip `time - lasttime` check
		cracking_write_hex(0x080951E7, (char *)"9090909090909090"); // lasttime = time;
#endif

#elif COD_VERSION == COD2_1_2
		cracking_hook_call(0x08062301, (int)hook_sv_init);
		cracking_hook_call(0x08090B0C, (int)hook_ClientCommand);
		cracking_hook_call(0x0808DA52, (int)hook_AuthorizeState);
		cracking_hook_call(0x0808BCFC, (int)hook_findMap);
		cracking_hook_call(0x080909BE, (int)hook_ClientUserinfoChanged);
		cracking_hook_call(0x08070B1B, (int)Scr_GetCustomFunction);
		cracking_hook_call(0x08070D3F, (int)Scr_GetCustomMethod);
		cracking_hook_call(0x0808F533, (int)hook_gamestate_info);
		cracking_hook_call(0x08090CA2, (int)hook_vid_restart);
		cracking_hook_call(0x080E2546, (int)hook_player_setmovespeed);
		cracking_hook_call(0x080F76BF, (int)hook_player_g_speed);
		cracking_hook_call(0x080EBB14, (int)hook_findWeaponIndex);
		cracking_hook_call(0x0808227A, (int)hook_scriptError);
		hook_gametype_scripts = new cHook(0x0811012A, (int)hook_codscript_gametype_scripts);
		hook_gametype_scripts->hook();
		hook_set_anim = new cHook(0x080D8F92, (int)set_anim);
		hook_set_anim->hook();
		hook_player_collision = new cHook(0x080F553E, (int)player_collision);
		hook_player_collision->hook();
		hook_player_eject = new cHook(0x080F6D5A, (int)player_eject);
		hook_player_eject->hook();

#if COMPILE_BOTS == 1
		hook_set_bot_variables = new cHook(0x0809630E, (int)set_bot_variables);
		hook_set_bot_variables->hook();
		hook_fire_antilag = new cHook(0x08120714, (int)fire_antilag);
		hook_fire_antilag->hook();
		hook_free_slot = new cHook(0x0808EF9A, (int)free_slot);
		hook_free_slot->hook();
#endif

		hook_play_movement = new cHook(0x08090D18, (int)play_movement);
		hook_play_movement->hook();
		hook_fire_grenade = new cHook(0x0810E532, (int)fire_grenade);
		hook_fire_grenade->hook();
		cracking_hook_function(0x080EBDE0, (int)hook_BG_IsWeaponValid);
		cracking_hook_function(0x0808FD2E, (int)custom_SV_WriteDownloadToClient);

#if COMPILE_RATELIMITER == 1
		cracking_hook_call(0x08095B8E, (int)hook_SVC_Info);
		cracking_hook_call(0x08095ADA, (int)hook_SVC_Status);
		cracking_hook_call(0x08095BF8, (int)hook_SV_GetChallenge);
		cracking_hook_call(0x08095D63, (int)hook_SVC_RemoteCommand);
		cracking_write_hex(0x080970D6, (char *)"9090909090909090"); // time = Com_Milliseconds();
		cracking_write_hex(0x080970F8, (char *)"EB"); // skip `time - lasttime` check
		cracking_write_hex(0x080970FF, (char *)"9090909090909090"); // lasttime = time;
#endif

#elif COD_VERSION == COD2_1_3
		cracking_hook_call(0x080622F9, (int)hook_sv_init);
		cracking_hook_call(0x08090BA0, (int)hook_ClientCommand);
		cracking_hook_call(0x0808DB12, (int)hook_AuthorizeState);
		cracking_hook_call(0x0808BDC8, (int)hook_findMap);
		cracking_hook_call(0x08090A52, (int)hook_ClientUserinfoChanged);
		cracking_hook_call(0x08070BE7, (int)Scr_GetCustomFunction);
		cracking_hook_call(0x08070E0B, (int)Scr_GetCustomMethod);
		cracking_hook_call(0x0808F5C7, (int)hook_gamestate_info);
		cracking_hook_call(0x08090D36, (int)hook_vid_restart);
		cracking_hook_call(0x080E268A, (int)hook_player_setmovespeed);
		cracking_hook_call(0x080F7803, (int)hook_player_g_speed);
		cracking_hook_call(0x080EBC58, (int)hook_findWeaponIndex);
		cracking_hook_call(0x08082346, (int)hook_scriptError);
		hook_gametype_scripts = new cHook(0x08110286, (int)hook_codscript_gametype_scripts);
		hook_gametype_scripts->hook();
		hook_set_anim = new cHook(0x080D90D6, (int)set_anim);
		hook_set_anim->hook();
		hook_player_collision = new cHook(0x080F5682, (int)player_collision);
		hook_player_collision->hook();
		hook_player_eject = new cHook(0x080F6E9E, (int)player_eject);
		hook_player_eject->hook();

#if COMPILE_BOTS == 1
		hook_set_bot_variables = new cHook(0x080963C8, (int)set_bot_variables);
		hook_set_bot_variables->hook();
		hook_fire_antilag = new cHook(0x08120870, (int)fire_antilag);
		hook_fire_antilag->hook();
		hook_free_slot = new cHook(0x0808F02E, (int)free_slot);
		hook_free_slot->hook();
#endif

		hook_play_movement = new cHook(0x08090DAC, (int)play_movement);
		hook_play_movement->hook();
		hook_fire_grenade = new cHook(0x0810E68E, (int)fire_grenade);
		hook_fire_grenade->hook();
		cracking_hook_function(0x080EBF24, (int)hook_BG_IsWeaponValid);
		cracking_hook_function(0x0808FDC2, (int)custom_SV_WriteDownloadToClient);

#if COMPILE_RATELIMITER == 1
		cracking_hook_call(0x08095C48, (int)hook_SVC_Info);
		cracking_hook_call(0x08095B94, (int)hook_SVC_Status);
		cracking_hook_call(0x08095CB2, (int)hook_SV_GetChallenge);
		cracking_hook_call(0x08095E1D, (int)hook_SVC_RemoteCommand);
		cracking_write_hex(0x080971BC, (char *)"9090909090909090"); // time = Com_Milliseconds();
		cracking_write_hex(0x080971DE, (char *)"EB"); // skip `time - lasttime` check
		cracking_write_hex(0x080971F3, (char *)"9090909090909090"); // lasttime = time;
#endif

#endif

		// Register custom cvars
		sv_cracked = Cvar_RegisterBool("sv_cracked", 0, 0x1000u);
		nodeveloper_errors = Cvar_RegisterBool("nodeveloper_errors", 0, 0x1000u);
		g_playerCollision = Cvar_RegisterBool("g_playerCollision", 1, 0x1000u);
		g_playerEject = Cvar_RegisterBool("g_playerEject", 1, 0x1000u);
		fs_library = Cvar_RegisterString("fs_library", "", 0x1000u);
		sv_downloadMessage = Cvar_RegisterString("sv_downloadMessage", "", 0x1000u);

		gsc_utils_init();
		printf("> [PLUGIN LOADED]\n");
	}

	~cCallOfDuty2Pro()
	{
		gsc_utils_free();
		printf("> [PLUGIN UNLOADED]\n");
	}
};

cCallOfDuty2Pro *pro;

// lol, single again: because it got loaded two times now
// both now: able to load with wrapper AND directly
// IMPORTANT: file needs "lib" infront of name, otherwise it wont be loaded

extern "C" void __attribute__ ((constructor)) lib_load(void) // will be called when LD_PRELOAD is referencing this .so
{
	pro = new cCallOfDuty2Pro;
}

extern "C" void __attribute__ ((destructor)) lib_unload(void)
{
	delete pro;
}
