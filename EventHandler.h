#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

#include <queue>
#include <mutex>
#include <thread>
#include <vector>
#include "RconPacket.h"
#include "RconConnection.h"
//#include "AutoMessage.h"
#include <iostream>
#include <fstream>
#include <boost/algorithm/string/predicate.hpp>
#include <ctime>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <mysql_connection.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>

class EventHandler
{
public:
	enum class EVENT_TYPE { PLAYER_AUTH, PLAYER_JOIN, PLAYER_LEAVE, PLAYER_SPAWN, PLAYER_KILL, PLAYER_CHAT, PLAYER_SQUAD_CHANGE, PLAYER_TEAM_CHANGE, PB_MESSAGE, SERVER_MAX_PLAYER_COUNT_CHANGE, SERVER_LEVEL_LOADED, SERVER_ROUND_OVER, SERVER_ROUND_OVER_PLAYER_STATS, SERVER_ROUND_OVER_TEAM_SCORES };
	enum class OUT_TYPE { DEBUG, ERR, WARNING, INFO, MSG };
	struct EVENT { EVENT(void) {}; EVENT_TYPE eventType; TextRconPacket * packet; };
	struct PLAYER { PLAYER(void) {}; PLAYER(string nickname, string eaid, uint32_t teamId) : nickname(nickname), eaid(eaid), teamId(teamId) {}; string nickname; string eaid; uint32_t teamId; };
private:
	
	bool							debug;
	bool							InGameCommandsEnabled;
	std::string						InGameCommandPrefix;
	bool							MySQLChatlogEnabled;
	bool							StatisticsEnabled;

	std::queue<EVENT> queue;
	std::mutex queueMutex;

	RconConnection * rcon;

	std::thread * thread_Countdown;

	//AutoMessage	* automsg;

	bool Running;
	bool CountdownRunning;

	void InGameCommand(TextRconPacket & packet);
	void MySQLChatlog(vector<string>* data);
	void MySQLAddPlayer(string solidername, string eaid);
	void MySQLAddKillAndDeath(string killer, string killed, string weapon, string headshot);

	void ServerSizeChange();
	void ServerSizeOnRoundOver();
	void ServerSizeOnLevelLoaded();

	bool canUseCommands(string sender);
	bool canUseCommand(string sender, string right);
	bool canUseCommandWithRconMessage(string sender, string right);
	int isAccountActive(string nickname);
	string getAccountDisabledReason(string nickname);
	uint32_t getNumTeams();
	vector<EventHandler::PLAYER> getPlayerlist();

	// InGame Commands
	void InGameCommandRestart();
	void InGameCommandSwapteams();
	void InGameCommandBan(TextRconPacket & packet);
	void InGameCommandKick(TextRconPacket & packet);
	void InGameCommandCountdown(TextRconPacket & packet);
	void InGameCommandKill(TextRconPacket & packet);
	void InGameCommandMove(TextRconPacket & packet);
	void InGameCommandNuke(TextRconPacket & packet);
	void InGameCommandPban(TextRconPacket & packet);
	void InGameCommandSay(TextRconPacket & packet);
	void InGameCommandSlots(TextRconPacket & packet);
	void InGameCommandSwap(TextRconPacket & packet);
	void InGameCommandWin(TextRconPacket & packet);
	void InGameCommandYell(TextRconPacket & packet);

public:

	uint32_t						needResponse;
	TextRconPacket*					response;
	boost::property_tree::ptree*	pt;

	string getCurrentDateTime();
	void out(string str, EventHandler::OUT_TYPE type = EventHandler::OUT_TYPE::MSG);

	EventHandler(RconConnection * rcon, boost::property_tree::ptree* pt);
	~EventHandler(void);

	void AddEvent(TextRconPacket packet);
	void DoWork();

	void Enable();
	void Disable();

	void StringExplode(string str, string separator, vector<string>* results);
	string StringImplode(vector<string>* input, string separator = " ");
	string StringImplode(vector<string>::iterator from, vector<string>::iterator to, string separator = " ");
	string convertInt(int number);

	bool StringStartsWith(string str, string startStr);
};

#endif